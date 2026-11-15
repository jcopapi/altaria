#include "game/world_generation/voxel_core.h"
#include "game/world_generation/block_shapes.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <cstdlib>
using namespace altaria;
using caves::NoiseCell;
static int failures = 0;
static void check(bool ok, const char *what) {
    if (!ok) {
        std::cerr << "FAIL " << what << "\n";
        ++failures;
    }
}
static Registry blocks() {
    Registry r;std::string error;
    const char *ids[]={"air","stone","dirt","grass","sand","water","snow","bedrock"};
    for(int i=0;i<8;i++){BlockDef b;b.id=i;b.key=ids[i];b.solid=i!=0 && i!=5;b.water=i==5;assert(r.add(b,error));}
    BlockDef cave_grass;cave_grass.id=35;cave_grass.key="altaria:cave_grass";cave_grass.grass_on_rock=true;
    cave_grass.texture_layer=3;assert(r.add(cave_grass,error));
    BlockDef lava;lava.id=13;lava.key="altaria:lava";lava.solid=false;lava.liquid=true;lava.lava=true;assert(r.add(lava,error));
    r.lava=13;
    return r;
}
int main() {
    caves::NoiseSettings settings;settings.enabled=true;
    std::string error;
    check(caves::valid_noise(settings,-704,error),"default noise settings validate");
    caves::NoiseField field(settings,1337,-704);
    // Batched fills and scalar samples agree at every resolution used by tiles.
    for(int step:{1,2,4}) {
        const int n=20,ny=200,y0=-640;
        std::vector<float> surface(n*n,60.f);
        for(int i=0;i<n*n;i++)surface[i]+=float(i%7)*9.f;
        std::vector<uint8_t> cells;
        field.fill(-37,91,step,n,n,y0,step,ny,surface.data(),cells);
        int mismatches=0;
        for(int iz=0;iz<n;iz+=3)for(int ix=0;ix<n;ix+=3)for(int iy=0;iy<ny;iy+=2)
            mismatches+=int(field.at(-37+ix*step,y0+iy*step,91+iz*step,surface[iz*n+ix]))!=cells[(size_t(iz)*n+ix)*ny+iy];
        check(mismatches==0,"fill matches scalar samples");
    }
    // Depth profile: sparse near the surface, opening up toward the underworld,
    // and nearly open inside it. Lava only below its level; rock under the floor.
    auto open_fraction=[&](int y0,int y1,int *lava_above=nullptr) {
        const int n=192;std::vector<float> surface(n*n,80.f);std::vector<uint8_t> cells;
        const int ny=(y1-y0)/4+1;field.fill(0,0,1,n,n,y0,4,ny,surface.data(),cells);
        size_t open=0;
        for(size_t i=0;i<cells.size();i++) {
            open+=cells[i]!=0;
            if(lava_above && cells[i]==uint8_t(NoiseCell::lava) && y0+int(i%ny)*4>settings.hell_lava && y0+int(i%ny)*4<settings.hell_ceiling+30)
                ++*lava_above;
        }
        return double(open)/cells.size();
    };
    const double shallow=open_fraction(-120,-20),middle=open_fraction(-250,-200),near=open_fraction(-440,-400);
    int stray_lava=0;
    const double hell=open_fraction(settings.hell_floor+settings.hell_floor_amplitude+2,settings.hell_ceiling-settings.hell_ceiling_amplitude-6,&stray_lava);
    std::cout<<"open shallow "<<shallow<<" middle "<<middle<<" near underworld "<<near<<" underworld "<<hell<<"\n";
    check(shallow>.03 && shallow<.16,"near-surface caves stay sparse");
    check(near>shallow*1.25,"caves open up approaching the underworld");
    check(hell>.85,"the underworld is an open layer");
    // Only thin lava falls rise above the lava ocean.
    std::cout<<"lava above the ocean (falls) "<<stray_lava<<"\n";
    check(stray_lava<192*192*40/100,"lava above the ocean is only thin falls");
    check(field.at(10,settings.hell_floor-int(settings.hell_floor_amplitude)-3,10,80)==NoiseCell::solid,"rock below the underworld floor");
    check(field.at(10,40,10,48)==NoiseCell::solid,"no noise cave within the surface margin");
    // Lava seas are sealed bowls: lava never sits beside open air sideways.
    {
        int lava=0,leaks=0;const int n=256;std::vector<float> surface(n*n,80.f);std::vector<uint8_t> cells;
        for(int level:settings.lava_seas) {
            // From below the flooded caves' floor to just above the lava plane.
            const int rows=50;
            field.fill(-128,-128,1,n,n,level-46,1,rows,surface.data(),cells);
            for(int z=1;z<n-1;z++)for(int x=1;x<n-1;x++)for(int y=0;y<47;y++) {
                if(cells[(size_t(z)*n+x)*rows+y]!=uint8_t(NoiseCell::lava))continue;
                ++lava;
                for(auto [dx,dz]:{std::pair{1,0},{-1,0},{0,1},{0,-1}})
                    leaks+=cells[(size_t(z+dz)*n+x+dx)*rows+y]==uint8_t(NoiseCell::air);
                if(y>0)leaks+=cells[(size_t(z)*n+x)*rows+y-1]==uint8_t(NoiseCell::air);
            }
        }
        std::cout<<"lava sea cells "<<lava<<" side leaks "<<leaks<<"\n";
        check(lava>0,"lava seas exist");
        check(leaks==0,"lava seas never touch open air sideways");
    }
    // Generator integration inside the underworld window: tiles, scalar
    // samples and dripstone shapes agree; cave floors stay plain cubes.
    Settings cfg;cfg.min_y=-704;cfg.vegetation=false;cfg.caves.enabled=true;
    cfg.caves.noise=settings;
    auto r=blocks();auto g=std::make_shared<Generator>(cfg,r,1337);
    check(g->valid_generation(error) && g->noise_caves(),"generator builds noise caves");
    VerticalWindow window{VerticalWindow::underground,-620,-430};
    size_t graph_different=0,spikes=0,tips=0,other_shapes=0,lava=0,compared=0,different=0;
    for(auto [key,window]:{std::pair{TileKey{0,0,0},window},{TileKey{96,-64,0},window},
                           {TileKey{0,0,0},VerticalWindow{VerticalWindow::underground,-240,-60}},
                           {TileKey{-64,160,0},VerticalWindow{VerticalWindow::underground,-240,-60}}}) {
        auto tile=build_tile(key,0,*g,std::make_shared<EditMap>(),std::make_shared<AppearanceMap>(),false,window);
        check(bool(tile.volume),"underworld tile builds");
        if(!tile.volume)continue;
        const auto &v=*tile.volume;
        for(const auto &[index,code]:v.shapes) {
            const int type=shape_type(code);
            spikes+=type==int(BlockShape::Pillar);tips+=type==int(BlockShape::MiniPillar);
            other_shapes+=type!=int(BlockShape::Pillar) && type!=int(BlockShape::MiniPillar);
        }
        for(int z=0;z<32;z+=2)for(int x=0;x<32;x+=2)for(int y=4;y<v.height-4;y++) {
            const auto id=v.at(x,y,z);lava+=id==r.lava;
            ++compared;const auto want=g->sample(key.x+x,v.min_y+y,key.z+z);
            // Graph chambers have their own scalar/bulk suites; count them apart.
            if(id!=want && g->underground()->sample(g->underground()->point_query(key.x+x,key.z+z),key.x+x,v.min_y+y,key.z+z,g->column(key.x+x,key.z+z).height).carved){++graph_different;continue;}
            if(id!=want && different<12)std::cout<<"mismatch tile "<<id<<" scalar "<<want<<" at "<<key.x+x<<","<<v.min_y+y<<","<<key.z+z<<"\n";
            different+=id!=want;
        }
    }
    std::cout<<"underworld tiles: pillars "<<spikes<<" tips "<<tips<<" other shapes "<<other_shapes<<" lava "<<lava<<" mismatches "<<different<<"/"<<compared<<" (graph-cave cells "<<graph_different<<")"<<"\n";
    check(spikes>0 && tips>0,"dripstone uses Pillar and MiniPillar shapes");
    check(other_shapes==0,"cave floors and rock stay blocky");
    check(lava>0,"the underworld holds a lava ocean");
    check(different==0,"tiles match scalar sampling");
    if(failures){std::cerr<<failures<<" cave noise checks failed\n";return 1;}
    std::cout<<"PASS cave noise tests\n";
}
