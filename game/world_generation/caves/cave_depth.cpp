#include "cave_depth.h"
#include <algorithm>
#include <cmath>
namespace altaria::caves {
static float smooth(float a,float b,float x) {
    float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3-2*t);
}
bool valid_depth(const DepthSettings &s,const std::array<float,4> &targets) {
    float previous=0;
    for(float d:targets){if(!std::isfinite(d) || d<=previous || d>16384)return false;previous=d;}
    for(int i=0;i<3;i++)if(!std::isfinite(s.transition_widths[i]) || s.transition_widths[i]<1 ||
                         s.transition_widths[i]>targets[i+1]-targets[i])return false;
    return std::isfinite(s.surface_influence_depth) && s.surface_influence_depth>=32 && s.surface_influence_depth<=1024;
}
DepthSample classify_depth(float roof,float y,const std::array<float,4> &targets,const DepthSettings &s,float geothermal) {
    if(!valid_depth(s,targets))return {};
    return classify_depth_validated(roof,y,targets,s,geothermal);
}
DepthSample classify_depth_validated(float roof,float y,const std::array<float,4> &targets,const DepthSettings &s,float geothermal) {
    DepthSample out;
    if(!std::isfinite(roof) || !std::isfinite(y) || !std::isfinite(geothermal))return out;
    out.below_surface=std::max(0.f,roof-y);
    out.surface_influence=1-smooth(8,s.surface_influence_depth,out.below_surface);
    out.geothermal=std::clamp(geothermal,0.f,1.f);
    std::array<float,3> transitions;
    for(int i=0;i<3;i++) {
        float middle=targets[i]+(targets[i+1]-targets[i])*.5f,half=s.transition_widths[i]*.5f;
        transitions[i]=smooth(middle-half,middle+half,out.below_surface);
        if(i)transitions[i]=std::min(transitions[i],transitions[i-1]);
    }
    // Deep cold regions remain abyssal. Depth alone never makes every region
    // infernal; the same regional heat potential also controls planned chambers.
    transitions[2]*=smooth(.2f,.65f,out.geothermal);
    out.weights={1-transitions[0],transitions[0]-transitions[1],transitions[1]-transitions[2],transitions[2]};
    for(int i=1;i<4;i++)if(out.weights[i]>out.weights[out.primary])out.primary=i;
    return out;
}
const char *depth_name(int level) {
    const char *names[]={"normal","deep","abyssal","infernal"};return names[std::clamp(level,0,3)];
}
}
