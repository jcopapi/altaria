#include "voxel_core.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace altaria {
uint32_t pack_rgb(RGB c) {
    auto v = [](float f) { return uint32_t(std::clamp(int(std::round(f * 255)), 0, 255)); };
    return (v(c.r) << 16) | (v(c.g) << 8) | v(c.b);
}
RGB unpack_rgb(uint32_t c) {
    return {float((c >> 16) & 255) / 255, float((c >> 8) & 255) / 255, float(c & 255) / 255};
}
std::vector<BiomeDef> default_biomes() {
    std::vector<BiomeDef> biomes = {
        {"normal", "Meadow", {.30f, .64f, .22f}, .64f, .5f, 1, 1, .10f, .65f, 1.35f, false, false},
        {"snow", "Snowfields", {.57f, .75f, .69f}, .10f, .5f, .8f, 1, .035f, .45f, 1.2f, false, true},
        {"desert", "Desert", {.69f, .67f, .27f}, .96f, .08f, .30f, .10f, .08f, .6f, 1.3f, true, false},
        {"jungle", "Jungle", {.075f, .49f, .20f}, .82f, .95f, .85f, .9f, .16f, .7f, 1.6f, false, false},
        {"enchanted", "Enchanted", {.65f, .24f, .88f}, .59f, .7f, 1, 1.05f, .15f, .65f, 1.1f, false, false}};
    biomes[1].fog_day = {.88f, .94f, 1};
    biomes[1].fog_night = {.48f, .57f, .76f};
    biomes[1].fog_start = 80;
    biomes[1].fog_end = 780;
    biomes[2].fog_day = {.86f, .72f, .43f};
    biomes[2].fog_night = {.40f, .29f, .24f};
    biomes[2].fog_start = 55;
    biomes[2].fog_end = 550;
    biomes[3].fog_day = {.31f, .56f, .60f};
    biomes[3].fog_night = {.12f, .20f, .30f};
    biomes[3].fog_start = 450;
    biomes[3].fog_end = 2600;
    biomes[4].fog_day = {.61f, .35f, .82f};
    biomes[4].fog_night = {.26f, .12f, .49f};
    biomes[4].fog_start = 180;
    biomes[4].fog_end = 2200;
    return biomes;
}
static uint64_t hash_cell(int x, int z, int seed) {
    uint64_t v = uint32_t(x) * 0x9e3779b185ebca87ULL ^ uint32_t(z) * 0xc2b2ae3d27d4eb4fULL ^ uint32_t(seed);
    v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ULL;
    v = (v ^ (v >> 27)) * 0x94d049bb133111ebULL;
    return v ^ (v >> 31);
}
BiomeStage::BiomeStage(const Settings &s, int value) : cfg(s), seed(value) {
    auto t = FastNoise::New<FastNoise::Simplex>();
    t->SetScale(s.biome_scale * 2.3f);
    temperature = t;
    auto h = FastNoise::New<FastNoise::Simplex>();
    h->SetScale(s.biome_scale * 1.8f);
    humidity = h;
    auto w = FastNoise::New<FastNoise::Simplex>();
    w->SetScale(s.biome_scale * .75f);
    warp = w;
}
std::vector<BiomeSample> BiomeStage::regions(const std::vector<float> &x, const std::vector<float> &z) const {
    std::vector<BiomeSample> out(x.size());
    if (x.empty() || cfg.biomes.empty())
        return out;
    int n = int(x.size());

    // Pre-sort biomes along thermal-moisture axis when testing_biomes is active so warm and cold
    // biomes are separated by temperate buffer zones (Minecraft-style proximity temperature rule:
    // Snowfields (cold) can NEVER spawn adjacent to Desert (hot)).
    std::vector<int> thermal_order(cfg.biomes.size());
    for (size_t b = 0; b < cfg.biomes.size(); b++)
        thermal_order[b] = int(b);
    std::sort(thermal_order.begin(), thermal_order.end(), [&](int a, int b) {
        if (std::abs(cfg.biomes[a].temperature - cfg.biomes[b].temperature) > .06f)
            return cfg.biomes[a].temperature < cfg.biomes[b].temperature;
        return cfg.biomes[a].humidity < cfg.biomes[b].humidity;
    });

    auto classify_cell = [&](int gx, int gz, float px, float pz, uint64_t h) -> int {
        if (!cfg.testing_biomes || cfg.biomes.size() != 5) {
            float heat = temperature->GenSingle2D(px, pz, seed) * .5f + .5f;
            float wet = humidity->GenSingle2D(px, pz, int32_t(uint32_t(seed) + 131)) * .5f + .5f;
            float best = 1e9f;
            int id = 0;
            for (size_t b = 0; b < cfg.biomes.size(); b++) {
                float dt = heat - cfg.biomes[b].temperature, dh = wet - cfg.biomes[b].humidity;
                float score = dt * dt + dh * dh;
                if (score < best) {
                    best = score;
                    id = int(b);
                }
            }
            return id;
        }
        // Continuous macro-climate proximity field over cell coordinates (gx, gz):
        // Smoothly varies temperature in a wave across 2x2..3x3 cell clusters so biomes form
        // giant, long-lasting regions and follow a strict thermal gradient:
        // Cold (Snowfields) <-> Temperate (Meadow / Enchanted) <-> Hot (Jungle / Desert).
        // Because the thermal wave changes by at most 0.36 per cell step, Cold (< -0.42) and
        // Hot (> +0.42) are ALWAYS separated by at least one Temperate cell!
        uint64_t macro_h = hash_cell(gx >> 1, gz >> 1, seed + 19);
        float phase_x = float(seed & 255) * .0245f;
        float phase_z = float((seed >> 8) & 255) * .0245f;
        float wave_t = std::sin(float(gx) * .72f + float(gz) * .48f + phase_x) * .68f +
                       std::cos(float(gx) * .36f - float(gz) * .76f + phase_z) * .32f;
        float wave_w = std::cos(float(gx) * .64f - float(gz) * .58f + phase_z) * .62f +
                       std::sin(float(gx) * .42f + float(gz) * .68f + phase_x) * .38f +
                       (float(macro_h & 255) / 255.f - .5f) * .25f;
        if (wave_t < -.38f)
            return thermal_order[0]; // Coldest: Snowfields
        if (wave_t > .38f) {
            // Hot band: Desert (arid) or Jungle (humid)
            return wave_w < 0.f ? thermal_order[4] : thermal_order[3];
        }
        // Temperate buffer band between Cold and Hot: Enchanted or Meadow
        return wave_w > .08f ? thermal_order[1] : thermal_order[2];
    };

    // Enforce strict 3x3 Voronoi cell guard: Snowfields and Desert can never be 8-connected neighbors
    auto safe_cell = [&](int gx, int gz) -> int {
        uint64_t h = hash_cell(gx, gz, seed);
        float px = (gx + .15f + float(h & 65535) / 65535.f * .7f) * cfg.biome_scale;
        float pz = (gz + .15f + float((h >> 16) & 65535) / 65535.f * .7f) * cfg.biome_scale;
        int id = classify_cell(gx, gz, px, pz, h);
        if (cfg.biomes[id].snow) {
            for (int nz = -1; nz <= 1; nz++)
                for (int nx = -1; nx <= 1; nx++) {
                    if (nx == 0 && nz == 0)
                        continue;
                    uint64_t nh = hash_cell(gx + nx, gz + nz, seed);
                    float npx = (gx + nx + .15f + float(nh & 65535) / 65535.f * .7f) * cfg.biome_scale;
                    float npz = (gz + nz + .15f + float((nh >> 16) & 65535) / 65535.f * .7f) * cfg.biome_scale;
                    int nid = classify_cell(gx + nx, gz + nz, npx, npz, nh);
                    if (cfg.biomes[nid].desert)
                        return thermal_order.size() > 2 ? thermal_order[2] : 0;
                }
        }
        return id;
    };

    for (int i = 0; i < n; i++) {
        int cx = int(std::floor(x[i] / cfg.biome_scale)), cz = int(std::floor(z[i] / cfg.biome_scale));
        float first = std::numeric_limits<float>::max(), second = first, diff_second = first;
        int id1 = 0, id2 = 0;
        struct CellCandidate {
            float dist;
            int id;
        };
        CellCandidate cands[9];
        int cand_count = 0;
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++) {
                int gx = cx + dx, gz = cz + dz;
                uint64_t h = hash_cell(gx, gz, seed);
                float px = (gx + .15f + float(h & 65535) / 65535.f * .7f) * cfg.biome_scale;
                float pz = (gz + .15f + float((h >> 16) & 65535) / 65535.f * .7f) * cfg.biome_scale;
                float dist = (x[i] - px) * (x[i] - px) + (z[i] - pz) * (z[i] - pz);
                int id = safe_cell(gx, gz);
                cands[cand_count++] = {dist, id};
                if (dist < first) {
                    second = first;
                    id2 = id1;
                    first = dist;
                    id1 = id;
                } else if (dist < second) {
                    second = dist;
                    id2 = id;
                }
            }
        // Compute distance to nearest cell of a DIFFERENT biome so rivers and transition zones
        // follow actual biome boundaries rather than internal Voronoi edges of the same biome!
        // Because same-biome cells coalesce seamlessly (`id1 == id2`), each biome forms a giant,
        // continuous region across multiple Voronoi cells without artificial seams inside it.
        for (int c = 0; c < cand_count; c++) {
            if (cands[c].id != id1 && cands[c].dist < diff_second) {
                diff_second = cands[c].dist;
                id2 = cands[c].id;
            }
        }
        if ((cfg.biomes[id1].snow && cfg.biomes[id2].desert) ||
            (cfg.biomes[id1].desert && cfg.biomes[id2].snow)) {
            id2 = thermal_order.size() > 2 ? thermal_order[2] : 0;
        }
        float d1 = std::sqrt(first);
        float diff_d2 = diff_second < std::numeric_limits<float>::max() * .5f ? std::sqrt(diff_second) : d1 + cfg.biome_scale;
        float border_dist = std::max(0.f, diff_d2 - d1);
        float t = std::clamp(border_dist / std::max(1.f, cfg.biome_blend), 0.f, 1.f);
        t = t * t * (3.f - 2.f * t);
        out[i] = {id1,
                  id2,
                  .5f + .5f * t,
                  border_dist,
                  cfg.biomes[id1].temperature,
                  cfg.biomes[id1].humidity};
    }
    return out;
}
} // namespace altaria
