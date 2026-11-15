#include "game/world_generation/voxel_core.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_set>
using namespace altaria;
static IVec3 nearest(caves::Point p) {
    return {int(std::lround(p[0])),int(std::lround(p[1])),int(std::lround(p[2]))};
}
static bool open_path(const Generator &g,const Registry &r,const caves::Passage &p) {
    auto open=[&](IVec3 cell){
        for(int dy:{0,1})if(r.get(g.terrain_sample(cell.x,cell.y+dy,cell.z)).solid)return false;
        return true;
    };
    IVec3 last=nearest(p.path.front());
    if(!open(last))return false;
    for(size_t i=1;i<p.path.size();i++) {
        const auto &a=p.path[i-1],&b=p.path[i];
        int count=std::max(1,int(std::ceil(2*std::max({std::abs(b[0]-a[0]),std::abs(b[1]-a[1]),std::abs(b[2]-a[2])}))));
        for(int j=1;j<=count;j++) {
            float t=float(j)/count;
            IVec3 cell=nearest({a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1]),a[2]+t*(b[2]-a[2])});
            if(cell==last)continue;
            // Successive samples differ by at most one voxel per axis. Find
            // a six-connected path even at diagonal centerline crossings.
            int target[]={cell.x,cell.y,cell.z};
            std::array<int,3> order{0,1,2};
            bool connected=false;
            do {
                int current[]={last.x,last.y,last.z};
                connected=true;
                for(int axis:order)if(current[axis]!=target[axis]) {
                    current[axis]=target[axis];
                    if(!open({current[0],current[1],current[2]})){connected=false;break;}
                }
            } while(!connected && std::next_permutation(order.begin(),order.end()));
            if(!connected)return false;
            last=cell;
        }
    }
    return true;
}
static void test_landmark_routes(const Registry &r,bool regional,bool exploration=false) {
    Settings cfg;
    cfg.min_y=-896;
    cfg.cliff_strength=10;
    cfg.terrace_strength=.18f;
    cfg.vegetation=false;
    cfg.caves.enabled=true;
    cfg.caves.biome_variation=false;
    cfg.caves.plan.landmark_archetype="giant";
    cfg.caves.plan.depths={48,168,405,690};
    cfg.caves.plan.entrance_chance=.62f;
    cfg.caves.plan.flooded_basin_chance=.14f;
    cfg.caves.hydrology.basin_regions=3;
    cfg.caves.hydrology.lake_depth=3;
    if(regional) {
        cfg.generator_version=3;cfg.continental_scale=5300;cfg.mountain_scale=680;
        cfg.mountain_amplitude=204;cfg.warp_amplitude=34;cfg.cliff_strength=6;cfg.river_width=.028f;
    }
    if(exploration) {
        cfg.min_y=-704;cfg.meadow_hill_amplitude=9;
        cfg.caves.plan.region_size=224;cfg.caves.plan.depths={44,132,300,525};
        cfg.caves.plan.entrance_chance=.84f;cfg.caves.plan.branch_chance=.88f;cfg.caves.plan.natural_entrances=true;
    }
    auto bare=cfg;bare.caves.enabled=false;
    std::array<int,3> kinds{},dry{};
    int deepest=0,dry_landmarks=0,landmarks=0,branches=0;
    for(int seed:{1337,42,-89}) {
        Generator g(cfg,r,seed),surface(bare,r,seed);
        std::string error;assert(g.valid_generation(error));
        for(int z=-2;z<=2;z++)for(int x=-2;x<=2;x++) {
            const auto plan=g.underground()->region({x,z});
            assert(plan && plan->network.valid);
            const auto &net=plan->network;
            std::unordered_set<uint64_t> nodes;
            for(const auto &room:net.chambers)nodes.insert(room.id);
            for(const auto &room:net.external)nodes.insert(room.id);
            for(const auto &entry:net.entrances)nodes.insert(entry.id);
            for(const auto &junction:net.junctions)nodes.insert(junction.id);
            for(const auto &path:net.passages)assert(nodes.count(path.a) && nodes.count(path.b));
            for(const auto &path:net.passages)if(path.dead_end) {
                assert(path.radius<2.5f && open_path(g,r,path));
                ++branches;
            }
            for(const auto &room:net.chambers)if(room.depth==2 && room.rugged) {
                const IVec3 center=nearest(room.center);
                ++landmarks;
                if(g.terrain_sample(center.x,center.y,center.z)==r.air)++dry_landmarks;
            }
            for(const auto &room:net.chambers)if(room.depth==3) {
                IVec3 center=nearest(room.center);
                assert(!r.get(g.terrain_sample(center.x,center.y,center.z)).solid);
                deepest=std::min(deepest,center.y);
            }
            for(const auto &entry:net.entrances) {
                auto lip=nearest(entry.position);
                assert(!r.get(surface.terrain_sample(lip.x,lip.y,lip.z)).solid);
                if(exploration && entry.kind==caves::EntranceKind::mouth) {
                    const auto opening=std::find_if(net.passages.begin(),net.passages.end(),
                        [&](const auto &passage){return passage.entrance && passage.b==entry.id;});
                    assert(opening!=net.passages.end());
                    const auto inside=nearest(opening->path[opening->path.size()-2]);
                    assert(inside.y <= lip.y && lip.y - inside.y <= 6 &&
                           std::hypot(inside.x-lip.x,inside.z-lip.z) > 12);
                    for (size_t i = 1; i < opening->path.size(); ++i) {
                        const auto &a = opening->path[i - 1], &b = opening->path[i];
                        assert(b[1] >= a[1] && b[1] - a[1] <= .67f * std::hypot(b[0]-a[0],b[2]-a[2]));
                    }
                }
                int kind=int(entry.kind);
                ++kinds[kind];
                if(surface.terrain_sample(lip.x,lip.y,lip.z)==r.air)++dry[kind];
                std::unordered_set<uint64_t> reached{entry.id};
                for(size_t pass=0;pass<4;pass++)for(const auto &path:net.passages) {
                    if(reached.count(path.a)==reached.count(path.b))continue;
                    if(open_path(g,r,path)){reached.insert(path.a);reached.insert(path.b);}
                }
                bool destination=false;
                for(const auto &room:net.chambers)
                    if(room.depth==2 && room.rugged && reached.count(room.id))destination=true;
                if(!destination)std::cerr<<"Unreachable landmark: "<<seed<<' '<<x<<','<<z<<' '<<caves::entrance_kind_name(entry.kind)<<'\n';
                assert(destination);
            }
        }
    }
    for(int i=0;i<3;i++)assert(kinds[i]>0 && dry[i]>0);
    if(exploration) {
        assert(deepest<-410 && deepest>-600 && branches>30);
        assert(kinds[1] > 2 * (kinds[0] + kinds[2]));
    }
    else assert(deepest<-600);
    assert(dry_landmarks>landmarks/2);
    std::cout<<"CAVE_LANDMARK_ROUTES_PASS profile="<<cfg.generator_version<<" shafts="<<kinds[0]<<" mouths="<<kinds[1]<<" ravines="<<kinds[2]
             <<" dry="<<dry[0]<<','<<dry[1]<<','<<dry[2]<<" deepest_y="<<deepest
             <<" dry_landmarks="<<dry_landmarks<<'/'<<landmarks<<" narrow_branches="<<branches<<'\n';
}
int main() {
    Registry r;std::string error;const char *keys[]={"air","stone","dirt","grass","sand","water","snow","bedrock"};
    for(int i=0;i<8;i++){BlockDef b;b.id=i;b.key=keys[i];b.solid=i!=0 && i!=5;b.water=i==5;assert(r.add(b,error));}
    BlockDef lava;lava.id=8;lava.key="lava";lava.solid=false;lava.lava=true;lava.liquid=true;
    assert(r.add(lava,error));r.lava=lava.id;
    Settings cfg;cfg.min_y=-448;cfg.vegetation=false;cfg.caves.enabled=true;cfg.caves.plan.entrance_chance=1;
    cfg.caves.plan.archetypes={caves::default_archetypes().front()};auto bare=cfg;bare.caves.enabled=false;
    Generator caves(cfg,r,1337),surface(bare,r,1337);int checked=0;
    for(int rz=-2;rz<=2;rz++)for(int rx=-2;rx<=2;rx++) {
        auto plan=caves.underground()->region({rx,rz});assert(plan && plan->network.valid);
        for(const auto &entry:plan->network.entrances) {
            int x=int(std::floor(entry.position[0])),z=int(std::floor(entry.position[2]));auto c=surface.column(x,z);
            int highest=cfg.min_y;
            for(int y=std::min(cfg.max_y-1,int(std::ceil(c.height+cfg.cliff_strength*c.cliff))+2);y>cfg.min_y;y--)
                if(r.get(surface.sample(x,y,z)).solid){highest=y;break;}
            for(int y=std::min(int(std::floor(entry.position[1])),highest+1);y<=highest+1;y++)
                if(r.get(caves.sample(x,y,z)).solid) {
                    std::cerr<<"ENTRANCE_SEALED seed=1337 region="<<rx<<','<<rz<<" terminal="<<entry.position[1]<<" surface="<<highest<<" blocked="<<y<<'\n';return 1;
                }
            for(const auto &path:plan->network.passages)if(path.entrance && path.b==entry.id)
                for(const auto &point:path.path) {
                    int px=int(std::lround(point[0])),py=int(std::lround(point[1])),pz=int(std::lround(point[2]));
                    assert(!r.get(caves.sample(px,py,pz)).solid);
                }
            checked++;
        }
    }
    assert(checked>0);std::cout<<"CAVE_ENTRANCES_PASS checked="<<checked<<"; actual terrain surface is open above each tested terminal\n";
    test_landmark_routes(r,false);
    test_landmark_routes(r,true);
    test_landmark_routes(r,true,true);
}
