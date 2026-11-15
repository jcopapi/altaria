#include "voxel_core.h"
#include <chrono>
#include <iostream>
using namespace altaria;
int main() {
    Settings s;s.generator_version=3;s.vegetation=true;s.min_y=-896;s.continental_scale=5300;
    s.mountain_scale=680;s.mountain_amplitude=204;s.warp_amplitude=34;s.cliff_strength=6;s.river_width=.028f;
    Registry r;std::string error;
    for(int i:{0,1,2,3,4,5,6,7,9,10}) {
        BlockDef b;b.id=i;b.key=i==9?"altaria:wood":i==10?"altaria:leaves":std::to_string(i);
        b.solid=i!=0 && i!=5;b.water=i==5;b.wood=i==9;b.foliage=i==10;
        if(!r.add(b,error))return 1;
    }
    for(int seed:{1337,42,-89}) {
        Generator g(s,r,seed);
        size_t count=0;uint64_t digest=1469598103934665603ull;
        auto start=std::chrono::steady_clock::now();
        for(int j=0;j<10;j++)for(int i=0;i<10;i++) {
            int x=(i-5)*32,z=(j-5)*32;
            auto trees=g.trees(x-1,z-1,x+33,z+33);
            count+=trees.size();
            for(const auto &[p,id]:trees)
                for(int v:{p.x,p.y,p.z,int(id)}){digest^=uint32_t(v);digest*=1099511628211ull;}
        }
        std::cout<<seed<<" "<<count<<" "<<digest<<" "
                 <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<"ms\n";
    }
}
