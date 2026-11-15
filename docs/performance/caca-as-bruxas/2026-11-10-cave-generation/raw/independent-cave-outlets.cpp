#include "game/world_generation/voxel_core.h"
#include <algorithm>
#include <cmath>
#include <iostream>
using namespace altaria;
using namespace altaria::caves;
static Registry blocks() {
    Registry r; std::string error;
    const char *names[]={"air","stone","dirt","grass","sand","water","snow","bedrock","lava"};
    for(int i=0;i<9;i++) {
        BlockDef b; b.id=i;b.key=names[i];b.solid=i!=0&&i!=5&&i!=8;b.water=i==5;b.lava=i==8;b.liquid=b.water||b.lava;
        if(!r.add(b,error)) std::abort();
    }
    r.lava=8; return r;
}
static WaterSource contacts(const Generator &g,const Passage &p) {
    auto b=bounds(p);const auto &s=g.settings();
    int x0=int(std::floor(b.lo[0]))-2,x1=int(std::ceil(b.hi[0]))+2;
    int z0=int(std::floor(b.lo[2]))-2,z1=int(std::ceil(b.hi[2]))+2;
    auto query=g.underground()->query({{float(x0),float(s.min_y),float(z0)},{float(x1),float(s.max_y),float(z1)}});
    int width=x1-x0+1;
    std::vector<float> xs,zs;
    for(int z=z0;z<=z1;z++)for(int x=x0;x<=x1;x++){xs.push_back(x);zs.push_back(z);}
    auto columns=g.columns(xs,zs,nullptr,false);
    auto col=[&](int x,int z)->const Column&{return columns[(z-z0)*width+x-x0];};
    auto solid=[&](const Column &c,int x,int y,int z){
        float band=s.cliff_strength*c.cliff;
        if(y<c.height-band)return true;
        if(y>c.height+band)return false;
        std::vector<float> values;
        g.density_samples(values,{float(x)},{float(y)},{float(z)});
        return y<=c.height+values.front()*band;
    };
    WaterSource result=WaterSource::none;
    constexpr int dirs[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    int hi=std::min(s.sea_level,int(std::ceil(b.hi[1]))+1);
    for(int z=z0+1;z<z1;z++)for(int x=x0+1;x<x1;x++) {
        const auto &c=col(x,z);
        int lo=std::max({s.min_y+2,int(std::floor(b.lo[1]))-1,int(std::floor(c.height-s.cliff_strength*c.cliff))});
        for(int y=lo;y<=hi;y++) {
            if(solid(c,x,y,z))continue;
            for(const auto &d:dirs) {
                int nx=x+d[0],ny=y+d[1],nz=z+d[2];
                if(ny>s.sea_level)continue;
                const auto &neighbor=col(nx,nz);
                if(!g.underground()->sample(query,nx,ny,nz,neighbor.height).carved || !solid(neighbor,nx,ny,nz))continue;
                if(c.region==5)return WaterSource::ocean;
                result=WaterSource::surface_water;
            }
        }
    }
    return result;
}
int main() {
    Settings s;s.min_y=-448;s.vegetation=false;s.caves.enabled=true;s.caves.hydrology={3,1,0,.52f,.28f};
    s.caves.plan.entrance_chance=1;s.caves.plan.infernal_chance=1;
    int checked=0,wet=0,mismatch=0;
    for(int seed:{1337,42,7}) {
        Generator g(s,blocks(),seed);
        for(int bz=-2;bz<=2;bz++)for(int bx=-2;bx<=2;bx++) {
            auto h=g.underground()->hydrology({bx,bz});
            auto planned=h.source==WaterSource::ocean || h.source==WaterSource::surface_water ? h.source : WaterSource::none;
            WaterSource actual=WaterSource::none;
            for(int z=bz*3;z<bz*3+3;z++)for(int x=bx*3;x<bx*3+3;x++) {
                auto n=g.underground()->region({x,z});
                for(const auto &p:n->network.passages)if(p.entrance)actual=std::max(actual,contacts(g,p));
            }
            checked++;wet+=planned!=WaterSource::none;
            if(planned!=actual) {
                ++mismatch;
                std::cout<<"OUTLET_MISMATCH seed="<<seed<<" basin="<<bx<<','<<bz<<" planned="<<water_source_name(planned)<<" actual="<<water_source_name(actual)<<'\n';
            }
        }
    }
    std::cout<<"OUTLET_SWEEP checked="<<checked<<" wet="<<wet<<" mismatches="<<mismatch<<'\n';
    return mismatch!=0;
}
