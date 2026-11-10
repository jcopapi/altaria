#include "voxel_core.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace altaria {
static uint64_t mix(uint64_t n) {
    n = (n ^ (n >> 30)) * 0xbf58476d1ce4e5b9ULL;
    n = (n ^ (n >> 27)) * 0x94d049bb133111ebULL;
    return n ^ (n >> 31);
}
size_t CoordHash::operator()(const IVec3 &p) const {
    return mix(uint32_t(p.x)) ^ mix(uint64_t(uint32_t(p.y)) + 0x12345) ^
           mix(uint64_t(uint32_t(p.z)) + 0xabcdef);
}
size_t TileHash::operator()(const TileKey &p) const {
    return CoordHash{}({p.x, p.level, p.z});
}
static int offset_seed(int seed, uint32_t offset) {
    return int32_t(uint32_t(seed) + offset);
}
static float smooth(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
bool Registry::add(BlockDef def, std::string &error) {
    if (def.key.empty() || keys.count(def.key) || def.id > 4095 ||
        (def.id < blocks.size() && !blocks[def.id].key.empty())) {
        error = "Duplicate or invalid block id/key";
        return false;
    }
    if (blocks.size() <= def.id)
        blocks.resize(def.id + 1);
    keys[def.key] = def.id;
    blocks[def.id] = std::move(def);
    return true;
}
bool Registry::valid(BlockId id) const {
    return id < blocks.size() && !blocks[id].key.empty();
}
const BlockDef &Registry::get(BlockId id) const {
    return blocks[valid(id) ? id : air];
}
BlockId Registry::find(const std::string &key) const {
    auto it = keys.find(key);
    return it == keys.end() ? air : it->second;
}
static FastNoise::SmartNode<> fbm(float scale, int octaves, bool ridged = false) {
    auto src = FastNoise::New<FastNoise::Simplex>();
    src->SetScale(scale);
    if (ridged) {
        auto f = FastNoise::New<FastNoise::FractalRidged>();
        f->SetSource(src);
        f->SetOctaveCount(octaves);
        f->SetWeightedStrength(.65f);
        return f;
    }
    auto f = FastNoise::New<FastNoise::FractalFBm>();
    f->SetSource(src);
    f->SetOctaveCount(octaves);
    f->SetGain(.48f);
    return f;
}
Generator::Generator(const Settings &s, const Registry &r, int value)
    : cfg(s), registry(r), seed(value), biome_stage(s, value) {
    continent = fbm(s.continental_scale, 3);
    erosion = fbm(s.erosion_scale, 3);
    ridges = fbm(s.mountain_scale, s.octaves, true);
    spur_ridges = fbm(std::max(16.f, s.mountain_scale * .52f), std::max(2, s.octaves - 1), true);
    chain_spine = fbm(std::max(64.f, s.mountain_region_scale * .62f), 2);
    plateau_noise = fbm(std::max(64.f, s.continental_scale * .58f), 2);
    detail = fbm(s.detail_scale, 3);
    warp = fbm(s.warp_scale, 2);
    auto d = FastNoise::New<FastNoise::Simplex>();
    d->SetScale(s.density_scale);
    density = d;
    mountain_regions = fbm(s.mountain_region_scale, 2);
}
std::vector<Column> Generator::columns(const std::vector<float> &x, const std::vector<float> &z,
                                       StageTimings *timings) const {
    auto clock = std::chrono::steady_clock::now();
    auto biome_map = biome_stage.regions(x, z);
    auto after_biomes = std::chrono::steady_clock::now();
    if (timings)
        timings->biomes_ms += std::chrono::duration<double, std::milli>(after_biomes - clock).count();
    int n = int(x.size());
    std::vector<Column> result(n);
    if (!n)
        return result;
    std::vector<float> wx(n), wz(n), px(n), pz(n), c(n), e(n), r(n), spur_r(n), spine(n), plat(n), d(n),
        river(n), regions(n);
    warp->GenPositionArray2D(wx.data(), n, x.data(), z.data(), 0, 0, offset_seed(seed, 101));
    warp->GenPositionArray2D(wz.data(), n, x.data(), z.data(), 0, 0, offset_seed(seed, 509));
    for (int i = 0; i < n; i++) {
        px[i] = x[i] + wx[i] * cfg.warp_amplitude;
        pz[i] = z[i] + wz[i] * cfg.warp_amplitude;
    }
    continent->GenPositionArray2D(c.data(), n, px.data(), pz.data(), 0, 0, seed);
    erosion->GenPositionArray2D(e.data(), n, px.data(), pz.data(), 0, 0, offset_seed(seed, 57));
    ridges->GenPositionArray2D(r.data(), n, px.data(), pz.data(), 0, 0, offset_seed(seed, 93));
    spur_ridges->GenPositionArray2D(spur_r.data(), n, px.data(), pz.data(), 317, -241, offset_seed(seed, 331));
    chain_spine->GenPositionArray2D(spine.data(), n, px.data(), pz.data(), -512, 819, offset_seed(seed, 619));
    plateau_noise->GenPositionArray2D(plat.data(), n, px.data(), pz.data(), 419, -613, offset_seed(seed, 829));
    detail->GenPositionArray2D(d.data(), n, px.data(), pz.data(), 0, 0, offset_seed(seed, 87));
    erosion->GenPositionArray2D(river.data(), n, px.data(), pz.data(), 721, 391, offset_seed(seed, 713));
    mountain_regions->GenPositionArray2D(regions.data(), n, px.data(), pz.data(), 0, 0,
                                         offset_seed(seed, 947));
    auto after_noise = std::chrono::steady_clock::now();
    if (timings)
        timings->terrain_ms += std::chrono::duration<double, std::milli>(after_noise - after_biomes).count();
    for (int i = 0; i < n; i++) {
        float er = std::clamp(e[i] * .65f + .5f, 0.f, 1.f),
              rough = 1 - smooth(.18f, cfg.plains_threshold, er);
        float ridge = std::clamp(r[i] * .5f + .5f, 0.f, 1.f);
        float spur = std::clamp(spur_r[i] * .5f + .5f, 0.f, 1.f);
        float summit_crown = smooth(.48f, .86f, ridge) * (.76f + .24f * smooth(.35f, .85f, spur));
        const auto &mask = biome_map[i];
        const auto &primary = cfg.biomes[mask.primary];
        const auto &secondary = cfg.biomes[mask.secondary];
        float w = mask.primary_weight;
        float terrain_multiplier = primary.terrain_multiplier * w + secondary.terrain_multiplier * (1 - w);
        float biome_mountains = primary.mountain_multiplier * w + secondary.mountain_multiplier * (1 - w);
        float shape = cfg.generator_version == 1 ? 1.f : terrain_multiplier;

        // 1. Continental & Regional Scale:
        // Continental pattern: ocean basins (continentalness < 0.24), coastal shelves (0.24..0.34),
        // broad inland buildable plains, elevated plateaus, and interior mountain ranges.
        float cont_raw = std::clamp(c[i] * .62f + .52f, 0.f, 1.f);
        bool ocean_enabled = (cfg.generator_version >= 2 && cfg.continental_amplitude > 1.f && cfg.sea_level > cfg.min_y + 12);
        float ocean_mask = ocean_enabled ? (1.f - smooth(.17f, .30f, cont_raw)) : 0.f;
        float inland_mask = ocean_enabled ? smooth(.23f, .40f, cont_raw) : 1.f;
        float coastal_shelf = ocean_enabled ? smooth(.19f, .29f, cont_raw) * (1.f - smooth(.30f, .42f, cont_raw)) : 0.f;

        // Continuous mountain cordillera spine (wide zero-crossing ribbon with variable width)
        float spine_width = .28f + .14f * er;
        float spine_axis = 1.f - smooth(0.f, spine_width, std::abs(spine[i]));
        float region_raw = regions[i] * .5f + .5f;
        float region_env = smooth(cfg.mountain_threshold, cfg.mountain_threshold + cfg.mountain_blend, region_raw);
        float spine_bridge =
            spine_axis * smooth(std::max(0.f, cfg.mountain_threshold - .11f),
                                cfg.mountain_threshold + cfg.mountain_blend * .65f, region_raw);
        float mountain = cfg.generator_version == 1
                             ? 1.f
                             : std::clamp(region_env * (.72f + .38f * spine_axis) + spine_bridge * .45f, 0.f, 1.f) *
                                   inland_mask;
        // Volumetric mountain body envelope: gives mountains wide, full shoulders instead of thin cones
        float mountain_body = smooth(.08f, .78f, mountain);

        // Elevated flat-topped plateaus with steep escarpment rims
        float raw_plat = plat[i] * .5f + .5f;
        float plateau_mask = cfg.generator_version == 1
                                 ? 0.f
                                 : smooth(.61f, .72f, raw_plat) * (1.f - smooth(.48f, .80f, mountain)) *
                                       smooth(.26f, .50f, er) * inland_mask;
        float plat_edge = 4.f * plateau_mask * (1.f - plateau_mask);

        // 2. River Networks (Biome-Border Rivers + Tributary Rivers + Lakes + Mountain Waterfall Streams):
        // Border rivers follow biome boundaries (mask.border_distance -> 0 at border between distinct biomes).
        float river_meander = d[i] * 11.f + wx[i] * 7.f;
        float border_dist_warped = std::max(0.f, mask.border_distance + river_meander);
        float border_river_width = std::clamp(cfg.biome_scale * .042f, 14.f, 34.f);
        float border_river =
            (cfg.generator_version >= 2 && mask.primary != mask.secondary && cfg.valley_depth > 1.f)
                ? (1.f - smooth(0.f, border_river_width, border_dist_warped))
                : 0.f;

        // River breaks / widenings that form natural Lakes along the river course while keeping the river flowing to the ocean!
        // Where `plat[i]` or `er` creates a basin along a river or biome border, the river expands into a wide lake (40..75 blocks).
        float lake_basin_trigger = smooth(.52f, .76f, er) * smooth(.15f, .55f, 1.f - raw_plat) * (1.f - mountain_body);
        float border_lake = (cfg.generator_version >= 2 && mask.primary != mask.secondary && cfg.valley_depth > 1.f)
                                ? (1.f - smooth(0.f, border_river_width * 2.4f, border_dist_warped)) * lake_basin_trigger
                                : 0.f;

        // Continuous tributary river network (zero-crossing of `river[i]` and secondary `spine[i]` branch)
        // Because `river[i]` is a continuous 2D field whose zero-contour forms closed loops across the
        // continent-ocean shoreline (`cont_raw == 0.25`), every tributary, lake, and border river connects to ocean basins!
        float v_width = cfg.valley_width * (.78f + .48f * (1.f - .38f * mountain - .25f * plateau_mask));
        // Rivers widen into broad estuaries at the coast (`coastal_shelf`) and expand into lakes in inland basins (`lake_basin_trigger`)
        float lake_widen = 1.f + 1.85f * lake_basin_trigger;
        float estuary_widen = std::max(1.f + .65f * coastal_shelf, lake_widen);
        float valley_main = 1.f - smooth(0.f, v_width * estuary_widen, std::abs(river[i]));
        float main_lake = (1.f - smooth(0.f, v_width * 2.6f, std::abs(river[i]))) * lake_basin_trigger;
        float lake_mask = std::clamp(std::max(border_lake, main_lake) * inland_mask * (1.f - ocean_mask), 0.f, 1.f);

        float tributary_axis = std::abs(river[i] * .55f + wx[i] * .38f - wz[i] * .38f);
        float tributary = (1.f - smooth(0.f, v_width * .62f, tributary_axis)) * (.55f + .45f * (1.f - mountain));
        float ravine_axis = std::abs(wx[i] * .65f - wz[i] * .65f);
        float ravine = (1.f - smooth(0.f, cfg.valley_width * .45f, ravine_axis)) * rough * (.35f + .65f * mountain);

        // Combine lowland river channels + lakes: strongest along biome borders, main drainage valleys, lakes, and tributaries
        float river_channel =
            cfg.generator_version == 1
                ? 0.f
                : std::clamp(std::max({border_river, valley_main * .92f, tributary * .76f, lake_mask * .95f}) *
                                 (1.f - .65f * mountain_body),
                             0.f, 1.f);
        // Mountain headwater streams: where main rivers or tributaries climb into mountain flanks/plateaus (`valley_main` or `tributary` on `mountain > 0.22`)
        float mountain_stream =
            cfg.generator_version == 1
                ? 0.f
                : std::clamp(std::max(valley_main * .88f, tributary * .78f) *
                                 smooth(.18f, .58f, mountain + plateau_mask * .8f) * (1.f - .55f * summit_crown),
                             0.f, 1.f);

        float valley = std::clamp(std::max({valley_main, border_river * .88f, lake_mask * .85f}) +
                                      (ravine * .42f + tributary * .35f) * (1.f - valley_main),
                                  0.f, 1.f);
        plateau_mask *= (1.f - std::max(valley_main, border_river) * .88f);

        // 3. Buildable (Flat) Regions vs. Adventurous (Rugged) Regions:
        // Buildable areas are expansive, low-slope inland plains/meadows where erosion is high (smooth)
        // and free of mountains, deep rivers, or steep escarpments.
        float buildable_mask =
            cfg.generator_version == 1
                ? 0.f
                : smooth(.44f, .68f, er) * (1.f - smooth(.08f, .28f, mountain)) *
                      (1.f - smooth(.25f, .58f, plateau_mask)) * (1.f - smooth(.15f, .48f, river_channel)) *
                      inland_mask * (1.f - ocean_mask);
        // Adventurous areas feature high vertical divergence, dramatic relief, terraces, canyons, and arches
        float adventure_mask =
            cfg.generator_version == 1
                ? rough
                : std::clamp(std::max({mountain_body, plat_edge * 1.15f, rough * (1.f - buildable_mask)}), 0.f, 1.f);

        // 4. Geological Scale:
        // Massive, thick-bodied mountain chains with broad summits, shouldered ridges, and rocky crests
        float broad_power = std::max(.85f, cfg.ridge_power * .72f);
        float broad_ridge = std::pow(smooth(.08f, .92f, ridge), broad_power);
        float broad_spur = std::pow(smooth(.12f, .94f, spur), broad_power * 1.05f);
        float massif_dome = smooth(.12f, .82f, mountain) * (.48f + .52f * smooth(.15f, .75f, ridge));
        float backbone = smooth(.05f, .88f, spine_axis) * (.58f + .42f * broad_ridge) * (.80f + .20f * broad_spur);
        float crest_relief = .90f + .22f * broad_spur + .10f * d[i];
        float raw_chain = (broad_ridge * .56f + summit_crown * .28f + broad_spur * .16f) * crest_relief;
        float connected_chain =
            std::max({raw_chain, backbone * .92f, massif_dome * .88f}) * (.52f + .48f * rough);
        float pass_cut = 1.f - std::max(valley_main, border_river * .75f) * (.52f * mountain);
        float chain = cfg.generator_version == 1
                          ? std::pow(ridge, cfg.ridge_power) * rough
                          : (connected_chain * .82f + mountain_body * .34f) * mountain_body * biome_mountains * pass_cut;

        // Broad foothills and high piedmont shoulders around mountain belts
        float foothill_zone = smooth(std::max(0.f, cfg.mountain_threshold - .24f), cfg.mountain_threshold, region_raw) *
                              (1.f - mountain) * inland_mask * (1.f - .75f * buildable_mask);
        float foothill = cfg.generator_version == 1 ? 0.f : foothill_zone * (.35f + .65f * broad_spur) * rough * .26f * shape;

        // Steep cliff escarpments and dramatic stepped rock faces on mountain flanks and plateau rims
        float fault_step = smooth(.42f, .58f, spur) * (.45f + .55f * rough) * mountain_body;

        // 5. Local Scale & Procedural Erosion:
        float gully_channel = std::pow(1.f - std::min(1.f, std::abs(d[i]) * 2.1f), 2.f);
        float flank_mask =
            std::clamp(4.f * mountain * (1.f - mountain) * (1.f - .65f * summit_crown) + plat_edge * .8f, 0.f, 1.f);
        float gully_erosion = cfg.generator_version == 1 ? 0.f : gully_channel * flank_mask * (.4f + .6f * rough);

        // Strongly flatten local noise in buildable zones and plateau tops; amplify rocky buttresses in adventure zones
        float top_flatness = (1.f - .72f * smooth(.75f, .95f, plateau_mask)) * (1.f - .82f * buildable_mask);
        float outcrop =
            std::max(0.f, spur - .64f) * 3.4f * (.4f + .6f * rough) * (.3f + .7f * mountain_body) * (1.f - buildable_mask);

        float continental_elev =
            (c[i] * (1.f - .65f * buildable_mask) + plateau_mask * 1.45f + mountain_body * .55f) *
            cfg.continental_amplitude * shape;
        // Depress ocean basins smoothly below sea level and sculpt gentle sandy coastal beaches at the ocean transition!
        float beach_zone = ocean_enabled ? smooth(.19f, .27f, cont_raw) * (1.f - smooth(.29f, .39f, cont_raw)) *
                                               (1.f - smooth(.25f, .55f, mountain))
                                         : 0.f;
        if (ocean_mask > 0.f) {
            float ocean_depth = (cfg.base_height - float(cfg.sea_level) + 16.f) * smooth(.0f, .85f, ocean_mask);
            continental_elev -= ocean_depth;
        }
        float mountain_elev = (chain + foothill + fault_step * .18f * cfg.terrace_strength) * cfg.mountain_amplitude;
        float valley_carve =
            (valley * (.35f + rough * .85f + mountain * .45f) + gully_erosion * .32f) * cfg.valley_depth * shape;
        float local_detail =
            (d[i] * (.14f + .86f * rough) * top_flatness + outcrop) * cfg.detail_amplitude * shape;

        float height = cfg.base_height + continental_elev + mountain_elev - valley_carve + local_detail;

        // Smoothly grade coastal beaches down to `sea_level + 1..3` so the transition from continent to ocean forms wide walkable beaches
        if (beach_zone > .05f && cfg.valley_depth > 1.f) {
            float beach_target = float(cfg.sea_level) + 1.2f + 3.2f * smooth(.22f, .36f, cont_raw);
            float beach_pull = smooth(.05f, .75f, beach_zone) * (1.f - .85f * river_channel);
            height = height * (1.f - beach_pull) + beach_target * beach_pull;
        }

        // 6. Multi-Tier Geological Terraces (Rice-Paddy / Canyon Escarpment Steps — Image 1 Reference):
        // On valley walls, plateau slopes, and foothill flanks, sculpt crisp stepped horizontal shelves
        // with grassy flat treads and steep stone risers.
        float terrace_zone =
            cfg.generator_version == 1
                ? rough
                : std::clamp((plat_edge * 1.25f + flank_mask * .95f + foothill_zone * rough * .85f) *
                                 (1.f - buildable_mask) * (1.f - ocean_mask) * (1.f - .75f * summit_crown),
                             0.f, 1.f);
        float step_h = std::max(4.f, cfg.terrace_height * .38f); // ~6.8-block vertical steps for dramatic layered benches
        float step_phase = height / step_h - std::floor(height / step_h);
        // Flat bench tread over [0.0..0.68], steep cliff riser over [0.68..1.0]
        float stepped_h = (std::floor(height / step_h) + smooth(.64f, .92f, step_phase)) * step_h;
        float terrace_blend = std::clamp(cfg.terrace_strength * 2.4f * terrace_zone, 0.f, .88f);
        height += (stepped_h - height) * terrace_blend;
        // Track whether this column sits on the steep riser wall (terrace_step > 0.55) vs flat grassy bench tread
        float terrace_riser = terrace_zone * smooth(.58f, .86f, step_phase) * (1.f - smooth(.92f, .99f, step_phase));
        float terrace_tread = terrace_zone * (1.f - smooth(.52f, .68f, step_phase));

        // 7. Stepped Climbable Riverbanks, Lakes, and Mountain Waterfall Streams:
        // Instead of sheer vertical abyssal holes down to the river, grade the outer river valley (`river_channel` in `0.06..0.58`)
        // into a walkable 1-to-2-block staircase of river terraces so the player can always climb out of rivers!
        float bank_step_mask = 0.f;
        if (river_channel > .05f && ocean_mask < .65f && cfg.valley_depth > 1.f && cfg.sea_level > cfg.min_y + 8) {
            float lake_extra_depth = lake_mask * 2.8f;
            float river_bed = float(cfg.sea_level) - 2.5f - 3.5f * smooth(.38f, .95f, river_channel) - lake_extra_depth;
            float river_pull = smooth(.06f, .76f, river_channel) * (1.f - .75f * mountain_body);
            float raw_river_h = height * (1.f - river_pull) + std::min(height, river_bed) * river_pull;
            // On the riverbank transition (`river_channel` between 0.06 and 0.56, above sea_level),
            // quantize elevation into 1.5-block climbable staircase ledges (`bank_step_mask`)
            if (river_channel < .58f && raw_river_h > float(cfg.sea_level)) {
                bank_step_mask = 4.f * (river_channel - .05f) * (.58f - river_channel) / (.53f * .53f);
                bank_step_mask = std::clamp(bank_step_mask, 0.f, 1.f);
                float ledge_h = 1.5f;
                float l_phase = raw_river_h / ledge_h - std::floor(raw_river_h / ledge_h);
                float stepped_bank = (std::floor(raw_river_h / ledge_h) + smooth(.55f, .88f, l_phase)) * ledge_h;
                raw_river_h = raw_river_h * (1.f - .78f * bank_step_mask) + stepped_bank * (.78f * bank_step_mask);
            }
            height = raw_river_h;
        }

        // 7b. Mountain Streams & Waterfalls:
        // High-altitude streams carve stepped cascade pools into mountain & plateau slopes and spill down as waterfalls!
        int water_top = cfg.sea_level;
        float waterfall_mask = 0.f;
        if (mountain_stream > .32f && height > float(cfg.sea_level + 3) && cfg.valley_depth > 1.f &&
            cfg.generator_version >= 2) {
            // Quantize the mountain stream bed into cascade pools separated by vertical waterfall drops (every 9 blocks)
            float pool_step = 9.f;
            float p_phase = height / pool_step - std::floor(height / pool_step);
            float cascade_h = (std::floor(height / pool_step) + smooth(.68f, .94f, p_phase)) * pool_step;
            float stream_carve = smooth(.32f, .78f, mountain_stream);
            height = height * (1.f - .75f * stream_carve) + cascade_h * (.75f * stream_carve) - 1.8f * stream_carve;
            int bed_y = int(std::floor(height));
            // Fill the stream channel and waterfall drop with elevated water voxels (`water_top > bed_y`)
            waterfall_mask = stream_carve * (.35f + .65f * smooth(.55f, .92f, p_phase));
            water_top = std::max(cfg.sea_level, bed_y + (waterfall_mask > .45f ? 2 : 1));
        }

        height = std::clamp(height, float(cfg.min_y + 8), float(cfg.max_y - 24));

        // 8. Natural Rock Arches (Image 2 Reference):
        // Arches form along narrow, high-relief rocky ridges and spurs in adventurous/mountainous zones.
        float arch_candidate =
            cfg.generator_version == 1
                ? 0.f
                : smooth(.58f, .82f, ridge) * smooth(.45f, .78f, spur) *
                      (1.f - smooth(0.f, .24f, std::abs(d[i] + .15f * wx[i]))) *
                      smooth(.25f, .65f, mountain + plateau_mask * .7f) * (1.f - river_channel) *
                      (1.f - mountain_stream) * (1.f - ocean_mask);
        if (water_top > cfg.sea_level)
            arch_candidate = 0.f; // never bury an elevated stream under an arch lintel
        if (arch_candidate > .15f) {
            // Boost ridge crest slightly so the arch lintel/bridge is thick and dramatic
            height = std::min(float(cfg.max_y - 20), height + arch_candidate * 11.f);
        }

        // Local slope & cliff intensity
        float canyon_wall = 4.f * std::max(valley_main, border_river) *
                            (1.f - std::max(valley_main, border_river)) * (.35f + .65f * (mountain + plateau_mask)) *
                            (1.f - .75f * bank_step_mask);
        float cliff_intensity =
            cfg.generator_version == 1
                ? rough
                : std::clamp(rough * mountain * biome_mountains + plat_edge * .75f + canyon_wall * .55f +
                                 terrace_riser * .65f,
                             0.f, 1.f);
        float slope = std::clamp((cliff_intensity * .62f + plat_edge * .72f + canyon_wall * .65f +
                                  chain * (.38f + .32f * spur) + gully_erosion * .35f + terrace_riser * .75f) *
                                     (1.f - .65f * terrace_tread) * (1.f - .75f * bank_step_mask) *
                                     (1.f - .80f * buildable_mask) * (1.f - .85f * beach_zone),
                                 0.f, 1.f);

        // 9. Biome Blend, Block-Mixing Dither at Biome Transitions & Coastal Beaches:
        // Near biome borders (where `primary_weight < 0.85`), compute a deterministic spatial hash per block `(x, z)`
        // so surface blocks and grass tints organically intermix/dither across the border ("alguns blocos misturando entre eles")!
        int transition_id = (mask.primary != mask.secondary && w < .85f) ? mask.secondary : -1;
        uint64_t dither_h = mix(uint64_t(uint32_t(int(std::floor(x[i])))) * 0x9e3779b185ebca87ULL ^
                                uint64_t(uint32_t(int(std::floor(z[i])))) * 0xc2b2ae3d27d4eb4fULL ^
                                uint64_t(uint32_t(seed + 701)));
        float dither_u = float(dither_h & 65535) / 65535.f;
        // Probability of picking the secondary biome's block/tint increases smoothly as `w` approaches 0.5 at the border
        float sec_prob = (transition_id >= 0) ? smooth(.50f, .84f, 1.f - w) * .65f : 0.f;
        bool use_secondary_block = (transition_id >= 0 && dither_u < sec_prob);
        const auto &active_biome = use_secondary_block ? secondary : primary;

        float beach_mask =
            std::clamp(beach_zone * (height >= float(cfg.sea_level - 2) && height <= float(cfg.sea_level + 5) ? 1.f : 0.f),
                       0.f, 1.f);

        BlockId surface = height > cfg.snow_height ? registry.snow
                                                   : (height < cfg.sea_level + 3
                                                          ? registry.sand
                                                          : (rough > .72f ? registry.stone : registry.grass));
        float temperature = primary.temperature * w + secondary.temperature * (1 - w) -
                            std::max(0.f, height - cfg.cold_start_altitude) * cfg.altitude_cooling;
        int biome = mask.primary;
        if (cfg.generator_version >= 2) {
            bool cold = (!primary.desert && !secondary.desert && temperature < cfg.snow_temperature) || primary.snow;
            // In the transition zone between Snowfields and a temperate biome, dither snow and grass blocks.
            // A dithered grass block is reported as the temperate biome so "snow biome => snow surface" holds.
            bool snow_grass_dither = cold && use_secondary_block && !secondary.snow && !secondary.desert &&
                                     w < .64f && height < cfg.cold_start_altitude && height > cfg.sea_level + 3;
            if (snow_grass_dither) {
                surface = registry.grass;
                biome = mask.secondary;
            } else if (cold) {
                surface = registry.snow;
                for (size_t b = 0; b < cfg.biomes.size(); b++)
                    if (cfg.biomes[b].snow) {
                        biome = int(b);
                        break;
                    }
            } else if (beach_mask > .18f || (height <= cfg.sea_level + 3 && ocean_mask > .15f)) {
                // Wide sandy coastal beaches connecting land to the ocean!
                surface = registry.sand;
            } else if (height <= cfg.sea_level + 1 && river_channel > .32f) {
                // Riverbeds and lake bottoms: sand in lakes/estuaries/deserts, fertile dirt/sand along riverbanks
                surface = (lake_mask > .35f || coastal_shelf > .25f || active_biome.desert) ? registry.sand : registry.dirt;
            } else if (active_biome.desert || height < cfg.sea_level + 2) {
                surface = (active_biome.desert && height >= cfg.sea_level + 6 &&
                           (plat_edge > .68f || terrace_riser > .55f || (mountain * rough > .72f && slope > .68f)))
                              ? registry.stone
                              : registry.sand;
            } else {
                // Terrace treads and stepped riverbanks get walkable grass/soil,
                // while terrace risers, cliffs, and arches expose bare rock (`stone`).
                bool rocky_exposure = (arch_candidate > .22f) || (terrace_riser > .52f) ||
                                      ((mountain * rough > .58f || slope > .64f ||
                                        (plat_edge > .56f && rough > .32f) || (canyon_wall > .52f && spur > .58f)) &&
                                       terrace_tread < .35f && bank_step_mask < .35f);
                bool eroded_soil = !rocky_exposure && terrace_tread < .35f &&
                                   (gully_erosion > .42f || (slope > .48f && er < .46f && mountain > .22f));
                surface = rocky_exposure ? registry.stone : (eroded_soil ? registry.dirt : registry.grass);
            }
        }
        int region_id = (ocean_mask > .45f && height <= cfg.sea_level) ? 5
                        : mountain > .5f                               ? 4
                        : plateau_mask > .55f                          ? 2
                        : (valley > .48f || river_channel > .35f)      ? 3
                        : (buildable_mask > .42f)                      ? 0
                        : (rough > .38f || mountain > .18f)            ? 1
                                                                       : 0;
        Column out;
        out.height = height;
        out.erosion = er;
        out.ridge = ridge;
        out.cliff = water_top > cfg.sea_level ? 0.f : cliff_intensity;
        out.surface = surface;
        out.biome = biome;
        out.temperature = temperature;
        out.mountain = mountain;
        out.plateau = plateau_mask;
        out.valley = valley;
        out.slope = slope;
        out.chain_axis = spine_axis;
        out.continentalness = cont_raw;
        out.river = std::max(river_channel, mountain_stream);
        out.buildable = buildable_mask;
        out.adventure = adventure_mask;
        out.terrace_step = terrace_zone;
        out.arch_mask = arch_candidate;
        out.lake_mask = lake_mask;
        out.beach_mask = beach_mask;
        out.river_bank_step = bank_step_mask;
        out.waterfall_mask = waterfall_mask;
        out.water_top = water_top;
        out.transition_biome = transition_id;
        out.region = region_id;
        // In the transition zone (`transition_id >= 0`), dither a subtle per-block shift toward the neighbor biome's
        // grass palette on top of the smooth gradient so individual grass blocks visually intermix across borders!
        float tint_w = use_secondary_block ? std::max(0.f, w - .22f) : w;
        out.grass_tint = {primary.grass.r * tint_w + secondary.grass.r * (1 - tint_w),
                          primary.grass.g * tint_w + secondary.grass.g * (1 - tint_w),
                          primary.grass.b * tint_w + secondary.grass.b * (1 - tint_w)};
        out.preserve = primary.preserve_saturation * w + secondary.preserve_saturation * (1 - w);
        out.brightness = primary.brightness * w + secondary.brightness * (1 - w);
        out.pebbles = primary.pebbles * w + secondary.pebbles * (1 - w);
        result[i] = out;
    }
    if (timings)
        timings->surface_ms +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - after_noise).count();
    return result;
}
Column Generator::column(int x, int z) const {
    return columns({float(x)}, {float(z)})[0];
}
bool Generator::natural_arch_carved(const Column &c, int x, int y, int z) const {
    if (cfg.generator_version < 2 || c.arch_mask < .18f || y <= cfg.sea_level + 6)
        return false;
    // Carve a dramatic rounded window/archway through the rocky ridge while preserving a thick stone bridge on top
    float span_phase = std::sin(float(x) * .045f + float(z) * .038f + float(seed & 1023) * .01f);
    float span_open = std::clamp((span_phase - .15f) / .75f, 0.f, 1.f);
    if (span_open <= 0.f)
        return false;
    float arch_Span_Strength = c.arch_mask * span_open;
    float lintel_thickness = 4.5f + 3.5f * c.ridge;
    float opening_half_h = 5.5f + 7.5f * arch_Span_Strength;
    float center_y = c.height - lintel_thickness - opening_half_h * .85f;
    float dy = (float(y) - center_y) / std::max(1.f, opening_half_h);
    // Dome-shaped upper arch profile (elliptical top, wider base)
    float radial = dy * dy + (1.f - span_open) * .65f;
    return radial < .88f && float(y) < c.height - lintel_thickness && float(y) > center_y - opening_half_h * .9f;
}
void Generator::density_samples(std::vector<float> &out, const std::vector<float> &x,
                                const std::vector<float> &y, const std::vector<float> &z) const {
    out.resize(x.size());
    if (!out.empty())
        density->GenPositionArray3D(out.data(), int(out.size()), x.data(), y.data(), z.data(), 0, 0, 0,
                                    offset_seed(seed, 113));
}
BlockId Generator::terrain_sample(int x, int y, int z) const {
    if (y < cfg.min_y || y >= cfg.max_y)
        return registry.air;
    if (y == cfg.min_y)
        return registry.bedrock;
    auto c = column(x, z);
    int w_top = std::max(cfg.sea_level, c.water_top);
    if (natural_arch_carved(c, x, y, z))
        return y <= w_top ? registry.water : registry.air;
    float offset = density->GenSingle3D(float(x), float(y), float(z), offset_seed(seed, 113)) *
                   cfg.cliff_strength * c.cliff;
    if (y <= c.height + offset) {
        if (natural_arch_carved(c, x, y + 1, z))
            return registry.stone;
        float above = density->GenSingle3D(float(x), float(y + 1), float(z), offset_seed(seed, 113)) *
                      cfg.cliff_strength * c.cliff;
        return material(c, y, float(y + 1) > c.height + above);
    }
    return y <= w_top ? registry.water : registry.air;
}
BlockId Generator::sample(int x, int y, int z) const {
    auto terrain = terrain_sample(x, y, z);
    if (terrain != registry.air || !cfg.vegetation)
        return terrain;
    auto parts = trees(x, z, x + 1, z + 1);
    for (auto it = parts.rbegin(); it != parts.rend(); ++it)
        if (it->first.y == y)
            return it->second;
    return terrain;
}
BlockId Generator::material(const Column &c, int y, bool exposed) const {
    if (cfg.generator_version == 1)
        return y > c.height - 3 ? c.surface : (y > c.height - 6 ? registry.dirt : registry.stone);
    if (c.arch_mask > .22f)
        return registry.stone;
    if (c.surface == registry.stone)
        return registry.stone;
    if (!exposed && (c.slope > .58f || c.cliff > .52f))
        return registry.stone;
    if (c.surface == registry.sand) {
        int sand_depth = c.valley > .35f ? 9 : (c.slope > .38f ? 4 : 7);
        return y > c.height - sand_depth ? registry.sand : registry.stone;
    }
    if (exposed)
        return c.surface;
    int dirt_depth = c.valley > .40f    ? 7
                     : c.plateau > .60f ? 4
                     : (c.mountain > .45f || c.slope > .42f) ? 2
                                                             : 5;
    return y > c.height - dirt_depth ? registry.dirt : registry.stone;
}
void WorldModel::restore(EditMap data) {
    edits = std::make_shared<EditMap>(std::move(data));
    edit_regions.clear();
    for (const auto &entry : *edits) {
        const auto &p = entry.first;
        edit_regions[{int(std::floor(double(p.x) / 32)), 0, int(std::floor(double(p.z) / 32))}].push_back(p);
    }
    revision = edits->size();
    sequences.clear();
}
bool WorldModel::validate(const EditCommand &cmd, std::string &error) const {
    const auto &s = generator->settings();
    auto it = sequences.find(cmd.actor);
    if (!cmd.actor || !cmd.sequence || (it != sequences.end() && cmd.sequence <= it->second)) {
        error = "Stale or invalid edit sequence";
        return false;
    }
    if (!generator->blocks().valid(cmd.block)) {
        error = "Unknown block id";
        return false;
    }
    if (cmd.position.y <= s.min_y || cmd.position.y >= s.max_y ||
        std::abs(int64_t(cmd.position.x)) > 1000000 || std::abs(int64_t(cmd.position.z)) > 1000000) {
        error = "Edit outside world bounds";
        return false;
    }
    return true;
}
bool WorldModel::apply(const EditCommand &cmd, std::string &error) {
    if (!validate(cmd, error))
        return false;
    // Meshing jobs receive region-sized immutable snapshots, so local edits normally
    // update in O(1), without copying every modification in the world.
    if (!edits.unique())
        edits = std::make_shared<EditMap>(*edits);
    if (!edits->count(cmd.position)) {
        const auto &p = cmd.position;
        edit_regions[{int(std::floor(double(p.x) / 32)), 0, int(std::floor(double(p.z) / 32))}].push_back(p);
    }
    (*edits)[cmd.position] = cmd.block;
    sequences[cmd.actor] = cmd.sequence;
    ++revision;
    return true;
}
std::shared_ptr<const EditMap> WorldModel::snapshot_for(const TileKey &tile) const {
    auto result = std::make_shared<EditMap>();
    int pad = 1 << tile.level;
    int min_x = tile.x - pad, max_x = tile.x + tile.size() + pad;
    int min_z = tile.z - pad, max_z = tile.z + tile.size() + pad;
    for (int z = int(std::floor(double(min_z) / 32)); z <= int(std::floor(double(max_z) / 32)); ++z)
        for (int x = int(std::floor(double(min_x) / 32)); x <= int(std::floor(double(max_x) / 32)); ++x) {
            auto region = edit_regions.find({x, 0, z});
            if (region == edit_regions.end())
                continue;
            for (const auto &p : region->second)
                if (p.x >= min_x && p.x < max_x && p.z >= min_z && p.z < max_z)
                    (*result)[p] = edits->at(p);
        }
    return result;
}
BlockId WorldModel::block_at(int x, int y, int z) const {
    auto it = edits->find({x, y, z});
    return it == edits->end() ? generator->sample(x, y, z) : it->second;
}
static uint64_t checksum(const EditCommand &c) {
    return mix(c.actor) ^ mix(c.sequence) ^ mix(uint32_t(c.position.x)) ^
           mix(uint64_t(uint32_t(c.position.y)) + 17) ^ mix(uint64_t(uint32_t(c.position.z)) + 37) ^
           mix(uint64_t(c.block) + 71);
}
bool EditJournal::append(const EditCommand &c, std::string &error) const {
    std::FILE *file = std::fopen(filename.c_str(), "ab");
    if (!file) {
        error = "Could not open edit journal";
        return false;
    }
    int written = std::fprintf(file, "%llu %llu %d %d %d %u %llu\n", static_cast<unsigned long long>(c.actor),
                               static_cast<unsigned long long>(c.sequence), c.position.x, c.position.y,
                               c.position.z, unsigned(c.block), static_cast<unsigned long long>(checksum(c)));
    bool ok = written > 0 && std::fflush(file) == 0;
    if (std::fclose(file) != 0)
        ok = false;
    if (!ok)
        error = "Could not persist edit";
    return ok;
}
bool EditJournal::load(EditMap &out, const Registry &r, int min_y, int max_y, std::string &error) const {
    std::FILE *file = std::fopen(filename.c_str(), "rb");
    if (!file) {
        if (errno == ENOENT)
            return true;
        error = "Cannot read edit journal";
        return false;
    }
    char line[512];
    uintmax_t valid_bytes = 0;
    while (std::fgets(line, sizeof(line), file)) {
        size_t length = std::strlen(line);
        if (!length || line[length - 1] != '\n') {
            bool interrupted = std::feof(file);
            std::fclose(file);
            if (!interrupted) {
                error = "Oversized edit journal record";
                return false;
            }
            std::error_code ec;
            std::filesystem::resize_file(filename, valid_bytes, ec);
            if (ec) {
                error = "Could not recover interrupted journal append";
                return false;
            }
            error = "Recovered interrupted final journal record";
            return true;
        }
        valid_bytes += length;
        EditCommand c;
        unsigned long long actor = 0, sequence = 0, sum = 0;
        unsigned block = 0;
        char extra = 0;
        int fields = std::sscanf(line, "%llu %llu %d %d %d %u %llu %c", &actor, &sequence, &c.position.x,
                                 &c.position.y, &c.position.z, &block, &sum, &extra);
        c.actor = actor;
        c.sequence = sequence;
        c.block = BlockId(block);
        if (fields != 7 || !actor || !sequence || block > 4095 || checksum(c) != sum || !r.valid(c.block) ||
            c.position.y <= min_y || c.position.y >= max_y || std::abs(int64_t(c.position.x)) > 1000000 ||
            std::abs(int64_t(c.position.z)) > 1000000) {
            std::fclose(file);
            error = "Invalid edit journal record";
            return false;
        }
        out[c.position] = c.block;
    }
    bool ok = !std::ferror(file);
    std::fclose(file);
    if (!ok)
        error = "Could not read edit journal";
    return ok;
}
RayHit raycast(const std::array<float, 3> &o, const std::array<float, 3> &d, float reach,
               const std::function<BlockId(int, int, int)> &q, const Registry &r) {
    int p[3] = {int(std::floor(o[0])), int(std::floor(o[1])), int(std::floor(o[2]))};
    int step[3];
    float delta[3], next[3];
    for (int a = 0; a < 3; a++) {
        step[a] = d[a] >= 0 ? 1 : -1;
        delta[a] = std::abs(d[a]) < 1e-8f ? std::numeric_limits<float>::infinity() : std::abs(1 / d[a]);
        next[a] =
            std::abs(d[a]) < 1e-8f ? delta[a] : ((step[a] > 0 ? p[a] + 1 - o[a] : o[a] - p[a]) * delta[a]);
    }
    IVec3 previous{p[0], p[1], p[2]};
    float distance = 0;
    for (int n = 0; n < 256 && distance <= reach; n++) {
        BlockId id = q(p[0], p[1], p[2]);
        if (r.get(id).solid)
            return {true, {p[0], p[1], p[2]}, previous, distance, id};
        previous = {p[0], p[1], p[2]};
        int a = next[0] < next[1] ? (next[0] < next[2] ? 0 : 2) : (next[1] < next[2] ? 1 : 2);
        distance = next[a];
        next[a] += delta[a];
        p[a] += step[a];
    }
    return {};
}
float tile_distance(const TileKey &t, float x, float z) {
    float dx = std::max({float(t.x) - x, 0.f, x - float(t.x + t.size())}),
          dz = std::max({float(t.z) - z, 0.f, z - float(t.z + t.size())});
    return std::sqrt(dx * dx + dz * dz);
}
bool overlaps(const TileKey &a, const TileKey &b) {
    return a.x < b.x + b.size() && a.x + a.size() > b.x && a.z < b.z + b.size() && a.z + a.size() > b.z;
}
std::vector<TileKey> select_tiles(float x, float z, const Settings &s) {
    std::vector<TileKey> result;
    float splits[] = {
        0, s.near_distance, s.middle_distance, s.coarse_distance, s.distant_distance, s.horizon_distance};
    int max_level = s.generator_version >= 2 ? 5 : 3, root_size = 32 << max_level;
    std::function<void(TileKey)> visit = [&](TileKey t) {
        float d = tile_distance(t, x, z);
        if (d > s.view_distance)
            return;
        if (t.level > 0 && d < splits[t.level]) {
            int half = t.size() / 2;
            for (int dz = 0; dz < 2; dz++)
                for (int dx = 0; dx < 2; dx++)
                    visit({t.x + dx * half, t.z + dz * half, t.level - 1});
        } else
            result.push_back(t);
    };
    int radius = int(std::ceil(s.view_distance / root_size)) + 1, cx = int(std::floor(x / root_size)),
        cz = int(std::floor(z / root_size));
    for (int dz = -radius; dz <= radius; dz++)
        for (int dx = -radius; dx <= radius; dx++)
            visit({(cx + dx) * root_size, (cz + dz) * root_size, max_level});
    std::sort(result.begin(), result.end(), [&](const auto &a, const auto &b) {
        float da = tile_distance(a, x, z), db = tile_distance(b, x, z);
        return da == db ? (a.level < b.level) : da < db;
    });
    return result;
}
BlockId Volume::at(int x, int y, int z) const {
    if (x < -1 || x > 32 || z < -1 || z > 32 || y < -1 || y > height)
        return 0;
    return data[((z + 1) * (height + 2) + (y + 1)) * 34 + (x + 1)];
}
TerrainWorkers::TerrainWorkers(std::shared_ptr<const Generator> g, int count) : generator(std::move(g)) {
    for (int i = 0; i < count; i++)
        threads.emplace_back([this] {
            for (;;) {
                Job job;
                {
                    std::unique_lock<std::mutex> l(mutex);
                    wake.wait(l, [this] { return stopping || !jobs.empty(); });
                    if (stopping)
                        return;
                    job = std::move(jobs.front());
                    jobs.pop_front();
                }
                auto result =
                    job.vegetation_volume
                        ? build_vegetation(job.key, job.version, *generator, *job.vegetation_volume)
                        : build_tile(job.key, job.version, *generator, job.edits, job.surfaces,
                                     job.include_vegetation);
                {
                    std::unique_lock<std::mutex> l(mutex);
                    wake.wait(l, [this] { return stopping || completed.size() < 12; });
                    if (stopping)
                        return;
                    completed.push_back(std::move(result));
                }
            }
        });
}
TerrainWorkers::~TerrainWorkers() {
    {
        std::lock_guard<std::mutex> l(mutex);
        stopping = true;
    }
    wake.notify_all();
    for (auto &t : threads)
        t.join();
}
void TerrainWorkers::submit(TileKey k, uint64_t v, std::shared_ptr<const EditMap> e,
                            std::shared_ptr<const AppearanceMap> surfaces, bool include_vegetation) {
    {
        std::lock_guard<std::mutex> l(mutex);
        jobs.push_back({k, v, std::move(e), std::move(surfaces), nullptr, include_vegetation});
    }
    wake.notify_one();
}
void TerrainWorkers::submit_vegetation(TileKey k, uint64_t version, std::shared_ptr<const Volume> volume) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        Job job;
        job.key = k;
        job.version = version;
        job.vegetation_volume = std::move(volume);
        jobs.push_back(std::move(job));
    }
    wake.notify_one();
}
bool TerrainWorkers::pop(MeshResult &r) {
    {
        std::lock_guard<std::mutex> l(mutex);
        if (completed.empty())
            return false;
        r = std::move(completed.front());
        completed.pop_front();
    }
    wake.notify_all();
    return true;
}
size_t TerrainWorkers::queued() {
    std::lock_guard<std::mutex> l(mutex);
    return jobs.size();
}
} // namespace altaria
