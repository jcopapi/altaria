#include "cave_runtime.h"
#include "../voxel_core.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace altaria::caves {
bool validate(const Config &c,std::string &error) {
    if(!valid_biomes(c.biomes,error))return false;
    if(c.exploration_formations.size()>16){error="Too many exploration formations";return false;}
    for(const auto &id:c.exploration_formations)if(std::none_of(c.biomes.formations.begin(),c.biomes.formations.end(),
        [&](const FormationRule &rule){return rule.id==id;})) {
        error="Unknown exploration formation: "+id;return false;
    }
    if(!valid_hydrology(c.hydrology)){error="Invalid cave hydrology configuration";return false;}
    Planner probe(0,0,c.plan,[](float,float){return 100.f;});
    if(!probe.valid() || !valid_depth(c.depth,c.plan.depths) || c.plan.ceiling-c.plan.floor>max_vertical_span || c.roof_grid<4 || c.roof_grid>32 ||
       c.plan.region_size%c.roof_grid || c.cache_regions<16 || c.cache_regions>128 ||
       c.cache_bytes<1024*1024 || c.cache_bytes>128*1024*1024 || c.max_lod<0 || c.max_lod>2 ||
       !std::isfinite(c.detail_scale) || c.detail_scale<4 || c.detail_scale>128 ||
       !std::isfinite(c.detail_amplitude) || c.detail_amplitude<0 || c.detail_amplitude>.75f ||
       (c.roof_grid>0 && size_t(c.plan.region_size/c.roof_grid+1)*size_t(c.plan.region_size/c.roof_grid+1)*sizeof(float)+sizeof(Runtime)>c.cache_bytes/4)) {
        error="Invalid cave configuration (profiles, depth, roof grid, cache or detail limits)";return false;
    }
    return true;
}
Runtime::Runtime(Config config,int seed,float cliff,
    std::function<std::vector<Column>(const std::vector<float>&,const std::vector<float>&)> sampler,
    int sea,std::function<Outlet(const Passage&)> outlet,std::function<std::vector<Formation>(const Network&)> formations,
    std::function<bool(const Column&,int,int,int)> solid)
    :cfg(std::move(config)),columns(std::move(sampler)),cliff_strength(cliff),noise_seed(seed),
     sea_level(sea),outlet_probe(std::move(outlet)),host_solid(std::move(solid)) {
    static std::atomic<uint64_t> serial{0};identity=++serial;
    std::string error;if(!validate(cfg,error)){cfg.plan.region_size=0;cfg.hydrology.basin_regions=0;}
    cfg.plan.basin_regions=cfg.hydrology.basin_regions;
    auto n=FastNoise::New<FastNoise::Simplex>();n->SetScale(std::max(4.f,cfg.detail_scale));noise=n;
    planner=std::make_shared<Planner>(uint32_t(seed),cfg.dimension,cfg.plan,
        [this](float x,float z){return roof(x,z);},[this](float x,float z){return opening_roof(x,z);},
        [this](const Passage &opening){return outlet_probe && outlet_probe(opening).source!=WaterSource::none;});
    cache=std::make_unique<PlanCache>(planner,cfg.cache_regions,cfg.cache_bytes*5/8,std::move(formations));
}
std::shared_ptr<Runtime::RoofTile> Runtime::roof_tile(int x,int z) {
    Cell key{x,0,z};
    {std::lock_guard<std::mutex> lock(roof_mutex);auto it=roofs.find(key);if(it!=roofs.end()){it->second->used=++roof_clock;return it->second;}}
    int span=cfg.plan.region_size,n=span/cfg.roof_grid+1;
    std::vector<float> xs,zs;xs.reserve(n*n);zs.reserve(n*n);
    for(int iz=0;iz<n;iz++)for(int ix=0;ix<n;ix++){xs.push_back(float(x*span+ix*cfg.roof_grid));zs.push_back(float(z*span+iz*cfg.roof_grid));}
    auto data=columns(xs,zs);auto generated=std::make_shared<RoofTile>();generated->heights.reserve(data.size());
    for(const auto &c:data)generated->heights.push_back(c.height-cliff_strength*c.cliff-2);
    generated->bytes=sizeof(RoofTile)+generated->heights.capacity()*sizeof(float)+sizeof(Cell)+4*sizeof(void*);
    std::lock_guard<std::mutex> lock(roof_mutex);
    auto it=roofs.find(key);if(it!=roofs.end())return it->second;
    while(!roofs.empty() && (roofs.size()>=cfg.cache_regions || roof_bytes+generated->bytes>cfg.cache_bytes/4)) {
        auto oldest=roofs.begin();for(auto i=roofs.begin();i!=roofs.end();++i)if(i->second->used<oldest->second->used)oldest=i;
        roof_bytes-=oldest->second->bytes;roofs.erase(oldest);
    }
    generated->used=++roof_clock;roof_bytes+=generated->bytes;roofs.emplace(key,generated);return generated;
}
float Runtime::roof(float x,float z) {
    int span=cfg.plan.region_size,n=span/cfg.roof_grid+1;
    int tx=int(std::floor(x/span)),tz=int(std::floor(z/span));
    // A chamber probes many points in the same tile. Keep one immutable
    // height grid per worker so those probes don't contend on the shared LRU.
    struct LastRoof {uint64_t runtime=0;int x=0,z=0;std::shared_ptr<RoofTile> tile;};
    thread_local LastRoof last;
    if(last.runtime!=identity || last.x!=tx || last.z!=tz)
        last={identity,tx,tz,roof_tile(tx,tz)};
    const auto &tile=last.tile;
    int ix=std::clamp(int(std::floor((x-tx*span)/cfg.roof_grid)),0,n-2);
    int iz=std::clamp(int(std::floor((z-tz*span)/cfg.roof_grid)),0,n-2);
    return std::min({tile->heights[iz*n+ix],tile->heights[iz*n+ix+1],tile->heights[(iz+1)*n+ix],tile->heights[(iz+1)*n+ix+1]});
}
float Runtime::opening_roof(float x,float z) const {
    // Entrances need an upper bound on the actual terrain, not the conservative
    // lower roof atlas used to bury normal rooms. Sample the voxel mouth area in
    // one batch; using the lower atlas could leave a cap of uncarved rock.
    int wx=int(std::floor(x)),wz=int(std::floor(z));std::vector<float> xs,zs;
    xs.reserve(49);zs.reserve(49);
    for(int dz=-3;dz<=3;dz++)for(int dx=-3;dx<=3;dx++){xs.push_back(float(wx+dx));zs.push_back(float(wz+dz));}
    float high=cfg.plan.floor;
    for(const auto &c:columns(xs,zs)) {
        if(!std::isfinite(c.height) || !std::isfinite(c.cliff))return std::numeric_limits<float>::quiet_NaN();
        high=std::max(high,c.height+cliff_strength*c.cliff+1);
    }
    return std::min(high,cfg.plan.ceiling-1);
}
Query Runtime::query(Bounds bounds) const {
    auto query=cache->query(bounds);
    if(!host_solid)return query;
    for(const auto &[cell,formations]:query.formations)for(const auto *f:formations)
        if(!query.support.count(f->id))query.support.emplace(f->id,formation_support(*f));
    for(auto it=query.formations.begin();it!=query.formations.end();) {
        auto &list=it->second;
        list.erase(std::remove_if(list.begin(),list.end(),[&](const Formation *f){return !query.support.at(f->id)->kept;}),list.end());
        if(list.empty())it=query.formations.erase(it);else ++it;
    }
    return query;
}
std::shared_ptr<const FormationMask> Runtime::resident_support(uint64_t id) const {
    std::unique_lock<std::mutex> lock(support_mutex,std::try_to_lock);
    if(!lock.owns_lock())return {};
    auto entry=support.find(id);
    return entry==support.end() || !entry->second->ready?nullptr:entry->second->mask;
}
std::shared_ptr<const FormationMask> Runtime::formation_support(const Formation &formation) const {
    std::shared_ptr<SupportEntry> entry;
    {
        std::unique_lock<std::mutex> lock(support_mutex);
        if(auto it=support.find(formation.id);it!=support.end()) {
            entry=it->second;support_ready.wait(lock,[&]{return entry->ready;});
            entry->used=++support_clock;return entry->mask;
        }
        entry=std::make_shared<SupportEntry>();support.emplace(formation.id,entry);
    }
    Formation f=formation;
    auto basin=hydrology(basin_at(int(std::floor(f.base[0])),int(std::floor(f.base[2]))));
    if(f.kind==FormationKind::ice_sheet) {
        if(basin.fluid!=Fluid::water || basin.top<f.box.lo[1] || basin.top>f.box.hi[1])f.rock=0;
        else f.box.lo[1]=f.box.hi[1]=float(basin.top);
    }
    Bounds box=f.box;
    for(int axis=0;axis<3;axis++){box.lo[axis]-=1;box.hi[axis]+=1;}
    // The raw cache query intentionally does not qualify features. This breaks
    // dependency cycles while still including every overlapping cave primitive.
    auto geometry=cache->query(box);
    int x0=int(std::ceil(f.box.lo[0]))-1,x1=int(std::floor(f.box.hi[0]))+1;
    int z0=int(std::ceil(f.box.lo[2]))-1,z1=int(std::floor(f.box.hi[2]))+1;
    std::vector<float> xs,zs;
    for(int z=z0;z<=z1;z++)for(int x=x0;x<=x1;x++){xs.push_back(float(x));zs.push_back(float(z));}
    auto surface=columns(xs,zs);
    auto mask=support_formation(f,[&](int x,int y,int z) {
        FormationCell cell;
        if(y<=cfg.plan.floor || y>=cfg.plan.ceiling)return cell;
        const auto &column=surface[(z-z0)*(x1-x0+1)+x-x0];
        cell.rock=host_solid(column,x,y,z);
        if(!cell.rock)return cell;
        cell.carved=sample(geometry,x,y,z,column.height).carved;
        if(!cell.carved)return cell;
        cell.allowed=formation_clearance(geometry,{float(x),float(y),float(z)});
        auto fluid=basin.at(y);cell.water=fluid==Fluid::water;cell.lava=fluid==Fluid::lava;cell.liquid_top=basin.top;
        return cell;
    });
    auto result=std::make_shared<const FormationMask>(std::move(mask));
    {
        std::lock_guard<std::mutex> lock(support_mutex);
        entry->mask=result;entry->ready=true;entry->used=++support_clock;
        support_bytes+=result->bytes()+sizeof(SupportEntry)+sizeof(uint64_t)+4*sizeof(void*);
        while(support.size()>cfg.cache_regions*8 || support_bytes>cfg.cache_bytes/8) {
            auto oldest=support.end();
            for(auto it=support.begin();it!=support.end();++it)
                if(it->second->ready && it->second!=entry && (oldest==support.end() || it->second->used<oldest->second->used))oldest=it;
            if(oldest==support.end())break;
            support_bytes-=oldest->second->mask->bytes()+sizeof(SupportEntry)+sizeof(uint64_t)+4*sizeof(void*);
            support.erase(oldest);
        }
    }
    support_ready.notify_all();
    return result;
}
Region Runtime::basin_at(int x,int z) const {
    int span=cfg.plan.region_size*cfg.hydrology.basin_regions;
    return span>0?basin_of({x,z},span):Region{};
}
HydroPlan Runtime::hydrology(Region basin) const {
    int span=cfg.hydrology.basin_regions;
    if(!span)return {};
    const int limit=1000000/(cfg.plan.region_size*span)+1;
    if(std::abs(int64_t(basin.x))>limit || std::abs(int64_t(basin.z))>limit)return {};
    Cell key{basin.x,0,basin.z};
    std::shared_ptr<BasinEntry> entry;
    {
        std::unique_lock<std::mutex> lock(basin_mutex);
        if(auto it=basins.find(key);it!=basins.end()) {
            entry=it->second;
            basin_ready.wait(lock,[&]{return entry->ready;});
            entry->used=++basin_clock;
            return entry->plan;
        }
        entry=std::make_shared<BasinEntry>();
        basins.emplace(key,entry);
    }
    std::vector<std::shared_ptr<const IndexedNetwork>> owners;
    std::vector<const Network*> networks;
    for(int z=0;z<span;z++)for(int x=0;x<span;x++) {
        auto plan=cache->get({basin.x*span+x,basin.z*span+z});
        if(plan){networks.push_back(&plan->network);owners.push_back(std::move(plan));}
    }
    auto plan=plan_hydrology(basin,uint32_t(noise_seed),cfg.dimension,cfg.hydrology,sea_level,networks,outlet_probe);
    {
        std::lock_guard<std::mutex> lock(basin_mutex);
        entry->plan=plan;entry->ready=true;entry->used=++basin_clock;
        while(basins.size()>cfg.cache_regions) {
            auto oldest=basins.end();
            for(auto it=basins.begin();it!=basins.end();++it)
                if(it->second->ready && it->second!=entry &&
                   (oldest==basins.end() || it->second->used<oldest->second->used))oldest=it;
            if(oldest==basins.end())break;
            basins.erase(oldest);
        }
    }
    basin_ready.notify_all();
    return plan;
}
CacheStats Runtime::stats() const {
    auto out=cache->stats();
    {std::lock_guard<std::mutex> lock(roof_mutex);out.roof_regions=roofs.size();out.roof_bytes=roof_bytes;out.bytes+=roof_bytes;}
    {std::lock_guard<std::mutex> lock(support_mutex);out.support_entries=support.size();out.support_bytes=support_bytes;out.bytes+=support_bytes;}
    return out;
}
const Query &Runtime::point_query(int x,int z) const {
    static const Query empty;
    if(std::abs(int64_t(x))>1000000 || std::abs(int64_t(z))>1000000)return empty;
    // Reuse the immutable 32-column query for scalar mining/terrain tests. It is
    // bounded to one query per native thread and holds no generator/scene pointer.
    struct Local {uint64_t identity=0;int x=0,z=0;Query query;};
    thread_local Local local;
    int tx=int(std::floor(double(x)/32))*32,tz=int(std::floor(double(z)/32))*32;
    if(local.identity!=identity || local.x!=tx || local.z!=tz) {
        local.query=query({{float(tx-1),cfg.plan.floor,float(tz-1)},{float(tx+32),cfg.plan.ceiling,float(tz+32)}});
        local.identity=identity;local.x=tx;local.z=tz;
    }
    return local.query;
}
FieldSample Runtime::sample(const Query &query,int x,int y,int z,float roof,
                            const std::vector<uint32_t> *prepared_bin) const {
    Point p{float(x),float(y),float(z)};
    auto out=caves::sample(query,p,roof,cfg.plan.floor,cfg.plan.roof_margin,0,prepared_bin);
    if(std::abs(out.distance)<=cfg.detail_amplitude) {
        float detail=noise->GenSingle3D(p[0],p[1],p[2],noise_seed)*cfg.detail_amplitude;
        out=caves::sample(query,p,roof,cfg.plan.floor,cfg.plan.roof_margin,detail,prepared_bin);
    }
    return out;
}
bool Runtime::entrance_contains(const Passage &opening,int x,int y,int z) const {
    if(!opening.entrance || y<=cfg.plan.floor+1)return false;
    float d=passage_distance(opening,{float(x),float(y),float(z)});
    if(std::abs(d)<=cfg.detail_amplitude)d+=noise->GenSingle3D(float(x),float(y),float(z),noise_seed)*cfg.detail_amplitude;
    return d<0;
}
}
