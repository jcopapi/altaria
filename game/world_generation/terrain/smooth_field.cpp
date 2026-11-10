#include "smooth_field.h"
#include <algorithm>
#include <cmath>
namespace altaria {
SmoothField::SmoothField(std::vector<SmoothCell> cells):sources(std::move(cells)) {
    std::sort(sources.begin(),sources.end(),[](const auto &a,const auto &b){
        if(a.origin!=b.origin)return a.origin<b.origin;
        return a.owner<b.owner;
    });
    for(size_t i=0;i<sources.size();i++) {
        const auto &c=sources[i];
        if(!c.block)continue;
        // Tent support extends half a block beyond each original box. A common
        // world lattice makes samples identical across regions and negative axes.
        for(int z=int(std::floor(c.origin[2]-c.size[2]*.5f));z<=int(std::ceil(c.origin[2]+c.size[2]*1.5f));z++)
            for(int y=int(std::floor(c.origin[1]-c.size[1]*.5f));y<=int(std::ceil(c.origin[1]+c.size[1]*1.5f));y++)
                for(int x=int(std::floor(c.origin[0]-c.size[0]*.5f));x<=int(std::ceil(c.origin[0]+c.size[0]*1.5f));x++)
                    bins[{x,y,z}].push_back(i);
    }
}
float SmoothField::distance(std::array<float,3> p) const {
    auto bin=bins.find({int(std::floor(p[0])),int(std::floor(p[1])),int(std::floor(p[2]))});
    if(bin==bins.end())return 2.f;
    float occupancy=0, cubic=2;
    for(auto index:bin->second) {
        const auto &c=sources[index];
        float tent=1, q[3],length=0,maximum=-1e20f;
        for(int a=0;a<3;a++) {
            float d=std::abs(p[a]-c.origin[a]-c.size[a]*.5f);
            tent*=std::max(0.f,1-d/c.size[a]);
            q[a]=d-c.size[a]*.5f;
            length+=std::max(0.f,q[a])*std::max(0.f,q[a]);
            maximum=std::max(maximum,q[a]);
        }
        if(c.modified)occupancy+=tent;
        else cubic=std::min(cubic,std::sqrt(length)+std::min(maximum,0.f));
    }
    // Linear occupancy reconstruction turns unit stair steps into an inclined
    // isosurface, rather than just rounding the corners of each stair cube.
    return std::min(cubic,std::clamp((.5f-occupancy)*2.f,-1.f,1.f));
}
bool SmoothField::modified_surface(std::array<float,3> p) const {
    auto bin=bins.find({int(std::floor(p[0])),int(std::floor(p[1])),int(std::floor(p[2]))});
    if(bin==bins.end())return false;
    float occupancy=0,cubic=2;
    for(auto i:bin->second) {
        auto &c=sources[i];float weight=1,length=0,maximum=-1e20f;
        for(int a=0;a<3;a++) {
            float d=std::abs(p[a]-c.origin[a]-c.size[a]*.5f);
            weight*=std::max(0.f,1-d/c.size[a]);
            float q=d-c.size[a]*.5f;length+=std::max(0.f,q)*std::max(0.f,q);maximum=std::max(maximum,q);
        }
        if(c.modified)occupancy+=weight;
        else cubic=std::min(cubic,std::sqrt(length)+std::min(maximum,0.f));
    }
    return (.5f-occupancy)*2 <= cubic+.005f;
}
const SmoothCell *SmoothField::owner_at(std::array<float,3> p) const {
    const SmoothCell *owner=nullptr;
    float best=1e20f;
    auto bin=bins.find({int(std::floor(p[0])),int(std::floor(p[1])),int(std::floor(p[2]))});
    if(bin==bins.end())return owner;
    for(auto i:bin->second) {
        auto &c=sources[i];if(!c.modified)continue;
        float d=0;
        for(int a=0;a<3;a++) {float q=p[a]-c.origin[a]-c.size[a]*.5f;d+=q*q;}
        if(d<best){best=d;owner=&c;}
    }
    return owner;
}
}
