#pragma once
#include "cave_plan.h"
#include <memory>
namespace altaria::caves {
enum class Fluid : uint8_t { dry, water, lava };
enum class WaterSource : uint8_t { none, lake, surface_water, ocean, thermal };
struct HydroSettings {
    int basin_regions=0;
    float lake_chance=.58f,lava_chance=.32f,lake_fill=.52f,lava_fill=.28f;
    int lake_depth=0; // 0 uses either intermediate layer.
    int lava_min_depth=3; // Shallowest layer that may hold lava; deeper rooms win.
};
struct Outlet {
    WaterSource source=WaterSource::none;
    int dry_limit=8192; // Highest cell that cannot spill through this entrance.
};
struct RiverCourse {uint64_t passage;bool reverse;};
struct HydroPlan {
    Region basin;
    Fluid fluid=Fluid::dry;
    WaterSource source=WaterSource::none;
    int top=0; // Inclusive highest voxel; the exposed liquid plane is top + 1.
    uint64_t source_room=0;
    std::shared_ptr<const std::vector<RiverCourse>> rivers;
    Fluid at(int y) const {return y<=top?fluid:Fluid::dry;}
};
bool valid_hydrology(const HydroSettings &s);
HydroPlan plan_hydrology(Region basin,uint64_t seed,uint64_t dimension,const HydroSettings &settings,int sea_level,
                        const std::vector<const Network*> &networks,
                        const std::function<Outlet(const Passage&)> &outlet);
const char *fluid_name(Fluid fluid);
const char *water_source_name(WaterSource source);
}
