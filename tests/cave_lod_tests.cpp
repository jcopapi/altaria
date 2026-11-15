#include "game/world_generation/terrain_mesh.cpp"
#include <cassert>
#include <iostream>
using namespace altaria;
static void compact_volume(const Registry &r) {
    Settings cfg;cfg.min_y=-896;cfg.surface_shapes=true;cfg.caves.enabled=true;
    cfg.caves.biome_variation=false;cfg.caves.plan.landmark_archetype="giant";
    cfg.caves.plan.depths={48,168,405,690};
    Generator g(cfg,r,1337);std::string error;assert(g.valid_generation(error));
    auto edits=std::make_shared<EditMap>();
    auto surfaces=std::make_shared<AppearanceMap>();
    auto block=[&](const Volume &v,int x,int y,int z){
        if(y==cfg.min_y)return r.bedrock;
        if(y<v.min_y)return v.bottom_fill;
        if(y>=v.min_y+v.height*v.y_step)return r.air;
        return v.at(x,(y-v.min_y)/v.y_step,z);
    };
    int verified=0;
    for(int level:{0,1,2}) {
        const int step=1<<level, size=32*step;
        TileKey key{0,0,level},east{size,0,level};
        StageTimings timings;
        auto a=make_volume(key,g,edits,surfaces,timings);
        auto b=make_volume(east,g,edits,surfaces,timings);
        assert(a->min_y>cfg.min_y && a->min_y+a->height*step<cfg.max_y);
        for(int z=0;z<32;z+=5)for(int y=cfg.min_y;y<cfg.max_y;y+=step) {
            auto id=block(*a,32,y,z);
            assert(id==block(*b,0,y,z));
            if(y<g.column(size,z*step).height-16)
                assert(id==g.terrain_sample(size,y,z*step));
            verified++;
        }
    }
    const IVec3 low{10,cfg.min_y+4,10},high{10,cfg.max_y-2,10};
    (*edits)[low]=r.air;(*edits)[high]=r.stone;
    StageTimings timings;
    auto edited=make_volume({0,0,0},g,edits,surfaces,timings);
    assert(low.y>=edited->min_y && high.y<edited->min_y+edited->height);
    assert(block(*edited,low.x,low.y,low.z)==r.air && block(*edited,high.x,high.y,high.z)==r.stone);
    assert(block(*edited,low.x,low.y-1,low.z)==r.stone);
    auto mesh=build_tile({0,0,0},0,g,std::make_shared<EditMap>(),surfaces,false);
    assert(mesh.volume->bottom_fill==r.stone);
    assert(std::any_of(mesh.collision_rects.begin(),mesh.collision_rects.end(),[&](const CollisionRect &face){
        return face.axis==1 && !face.positive && face.corner[1]==cfg.min_y;
    }));
    std::cout<<"COMPACT_CAVE_LOD_PASS halo_samples="<<verified<<" bottom_and_sky_edits=2\n";
}
int main() {
    Registry r;std::string error;const char *keys[]={"air","stone","dirt","grass","sand","water","snow","bedrock"};
    for(int i=0;i<8;i++){BlockDef b;b.id=i;b.key=keys[i];b.solid=i!=0 && i!=5;b.water=i==5;assert(r.add(b,error));}
    Settings cfg;cfg.min_y=-448;cfg.caves.enabled=true;cfg.caves.plan.infernal_chance=1;
    cfg.caves.plan.archetypes={caves::default_archetypes().front()};cfg.vegetation=false;cfg.far_vertical_trim=true;
    Generator g(cfg,r,1337);assert(g.valid_generation(error));size_t checked=0,air=0;
    for(auto region:{caves::Region{0,0},caves::Region{-1,-1}}) {
        auto plan=g.underground()->region(region);assert(plan && !plan->network.chambers.empty());
        auto center=plan->network.chambers.front().center;
        for(int lod:{1,2}) {
            int size=32<<lod;TileKey key{int(std::floor(center[0]/size))*size,int(std::floor(center[2]/size))*size,lod};
            StageTimings timings;auto v=make_volume(key,g,std::make_shared<EditMap>(),std::make_shared<AppearanceMap>(),timings);
            assert(v->min_y==cfg.min_y && v->height==(cfg.max_y-cfg.min_y)/v->y_step);
            size_t carved=0;
            for(int z=-1;z<=32;z+=3)for(int x=-1;x<=32;x+=3) {
                const auto &column=v->columns[(z+1)*34+x+1];
                for(int y=1;y<v->height;y+=3) {
                    int wy=v->min_y+y*v->y_step;if(wy>=column.height-cfg.cliff_strength*column.cliff-16)continue;
                    auto id=v->at(x,y,z);assert(id==g.sample(key.x+x*v->step,wy,key.z+z*v->step));
                    carved+=id==r.air;checked++;
                }
            }
            assert(carved>0);air+=carved;
            for(auto offset:{IVec3{-size,0,0},IVec3{size,0,0},IVec3{0,0,-size},IVec3{0,0,size}}) {
                auto neighbor=make_volume({key.x+offset.x,key.z+offset.z,lod},g,std::make_shared<EditMap>(),std::make_shared<AppearanceMap>(),timings);
                for(int edge=0;edge<32;edge++)for(int y=1;y<v->height;y++) {
                    int ax=offset.x<0?-1:offset.x>0?32:edge,az=offset.z<0?-1:offset.z>0?32:edge;
                    int bx=offset.x<0?31:offset.x>0?0:edge,bz=offset.z<0?31:offset.z>0?0:edge;
                    assert(v->at(ax,y,az)==neighbor->at(bx,y,bz));
                }
            }
        }
    }
    std::cout<<"CAVE_LOD_PASS scalar_samples="<<checked<<" carved_samples="<<air<<"; LOD1/2 four halos, negative coordinates and full logical depth\n";
    compact_volume(r);
}
