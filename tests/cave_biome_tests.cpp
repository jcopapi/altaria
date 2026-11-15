#include "game/world_generation/caves/cave_biomes.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <unordered_set>
using namespace altaria::caves;
static bool resolve(const std::string &key,uint16_t &id) {
    if(key=="$stone"){id=1;return true;}
    if(key=="ice"){id=2;return true;}
    if(key=="sandstone"){id=3;return true;}
    if(key=="moss"){id=4;return true;}
    if(key.size()==5 && key.substr(0,4)=="rock" && key[4]>='0' && key[4]<='3'){id=10+key[4]-'0';return true;}
    return false;
}
int main() {
    BiomeSettings s;s.frozen_rock="ice";s.desert_rock="sandstone";s.rooted_rock="moss";
    for(int i=0;i<4;i++) {
        BiomeDefinition b;b.id="mod:depth"+std::to_string(i);b.rock="rock"+std::to_string(i);
        b.depths={0,0,0,0};b.depths[i]=1;b.weight=100;s.definitions.push_back(b);
    }
    std::array<float,4> targets{24,90,190,340};DepthSettings ds;
    BiomeStage a(s,ds,targets,1337,0,resolve),same(s,ds,targets,1337,0,resolve),other(s,ds,targets,1338,0,resolve),dimension(s,ds,targets,1337,1,resolve);
    std::string error;assert(a.valid(error));SurfaceClimate surface;bool changed=false;
    for(int z=-500;z<500;z+=71)for(int x=-500;x<500;x+=63) {
        auto c=a.column(x,z,surface,1),b=same.column(x,z,surface,1);
        assert(c.heat==b.heat && c.humidity==b.humidity && c.geology==b.geology);
        changed|=c.heat!=other.column(x,z,surface,1).heat;
        assert(c.heat!=dimension.column(x,z,surface,1).heat);
        for(int level=0;level<4;level++) {
            auto e=a.environment(c,100,100-targets[level]);
            assert(e.depth.primary==level && e.biomes.primary==level+1);
            assert(a.material(c,100,x,int(100-targets[level]),z)==same.material(b,100,x,int(100-targets[level]),z));
            assert(e.oxygen>=0 && e.oxygen<=1 && e.visibility>0);
        }
    }
    assert(changed);
    SurfaceClimate cold;cold.heat=.05f;cold.frozen=1;
    SurfaceClimate dry;dry.heat=.9f;dry.humidity=.05f;dry.desert=1;
    SurfaceClimate jungle;jungle.humidity=.95f;jungle.lush=1;
    auto frozen=a.column(40,60,cold,0),desert=a.column(40,60,dry,0),lush=a.column(40,60,jungle,0);
    auto shallow=a.environment(frozen,100,76),deep=a.environment(frozen,100,-300);
    assert(shallow.frozen>.9f && deep.frozen==0 && deep.depth.surface_influence==0);
    assert(a.environment(desert,100,76).sedimentary>.9f);
    assert(a.environment(lush,100,76).rooted>.8f);
    int ice=0,sand=0,moss=0;
    for(int z=0;z<32;z++)for(int x=0;x<32;x++) {
        ice+=a.material(frozen,100,x,76,z)==2;sand+=a.material(desert,100,x,76,z)==3;moss+=a.material(lush,100,x,76,z)==4;
        assert(a.material(frozen,100,x,-300,z)==a.material(desert,100,x,-300,z));
    }
    assert(ice>400 && sand>400 && moss>200);
    BiomeSettings wet;BiomeDefinition fungus;fungus.id="mod:fungus";fungus.weight=100;fungus.humidity.min=.75f;wet.definitions.push_back(fungus);
    BiomeStage humidity(wet,ds,targets,1337,0,resolve);
    auto under_dry=humidity.column(40,60,dry,0),under_wet=humidity.column(40,60,jungle,0);
    assert(humidity.environment(under_dry,100,76).biomes.primary==0);
    assert(humidity.environment(under_wet,100,76).biomes.primary==1);
    auto ed=humidity.environment(under_dry,100,-300),ew=humidity.environment(under_wet,100,-300);
    assert(ed.biomes.primary==ew.biomes.primary && ed.biomes.secondary==ew.biomes.secondary && ed.biomes.primary_weight==ew.biomes.primary_weight);
    BiomeSettings aquatic;BiomeDefinition flooded;flooded.id="fixture:flooded";
    flooded.requires_water=true;flooded.weight=1000;flooded.humidity.min=.8f;
    aquatic.definitions.push_back(flooded);
    BiomeStage water_biome(aquatic,ds,targets,1337,0,resolve);
    auto water_column=water_biome.column(80,-60,surface,0);
    assert(water_biome.environment(water_column,100,10,false).biomes.primary==0);
    assert(water_biome.environment(water_column,100,10,true).biomes.primary==1);
    // A biome valid only in the middle of the actual surface/deep humidity
    // transition must not disappear because neither endpoint was eligible.
    BiomeSettings mid;BiomeDefinition middle;middle.id="mod:middle_humidity";middle.rock="ice";
    middle.humidity={.45f,.55f,.01f};middle.weight=1000;mid.definitions.push_back(middle);
    BiomeStage intermediate(mid,ds,targets,1337,0,resolve);SurfaceClimate saturated;saturated.humidity=1;
    auto mid_column=intermediate.column(-2602,-3000,saturated,0);float lo=8,hi=ds.surface_influence_depth;
    for(int i=0;i<32;i++) {
        float depth=(lo+hi)*.5f;
        if(intermediate.environment(mid_column,100,100-depth).humidity>.5f)lo=depth;else hi=depth;
    }
    auto mid_environment=intermediate.environment(mid_column,100,100-(lo+hi)*.5f);
    assert(std::abs(mid_environment.humidity-.5f)<.0001f && mid_environment.biomes.primary==1);
    // The second/third ranking may swap. Environmental properties must still
    // include all contributions instead of jumping when one ID is summarized.
    BiomeSettings triple;BiomeDefinition warm,cool;warm.id="mod:warm";cool.id="mod:cool";
    warm.depths={1,0,0,0};cool.depths={0,1,0,0};warm.temperature_delta=80;cool.temperature_delta=-80;
    warm.oxygen=.2f;cool.visibility=.1f;triple.definitions.push_back(warm);triple.definitions.push_back(cool);
    BiomeStage junction(triple,ds,targets,1337,0,resolve);auto junction_column=junction.column(0,0,SurfaceClimate(),0);
    junction_column.spatial_weights[0]=1.5f;junction_column.spatial_weights[1]=2;junction_column.spatial_weights[2]=2;
    auto before=junction.environment(junction_column,100,100-56.999f),after=junction.environment(junction_column,100,100-57.001f);
    assert(std::abs(before.temperature_c-after.temperature_c)<.1f);
    assert(std::abs(before.oxygen-after.oxygen)<.001f && std::abs(before.visibility-after.visibility)<.001f);
    // Unknown references and catalogs with no valid fallback are errors, not air.
    auto invalid=s;invalid.definitions[1].rock="missing:block";BiomeStage bad(invalid,ds,targets,1,0,resolve);assert(!bad.valid(error) && error.find("missing:block")!=std::string::npos);
    invalid=s;invalid.definitions.push_back(invalid.definitions.front());assert(!valid_biomes(invalid,error));
    invalid=s;invalid.definitions.front().depths[3]=0;assert(!valid_biomes(invalid,error));
    invalid=s;invalid.region_scale=std::numeric_limits<float>::quiet_NaN();assert(!valid_biomes(invalid,error));
    // Resource metadata is opt-in, varies coherently within one biome and has
    // no effect on the material registry or voxel material selection.
    BiomeSettings resources;ResourceRule rule;rule.id="example:deep_aquatic";
    rule.resource="example:future_mineral";rule.abundance=2;
    rule.depth_weights={0,1,0,0};rule.requires_water=true;rule.regional_variation=.65f;
    resources.resources.push_back(rule);resources.definitions[0].resource_rules.push_back(rule.id);
    BiomeStage with_resource(resources,ds,targets,1337,0,resolve),without_resource(BiomeSettings(),ds,targets,1337,0,resolve);
    assert(with_resource.valid(error));
    auto c0=with_resource.column(-500,140,surface,0),c1=with_resource.column(1620,140,surface,0);
    float a0=with_resource.resource_abundance(rule.resource,c0,100,-500,10,140,true);
    float a1=with_resource.resource_abundance(rule.resource,c1,100,1620,10,140,true);
    assert(a0>0 && a1>0 && a0!=a1);
    assert(a0==with_resource.resource_abundance(rule.resource,c0,100,-500,10,140,true));
    assert(with_resource.resource_abundance(rule.resource,c0,100,-500,10,140)==0);
    assert(with_resource.resource_abundance(rule.resource,c0,100,-500,76,140,true)==0);
    assert(with_resource.resource_abundance("example:unknown",c0,100,-500,10,140,true)==0);
    assert(with_resource.material(c0,100,-500,10,140,true)==without_resource.material(c0,100,-500,10,140,true));
    auto dangling=resources;dangling.resources.clear();assert(!valid_biomes(dangling,error));
    auto duplicate=resources;duplicate.resources.push_back(rule);assert(!valid_biomes(duplicate,error));
    auto invalid_resource=resources;invalid_resource.resources[0].resource="no_namespace";assert(!valid_biomes(invalid_resource,error));
    cold.heat=std::numeric_limits<float>::quiet_NaN();assert(std::isfinite(a.environment(a.column(0,0,cold,0),100,76).temperature_c));
    std::cout<<"CAVE_BIOMES_PASS: coherent climate and depth, surface influence, material references, opt-in resource metadata without placement\n";
}
