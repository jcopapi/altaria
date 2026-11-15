#include "game/world_generation/caves/cave_field.h"
#include <cassert>
#include <iostream>
#include <thread>
using namespace altaria::caves;
int main() {
    PlanSettings cfg;cfg.infernal_chance=1;cfg.archetypes={default_archetypes().front()};
    auto planner=std::make_shared<Planner>(1337,0,cfg,[](float,float){return 110.f;});
    PlanCache cache(planner,2,4*1024*1024);
    auto first=cache.get({0,0});assert(first && first->network.valid);assert(cache.resident({0,0})==first);
    auto c=first->network.chambers.front();
    Bounds area;for(int axis=0;axis<3;axis++){area.lo[axis]=c.center[axis]-1;area.hi[axis]=c.center[axis]+1;}
    auto q=cache.query(area);assert(!q.primitives.empty());
    auto inside=sample(q,c.center,110,-512,10);assert(inside.carved && inside.owner);
    auto clipped=sample(q,c.center,c.center[1]+3,-512,10);assert(!clipped.carved);
    auto bedrock=sample(q,{c.center[0],-512,c.center[2]},110,-512,10);assert(!bedrock.carved);
    cache.get({8,9});cache.get({-8,-9});auto stats=cache.stats();assert(stats.regions<=2 && stats.bytes<=4*1024*1024);
    // A query owns immutable plans across eviction, while regenerated plans agree.
    auto after=sample(q,c.center,110,-512,10);assert(after.carved==inside.carved && after.owner==inside.owner && after.distance==inside.distance);
    auto again=cache.query(area);auto repeated=sample(again,c.center,110,-512,10);assert(repeated.distance==inside.distance && repeated.owner==inside.owner);
    std::thread a([&]{for(int i=0;i<5;i++)cache.get({i,2});}),b([&]{for(int i=0;i<5;i++)cache.get({-i,3});});a.join();b.join();
    stats=cache.stats();assert(stats.regions<=2 && stats.bytes<=4*1024*1024);
    Query bad=cache.query({{0,0,0},{99999,99999,99999}});assert(bad.primitives.empty());
    PlanCache tiny(planner,2,1);auto transient=tiny.get({0,0});assert(transient && transient->network.valid && tiny.stats().bytes==0);
    bool seam=false;
    for(const auto &passage:first->network.passages)for(size_t i=1;i<passage.path.size() && !seam;i++) {
        auto a=passage.path[i-1],b=passage.path[i];
        if(a[0]==b[0] || (a[0]-256)*(b[0]-256)>0)continue;
        float t=(256-a[0])/(b[0]-a[0]);Point p{256,a[1]+t*(b[1]-a[1]),a[2]+t*(b[2]-a[2])};
        auto left=cache.query({{224,p[1]-16,p[2]-16},{256,p[1]+16,p[2]+16}});
        auto right=cache.query({{256,p[1]-16,p[2]-16},{288,p[1]+16,p[2]+16}});
        auto l=sample(left,p,110,-512,10),r=sample(right,p,110,-512,10);
        assert(l.carved && r.carved && l.owner==r.owner && l.distance==r.distance);seam=true;
    }
    assert(seam);
    PlanCache pinned(planner,2,4*1024*1024);
    auto near_a=pinned.get({0,0}),near_b=pinned.get({1,0});
    auto distant=pinned.get({40,40});assert(distant && distant->network.valid);
    assert(pinned.resident({0,0})==near_a && pinned.resident({1,0})==near_b && pinned.resident({40,40})==distant);
    assert(pinned.stats().regions==2 && pinned.stats().pinned_regions==2);
    for(int i=0;i<64;i++)assert(pinned.get({40,40})==distant);
    assert(pinned.stats().misses==3); // One live plan for the uncached region.
    near_a.reset();near_b.reset();distant=pinned.get({40,40});assert(pinned.resident({40,40})==distant);
    distant.reset();assert(pinned.resident({40,40})); // Promoted to strong cache ownership.
    PlanCache uncacheable(planner,2,1);
    std::vector<std::shared_ptr<const IndexedNetwork>> concurrent(8);
    std::vector<std::thread> requests;
    for(int i=0;i<8;i++)requests.emplace_back([&,i]{concurrent[i]=uncacheable.get({20,20});});
    for(auto &request:requests)request.join();
    for(const auto &plan:concurrent)assert(plan==concurrent.front());
    std::weak_ptr<const IndexedNetwork> released=concurrent.front();concurrent.clear();
    assert(released.expired() && !uncacheable.resident({20,20}));
    // Queries own their immutable plans even after their entire cache is gone.
    Query retained;
    {PlanCache short_lived(planner,2,4*1024*1024);retained=short_lived.query(area);}
    assert(sample(retained,c.center,110,-512,10).carved);
    std::cout<<"CAVE_FIELD_TESTS_PASS: carving, host/floor guards, bounded concurrent cache, query lifetime and eviction determinism\n";
}
