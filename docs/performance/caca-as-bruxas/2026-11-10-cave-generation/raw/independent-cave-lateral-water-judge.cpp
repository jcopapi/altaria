#include "game/world_generation/terrain_mesh.cpp"
#include <cassert>
#include <iostream>
#include <map>
#include <set>
using namespace altaria;
using namespace altaria::caves;
struct TileCandidate {TileKey key; int score; int potential;};
static Registry registry() {
    Registry r; std::string error;
    const char *keys[]={"air","stone","dirt","grass","sand","water","snow","bedrock","lava"};
    for(int i=0;i<9;++i) {
        BlockDef b; b.id=i; b.key=keys[i]; b.water=i==5; b.lava=i==8;
        b.liquid=b.water||b.lava; b.solid=i!=0&&!b.liquid;
        assert(r.add(b,error));
    }
    r.lava=8;
    return r;
}
int main() {
    std::cout << std::unitbuf;
    auto r=registry();
    Settings cfg;
    cfg.min_y=-448; cfg.vegetation=false; cfg.surface_shapes=false;
    cfg.cliff_strength=10; cfg.terrace_strength=.18f;
    cfg.mountain_threshold=.68f; cfg.mountain_blend=.20f;
    cfg.caves.enabled=true; cfg.caves.plan.entrance_chance=0;
    cfg.caves.plan.archetypes={default_archetypes().front()};
    cfg.caves.hydrology.basin_regions=3;
    auto bare_cfg=cfg;
    bare_cfg.caves.enabled=false;
    const int d[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    size_t all_tiles=0,all_candidates=0,all_carved=0,all_water=0,all_shore_faces=0,all_possible=0;
    float max_cliff=0,max_terrace=0;
    for(int seed:{1337,19,42}) {
        Generator bare(bare_cfg,r,seed),carved(cfg,r,seed);
        std::string error;
        assert(carved.valid_generation(error));
        auto *rt=carved.underground();
        std::vector<float> xs,zs;
        constexpr int radius=4096,grid=64,width=2*radius/grid+1;
        for(int z=-radius;z<=radius;z+=grid)for(int x=-radius;x<=radius;x+=grid){xs.push_back(x);zs.push_back(z);}
        auto atlas=bare.columns(xs,zs);
        std::map<std::pair<int,int>,int> scores;
        for(int j=0;j<width;++j)for(int i=0;i<width;++i) {
            int k=j*width+i;
            for(auto off:{std::pair{1,0},std::pair{0,1}}) {
                int ni=i+off.first,nj=j+off.second;
                if(ni>=width||nj>=width)continue;
                const auto &a=atlas[k], &b=atlas[nj*width+ni];
                if((a.height<=cfg.sea_level)==(b.height<=cfg.sea_level))continue;
                auto reg=basin_of({int(xs[k]),int(zs[k])},cfg.caves.plan.region_size);
                scores[{reg.x,reg.z}]+=10+int(std::abs(a.height-b.height))
                     +int(30*std::max({a.cliff,b.cliff,a.terrace_step,b.terrace_step}));
            }
        }
        std::vector<std::pair<std::pair<int,int>,int>> ranking(scores.begin(),scores.end());
        std::sort(ranking.begin(),ranking.end(),[](auto a,auto b){return a.second!=b.second?a.second>b.second:a.first<b.first;});
        std::vector<std::pair<int,int>> regions;
        for(int quadrant=0;quadrant<4;++quadrant)for(auto &[reg,score]:ranking) {
            if((reg.first>=0?1:0)+(reg.second>=0?2:0)!=quadrant)continue;
            int count=0;
            for(auto q:regions)count+=(q.first>=0?1:0)+(q.second>=0?2:0)==quadrant;
            if(count>=3)break;
            regions.push_back(reg);
        }
        if(seed==1337 && std::find(regions.begin(),regions.end(),std::pair{-6,7})==regions.end())regions.push_back({-6,7});
        std::vector<TileCandidate> candidates;
        for(auto [rx,rz]:regions) {
            std::vector<std::shared_ptr<const IndexedNetwork>> owners;
            std::vector<Bounds> shallow;
            for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx) {
                auto n=rt->region({rx+dx,rz+dz}); assert(n&&n->network.valid&&n->network.entrances.empty());
                for(auto &p:n->primitives)if(p.bounds.lo[1]<=cfg.sea_level)shallow.push_back(p.bounds);
                owners.push_back(std::move(n));
            }
            constexpr int span=256, side=span+2;
            const int base_x=rx*span,base_z=rz*span;
            xs.clear();zs.clear();
            for(int z=-1;z<=span;++z)for(int x=-1;x<=span;++x){xs.push_back(base_x+x);zs.push_back(base_z+z);}
            auto cols=bare.columns(xs,zs);
            auto at=[&](int x,int z)->const Column&{return cols[(z+1)*side+x+1];};
            for(int oz=0;oz<span;oz+=32)for(int ox=0;ox<span;ox+=32) {
                int lowest_water=999,land=0,water=0,possible=0,score=0;
                for(int z=oz-1;z<=oz+32;++z)for(int x=ox-1;x<=ox+32;++x) {
                    const auto &c=at(x,z);
                    int first=int(std::floor(c.height-cfg.cliff_strength*c.cliff))+1;
                    if(first<=cfg.sea_level){lowest_water=std::min(lowest_water,first);water++;}
                    land+=c.height>cfg.sea_level;
                    if(x<ox||z<oz||x>=ox+32||z>=oz+32)continue;
                    int upper=std::min(cfg.sea_level,int(std::floor(c.height-cfg.caves.plan.roof_margin)));
                    for(auto &n:d) {
                        const auto &q=at(x+n[0],z+n[2]);
                        int low=int(std::floor(q.height-cfg.cliff_strength*q.cliff))+1;
                        if(low<=cfg.sea_level&&low-n[1]<=upper)possible++;
                    }
                    if(first<=cfg.sea_level+8)score=std::max(score,int(100*std::max(c.cliff,c.terrace_step)));
                }
                if(!water||!land)continue;
                const Bounds tile{{float(base_x+ox),float(lowest_water-1),float(base_z+oz)},
                                  {float(base_x+ox+31),float(cfg.sea_level),float(base_z+oz+31)}};
                if(std::none_of(shallow.begin(),shallow.end(),[&](const Bounds& b){return b.intersects(tile);}))continue;
                candidates.push_back({{base_x+ox,base_z+oz,0},score+std::min(land,water),possible});
            }
        }
        std::sort(candidates.begin(),candidates.end(),[](auto a,auto b){
            if(a.potential!=b.potential)return a.potential>b.potential;
            if(a.score!=b.score)return a.score>b.score;
            return std::pair{a.key.x,a.key.z}<std::pair{b.key.x,b.key.z};
        });
        all_candidates+=candidates.size();
        size_t tiles=0,new_void=0,old_water=0,shore=0,possible=0;
        std::map<int,size_t> fluid_tiles;
        auto edits=std::make_shared<EditMap>();auto appearances=std::make_shared<AppearanceMap>();
        for(auto entry:candidates) {
            if(tiles==56)break;
            const auto &key=entry.key;
            StageTimings timings;
            auto raw=make_volume(key,bare,edits,appearances,timings);
            auto cave=make_volume(key,carved,edits,appearances,timings);
            assert(raw->min_y==cave->min_y&&raw->height==cave->height);
            tiles++;possible+=entry.potential;
            auto plan=rt->hydrology(rt->basin_at(key.x,key.z));fluid_tiles[int(plan.fluid)]++;
            for(int z=0;z<32;++z)for(int x=0;x<32;++x) {
                const auto &col=raw->columns[(z+1)*34+x+1];
                for(int y=1;y<=cfg.sea_level-raw->min_y;++y) {
                    auto a=raw->at(x,y,z),b=cave->at(x,y,z);
                    if(a==r.water) {
                        old_water++;
                        for(auto &n:d)if(r.get(raw->at(x+n[0],y+n[1],z+n[2])).solid) {
                            shore++;max_cliff=std::max(max_cliff,col.cliff);max_terrace=std::max(max_terrace,col.terrace_step);
                        }
                    }
                    if(!r.get(a).solid||r.get(b).solid)continue;
                    new_void++;
                    for(auto &n:d) {
                        if(raw->at(x+n[0],y+n[1],z+n[2])!=r.water)continue;
                        int wx=key.x+x,wy=raw->min_y+y,wz=key.z+z;
                        auto h=rt->hydrology(rt->basin_at(wx,wz));
                        bool ok=h.fluid==Fluid::water&&h.top==cfg.sea_level
                             &&(h.source==WaterSource::surface_water||h.source==WaterSource::ocean)&&b==r.water;
                        std::cout<<(ok?"MATCHED_CONTACT":"LATERAL_WATER_LEAK")<<" seed="<<seed<<" cavity="<<wx<<','<<wy<<','<<wz
                                 <<" exterior="<<wx+n[0]<<','<<wy+n[1]<<','<<wz+n[2]
                                 <<" raw_id="<<a<<" cavity_id="<<b<<" scalar_id="<<carved.sample(wx,wy,wz)
                                 <<" exterior_scalar="<<bare.sample(wx+n[0],wy+n[1],wz+n[2])
                                 <<" source="<<water_source_name(h.source)<<" top="<<h.top
                                 <<" cliff="<<col.cliff<<" terrace="<<col.terrace_step<<'\n';
                        if(!ok)return 1;
                    }
                }
            }
        }
        all_tiles+=tiles;all_carved+=new_void;all_water+=old_water;all_shore_faces+=shore;all_possible+=possible;
        std::cout<<"SEED seed="<<seed<<" regions="<<regions.size()<<" eligible_tiles="<<candidates.size()
                 <<" tested_tiles="<<tiles<<" pre_cave_water="<<old_water<<" carved_below_sea="<<new_void
                 <<" shoreline_faces="<<shore<<" possible_envelope_columns="<<possible
                 <<" dry_tiles="<<fluid_tiles[0]<<" lake_tiles="<<fluid_tiles[1]<<" lava_tiles="<<fluid_tiles[2]<<'\n';
    }
    if(!all_tiles||!all_carved||!all_water||!all_shore_faces)return 2;
    std::cout<<"LATERAL_WATER_SWEEP_PASS eligible_tiles="<<all_candidates<<" tested_tiles="<<all_tiles
             <<" pre_cave_water="<<all_water<<" carved_below_sea="<<all_carved<<" shoreline_faces="<<all_shore_faces
             <<" possible_envelope_columns="<<all_possible<<" shoreline_cliff_max="<<max_cliff
             <<" shoreline_terrace_max="<<max_terrace<<'\n';
}
