#include "game/world_generation/caves/cave_depth.h"
#include "game/world_generation/caves/cave_plan.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace altaria::caves;
int main() {
    PlanSettings cfg;DepthSettings settings;assert(valid_depth(settings,cfg.depths));
    for(int i=0;i<4;i++)assert(classify_depth(100,100-cfg.depths[i],cfg.depths,settings,1).primary==i);
    assert(classify_depth(100,-400,cfg.depths,settings,0).primary==2);
    float last=0;
    for(float d=0;d<=600;d+=.25f) {
        auto v=classify_depth(300,300-d,cfg.depths,settings,.8f);float sum=0,position=0;
        for(int i=0;i<4;i++){assert(v.weights[i]>=0 && v.weights[i]<=1);sum+=v.weights[i];position+=i*v.weights[i];}
        assert(std::abs(sum-1)<1e-6f && position>=last-1e-6f);last=position;
        assert(v.surface_influence>=0 && v.surface_influence<=1);
        auto next=classify_depth(300,300-d-.001f,cfg.depths,settings,.8f);
        for(int i=0;i<4;i++)assert(std::abs(next.weights[i]-v.weights[i])<.001f);
    }
    assert(classify_depth(100,100,cfg.depths,settings,0).surface_influence==1);
    assert(classify_depth(100,-200,cfg.depths,settings,0).surface_influence==0);
    settings.transition_widths[0]=0;assert(!valid_depth(settings,cfg.depths));
    settings=DepthSettings();settings.surface_influence_depth=std::numeric_limits<float>::quiet_NaN();assert(!valid_depth(settings,cfg.depths));
    std::array<float,4> huge{1e38f,2e38f,2.5e38f,3e38f};assert(!valid_depth(DepthSettings(),huge));
    for(float w:classify_depth(100,0,huge,DepthSettings(),1).weights)assert(std::isfinite(w));
    auto roof=[](float,float){return 100.f;};PlanSettings cold,hot;cold.infernal_chance=0;hot.infernal_chance=1;
    Planner a(1337,0,cold,roof),b(1337,0,hot,roof),mixed(1337,0,cfg,roof);bool found_cold=false,found_hot=false;
    for(int z=-3;z<=3;z++)for(int x=-3;x<=3;x++) {
        float px=(x+.5f)*cfg.region_size,pz=(z+.5f)*cfg.region_size;
        assert(a.geothermal(px,pz)==0 && b.geothermal(px,pz)==1);
        float v=mixed.geothermal(px,pz);found_cold|=v==0;found_hot|=v==1;
        assert(std::abs(mixed.geothermal(px-.001f,pz)-mixed.geothermal(px+.001f,pz))<.001f);
    }
    assert(found_cold && found_hot);
    std::cout<<"CAVE_DEPTH_PASS: four weighted levels, continuous transitions, diminishing surface influence and optional coherent geothermal regions\n";
}
