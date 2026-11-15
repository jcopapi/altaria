#pragma once
#include <array>
namespace altaria::caves {
struct DepthSettings {
    std::array<float,3> transition_widths{20,30,44};
    float surface_influence_depth=210;
};
struct DepthSample {
    std::array<float,4> weights{1,0,0,0};
    float below_surface=0,surface_influence=1,geothermal=0;
    int primary=0;
};
bool valid_depth(const DepthSettings &settings,const std::array<float,4> &targets);
DepthSample classify_depth(float roof,float y,const std::array<float,4> &targets,
                           const DepthSettings &settings,float geothermal);
// Same, for callers that validated the settings once up front.
DepthSample classify_depth_validated(float roof,float y,const std::array<float,4> &targets,
                           const DepthSettings &settings,float geothermal);
const char *depth_name(int level);
}
