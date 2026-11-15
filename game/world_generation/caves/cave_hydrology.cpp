#include "cave_hydrology.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
namespace altaria::caves {
static uint64_t mix(uint64_t n) {
    n=(n^(n>>30))*0xbf58476d1ce4e5b9ULL;n=(n^(n>>27))*0x94d049bb133111ebULL;return n^(n>>31);
}
static float unit(uint64_t n){return float(mix(n)>>40)/16777216.f;}
bool valid_hydrology(const HydroSettings &s) {
    if(s.basin_regions!=0 && (s.basin_regions<2 || s.basin_regions>4))return false;
    if(s.lake_depth<0 || s.lake_depth>3 || s.lava_min_depth<1 || s.lava_min_depth>3)return false;
    for(auto v:{s.lake_chance,s.lava_chance,s.lake_fill,s.lava_fill})
        if(!std::isfinite(v) || v<0 || v>1)return false;
    return s.lake_fill>=.1f && s.lake_fill<=.85f && s.lava_fill>=.1f && s.lava_fill<=.85f;
}
HydroPlan plan_hydrology(Region basin,uint64_t seed,uint64_t dimension,const HydroSettings &s,int sea,
                        const std::vector<const Network*> &networks,const std::function<Outlet(const Passage&)> &outlet) {
    HydroPlan p;p.basin=basin;
    if(!valid_hydrology(s) || !s.basin_regions)return p;
    const uint64_t key=mix(mix(mix(mix(seed^0x485944524f4c4f47ULL)^dimension)^uint32_t(basin.x))^uint32_t(basin.z));
    // We keep inland reservoirs at or below sea level until natural surface
    // arches participate in catchment planning. Above it, arches can spill.
    int limit=sea;
    WaterSource connection=WaterSource::none;
    const Chamber *lake=nullptr,*lava=nullptr;
    uint64_t lake_rank=0,lava_rank=0;
    for(auto n:networks) {
        if(!n || !n->valid || !(basin_of(n->region,s.basin_regions)==basin))continue;
        for(const auto &e:n->passages)if(e.entrance && outlet) {
            auto lip=outlet(e);
            if(lip.source==WaterSource::ocean || lip.source==WaterSource::surface_water)
                connection=std::max(connection,lip.source);
            else limit=std::min(limit,lip.dry_limit);
        }
        for(const auto &c:n->chambers) {
            if(c.isolated || c.radius[1]<6)continue;
            const bool lake_layer=s.lake_depth ? c.depth==s.lake_depth : (c.depth==1 || c.depth==2);
            if(lake_layer && c.center[1]-c.radius[1]+3<sea) {
                uint64_t rank=mix(c.id^key);
                if(!lake || rank>lake_rank || (rank==lake_rank && c.id>lake->id)){lake=&c;lake_rank=rank;}
            }
            if(c.depth>=s.lava_min_depth) {
                // Deeper layers outrank shallower ones; rank breaks ties within a layer.
                uint64_t rank=(uint64_t(c.depth)<<56)|(mix(c.id^key^19)>>8);
                if(!lava || rank>lava_rank || (rank==lava_rank && c.id>lava->id)){lava=&c;lava_rank=rank;}
            }
        }
    }
    // A wet outlet is already part of an exterior body with this same plane.
    if(connection!=WaterSource::none)p={basin,Fluid::water,connection,sea,0,{}};
    auto fill=[&](const Chamber *c,float fraction,Fluid fluid,WaterSource source) {
        float floor=c->center[1]-c->radius[1];
        int top=std::min(limit,int(std::floor(floor+2*c->radius[1]*fraction)));
        if(top>=std::ceil(floor+3))p={basin,fluid,source,top,c->id,{}};
    };
    // Hot basins can host lava; water and lava never overlap as independent
    // unbounded fields. This also keeps the static result order independent.
    if(p.fluid==Fluid::dry && lava && unit(key+2)<s.lava_chance)fill(lava,s.lava_fill,Fluid::lava,WaterSource::thermal);
    if(p.fluid==Fluid::dry && lake && unit(key+1)<s.lake_chance)fill(lake,s.lake_fill,Fluid::water,WaterSource::lake);
    if(p.fluid==Fluid::water) {
        std::unordered_map<uint64_t,const Chamber*> rooms;
        for(auto n:networks)if(n && n->valid && basin_of(n->region,s.basin_regions)==basin)
            for(const auto &c:n->chambers)rooms.emplace(c.id,&c);
        auto wet_room=[&](uint64_t id) {
            auto it=rooms.find(id);
            return it!=rooms.end() && it->second->center[1]-it->second->radius[1]+2<=p.top;
        };
        std::vector<RiverCourse> rivers;
        for(auto n:networks)if(n && n->valid && basin_of(n->region,s.basin_regions)==basin)
            for(const auto &e:n->passages) {
                if(e.entrance || e.vertical || e.path.size()<4 || !wet_room(e.a) || !wet_room(e.b))continue;
                float dx=e.to[0]-e.from[0],dz=e.to[2]-e.from[2],drop=e.from[1]-e.to[1];
                if(dx*dx+dz*dz<16*16 || std::abs(drop)<1)continue;
                // Both rooms have liquid and the whole bed lies below the
                // table. A visible stretch distinguishes a river from a
                // fully drowned connector. The pool plane remains continuous.
                bool connected=true;size_t open=0;
                for(auto q:e.path) {
                    if(q[1]-e.radius+1.25f>p.top){connected=false;break;}
                    open+=q[1]+e.radius-1.25f>p.top;
                }
                if(connected && open>=e.path.size()/8+1)rivers.push_back({e.id,drop<0});
            }
        std::sort(rivers.begin(),rivers.end(),[](const auto &a,const auto &b){return a.passage<b.passage;});
        rivers.erase(std::unique(rivers.begin(),rivers.end(),[](const auto &a,const auto &b){return a.passage==b.passage;}),rivers.end());
        if(!rivers.empty())p.rivers=std::make_shared<const std::vector<RiverCourse>>(std::move(rivers));
    }
    return p;
}
const char *fluid_name(Fluid fluid) {
    switch(fluid){case Fluid::water:return "water";case Fluid::lava:return "lava";default:return "dry";}
}
const char *water_source_name(WaterSource source) {
    switch(source){case WaterSource::lake:return "lake";case WaterSource::surface_water:return "surface_water";
        case WaterSource::ocean:return "ocean";case WaterSource::thermal:return "thermal";default:return "none";}
}
}
