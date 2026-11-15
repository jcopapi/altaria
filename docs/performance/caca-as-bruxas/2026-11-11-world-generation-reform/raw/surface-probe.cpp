#include "game/world_generation/voxel_core.h"
#include <cmath>
#include <iostream>
using namespace altaria;
int main(int argc,char **argv) {
    Settings s;
    if(argc>1){s.generator_version=3;s.continental_scale=5300;s.mountain_amplitude=184;s.mountain_scale=680;s.warp_amplitude=35;s.terrace_strength=.06f;s.cliff_strength=6;s.valley_width=.10f;}
    Registry r;std::string error;
    for(int i=0;i<8;i++){BlockDef b;b.id=i;b.key=std::to_string(i);b.solid=i!=0&&i!=5;b.water=i==5;r.add(b,error);}
    for(int seed:{1337,42,-89}) {
        Generator g(s,r,seed);
        std::vector<float> xs,zs;
        for(int z=-14000;z<=14000;z+=48)for(int x=-14000;x<=14000;x+=48){xs.push_back(x);zs.push_back(z);}
        auto cols=g.columns(xs,zs);
        int rivers=0,ocean=0,beaches=0,wet=0,run=0,longest=0;
        for(size_t i=0;i<cols.size();i++) {
            const auto &c=cols[i];
            if(i && zs[i]!=zs[i-1])run=0;
            rivers+=c.river>.5f && c.region!=5;beaches+=c.beach_mask>.18f;
            ocean+=c.region==5;wet+=c.height<s.sea_level;
            run=c.region==5?run+48:0;longest=std::max(longest,run);
        }
        std::cout<<seed<<" columns="<<cols.size()<<" rivers="<<rivers<<" ocean="<<ocean<<" beaches="<<beaches<<" wet="<<wet<<" longest_ocean_x="<<longest<<'\n';
        for(auto origin:{std::pair{-68,193},std::pair{-61,157},std::pair{-59,163}}){
            xs.clear();zs.clear();
            for(int z=-32;z<=32;z++)for(int x=-32;x<=32;x++){xs.push_back(x+origin.first);zs.push_back(z+origin.second);}
            cols=g.columns(xs,zs);float jump=0;int steep=0;float low=1e6f,high=-1e6f;
            for(size_t i=0;i<cols.size();i++){
                low=std::min(low,cols[i].height);high=std::max(high,cols[i].height);
                auto eval=[&](size_t j){float d=std::abs(cols[i].height-cols[j].height);jump=std::max(jump,d);steep+=d>6;};
                if(i%65)eval(i-1);if(i>=65)eval(i-65);
            }
            std::cout<<"near "<<origin.first<<','<<origin.second<<" jump="<<jump<<" steep_edges="<<steep<<" low="<<low<<" high="<<high<<'\n';
        }
    }
}
