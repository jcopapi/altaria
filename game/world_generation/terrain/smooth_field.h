#pragma once
#include "../voxel_core.h"
namespace altaria {
struct SmoothCell {
    std::array<float,3> origin{}, size{1,1,1};
    std::array<float,3> anchor{}; // original grid/piece position, independent of shape bounds
    Column column;
    BlockId block = 0;
    uint64_t owner = 0;
    bool modified = false;
    Appearance appearance;
};
// A value snapshot: workers never read the scene tree or authoritative stores.
struct SmoothCoordHash {
    size_t operator()(const IVec3 &p) const { return uint32_t(p.x)*73856093u ^ uint32_t(p.y)*19349663u ^ uint32_t(p.z)*83492791u; }
};
class SmoothField {
    std::vector<SmoothCell> sources;
    std::vector<std::array<float,9>> heights;
    float cell_distance(size_t index,std::array<float,3> p) const;
    std::unordered_map<IVec3,std::vector<size_t>,SmoothCoordHash> bins;
public:
    explicit SmoothField(std::vector<SmoothCell> cells,float maximum_height=1);
    float distance(std::array<float,3> p) const;
    bool modified_surface(std::array<float,3> p) const;
    const std::vector<SmoothCell>& cells() const {return sources;}
    const SmoothCell *owner_at(std::array<float,3> p) const;
};
}
