#pragma once
#include "cave_field.h"
#include "cave_depth.h"
#include "cave_biomes.h"
#include "cave_hydrology.h"
#include "cave_noise.h"
#include <FastNoise/FastNoise.h>
#include <atomic>
#include <condition_variable>
#include <functional>
namespace altaria { struct Column; }
namespace altaria::caves {
struct Config {
    bool enabled=false;
    bool biome_variation=true;
    bool formation_shapes=true;
    PlanSettings plan;
    DepthSettings depth;
    BiomeSettings biomes;
    std::vector<std::string> exploration_formations;
    HydroSettings hydrology;
    NoiseSettings noise;
    uint64_t dimension=0;
    size_t cache_regions=64,cache_bytes=32*1024*1024;
    int roof_grid=8,max_lod=2;
    float detail_scale=13,detail_amplitude=.65f;
};
bool validate(const Config &config,std::string &error);
// A worker-only, batched surface atlas avoids thousands of scalar FastNoise
// calls per plan. Immutable plans never reference scene objects or edit maps.
class Runtime {
    struct RoofTile {std::vector<float> heights;uint64_t used;size_t bytes=0;};
    Config cfg;
    std::function<std::vector<Column>(const std::vector<float>&,const std::vector<float>&)> columns;
    float cliff_strength;
    std::unordered_map<Cell,std::shared_ptr<RoofTile>,CellHash> roofs;
    mutable std::mutex roof_mutex;
    size_t roof_bytes=0;
    uint64_t roof_clock=0,identity;
    std::shared_ptr<const Planner> planner;
    std::unique_ptr<PlanCache> cache;
    FastNoise::SmartNode<> noise;
    int noise_seed;
    int sea_level;
    std::function<Outlet(const Passage&)> outlet_probe;
    std::function<bool(const Column&,int,int,int)> host_solid;
    struct BasinEntry {HydroPlan plan;uint64_t used=0;bool ready=false;};
    mutable std::mutex basin_mutex;
    mutable std::condition_variable basin_ready;
    mutable uint64_t basin_clock=0;
    mutable std::unordered_map<Cell,std::shared_ptr<BasinEntry>,CellHash> basins;
    struct SupportEntry {std::shared_ptr<const FormationMask> mask;uint64_t used=0;bool ready=false;};
    mutable std::mutex support_mutex;
    mutable std::condition_variable support_ready;
    mutable uint64_t support_clock=0;
    mutable size_t support_bytes=0;
    mutable std::unordered_map<uint64_t,std::shared_ptr<SupportEntry>> support;
    std::shared_ptr<const FormationMask> formation_support(const Formation &formation) const;
    std::shared_ptr<RoofTile> roof_tile(int x,int z);
    float roof(float x,float z);
    float opening_roof(float x,float z) const;
public:
    Runtime(Config config,int seed,float cliff_strength,
            std::function<std::vector<Column>(const std::vector<float>&,const std::vector<float>&)> columns,
            int sea_level=0,std::function<Outlet(const Passage&)> outlet_probe={},
            std::function<std::vector<Formation>(const Network&)> formations={},
            std::function<bool(const Column&,int,int,int)> host_solid={});
    Query query(Bounds bounds) const;
    const Query &point_query(int x,int z) const;
    FieldSample sample(const Query &query,int x,int y,int z,float host_roof,
                       const std::vector<uint32_t> *prepared_bin=nullptr) const;
    bool entrance_contains(const Passage &opening,int x,int y,int z) const;
    Region basin_at(int x,int z) const;
    HydroPlan hydrology(Region basin) const; // Worker/scalar generation only.
    DepthSample depth(float roof,float y,float x,float z) const {
        return classify_depth(roof,y,cfg.plan.depths,cfg.depth,planner->geothermal(x,z));
    }
    float geothermal(float x,float z) const {return planner->geothermal(x,z);}
    std::shared_ptr<const IndexedNetwork> region(Region r) const {return cache->get(r);}
    std::shared_ptr<const IndexedNetwork> resident(Region r) const {return cache->resident(r);}
    std::shared_ptr<const FormationMask> resident_support(uint64_t id) const;
    CacheStats stats() const;
};
}
