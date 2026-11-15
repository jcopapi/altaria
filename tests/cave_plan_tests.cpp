#include "game/world_generation/caves/cave_plan.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <unordered_set>
using namespace altaria::caves;
int main() {
    auto roof=[](float,float){return 110.f;};
    PlanSettings cfg;cfg.infernal_chance=1;cfg.isolated_chance=0;cfg.entrance_chance=0;cfg.archetypes={default_archetypes().front()};
    Planner p(1337,0,cfg,roof),other(1338,0,cfg,roof),dimension(1337,1,cfg,roof);
    std::unordered_set<uint64_t> all;
    for(int z=-3;z<=3;z++)for(int x=-3;x<=3;x++) {
        Region r{x,z};auto a=p.region(r);auto unused=p.region({r.x+8,r.z-4});auto b=p.region(r);
        assert(a.valid && a.chambers.size()==12 && a.passages.size()==b.passages.size());
        std::unordered_set<uint64_t> reached{a.chambers[0].id},nodes,edges;
        for(auto &c:a.chambers){assert(all.insert(c.id).second);nodes.insert(c.id);}
        for(auto &c:a.external)nodes.insert(c.id);
        for(int i=0;i<12;i++)for(auto &e:a.passages) {if(reached.count(e.a))reached.insert(e.b);if(reached.count(e.b))reached.insert(e.a);}
        for(size_t i=0;i<a.chambers.size();i++) {
            auto &c=a.chambers[i];auto &d=b.chambers[i];assert(c.id==d.id && c.center==d.center && c.radius==d.radius && c.depth==d.depth && c.archetype==d.archetype);
            assert(reached.count(c.id) && c.center[1]+c.radius[1]<=100 && c.center[1]-c.radius[1]>=-508);
        }
        for(size_t i=0;i<a.passages.size();i++) {
            auto &e=a.passages[i];auto &f=b.passages[i];assert(nodes.count(e.a) && nodes.count(e.b) && e.a!=e.b && edges.insert(e.id).second);
            assert(e.id==f.id && e.path==f.path && e.radius==f.radius);
            for(auto q:e.path){assert(q[1]+e.radius<=100.001f && q[1]-e.radius>=-508);if(e.vertical)assert(q[0]==e.from[0] && q[2]==e.from[2]);}
        }
        for(auto neighbor:{Region{r.x-1,r.z},Region{r.x+1,r.z},Region{r.x,r.z-1},Region{r.x,r.z+1}}) {
            auto n=p.region(neighbor);int shared=0;
            for(auto &e:a.passages)for(auto &f:n.passages)if(e.id==f.id){assert(e.path==f.path && e.radius==f.radius);shared++;}
            assert(shared==4);
        }
        assert(a.chambers[0].id!=other.region(r).chambers[0].id && a.chambers[0].id!=dimension.region(r).chambers[0].id);
    }
    assert(p.region({40,5}).chambers[0].id!=p.region({42,3}).chambers[0].id);
    assert(p.region({40,10}).chambers[0].id!=p.region({47,3}).chambers[0].id);
    assert(!p.region({std::numeric_limits<int>::max(),0}).valid);
    for(int bad=1;bad<=10;bad++) {
        int probes=0;Planner invalid(1337,0,cfg,[&](float,float){return ++probes==bad?std::numeric_limits<float>::quiet_NaN():110.f;});assert(!invalid.region({0,0}).valid);
    }
    PlanSettings thin=cfg;thin.floor=105;Planner no_rock(1337,0,thin,roof);assert(no_rock.region({0,0}).valid && no_rock.region({0,0}).chambers.empty());
    cfg.entrance_chance=1;Planner entrances(1337,0,cfg,roof);assert(!entrances.region({0,0}).entrances.empty());
    PlanSettings custom=cfg;custom.archetypes={{"mod:test",0,3,{8,5,8},{8,5,8},1}};Planner mods(1337,0,custom,roof);assert(mods.region({0,0}).chambers.front().archetype=="mod:test");
    for(int x=-2;x<=2;x++) {
        Planner cliff(19,0,cfg,[](float px,float pz){return px<0?25.f:110.f+10*std::sin(pz/37.f);});
        auto n=cliff.region({x,0});assert(n.valid);
        std::unordered_set<uint64_t> nodes;
        for(auto &c:n.chambers)nodes.insert(c.id);
        for(auto &c:n.external)nodes.insert(c.id);
        for(auto &t:n.entrances)nodes.insert(t.id);
        for(auto &e:n.passages)assert(nodes.count(e.a) && nodes.count(e.b));
    }
    PlanSettings pockets=cfg;pockets.entrance_chance=0;pockets.isolated_chance=1;
    Planner isolated(1337,0,pockets,roof);int count=0;
    for(int x=-4;x<=4;x++)for(int z=-4;z<=4;z++)for(auto &c:isolated.region({x,z}).chambers)count+=c.isolated;
    assert(count>0);
    PlanSettings contact;contact.floor=-448;contact.ceiling=320;
    contact.isolated_chance=1;contact.entrance_chance=1;contact.infernal_chance=1;
    Planner expanded(95,0,contact,[](float x,float z){return 110.f+35*std::sin(x/150)+25*std::cos(z/180);});
    for(const auto &c:expanded.region({-1,-2}).chambers)assert(c.id!=11352225579588677626ULL);
    contact.archetypes={{"small",0,3,{6,5,6},{6,5,6},4},{"giant",1,1,{100,25,100},{100,25,100},2},
                        {"tall",1,1,{6,100,6},{6,100,6},8}};
    Planner neighboring(5,0,contact,roof);
    for(const auto &c:neighboring.region({-2,1}).chambers)assert(c.id!=9279416757114523918ULL);
    // Mixed archetypes can omit a shallow tall hub while a side room fits.
    // Neighbor plans must still agree about every surviving deeper hub/portal.
    PlanSettings mixed;mixed.ceiling=512;mixed.infernal_chance=1;
    mixed.entrance_chance=1;mixed.isolated_chance=1;
    Planner hills(1337,0,mixed,[](float x,float z){return 110.f+35*std::sin(x/150.f)+25*std::cos(z/180.f);});
    for(int z=-2;z<=2;z++)for(int x=-2;x<=2;x++) {
        auto n=hills.region({x,z});assert(n.valid);
        for(const auto &external:n.external) {
            // Hubs lie inside their owning macroregion regardless of radius.
            Region owner{int(std::floor(external.center[0]/mixed.region_size)),int(std::floor(external.center[2]/mixed.region_size))};
            auto other=hills.region(owner);bool room=false;
            for(const auto &c:other.chambers)if(c.id==external.id){assert(c.center==external.center && c.radius==external.radius);room=true;}
            assert(room);
            for(const auto &e:n.passages)if(e.a==external.id || e.b==external.id) {
                bool shared=false;
                for(const auto &f:other.passages)if(e.id==f.id){assert(e.path==f.path && e.radius==f.radius);shared=true;}
                assert(shared);
            }
        }
        for(const auto &c:n.chambers)if(!c.isolated) {
            bool connected=false;
            for(const auto &e:n.passages)connected|=e.a==c.id || e.b==c.id;
            assert(connected);
        }
    }
    std::cout<<"CAVE_PLAN_TESTS_PASS: 49 regions, full state/order, all borders, connectivity, shafts, host bounds, omissions, entrances, archetypes\n";
}
