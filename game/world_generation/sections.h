#pragma once
#include "voxel_core.h"
#include <cmath>
namespace altaria {
// Full-resolution tiles render as stacked 32 m sections, so the renderer culls
// each against the view instead of a tile's whole column (caves included).
inline constexpr int section_height = 32;
inline int section_band(float y) { return int(std::floor(y / section_height)); }
struct SectionMesh {
    int band = 0;
    Surface solid, water;
};
// Buckets triangles by world-space band; triangles crossing a boundary are
// clipped so every piece lies inside exactly one band.
std::vector<SectionMesh> split_sections(const Surface &solid, const Surface &water);
} // namespace altaria
