#include "game/world_generation/caves/cave_field.h"
#include <chrono>
#include <iostream>
using namespace altaria::caves;
using Clock=std::chrono::steady_clock;
static double ms(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
int main() {
    std::cout<<"iteration,plan_ms,cold_query_ms,warm_query_ms,sample_32768_ms,carved,query_primitives,cache_regions,estimated_cache_bytes\n";
    for(int run=0;run<3;run++) {
        auto planner=std::make_shared<Planner>(1337,0,PlanSettings(),[](float,float){return 110.f;});
        auto start=Clock::now();auto n=planner->region({0,0});double plan=ms(start);
        if(!n.valid || n.chambers.empty())return 1;
        auto c=n.chambers.front();Bounds box;for(int a=0;a<3;a++){box.lo[a]=c.center[a]-16;box.hi[a]=c.center[a]+16;}
        PlanCache cache(planner);start=Clock::now();auto q=cache.query(box);double cold=ms(start);
        start=Clock::now();auto warm=cache.query(box);double reused=ms(start);
        size_t carved=0;start=Clock::now();
        for(int z=0;z<32;z++)for(int y=0;y<32;y++)for(int x=0;x<32;x++)carved+=sample(warm,{box.lo[0]+x,box.lo[1]+y,box.lo[2]+z},110,-512,10).carved;
        double field=ms(start);auto stats=cache.stats();
        std::cout<<run<<','<<plan<<','<<cold<<','<<reused<<','<<field<<','<<carved<<','<<q.primitives.size()<<','<<stats.regions<<','<<stats.bytes<<'\n';
        if(!carved || q.primitives.size()!=warm.primitives.size())return 1;
    }
}
