#include "voxel_core.h"
#include "block_shapes.h"
#include <algorithm>
#include <cmath>
namespace altaria {
static uint64_t hash(int x, int z, int seed) {
    uint64_t n = uint32_t(x) * 0x9e3779b185ebca87ULL ^ uint32_t(z) * 0xc2b2ae3d27d4eb4fULL ^ uint32_t(seed);
    n = (n ^ (n >> 30)) * 0xbf58476d1ce4e5b9ULL;
    return n ^ (n >> 27);
}
static float smooth(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
std::vector<std::pair<IVec3, BlockId>> Generator::trees(int minx, int minz, int maxx, int maxz) const {
    std::vector<std::pair<IVec3, BlockId>> out;
    if (!cfg.vegetation)
        return out;
    auto wood = registry.find("altaria:wood"), leaves = registry.find("altaria:leaves");
    if (!wood || !leaves)
        return out;
    int spacing = std::max(8, cfg.tree_spacing), pad = 3;
    auto emit = [&](int x, int y, int z, BlockId id) {
        if (x >= minx && x < maxx && z >= minz && z < maxz && y < cfg.max_y)
            out.push_back({{x, y, z}, id});
    };
    for (int gz = int(std::floor(double(minz - pad) / spacing));
         gz <= int(std::floor(double(maxz + pad) / spacing)); gz++)
        for (int gx = int(std::floor(double(minx - pad) / spacing));
             gx <= int(std::floor(double(maxx + pad) / spacing)); gx++) {
            auto h = hash(gx, gz, seed);
            int x = gx * spacing + 3 + int((h >> 16) % uint64_t(spacing - 6));
            int z = gz * spacing + 3 + int((h >> 32) % uint64_t(spacing - 6));
            auto c = column(x, z);
            auto &biome = cfg.biomes[c.biome];
            float valley_boost = 1.f + .32f * c.valley * biome.humidity;
            float mountain_thin = 1.f - .65f * smooth(.35f, .78f, c.mountain);
            float effective_density = std::clamp(biome.tree_density * valley_boost * mountain_thin, 0.f, .95f);
            if (float(h & 65535) / 65535 > effective_density || c.height < cfg.sea_level + 2 ||
                c.height > cfg.snow_height - 6 || c.cliff > .24f || c.slope > .46f ||
                c.surface == registry.stone || c.surface == registry.sand)
                continue;
            int expected_y = int(std::floor(c.height));
            int floor_y = -999999;
            for (int y = expected_y + 5; y >= expected_y - 6; y--) {
                BlockId ground = terrain_sample(x, y, z);
                if (registry.get(ground).solid) {
                    floor_y = y;
                    break;
                }
            }
            if (floor_y < cfg.sea_level + 1 || std::abs(floor_y - expected_y) > 3)
                continue;
            BlockId ground_block = terrain_sample(x, floor_y, z);
            if (ground_block == registry.stone || ground_block == registry.sand ||
                registry.get(terrain_sample(x, floor_y + 1, z)).solid)
                continue;
            int height = 5 + int((h >> 48) % 3);
            // Voxel canopy; trunk is emitted last so leaf overlaps cannot replace wood.
            for (int dy = height - 2; dy <= height + 1; dy++)
                for (int dz = -2; dz <= 2; dz++)
                    for (int dx = -2; dx <= 2; dx++) {
                        int radius = dy == height + 1 ? 1 : 2;
                        if (std::abs(dx) > radius || std::abs(dz) > radius ||
                            std::abs(dx) + std::abs(dz) > radius + 1)
                            continue;
                        emit(x + dx, floor_y + dy, z + dz, leaves);
                    }
            for (int y = 1; y <= height; y++)
                emit(x, floor_y + y, z, wood);
        }
    return out;
}
uint8_t Generator::surface_shape(int x, int y, int z, const Column &c) const {
    if (!cfg.surface_shapes || c.cliff > .2f || y != int(std::floor(c.height)))
        return 0;
    float fraction = c.height - std::floor(c.height);
    // Quantize to half-height caps. Flat interiors and cliffs keep full cubes.
    if (fraction < .48f && c.height > cfg.sea_level + 1)
        return 1;
    return 0;
}
} // namespace altaria
