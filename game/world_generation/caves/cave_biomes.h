#pragma once
#include "cave_depth.h"
#include "cave_formations.h"
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
namespace altaria::caves {
struct ClimateRange {float min=0,max=1,feather=.12f;};
struct BiomeDefinition {
    std::string id="altaria:limestone",name="Limestone caves",rock="$stone";
    std::array<float,4> depths{1,1,1,1};
    ClimateRange heat,humidity,geology,geothermal;
    float weight=1,temperature_delta=0,oxygen=1,visibility=1,environmental_damage=0;
    bool requires_water=false;
    std::vector<std::string> formations,entities,resource_rules;
};
// Metadata for a future resource generator. Nothing in terrain generation
// queries these rules or resolves their resource IDs to voxel block IDs.
struct ResourceRule {
    std::string id,resource;
    float abundance=1,region_scale=720,regional_variation=.35f;
    std::array<float,4> depth_weights{1,1,1,1};
    ClimateRange geology,geothermal;
    bool requires_water=false;
};
struct BiomeSettings {
    float region_scale=320,geology_scale=670,transition_ratio=.22f;
    std::string frozen_rock="$stone",desert_rock="$stone",rooted_rock="$stone";
    std::string fallback="altaria:limestone";
    std::vector<BiomeDefinition> definitions{BiomeDefinition()};
    std::vector<ResourceRule> resources;
    std::vector<FormationRule> formations;
};
bool valid_biomes(const BiomeSettings &settings,std::string &error);
struct SurfaceClimate {
    float heat=.6f,humidity=.5f,frozen=0,desert=0,lush=0,volcanic=0;
};
struct BiomePair {uint16_t primary=0,secondary=0;float primary_weight=1;};
struct BiomeColumn {
    std::array<float,64> spatial_weights{};
    SurfaceClimate surface;
    float geothermal=0,heat=.5f,humidity=.5f,geology=.5f;
};
struct CaveEnvironment {
    DepthSample depth;
    BiomePair biomes;
    float temperature_c=12,humidity=.5f,pressure_kpa=101.325f,oxygen=1,visibility=1,damage_per_second=0;
    float frozen=0,sedimentary=0,rooted=0;
};
// No scene, cache or planner access. Column precursors are computed once per
// terrain column; point/material queries use the immutable compact result.
class BiomeStage {
    BiomeSettings cfg;
    DepthSettings depth_cfg;
    std::array<float,4> targets;
    uint64_t seed;
    std::vector<uint16_t> rocks;
    std::vector<uint64_t> salts;
    std::vector<uint64_t> resource_salts;
    std::vector<std::vector<uint16_t>> biome_resources;
    std::vector<std::vector<uint16_t>> biome_formations;
    std::vector<std::array<uint16_t,2>> formation_rocks;
    uint16_t frozen=0,desert=0,rooted=0;
    bool configured=false;
    std::string failure;
public:
    using Resolver=std::function<bool(const std::string &,uint16_t &)>;
    BiomeStage(BiomeSettings settings,DepthSettings depths,std::array<float,4> targets,
               uint64_t seed,uint64_t dimension,const Resolver &resolve,const Resolver &resolve_plant = {});
    bool valid(std::string &error) const {error=failure;return configured;}
    BiomeColumn column(float x,float z,SurfaceClimate surface,float geothermal) const;
    CaveEnvironment environment(const BiomeColumn &column,float roof,float y,bool water=false) const;
    uint16_t material(const BiomeColumn &column,float roof,int x,int y,int z,bool water=false) const;
    float resource_abundance(std::string_view resource,const BiomeColumn &column,float roof,
                             float x,float y,float z,bool water=false) const;
    bool has_formations() const {return !cfg.formations.empty();}
    void plan_formations(const Chamber &room,const BiomeColumn &column,float roof,
                         std::vector<Formation> &out) const;
    const FormationRule &formation_definition(uint16_t index) const {return cfg.formations.at(index);}
    const BiomeDefinition &definition(uint16_t index) const {return cfg.definitions[index<cfg.definitions.size()?index:0];}
};
}
