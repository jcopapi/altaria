#include "game/world_generation/voxel_core.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
using namespace altaria;
static Registry blocks() {
    Registry r;std::string error;
    const char *ids[]={"air","stone","dirt","grass","sand","water","snow","bedrock"};
    for(int i=0;i<8;i++){BlockDef b;b.id=i;b.key=ids[i];b.solid=i!=0 && i!=5;b.water=i==5;assert(r.add(b,error));}
    BlockDef cave_grass;
    cave_grass.id=29;cave_grass.key="altaria:rock_grass";cave_grass.grass_on_rock=true;
    cave_grass.texture_layer=3;
    assert(r.add(cave_grass,error));
    return r;
}
int main() {
    Settings cfg;cfg.min_y=-384;cfg.vegetation=false;cfg.caves.enabled=true;
    cfg.caves.plan.infernal_chance=1;
    cfg.caves.plan.entrance_chance=1;
    cfg.caves.plan.archetypes={caves::default_archetypes().front()};
    auto r=blocks();auto g=std::make_shared<Generator>(cfg,r,1337);
    auto net=g->underground()->region({0,0});assert(net && net->network.valid && !net->network.chambers.empty());
    auto room=net->network.chambers.front();
    int cx=int(std::floor(room.center[0])),cy=int(std::floor(room.center[1])),cz=int(std::floor(room.center[2]));
    TileKey key{int(std::floor(double(cx)/32))*32,int(std::floor(double(cz)/32))*32,0};
    auto edits=std::make_shared<EditMap>();
    auto first=build_tile(key,0,*g,edits);assert(first.volume);
    auto at=[&](const MeshResult &m,int x,int y,int z){return m.volume->at(x-key.x,y-cfg.min_y,z-key.z);};
    assert(at(first,cx,cy,cz)==r.air && g->sample(cx,cy,cz)==r.air);
    // Scalar and SIMD/bulk paths must agree on floors, tunnels and host rock.
    int air=0,rock=0;
    for(int z=key.z;z<key.z+32;z+=3)for(int x=key.x;x<key.x+32;x+=3)
        for(int y=cy-24;y<=cy+24;y++) {
            auto id=at(first,x,y,z);assert(id==g->sample(x,y,z));
            air+=id==r.air;rock+=r.get(id).solid;
            assert(id!=r.snow && id!=r.water);
        }
    assert(air && rock);
    size_t open_cave_grass = 0;
    for (int z=0;z<32;z++)for (int x=0;x<32;x++)
        for (int y=0;y<first.volume->height;y++)
            if (first.volume->at(x,y,z)==r.grass && first.volume->at(x,y+1,z)==r.air) {
                assert(y > 0 && r.get(first.volume->at(x,y-1,z)).solid &&
                       first.volume->at(x,y-1,z) != r.grass);
                assert(g->sample(key.x+x,first.volume->min_y+y,key.z+z)==r.grass);
                ++open_cave_grass;
            }
    assert(open_cave_grass>0 && first.grass.size()>=open_cave_grass);
    // An independent adjacent chunk must reproduce its shared halo, including caves.
    auto neighbor=build_tile({key.x+32,key.z,0},0,*g,edits);
    for(int z=0;z<32;z++)for(int y=1;y<first.volume->height;y++)
        assert(first.volume->at(32,y,z)==neighbor.volume->at(0,y,z));
    // Session edits are authoritative. Drop volumes and regenerate from the
    // same model snapshots, without writing or reading a world file.
    WorldModel world(g);std::string error;
    assert(world.apply({1,1,{cx,cy,cz},r.stone},error));
    IVec3 mined{};bool found=false;
    for(int y=cy-24;y<cy && !found;y++)for(int x=key.x;x<key.x+32 && !found;x++)
        if(r.get(at(first,x,y,cz)).solid){mined={x,y,cz};found=true;}
    assert(found && r.get(world.block_at(mined.x,mined.y,mined.z)).solid);
    assert(world.apply({1,2,mined,r.air},error));
    auto snapshot=world.snapshot_for(key);first=MeshResult();neighbor=MeshResult();
    auto reload=build_tile(key,world.revision,*g,snapshot);
    assert(at(reload,cx,cy,cz)==r.stone && at(reload,mined.x,mined.y,mined.z)==r.air);
    WorldModel fresh(g);assert(fresh.snapshot()->empty() && fresh.block_at(cx,cy,cz)==r.air);
    assert(!net->network.entrances.empty());
    // Entrance shells expose shallow cave floors: compare every voxel near the
    // surface, including neighbors that were solid before carving.
    auto entry=net->network.entrances.front().position;
    int ex=int(entry[0]),ez=int(entry[2]);
    TileKey mouth{int(std::floor(double(ex)/32))*32,int(std::floor(double(ez)/32))*32,0};
    auto opening=build_tile(mouth,0,*g,std::make_shared<EditMap>());
    for(int z=0;z<32;z++)for(int x=0;x<32;x++) {
        auto col=g->column(mouth.x+x,mouth.z+z);
        for(int y=int(std::floor(col.height))-18;y<=int(std::ceil(col.height))+3;y++)
            assert(opening.volume->at(x,y-cfg.min_y,z)==g->sample(mouth.x+x,y,mouth.z+z));
    }
    assert(g->terrain_sample(INT32_MAX,cy,INT32_MIN)==r.air);
    Settings planted=cfg;planted.vegetation=true;Generator vegetation(planted,r,1337);
    assert(vegetation.sample(INT32_MAX,cy,INT32_MIN)==r.air);
    assert(g->underground()->point_query(INT32_MIN,INT32_MAX).primitives.empty());
    std::string invalid;auto bad=cfg.caves;bad.roof_grid=0;assert(!caves::validate(bad,invalid));
    bad=cfg.caves;bad.plan.region_size=2048;bad.roof_grid=4;bad.cache_bytes=1024*1024;
    assert(!caves::validate(bad,invalid));
    // Use distinct synthetic materials so equality cannot pass merely because
    // every subterranean biome happens to resolve to the same stone ID.
    Settings colored=cfg;auto palette=r;
    for(int i=0;i<4;i++) {
        BlockDef block;block.id=8+i;block.key="mod:rock"+std::to_string(i);assert(palette.add(block,error));
        caves::BiomeDefinition biome;biome.id="mod:layer"+std::to_string(i);biome.rock=block.key;
        biome.depths={0,0,0,0};biome.depths[i]=1;biome.weight=100;colored.caves.biomes.definitions.push_back(biome);
    }
    auto geology=std::make_shared<Generator>(colored,palette,1337);assert(geology->valid_generation(error));
    auto multicolor=build_tile(key,0,*geology,std::make_shared<EditMap>());bool colors[4]={};
    for(int z=0;z<32;z+=5)for(int x=0;x<32;x+=5)for(int y=cfg.min_y+1;y<80;y+=7) {
        auto id=multicolor.volume->at(x,y-cfg.min_y,z);assert(id==geology->sample(key.x+x,y,key.z+z));
        if(id>=8 && id<=11)colors[id-8]=true;
    }
    for(bool color:colors)assert(color);
    for(auto delta:{IVec3{-32,0,0},IVec3{32,0,0},IVec3{0,0,-32},IVec3{0,0,32}}) {
        auto neighbor=build_tile({key.x+delta.x,key.z+delta.z,0},0,*geology,std::make_shared<EditMap>());
        for(int edge=0;edge<32;edge++)for(int y=1;y<multicolor.volume->height;y++) {
            int ax=delta.x<0?-1:delta.x>0?32:edge,az=delta.z<0?-1:delta.z>0?32:edge;
            int bx=delta.x<0?31:delta.x>0?0:edge,bz=delta.z<0?31:delta.z>0?0:edge;
            assert(multicolor.volume->at(ax,y,az)==neighbor.volume->at(bx,y,bz));
        }
    }
    colored.caves.biomes.definitions[1].rock="missing:rock";Generator broken(colored,palette,1337);assert(!broken.valid_generation(error));
    colored.caves.biomes.definitions[1].rock="water";Generator liquid_rock(colored,palette,1337);assert(!liquid_rock.valid_generation(error));
    auto stats=g->underground()->stats();
    std::cout<<"CAVE_INTEGRATION_PASS: chamber/entrance scalar-bulk equality, four-color geology and four halos, grassy rock floors, dry voids, session edit-last, fresh-world reset, bounds/cache/material validation\n";
    std::cout<<"plan_ms="<<reload.stages.cave_plan_ms<<" field_ms="<<reload.stages.cave_field_ms<<" total_tile_ms="<<reload.milliseconds<<" mesh_ms="<<reload.stages.mesh_ms<<" cache_regions="<<stats.regions<<" estimated_cache_bytes="<<stats.bytes<<'\n';
}
