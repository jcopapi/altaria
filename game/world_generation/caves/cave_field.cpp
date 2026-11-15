#include "cave_field.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
namespace altaria::caves {
size_t CellHash::operator()(Cell c) const {return uint32_t(c.x)*73856093u^uint32_t(c.y)*19349663u^uint32_t(c.z)*83492791u;}
static Cell cell(Point p) {return {int(std::floor(p[0]/32)),int(std::floor(p[1]/32)),int(std::floor(p[2]/32))};}
IndexedNetwork::IndexedNetwork(Network p,std::vector<Formation> details):network(std::move(p)),formations(std::move(details)) {
    for(uint32_t i=0;i<network.chambers.size();i++)primitives.push_back({false,i,bounds(network.chambers[i])});
    for(uint32_t i=0;i<network.passages.size();i++) {
        const auto &p=network.passages[i];
        for(uint32_t j=1;j<p.path.size();j++) {
            Bounds b;for(int axis=0;axis<3;axis++){b.lo[axis]=std::min(p.path[j-1][axis],p.path[j][axis])-p.radius;b.hi[axis]=std::max(p.path[j-1][axis],p.path[j][axis])+p.radius;}
            primitives.push_back({true,i,b,j});
        }
    }
    for(uint32_t i=0;i<primitives.size();i++) {
        for(int axis=0;axis<3;axis++){primitives[i].bounds.lo[axis]-=.75f;primitives[i].bounds.hi[axis]+=.75f;}
        auto a=cell(primitives[i].bounds.lo),b=cell(primitives[i].bounds.hi);
        int64_t count=int64_t(b.x-a.x+1)*(b.y-a.y+1)*(b.z-a.z+1);
        if(count>4096){large.push_back(i);continue;}
        for(int z=a.z;z<=b.z;z++)for(int y=a.y;y<=b.y;y++)for(int x=a.x;x<=b.x;x++)bins[{x,y,z}].push_back(i);
    }
    bytes=sizeof(*this)+network.chambers.capacity()*sizeof(Chamber)+network.external.capacity()*sizeof(Chamber)+network.passages.capacity()*sizeof(Passage)+network.entrances.capacity()*sizeof(Terminal)+primitives.capacity()*sizeof(Primitive)+large.capacity()*sizeof(uint32_t);
    bytes+=network.junctions.capacity()*sizeof(Junction);
    for(auto &p:network.passages)bytes+=p.path.capacity()*sizeof(Point);
    for(auto &c:network.chambers)bytes+=c.archetype.capacity();
    for(auto &c:network.external)bytes+=c.archetype.capacity();
    bytes+=formations.capacity()*sizeof(Formation);
    bytes+=bins.bucket_count()*sizeof(void*);
    for(auto &[key,values]:bins)bytes+=sizeof(key)+sizeof(values)+2*sizeof(void*)+values.capacity()*sizeof(uint32_t);
}
PlanCache::PlanCache(std::shared_ptr<const Planner> p,size_t r,size_t b,
                     std::function<std::vector<Formation>(const Network &)> decorate)
    :planner(std::move(p)),formation_planner(std::move(decorate)),max_regions(std::max(size_t(1),r)),max_bytes(std::max(size_t(1),b)) {}
std::shared_ptr<const IndexedNetwork> PlanCache::resident(Region r) const {
    std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock())return {};
    Cell key{r.x,0,r.z};auto it=entries.find(key);if(it!=entries.end())return it->second.plan;
    auto retained=live.find(key);return retained==live.end()?nullptr:retained->second.lock();
}
std::shared_ptr<const IndexedNetwork> PlanCache::get(Region r) {
    if(!planner)return {};
    Cell key{r.x,0,r.z};
    std::shared_ptr<const IndexedNetwork> generated;
    {
        std::lock_guard<std::mutex> lock(mutex);auto it=entries.find(key);
        if(it!=entries.end()){++hits;it->second.used=++clock;return it->second.plan;}
        auto retained=live.find(key);if(retained!=live.end())generated=retained->second.lock();
        if(generated)++hits;else ++misses;
    }
    if(!generated) {
        auto network=planner->region(r);
        auto details=network.valid && formation_planner?formation_planner(network):std::vector<Formation>{};
        generated=std::make_shared<IndexedNetwork>(std::move(network),std::move(details));
    }
    if(!generated->network.valid)return generated;
    std::lock_guard<std::mutex> lock(mutex);
    auto existing=entries.find(key);if(existing!=entries.end())return existing->second.plan;
    // Reuse a plan that another worker published, including plans whose
    // lifetime is tied to in-flight queries rather than a resident entry.
    auto retained=live.find(key);
    if(retained!=live.end())if(auto shared=retained->second.lock())generated=std::move(shared);
    live[key]=generated;
    if(live.size()>max_regions*2) {
        for(auto it=live.begin();it!=live.end();) {
            if(it->second.expired())it=live.erase(it);else ++it;
        }
    }
    if(generated->bytes>max_bytes)return generated;
    while(!entries.empty() && (entries.size()>=max_regions || bytes+generated->bytes>max_bytes)) {
        auto oldest=entries.end();
        for(auto it=entries.begin();it!=entries.end();++it)
            if(it->second.plan.use_count()==1 && (oldest==entries.end() || it->second.used<oldest->second.used))oldest=it;
        // Loaded near chunks and in-flight queries pin their plans. Under a
        // tight budget a cold distant query remains uncached instead of evicting
        // nearby metadata or exceeding the owned cache budget.
        if(oldest==entries.end())return generated;
        bytes-=oldest->second.plan->bytes;entries.erase(oldest);
    }
    bytes+=generated->bytes;entries.emplace(key,Entry{generated,++clock});return generated;
}
Query PlanCache::query(Bounds box) {
    Query out;if(!planner || !planner->valid())return out;
    for(int a=0;a<3;a++)if(!std::isfinite(box.lo[a]) || !std::isfinite(box.hi[a]) || box.lo[a]>box.hi[a] || std::abs(box.lo[a])>1000000 || std::abs(box.hi[a])>1000000)return out;
    // Tile queries only; a debug request must not accidentally plan the world.
    if(box.hi[0]-box.lo[0]>256 || box.hi[1]-box.lo[1]>max_vertical_span || box.hi[2]-box.lo[2]>256)return out;
    int span=planner->region_size();auto lo=cell(box.lo),hi=cell(box.hi);
    std::unordered_set<uint64_t> rooms;
    std::unordered_map<uint64_t,std::unordered_set<uint32_t>> passages;
    for(int z=int(std::floor(box.lo[2]/span))-1;z<=int(std::floor(box.hi[2]/span))+1;z++)for(int x=int(std::floor(box.lo[0]/span))-1;x<=int(std::floor(box.hi[0]/span))+1;x++) {
        auto owner=get({x,z});if(!owner || !owner->network.valid)continue;
        std::unordered_set<uint32_t> local;
        for(auto i:owner->large)local.insert(i);
        for(int cz=lo.z;cz<=hi.z;cz++)for(int cy=lo.y;cy<=hi.y;cy++)for(int cx=lo.x;cx<=hi.x;cx++) {
            auto it=owner->bins.find({cx,cy,cz});if(it!=owner->bins.end())local.insert(it->second.begin(),it->second.end());
        }
        bool retained=false;
        for(auto i:local) {
            auto &p=owner->primitives[i];if(!p.bounds.intersects(box))continue;
            uint64_t id=p.passage?owner->network.passages[p.index].id:owner->network.chambers[p.index].id;
            if(p.passage ? !passages[id].insert(p.segment).second : !rooms.insert(id).second)continue;
            out.primitives.push_back({owner.get(),i});retained=true;
        }
        for(const auto &f:owner->formations)if(f.box.intersects(box)) {
            Bounds clipped=f.box;
            for(int axis=0;axis<3;axis++){clipped.lo[axis]=std::max(clipped.lo[axis],box.lo[axis]);clipped.hi[axis]=std::min(clipped.hi[axis],box.hi[axis]);}
            auto low=cell(clipped.lo),high=cell(clipped.hi);
            for(int cz=low.z;cz<=high.z;cz++)for(int cy=low.y;cy<=high.y;cy++)for(int cx=low.x;cx<=high.x;cx++)
                out.formations[{cx,cy,cz}].push_back(&f);
            retained=true;
        }
        if(retained)out.owners.push_back(std::move(owner));
    }
    // Stable traversal and tie ownership regardless of cache or hash order.
    std::sort(out.primitives.begin(),out.primitives.end(),[](auto a,auto b){
        auto id=[](auto r){const auto &p=r.owner->primitives[r.primitive];return p.passage?r.owner->network.passages[p.index].id:r.owner->network.chambers[p.index].id;};auto ai=id(a),bi=id(b);return ai!=bi ? ai<bi : a.owner->primitives[a.primitive].segment<b.owner->primitives[b.primitive].segment;
    });
    for(uint32_t i=0;i<out.primitives.size();i++) {
        auto ref=out.primitives[i];auto b=ref.owner->primitives[ref.primitive].bounds;
        for(int axis=0;axis<3;axis++){b.lo[axis]=std::max(b.lo[axis],box.lo[axis]);b.hi[axis]=std::min(b.hi[axis],box.hi[axis]);}
        auto a=cell(b.lo),z=cell(b.hi);
        for(int cz=a.z;cz<=z.z;cz++)for(int cy=a.y;cy<=z.y;cy++)for(int cx=a.x;cx<=z.x;cx++)out.bins[{cx,cy,cz}].push_back(i);
    }
    for(auto &[key,details]:out.formations)
        std::sort(details.begin(),details.end(),[](const Formation *a,const Formation *b){return a->id<b->id;});
    return out;
}
CacheStats PlanCache::stats() const {
    std::lock_guard<std::mutex> lock(mutex);CacheStats out{entries.size(),bytes,hits,misses};
    for(const auto &entry:entries)out.pinned_regions+=entry.second.plan.use_count()>1;
    return out;
}
static float segment(Point p,Point a,Point b) {
    float dot=0,length=0;for(int axis=0;axis<3;axis++){dot+=(p[axis]-a[axis])*(b[axis]-a[axis]);length+=(b[axis]-a[axis])*(b[axis]-a[axis]);}
    float t=length>1e-10f?std::clamp(dot/length,0.f,1.f):0,d=0;
    for(int axis=0;axis<3;axis++){float q=p[axis]-a[axis]-t*(b[axis]-a[axis]);d+=q*q;}return std::sqrt(d);
}
float passage_distance(const Passage &p,Point point) {
    float d=1e6f;
    for(size_t i=1;i<p.path.size();i++)d=std::min(d,segment(point,p.path[i-1],p.path[i])-p.radius);
    return d;
}
FieldSample sample(const Query &query,Point p,float roof,float floor,float margin,float detail,
                   const std::vector<uint32_t> *prepared_bin) {
    FieldSample out;
    if(!std::isfinite(roof) || !std::isfinite(floor) || !std::isfinite(margin) || !std::isfinite(detail) || margin<0 || p[1]<=floor+1)return out;
    for(float v:p)if(!std::isfinite(v) || std::abs(v)>1000000)return out;
    if(!prepared_bin) {
        auto candidates=query.bins.find(cell(p));if(candidates==query.bins.end())return out;
        prepared_bin=&candidates->second;
    }
    for(auto index:*prepared_bin) {
        const auto &ref=query.primitives[index];
        const auto &primitive=ref.owner->primitives[ref.primitive];
        bool outside=false;
        for(int a=0;a<3;a++)if(p[a]<primitive.bounds.lo[a] || p[a]>primitive.bounds.hi[a]){outside=true;break;}
        if(outside)continue;
        float d=1e6f;uint64_t id;int depth;bool entrance=false;
        if(primitive.passage) {
            const auto &path=ref.owner->network.passages[primitive.index];id=path.id;depth=-1;
            auto i=primitive.segment;d=segment(p,path.path[i-1],path.path[i])-path.radius;
            // Only explicitly planned entry capsules may cross the roof. The
            // whole route needs this exception, including steep outlet climbs.
            entrance=path.entrance;
        } else {
            const auto &c=ref.owner->network.chambers[primitive.index];id=c.id;depth=c.depth;
            Point q;
            float k0=0,k1=0;
            for(int axis=0;axis<3;axis++){q[axis]=(p[axis]-c.center[axis])/c.radius[axis];k0+=q[axis]*q[axis];k1+=q[axis]*q[axis]/(c.radius[axis]*c.radius[axis]);}
            if(c.rugged) {
                // Rock ribs cut into the hall's envelope while leaving its center open.
                const float diagonal=q[0]+((c.id&1)?q[2]:-q[2]);
                const float rib=std::max(0.f,1.f-5.f*std::abs(diagonal-.42f));
                k0+=rib*(.75f*std::max(-q[1],0.f)+.25f*std::abs(q[1]));
                k0+=.36f*std::abs(q[0]*q[2])*(1.f+q[0]);
            }
            const float radial=std::sqrt(k0);
            d=k1<1e-12f?-std::min({c.radius[0],c.radius[1],c.radius[2]}):radial*(radial-1)/std::sqrt(k1);
            if(c.rugged && d<.75f) {
                // A tilted column and a low pinnacle leave the central route
                // free. Passage primitives can still cut through the rock.
                const float z=(c.id&1)?q[2]:-q[2];
                const float scale=std::min(c.radius[0],c.radius[2]);
                const float detail_margin=.75f/scale;
                const float px=q[0]-.36f-.13f*q[1],pz=z+.34f-.09f*q[1];
                const float radius=.12f+.17f*q[1]*q[1];
                if(std::abs(px)<radius+detail_margin && std::abs(pz)<radius+detail_margin)
                    d=std::max(d,(radius-std::sqrt(px*px+pz*pz))*scale);
                if(q[1]<-.12f) {
                    const float sx=q[0]+.31f+.1f*q[1],sz=z-.41f;
                    const float footing=(-q[1]-.12f)*.26f;
                    if(std::abs(sx)<footing+detail_margin && std::abs(sz)<footing+detail_margin)
                        d=std::max(d,(footing-std::sqrt(sx*sx+sz*sz))*scale);
                }
            }
        }
        if(!entrance && p[1]>roof-margin)continue;
        d+=std::clamp(detail,-.75f,.75f);
        if(d<out.distance){out.distance=d;out.owner=id;out.depth=depth;}
    }
    out.carved=out.distance<0;return out;
}
uint16_t sample_formation(const Query &query,Point p,bool water,bool lava,int top,const std::vector<uint32_t> *prepared,
                          const std::vector<const Formation*> *details, DripstoneSection *dripstone) {
    if(query.formations.empty())return 0;
    if(!details || !prepared)for(float v:p)if(!std::isfinite(v) || std::abs(v)>1000000)return 0;
    if(!details) {
        auto i=query.formations.find(cell(p));if(i==query.formations.end())return 0;
        details=&i->second;
    }
    if (dripstone)
        *dripstone = DripstoneSection::none;
    for(auto f:*details) {
        auto material=formation_voxel(*f,p,water,lava,top);
        if(!material)continue;
        auto support=query.support.find(f->id);
        if(support!=query.support.end() && !support->second->contains(p))continue;
        if (!formation_clearance(query, p, prepared))
            return 0;
        if (dripstone && f->kind == FormationKind::stalactite)
            *dripstone = (f->base[1] - p[1]) / f->height > .62f ? DripstoneSection::tip : DripstoneSection::base;
        return material;
    }
    return 0;
}
bool formation_clearance(const Query &query,Point p,const std::vector<uint32_t> *prepared) {
    if(!prepared) {
        auto bin=query.bins.find(cell(p));
        if(bin==query.bins.end())return false;
        prepared=&bin->second;
    }
    // Keep all planned corridors and the central shaft of every chamber
    // free of solid decoration, including crossings of two chamber profiles.
    for(auto j:*prepared) {
        auto ref=query.primitives[j];const auto &primitive=ref.owner->primitives[ref.primitive];
        bool outside=false;
        for(int axis=0;axis<3;axis++)outside|=p[axis]<primitive.bounds.lo[axis] || p[axis]>primitive.bounds.hi[axis];
        if(outside)continue;
        if(primitive.passage) {
            const auto &e=ref.owner->network.passages[primitive.index];
            if(segment(p,e.path[primitive.segment-1],e.path[primitive.segment])<e.radius+.75f)return false;
        } else {
            const auto &c=ref.owner->network.chambers[primitive.index];
            float x=(p[0]-c.center[0])/c.radius[0],z=(p[2]-c.center[2])/c.radius[2];
            if(x*x+z*z<.09f)return false;
        }
    }
    return true;
}
}
