#include "game/world_generation/voxel_core.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
using namespace altaria;
using Record=std::array<float,22>;
using Quad=std::array<Record,4>;
static std::vector<Quad> quads(const Surface &surface) {
    assert(surface.vertices.size()%4==0);std::vector<Quad> out;
    for(size_t start=0;start<surface.vertices.size();start+=4) {
        Quad q;
        for(int i=0;i<4;i++) {
            const auto &v=surface.vertices[start+i];
            q[i]={v.x,v.y,v.z,v.nx,v.ny,v.nz,v.u,v.v,v.r,v.g,v.b,v.tr,v.tg,v.tb,v.preserve,v.brightness,v.texture,v.overlay,v.painted,v.overlay_tint,v.decal,v.wind};
            for(float value:q[i])assert(std::isfinite(value));
        }
        std::sort(q.begin(),q.end());out.push_back(q);
    }
    for(size_t i=0;i<surface.indices.size();i+=3) {
        auto a=surface.vertices[surface.indices[i]],b=surface.vertices[surface.indices[i+1]],c=surface.vertices[surface.indices[i+2]];
        float ab[3]={b.x-a.x,b.y-a.y,b.z-a.z},ac[3]={c.x-a.x,c.y-a.y,c.z-a.z};
        float dot=(ab[1]*ac[2]-ab[2]*ac[1])*a.nx+(ab[2]*ac[0]-ab[0]*ac[2])*a.ny+(ab[0]*ac[1]-ab[1]*ac[0])*a.nz;
        assert(dot<=.001f); // Godot clockwise front faces, including the -Y base.
    }
    std::sort(out.begin(),out.end());return out;
}
static void equivalent(const MeshResult &a,const MeshResult &b) {
    auto left=quads(a.solid),right=quads(b.solid);
    if(left!=right) {
        std::cerr<<"FAR_MESH_DIFF tile="<<a.key.x<<','<<a.key.z<<','<<a.key.level<<" windows="
                 <<a.volume_min_y<<':'<<a.volume_height<<'/'<<b.volume_min_y<<':'<<b.volume_height<<'\n';
        std::vector<Quad> delta;std::set_difference(left.begin(),left.end(),right.begin(),right.end(),std::back_inserter(delta));
        if(!delta.empty()){std::cerr<<"full_only:";for(auto value:delta.front()[0])std::cerr<<' '<<value;std::cerr<<'\n';}
        delta.clear();std::set_difference(right.begin(),right.end(),left.begin(),left.end(),std::back_inserter(delta));
        if(!delta.empty()){std::cerr<<"trim_only:";for(auto value:delta.front()[0])std::cerr<<' '<<value;std::cerr<<'\n';}
    }
    assert(left==right);assert(quads(a.water)==quads(b.water));
    assert(quads(a.decorations)==quads(b.decorations));assert(quads(a.bushes)==quads(b.bushes));
    assert(a.grass.size()==b.grass.size());
}
static Registry registry() {
    Registry r;std::string error;const char *keys[]={"air","stone","dirt","grass","sand","water","snow","bedrock"};
    for(int i=0;i<8;i++){BlockDef b;b.id=i;b.key=keys[i];b.color={.1f*i,.07f*i,.05f*i};b.solid=i!=0 && i!=5;b.water=i==5;assert(r.add(b,error));}
    return r;
}
int main() {
    auto r=registry();Settings full;full.min_y=-448;full.max_y=320;full.vegetation=false;
    Settings trimmed=full;trimmed.far_vertical_trim=true;
    Generator baseline(full,r,1337),fast(trimmed,r,1337);
    auto empty=std::make_shared<EditMap>();auto surfaces=std::make_shared<AppearanceMap>();
    std::vector<TileKey> cases{{0,0,3},{-256,-256,3},{1024,0,3},{-4096,2048,4},{8192,8192,5}};
    std::vector<float> xs,zs;
    for(int z=-8192;z<=8192;z+=128)for(int x=-8192;x<=8192;x+=128){xs.push_back(x);zs.push_back(z);}
    auto columns=baseline.columns(xs,zs);bool arch=false,ocean=false,coast=false;
    for(size_t i=0;i<columns.size();i++) {
        const auto &c=columns[i];TileKey key{int(std::floor(xs[i]/256))*256,int(std::floor(zs[i]/256))*256,3};
        if(!ocean && c.height<full.sea_level-20){cases.push_back(key);ocean=true;}
        if(!coast && std::abs(c.height-full.sea_level)<2){cases.push_back(key);coast=true;}
        if(!arch && c.arch_mask>=.18f) {
            for(int y=int(std::floor(c.height/4))*4;y>full.sea_level+6;y-=4)
                if(baseline.natural_arch_carved(c,int(xs[i]),y,int(zs[i]))){arch=true;cases.push_back(key);break;}
        }
        if(arch && ocean && coast)break;
    }
    assert(arch && ocean && coast);
    size_t bytes_full=0,bytes_trim=0;int clipped=0,water=0;
    MeshResult first;
    for(auto key:cases) {
        auto a=build_tile(key,0,baseline,empty,surfaces,false),b=build_tile(key,0,fast,empty,surfaces,false);
        equivalent(a,b);assert(b.voxel_bytes<=a.voxel_bytes);
        clipped+=b.volume_height<a.volume_height;water+=!a.water.indices.empty();
        bytes_full+=a.voxel_bytes;bytes_trim+=b.voxel_bytes;
        if(key==cases.front())first=std::move(b);
    }
    assert(clipped==int(cases.size()) && water>0 && bytes_trim<bytes_full);
    // Adjacent windows choose their own bounds but reproduce the complete mesh.
    for(auto key:{TileKey{-256,0,3},TileKey{256,0,3},TileKey{0,-256,3},TileKey{0,256,3}})
        equivalent(build_tile(key,0,baseline,empty,surfaces,false),build_tile(key,0,fast,empty,surfaces,false));
    for(int offset:{-3,-2,-1,0,1}) {
        auto edits=std::make_shared<EditMap>();(*edits)[{8,first.volume_min_y+offset,8}]=r.air;
        auto a=build_tile(cases.front(),0,baseline,edits,surfaces,false),b=build_tile(cases.front(),0,fast,edits,surfaces,false);
        assert(b.volume_min_y==full.min_y && b.volume_height==a.volume_height);equivalent(a,b);
        auto paint=std::make_shared<AppearanceMap>();Appearance appearance;appearance.painted=true;appearance.tint=0xaa44cc;
        (*paint)[{8,first.volume_min_y+offset,8}]=appearance;
        a=build_tile(cases.front(),0,baseline,empty,paint,false);b=build_tile(cases.front(),0,fast,empty,paint,false);
        assert(b.volume_min_y==full.min_y);equivalent(a,b);
    }
    auto high=std::make_shared<EditMap>();(*high)[{8,full.max_y-8,8}]=r.stone;
    equivalent(build_tile(cases.front(),0,baseline,high,surfaces,false),build_tile(cases.front(),0,fast,high,surfaces,false));
    for(int lod:{0,2}) {
        auto a=build_tile({0,0,lod},0,baseline,empty,surfaces,false),b=build_tile({0,0,lod},0,fast,empty,surfaces,false);
        assert(a.volume_min_y==b.volume_min_y && a.volume_height==b.volume_height);equivalent(a,b);
        if(lod==0)assert(a.volume->packed==b.volume->packed && a.volume->layer_slot==b.volume->layer_slot);
    }
    // Same gate with real geological role keys and nontrivial render attributes.
    // Base tint/overlay varies per column, so the underside cannot be a naive
    // uniform quad using the first column's appearance.
    auto enriched=r;enriched.textures.resize(3);enriched.overlays.resize(2);
    enriched.overlays[1].id=1;enriched.overlays[1].top=2;enriched.overlays[1].side=1;enriched.overlays[1].bottom=2;
    enriched.blocks[enriched.bedrock].texture_layer=1;enriched.blocks[enriched.bedrock].tintable=true;
    enriched.blocks[enriched.bedrock].overlay=1;enriched.blocks[enriched.bedrock].distant_outline=false;
    enriched.blocks[enriched.grass].texture_layer=2;enriched.blocks[enriched.grass].tintable=true;enriched.blocks[enriched.grass].transition_overlay=1;
    std::string error;const char *rocks[]={"basalt","limestone","sandstone","moss_stone","crystal_stone","fossil_limestone","wet_stone"};
    for(int i=0;i<7;i++){BlockDef b;b.id=8+i;b.key=std::string("altaria:")+rocks[i];b.texture_layer=1;assert(enriched.add(b,error));}
    Settings geological=full;geological.caves.enabled=true;geological.caves.biomes.definitions[0].rock="altaria:limestone";
    geological.caves.biomes.frozen_rock="snow";geological.caves.biomes.desert_rock="altaria:sandstone";geological.caves.biomes.rooted_rock="altaria:moss_stone";
    caves::BiomeDefinition crystal;crystal.id="mod:crystal";crystal.rock="altaria:crystal_stone";crystal.geology.min=.4f;crystal.weight=2;
    geological.caves.biomes.definitions.push_back(crystal);
    auto compact_geological=geological;compact_geological.far_vertical_trim=true;
    Generator geology(geological,enriched,1337),compact(compact_geological,enriched,1337);
    assert(geology.valid_generation(error) && compact.valid_generation(error));
    for(auto key:cases) {
        auto a=build_tile(key,0,geology,empty,surfaces,false),b=build_tile(key,0,compact,empty,surfaces,false);
        assert(b.volume_height<a.volume_height);equivalent(a,b);
    }
    assert(geology.underground()->stats().misses==0 && compact.underground()->stats().misses==0);
    Settings trees=trimmed;trees.tree_lod_max=3;Generator extra_tree_lod(trees,r,1337);
    auto fallback=build_tile(cases.front(),0,extra_tree_lod,empty,surfaces,false);
    assert(fallback.volume_min_y==full.min_y && fallback.volume_height==(full.max_y-full.min_y)/4);
    std::cout<<"FAR_VOLUME_PASS cases="<<cases.size()<<" full_bytes="<<bytes_full<<" trimmed_bytes="<<bytes_trim
             <<" clipped="<<clipped<<" water_tiles="<<water<<" arches=1; exact geometry/attributes, underside, skirts, negative edges and edit/paint fallback\n";
}
