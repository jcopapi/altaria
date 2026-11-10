#include "smooth_field.h"
#include <algorithm>
#include <cmath>
namespace altaria {
static float ease(float t) {t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);}
static float box_distance(const SmoothCell &c,std::array<float,3> p,float top) {
    float length=0,maximum=-1e20f;
    for(int a=0;a<3;a++) {
        float hi=a==1 ? top : c.origin[a]+c.size[a];
        float q=std::max(c.origin[a]-p[a],p[a]-hi);
        length+=std::max(q,0.f)*std::max(q,0.f);maximum=std::max(maximum,q);
    }
    return std::sqrt(length)+std::min(maximum,0.f);
}
float SmoothField::cell_distance(size_t index,std::array<float,3> p) const {
    const auto &c=sources[index];float top=c.origin[1]+c.size[1];
    if(c.modified) {
        float x=std::clamp((p[0]-c.origin[0])/c.size[0],0.f,1.f)*2,z=std::clamp((p[2]-c.origin[2])/c.size[2],0.f,1.f)*2;
        int ix=std::min(1,int(x)),iz=std::min(1,int(z));float u=ease(x-ix),v=ease(z-iz);
        const auto &h=heights[index];
        float a=h[iz*3+ix]*(1-u)+h[iz*3+ix+1]*u,b=h[(iz+1)*3+ix]*(1-u)+h[(iz+1)*3+ix+1]*u;
        top=a*(1-v)+b*v;
    }
    return box_distance(c,p,top);
}
SmoothField::SmoothField(std::vector<SmoothCell> cells,float maximum_height):sources(std::move(cells)) {
    std::sort(sources.begin(),sources.end(),[](const auto &a,const auto &b){if(a.origin!=b.origin)return a.origin<b.origin;return a.owner<b.owner;});
    for(size_t i=0;i<sources.size();i++) {
        auto &c=sources[i];if(c.modified)c.size[1]=std::min(c.size[1],maximum_height);
        for(int z=int(std::floor(c.origin[2]-1));z<=int(std::ceil(c.origin[2]+c.size[2]+1));z++)
            for(int y=int(std::floor(c.origin[1]-1));y<=int(std::ceil(c.origin[1]+c.size[1]+1));y++)
                for(int x=int(std::floor(c.origin[0]-1));x<=int(std::ceil(c.origin[0]+c.size[0]+1));x++)bins[{x,y,z}].push_back(i);
    }
    // A cap changes only its own cell. Its support and buried blocks retain
    // their exact box geometry; there is no occupancy spreading into the world.
    heights.resize(sources.size());
    for(size_t i=0;i<sources.size();i++) {
        auto &c=sources[i];if(!c.modified)continue;
        for(int z=0;z<3;z++)for(int x=0;x<3;x++) {
            float px=c.origin[0]+c.size[0]*x*.5f,pz=c.origin[2]+c.size[2]*z*.5f;
            float height=c.origin[1]+c.size[1];
            for(float dz:{-.001f,.001f})for(float dx:{-.001f,.001f}) {
                float top=c.origin[1];
                auto b=bins.find({int(std::floor(px+dx)),int(std::floor(c.origin[1])),int(std::floor(pz+dz))});
                if(b!=bins.end())for(auto j:b->second) {
                    const auto &other=sources[j];
                    if(px+dx<other.origin[0] || px+dx>=other.origin[0]+other.size[0] || pz+dz<other.origin[2] || pz+dz>=other.origin[2]+other.size[2])continue;
                    if(other.origin[1]>c.origin[1]+c.size[1]+.001f)continue;
                    top=std::max(top,other.origin[1]+other.size[1]);
                }
                height=std::min(height,top);
            }
            heights[i][z*3+x]=std::clamp(height,c.origin[1],c.origin[1]+c.size[1]);
        }
    }
}
float SmoothField::distance(std::array<float,3> p) const {
    auto bin=bins.find({int(std::floor(p[0])),int(std::floor(p[1])),int(std::floor(p[2]))});if(bin==bins.end())return 2;
    float d=2;for(auto i:bin->second)d=std::min(d,cell_distance(i,p));return d;
}
bool SmoothField::modified_surface(std::array<float,3> p) const {
    auto bin=bins.find({int(std::floor(p[0])),int(std::floor(p[1])),int(std::floor(p[2]))});if(bin==bins.end())return false;
    float cap=2,cubic=2;
    for(auto i:bin->second)if(sources[i].modified)cap=std::min(cap,cell_distance(i,p));else cubic=std::min(cubic,cell_distance(i,p));
    return cap<1 && cap<=cubic+.005f;
}
const SmoothCell *SmoothField::owner_at(std::array<float,3> p) const {
    const SmoothCell *owner=nullptr;float best=1e20f;
    auto bin=bins.find({int(std::floor(p[0])),int(std::floor(p[1])),int(std::floor(p[2]))});if(bin==bins.end())return owner;
    for(auto i:bin->second)if(sources[i].modified) {float d=std::abs(cell_distance(i,p));if(d<best){best=d;owner=&sources[i];}}
    return owner;
}
}
