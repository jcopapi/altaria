#include "game/world_generation/caves/cave_formations.h"
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <vector>
using namespace altaria::caves;
using V=std::array<int,3>;
static bool carved(const Chamber &c,V p) {
    float d=0;
    for(int a=0;a<3;a++){float q=(p[a]-c.center[a])/c.radius[a];d+=q*q;}
    return d<1;
}
static std::pair<size_t,V> unsupported(const Chamber &c,const Formation &f) {
    std::set<V> voxels;
    for(int z=int(std::ceil(f.box.lo[2]));z<=f.box.hi[2];z++)
        for(int y=int(std::ceil(f.box.lo[1]));y<=f.box.hi[1];y++)
            for(int x=int(std::ceil(f.box.lo[0]));x<=f.box.hi[0];x++)
                if(carved(c,{x,y,z}) && formation_voxel(f,{float(x),float(y),float(z)},false))voxels.insert({x,y,z});
    while(!voxels.empty()) {
        std::vector<V> wave{*voxels.begin()}; voxels.erase(wave.front());bool anchors=false;
        for(size_t i=0;i<wave.size();i++)for(int a=0;a<3;a++)for(int dir:{-1,1}){
            auto next=wave[i];next[a]+=dir;
            if(!carved(c,next))anchors=true;
            else if(voxels.erase(next))wave.push_back(next);
        }
        if(!anchors)return {wave.size(),wave.front()};
    }
    return {};
}
int main() {
    const char *names[]={"stalactite","stalagmite","pillar","rubble","crystal","geode","fossil","roots","fungus","vent"};
    const float sizes[][4]={{1.5,3.3,5,13},{1.8,3.5,5,12},{2.4,5,5,18},{3.2,7,3,8},{3.2,7,6,14},{5.2,9.5,6,12},{4.2,8.8,3.5,6.5},{2.2,4.2,6,15},{2.4,4.5,4,8},{2.4,4.3,5,11}};
    for(int kind=0;kind<=int(FormationKind::vent);kind++) {
        FormationRule rule;rule.id=std::string("altaria:")+names[kind];rule.kind=FormationKind(kind);
        rule.radius={sizes[kind][0],sizes[kind][1]};rule.height={sizes[kind][2],sizes[kind][3]};rule.count=6;
        size_t failures=0,largest=0; uint64_t first=0;int first_index=0;V first_cell{};
        for(uint64_t id=1;id<=100;id++) {
            Chamber c{id,{60.125f,-130.37f,-80.23f},{45,28,38},2,false,"giant"};
            for(int i=0;i<rule.count;i++) {
                auto f=plan_formation(c,rule,i,8,9);auto [n,p]=unsupported(c,f);
                if(n){if(!failures){first=id;first_index=i;first_cell=p;}++failures;largest=std::max(largest,n);}
            }
        }
        if(failures)std::cout<<names[kind]<<" floating="<<failures<<" largest="<<largest<<" first_room="<<first<<" index="<<first_index<<" cell="<<first_cell[0]<<','<<first_cell[1]<<','<<first_cell[2]<<'\n';
    }
}
