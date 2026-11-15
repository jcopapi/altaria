#include <climits>
#include "voxel_core.h"
#include "block_shapes.h"
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
RGB grass_on_support(RGB grass, RGB support) {
    return {grass.r * .75f + support.r * .25f, grass.g * .75f + support.g * .25f,
            grass.b * .75f + support.b * .25f};
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
    if (def.grass_on_rock && def.solid) {
        if (def.key == "altaria:cave_grass")
            cave_grass = def.id;
        else
            rock_grass = def.id;
    }
    blocks[def.id] = std::move(def);
    return true;
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
    if (cfg.preset != WorldPreset::Default) {
        cfg.sea_level = cfg.min_y;
        cfg.vegetation = false;
        cfg.caves = {};
        if (cfg.preset == WorldPreset::Plain) {
            cfg.view_distance = std::min(cfg.view_distance, 896.f);
            cfg.near_distance = 40;
            cfg.middle_distance = 80;
            cfg.coarse_distance = 160;
            cfg.distant_distance = 320;
            cfg.horizon_distance = 640;
        }
        return;
    }
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
    if(s.generator_version>=3)drainage=fbm(s.river_scale,2);
    if (s.generator_version >= 3 && s.smooth_lowland_relief) {
        auto meadow = FastNoise::New<FastNoise::Simplex>();
        // Broad enough that meadow hills rise ~1 block per 2: block, slab, block stairs.
        meadow->SetScale(std::max(160.f, s.mountain_scale * .40f));
        meadow_relief = meadow;
    }
    if (s.generator_version >= 3) {
        // Drifts of snow on the grass side of a cold border and grass on the
        // snowy side: a few blocks to a dozen across.
        auto drifts = FastNoise::New<FastNoise::Simplex>();
        drifts->SetScale(15.f);
        snow_patches = drifts;
    }
    if (s.generator_version >= 3 && s.deeper_waterbeds) {
        auto seabed = FastNoise::New<FastNoise::Simplex>();
        seabed->SetScale(std::max(256.f, s.continental_scale * .24f));
        waterbed_relief = seabed;
        if (s.mixed_sediments) {
            auto patches = FastNoise::New<FastNoise::Simplex>();
            patches->SetScale(9.f);
            sediment_patches = patches;
        }
    }
    auto find_or = [this](const std::string &key, BlockId fallback) {
        BlockId found = registry.find(key);
        return found != 0 ? found : fallback;
    };
    id_basalt = find_or("altaria:basalt", registry.stone);
    id_limestone = find_or("altaria:limestone", registry.stone);
    id_sandstone = find_or("altaria:sandstone", registry.stone);
    id_moss_stone = find_or("altaria:moss_stone", registry.stone);
    id_crystal_stone = find_or("altaria:crystal_stone", registry.stone);
    id_fossil_limestone = find_or("altaria:fossil_limestone", registry.stone);
    id_wet_stone = find_or("altaria:wet_stone", registry.stone);
    id_hellstone = find_or("altaria:hellstone", registry.stone);
    id_gravel = find_or("altaria:gravel",registry.stone);
    id_clay = find_or("altaria:clay",id_gravel);
    if (cfg.caves.enabled) {
        auto cave_cfg=cfg.caves;
        cave_cfg.plan.floor=cfg.min_y;
        cave_cfg.plan.ceiling=cfg.max_y;
        if(!caves::validate(cave_cfg,generation_error))return;
        if(cave_cfg.hydrology.basin_regions && cave_cfg.hydrology.lava_chance>0 &&
           (registry.lava==registry.air || !registry.valid(registry.lava) || !registry.get(registry.lava).lava ||
            registry.get(registry.lava).solid)) {
            generation_error="Underground lava requires a registered non-solid lava block";return;
        }
        auto catalogue=cfg.caves.biome_variation || !cfg.caves.exploration_formations.empty()
                         ?cfg.caves.biomes:caves::BiomeSettings{};
        if(!cfg.caves.biome_variation && !cfg.caves.exploration_formations.empty()) {
            const auto &selected=cfg.caves.exploration_formations;
            auto unselected=[&](const std::string &id){return std::find(selected.begin(),selected.end(),id)==selected.end();};
            auto &rules=catalogue.formations;
            rules.erase(std::remove_if(rules.begin(),rules.end(),[&](const auto &rule){return unselected(rule.id);}),rules.end());
            for(auto &biome:catalogue.definitions) {
                biome.rock="$stone";
                auto &forms=biome.formations;
                forms.erase(std::remove_if(forms.begin(),forms.end(),unselected),forms.end());
            }
            catalogue.frozen_rock=catalogue.desert_rock=catalogue.rooted_rock="$stone";
        }
        cave_biomes=std::make_unique<caves::BiomeStage>(std::move(catalogue),
            cfg.caves.depth,cfg.caves.plan.depths,uint32_t(seed),cfg.caves.dimension,
            [this](const std::string &key,uint16_t &id) {
                if(key=="$stone")id=registry.stone;
                else {auto found=registry.keys.find(key);if(found==registry.keys.end())return false;id=found->second;}
                if(!registry.valid(id) || id==registry.air || id==registry.bedrock)return false;
                const auto &b=registry.get(id);return b.solid && !b.water && !b.liquid && !b.wood && !b.foliage;
            },
            [this](const std::string &key,uint16_t &id) {
                id = registry.find(key);
                return registry.get(id).root_plant;
            });
        if(!cave_biomes->valid(generation_error))return;
        cave_runtime=std::make_unique<caves::Runtime>(cave_cfg,seed,cfg.cliff_strength,
            [this](const std::vector<float> &x,const std::vector<float> &z){return columns(x,z,nullptr,false);},
            cfg.sea_level,[this](const caves::Passage &opening){return cave_outlet(opening);},
            [this](const caves::Network &network){return cave_formations(network);},
            [this](const Column &c,int x,int y,int z){return y==cfg.min_y || (density_solid(c,x,y,z) && !natural_arch_carved(c,x,y,z));});
        if(cfg.caves.noise.enabled) {
            if(!caves::valid_noise(cfg.caves.noise,cfg.min_y,generation_error))return;
            if(registry.lava==registry.air || !registry.valid(registry.lava) || !registry.get(registry.lava).lava) {
                generation_error="Underground lava seas require a registered lava block";return;
            }
            cave_noise=std::make_unique<caves::NoiseField>(cfg.caves.noise,offset_seed(seed,4099),cfg.min_y);
        }
    }
}
struct ReliefNoise {
    float continental, erosion, ridge, spur, spine, plateau, detail, drainage, range, meadow, waterbed;
};
static float alpine_ledge_height(float height, float sea) {
    // Start each band on a ledge of the previous band so the tiers join.
    const float first = sea + 75.f, middle = first + 36.f, upper = middle + 40.f;
    const float base = height >= upper ? upper : height >= middle ? middle : first;
    const float rise = height >= upper ? 5.f : height >= middle ? 4.f : 3.f;
    return base + std::floor((height - base) / rise) * rise;
}
// Sparse bell-shaped hills. Each grid cell may hold one mound with its own
// height, width, elongation and lobed outline; overlapping mounds keep the
// higher surface so each one still reads as a separate hill.
static float mound_height(const Settings &s, int seed, float x, float z) {
    if (s.hill_chance <= 0.f)
        return 0.f;
    const float spacing = s.hill_spacing;
    const int gx = int(std::floor(x / spacing)), gz = int(std::floor(z / spacing));
    float best = 0.f;
    for (int cz = gz - 1; cz <= gz + 1; ++cz)
        for (int cx = gx - 1; cx <= gx + 1; ++cx) {
            const uint64_t h = mix(uint64_t(uint32_t(cx)) * 0x9e3779b185ebca87ULL ^
                                   uint64_t(uint32_t(cz)) * 0xc2b2ae3d27d4eb4fULL ^
                                   uint64_t(uint32_t(offset_seed(seed, 1543))));
            const auto unit = [&](int shift) { return float((h >> shift) & 1023) / 1023.f; };
            if (unit(0) > s.hill_chance)
                continue;
            // Favour smaller hills; tall ones stay landmarks.
            const float size = unit(10) * unit(10);
            const float height = s.hill_height_min + (s.hill_height_max - s.hill_height_min) * size;
            const float radius = height * (s.hill_width_min + (s.hill_width_max - s.hill_width_min) * unit(20)) + 3.f;
            const float margin = std::min(radius, spacing * .5f - 1.f);
            const float ox = (cx + .5f) * spacing + (unit(30) * 2.f - 1.f) * (spacing * .5f - margin);
            const float oz = (cz + .5f) * spacing + (unit(40) * 2.f - 1.f) * (spacing * .5f - margin);
            const float angle = unit(50) * 6.2831853f, stretch = 1.f + .45f * unit(54);
            const float dx = x - ox, dz = z - oz;
            const float u = (dx * std::cos(angle) + dz * std::sin(angle)) / stretch;
            const float v = -dx * std::sin(angle) + dz * std::cos(angle);
            const float theta = std::atan2(v, u);
            const float lobes = 1.f + .14f * std::sin(2.f * theta + unit(4) * 6.28f) +
                                .08f * std::sin(3.f * theta + unit(14) * 6.28f);
            const float t = std::sqrt(u * u + v * v) / (radius * lobes);
            if (t >= 1.f)
                continue;
            const float bell = (1.f - t * t) * (1.f - t * t) * (1.f - t * t);
            best = std::max(best, height * bell);
        }
    return best;
}
static Column regional_relief(const Settings &s,const ReliefNoise &n,float terrain,float biome_mountains,
                              float mound) {
    Column out;
    const float sea=float(s.sea_level);
    const float land=std::clamp(.52f+.62f*n.continental,0.f,1.f);
    const bool oceans=s.continental_amplitude>1.f && s.sea_level>s.min_y+12;
    const float inland=oceans?smooth(.20f,.40f,land):1.f;
    const float erosion=std::clamp(n.erosion*.65f+.5f,0.f,1.f);
    const float rough=1.f-smooth(.18f,s.plains_threshold,erosion);
    const float ridge=std::clamp(.5f+.5f*n.ridge,0.f,1.f);
    const float spur=std::clamp(.5f+.5f*n.spur,0.f,1.f);
    const float axis=1.f-smooth(.06f,.52f,std::abs(n.spine));
    const float range=.5f+.5f*n.range;
    const float foothills=smooth(s.mountain_threshold-.29f,s.mountain_threshold+.05f,range)*inland;
    const float cordillera=smooth(s.mountain_threshold,s.mountain_threshold+s.mountain_blend,
                                   range+axis*.08f)*inland;
    const float mountain=smooth(.04f,.93f,cordillera)*biome_mountains;
    const float summit=std::pow(smooth(.13f,.90f,ridge),s.ridge_power);
    const float rugged=s.smooth_lowland_relief?smooth(.08f,.36f,mountain):1.f;
    // Low hills use a broad, non-ridged field. Only mountain relief needs
    // the sharper ridges that can rise by several blocks at a time.
    const float meadow_spur=std::clamp(.5f+.5f*n.meadow,0.f,1.f);
    const float rolling_spur=spur*rugged+meadow_spur*(1.f-rugged);
    // A change of biome shouldn't create a ledge in a gentle hillside.
    const float rolling_terrain=terrain*rugged+(1.f-rugged);
    const float rolling=rolling_terrain*(foothills*(6.f+13.f*rolling_spur)+rough*(2.f+3.f*n.plateau));
    const float mountain_height=s.mountain_amplitude*mountain*(.34f+.43f*summit+.23f*axis);
    const float meadow=(1.f-smooth(.12f,.48f,foothills+mountain))*(oceans?smooth(.40f,.54f,land):1.f);
    const float hills=s.meadow_hill_amplitude*meadow*smooth(.22f,.84f,rolling_spur);
    const float shelf=oceans?smooth(.22f,.43f,land):1.f;
    const float shelf_depth=std::min(56.f,s.ocean_depth);
    const float ocean_depth=s.deeper_waterbeds
        ?shelf_depth+(s.ocean_depth-shelf_depth)*(1.f-smooth(.12f,.32f,land))
            +.067f*s.ocean_depth*n.waterbed*(1.f-smooth(.14f,.29f,land))
        :s.ocean_depth;
    const float basin=oceans?ocean_depth*(1.f-smooth(.07f,.40f,land)):0.f;
    const float detail_weight=.13f+.57f*rough+.30f*cordillera;
    const float local=s.detail_amplitude*n.detail*detail_weight*(.25f+.75f*rugged)*rolling_terrain;
    // Hills rise from lowland and foothill ground, never from the sea or high ranges.
    const float hill_ground=(oceans?smooth(.40f,.52f,land):1.f)*(1.f-smooth(.18f,.55f,mountain));
    const float hill=mound*hill_ground*std::clamp(terrain,.3f,1.2f);
    float height=sea+(s.base_height-sea)*shelf+s.continental_amplitude*shelf*(.34f+.53f*land)
                 -basin+rolling+mountain_height+local*(1.f-.7f*smooth(1.f,4.f,hill))+hills+hill;

    // Low-gradient drainage. The floodplain absorbs most of the drop over
    // a wide corridor; the narrower channel only cuts the last few metres.
    const float distance=std::abs(n.drainage);
    const float lowland=1.f-smooth(.10f,.40f,cordillera);
    const float estuary=oceans?smooth(.21f,.35f,land):1.f;
    // Spread the descent across the valley shoulders. Leave the narrow
    // channel as a separate cut so the water can still have defined banks.
    const float valley=(1.f-smooth(0.f,s.valley_width,distance))*lowland*estuary;
    const float floodplain=s.smooth_lowland_relief
        ?(1.f-smooth(0.f,s.valley_width*2.6f,distance))*lowland*estuary:valley;
    const float bank_height=sea+1.65f;
    const float floodplain_cut=std::clamp(height-bank_height,0.f,std::max(s.valley_depth,0.f));
    if(s.valley_depth>1.f)height-=floodplain*floodplain_cut;
    const float channel=(1.f-smooth(s.river_width*.24f,s.river_width,distance))*lowland*estuary;
    const float channel_cut=std::clamp(height-(sea-2.8f),0.f,5.5f);
    if(s.valley_depth>1.f)height-=channel*channel_cut;
    if (s.deeper_waterbeds && s.valley_depth > 1.f) {
        const float pool = smooth(-.40f,.65f,n.waterbed + .65f*n.detail);
        const float submerged = 1.f-smooth(sea-1.f,sea,height);
        height -= (2.f+16.f*pool)*smooth(.42f,.96f,channel)*submerged;
    }

    float lowest = float(s.min_y+8);
    if (s.deeper_waterbeds && oceans && height < sea) {
        // Trenches develop only in the open ocean, below the continental shelf.
        const float abyss = (1.f-smooth(.14f,.24f,land))*smooth(.46f,.75f,n.waterbed);
        if (abyss > 0.f) {
            lowest = float(s.min_y+1);
            height = std::max(height,float(s.min_y+8))*(1.f-abyss)+lowest*abyss;
        }
    }
    const float ledges = s.mountain_ledges && height >= sea+75.f ? smooth(.15f,.34f,mountain) : 0.f;
    if (ledges > 0.f)
        height += (alpine_ledge_height(height,sea)-height)*ledges;

    const float escarpment=smooth(.30f,.80f,mountain)*smooth(.43f,.84f,spur);
    const float arch=smooth(.72f,.94f,ridge)*smooth(.66f,.90f,spur)*mountain
                     *(1.f-smooth(0.f,.16f,std::abs(n.detail)));
    const float coast_relief=smooth(.18f,.58f,foothills+mountain);
    const float beach=(1.f-coast_relief)*smooth(.20f,.32f,land)*(1.f-smooth(.43f,.51f,land));
    out.height=std::clamp(height,lowest,float(s.max_y-24));
    out.erosion=erosion;
    out.ridge=ridge;
    out.mountain=std::clamp(mountain,0.f,1.f);
    out.chain_axis=axis;
    out.continentalness=land;
    out.valley=s.valley_depth>1.f?valley:0.f;
    out.river=s.valley_depth>1.f && height<sea+1.f && land>.29f?channel:0.f;
    out.cliff=escarpment*(.25f+.45f*rough)*(1.f-ledges);
    out.slope=std::clamp(.08f*rough+.30f*foothills+.64f*mountain+.30f*escarpment,0.f,1.f);
    out.buildable=(1.f-smooth(.12f,.40f,foothills+mountain))*(1.f-.6f*out.valley)*inland;
    out.adventure=std::max(out.mountain,escarpment);
    out.arch_mask=arch;
    out.water_top=s.sea_level;
    const bool ocean=oceans && height<=sea && land<.35f;
    out.beach_mask=oceans && s.beach_height>0 && height>=sea-.6f && height<=sea+s.beach_height
                   ?beach:0.f;
    out.region=ocean?5:out.mountain>.45f?4:out.valley>.48f?3:foothills>.22f?1:0;
    return out;
}
std::vector<Column> Generator::columns(const std::vector<float> &x, const std::vector<float> &z,
                                       StageTimings *timings,bool include_underground) const {
    if (cfg.preset != WorldPreset::Default) {
        const bool plain = cfg.preset == WorldPreset::Plain;
        const auto &biome = cfg.biomes.front();
        Column column;
        column.height = plain ? std::clamp(64, cfg.min_y + 8, cfg.max_y - 24) : cfg.min_y - 2;
        column.surface = plain ? registry.grass : registry.air;
        column.water_top = cfg.min_y;
        column.temperature = biome.temperature;
        column.day_c = biome.day_c;
        column.night_c = biome.night_c;
        column.grass_tint = biome.grass;
        column.preserve = biome.preserve_saturation;
        column.brightness = biome.brightness;
        return std::vector<Column>(x.size(), column);
    }
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
    std::vector<float> meadow;
    if (meadow_relief) {
        meadow.resize(n);
        meadow_relief->GenPositionArray2D(meadow.data(),n,x.data(),z.data(),0,0,offset_seed(seed,1091));
    }
    std::vector<float> waterbed;
    if (waterbed_relief) {
        waterbed.resize(n);
        waterbed_relief->GenPositionArray2D(waterbed.data(),n,x.data(),z.data(),0,0,offset_seed(seed,1237));
    }
    std::vector<float> drifts;
    if (snow_patches) {
        drifts.resize(n);
        snow_patches->GenPositionArray2D(drifts.data(),n,x.data(),z.data(),0,0,offset_seed(seed,1489));
    }
    std::vector<float> patches;
    if (sediment_patches) {
        patches.resize(n);
        sediment_patches->GenPositionArray2D(patches.data(),n,x.data(),z.data(),0,0,offset_seed(seed,1361));
    }
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
    (drainage?drainage:erosion)->GenPositionArray2D(river.data(), n, px.data(), pz.data(), 721, 391,
                                                   offset_seed(seed, 713));
    mountain_regions->GenPositionArray2D(regions.data(), n, px.data(), pz.data(), 0, 0,
                                         offset_seed(seed, 947));
    auto after_noise = std::chrono::steady_clock::now();
    if (timings)
        timings->terrain_ms += std::chrono::duration<double, std::milli>(after_noise - after_biomes).count();
    auto apply_climate=[&](Column &out,const BiomeSample &mask,bool secondary_block) {
        const auto &a=cfg.biomes[mask.primary];
        const auto &b=cfg.biomes[mask.secondary];
        const float w=mask.primary_weight;
        auto &climate=out.underground.surface;
        climate.heat=out.temperature;
        climate.humidity=a.humidity*w+b.humidity*(1-w);
        climate.frozen=std::max((a.snow?w:0)+(b.snow?1-w:0),
                                 1-smooth(cfg.snow_temperature,cfg.snow_temperature+.12f,out.temperature));
        climate.desert=(a.desert?w:0)+(b.desert?1-w:0);
        climate.volcanic=(a.volcanic?w:0)+(b.volcanic?1-w:0);
        climate.lush=(1-climate.desert)*(1-climate.frozen)*(.35f+.65f*smooth(.5f,.85f,climate.humidity));
        out.day_c=a.day_c*w+b.day_c*(1-w);
        out.night_c=a.night_c*w+b.night_c*(1-w);
        if(out.surface==registry.snow && cfg.biomes[out.biome].snow) {
            out.day_c=cfg.biomes[out.biome].day_c;
            out.night_c=cfg.biomes[out.biome].night_c;
        }
        // Grass colour follows the biome weight itself, so it shifts
        // gradually across a border instead of speckling on dithered blocks.
        const float tint_w=cfg.generator_version>=3?w:secondary_block?std::max(0.f,w-.22f):w;
        out.grass_tint={a.grass.r*tint_w+b.grass.r*(1-tint_w),a.grass.g*tint_w+b.grass.g*(1-tint_w),
                       a.grass.b*tint_w+b.grass.b*(1-tint_w)};
        out.preserve=a.preserve_saturation*w+b.preserve_saturation*(1-w);
        out.brightness=a.brightness*w+b.brightness*(1-w);
        out.pebbles=a.pebbles*w+b.pebbles*(1-w);
    };
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
        const int transition_id=(mask.primary!=mask.secondary && w<.85f)?mask.secondary:-1;
        const uint64_t dither_h=mix(uint64_t(uint32_t(int(std::floor(x[i]))))*0x9e3779b185ebca87ULL ^
                                    uint64_t(uint32_t(int(std::floor(z[i]))))*0xc2b2ae3d27d4eb4fULL ^
                                    uint64_t(uint32_t(offset_seed(seed,701))));
        const bool use_secondary_block=transition_id>=0 &&
            float(dither_h&65535)/65535.f<smooth(.50f,.84f,1.f-w)*.65f;
        const auto &active_biome=use_secondary_block?secondary:primary;
        if(cfg.generator_version>=3) {
            const float waterbed_sample=waterbed.empty()?0.f:waterbed[i];
            auto out=regional_relief(cfg,{c[i],e[i],r[i],spur_r[i],spine[i],plat[i],d[i],river[i],regions[i],
                                         meadow.empty()?0.f:meadow[i],waterbed_sample},
                                     terrain_multiplier,biome_mountains,mound_height(cfg,seed,x[i],z[i]));
            out.transition_biome=transition_id;
            out.biome=mask.primary;
            out.temperature=primary.temperature*w+secondary.temperature*(1-w)-
                std::max(0.f,out.height-cfg.cold_start_altitude)*cfg.altitude_cooling;
            const float rock_exposure=out.slope+.23f*d[i]+.09f*(out.ridge-.5f);
            const bool shore=out.height<cfg.sea_level+cfg.beach_height+2.f && out.continentalness<.43f;
            const bool steep=rock_exposure>(shore?.48f:.64f) || out.arch_mask>.18f;
            // Coherent patches set the shape; block-scale jitter roughens their edges.
            const float grain=float((dither_h>>16)&65535)/32767.5f-1.f;
            // Snow cover rises smoothly across cold biome borders, the climate
            // snow line and the altitude snow line. Drift noise turns the
            // half-covered band into snow patches on grass and grass on snow;
            // fully covered or bare ground never flips.
            const bool any_desert=primary.desert || secondary.desert;
            const float biome_snow=(primary.snow?w:0.f)+(secondary.snow?1.f-w:0.f);
            float snow_cover=any_desert?0.f:1.f-smooth(cfg.snow_temperature-.035f,cfg.snow_temperature+.035f,
                                                        out.temperature);
            if (out.height>cfg.sea_level)
                snow_cover=std::max(snow_cover,smooth(.18f,.82f,biome_snow));
            snow_cover=std::max(snow_cover,smooth(cfg.snow_height-7.f,cfg.snow_height+7.f,out.height));
            const float drift=drifts.empty()?grain:.78f*drifts[i]+.22f*grain;
            const bool cold=snow_cover>0.f && snow_cover+.42f*drift>.5f;
            const float patch=patches.empty()?0.f:.80f*patches[i]+.20f*grain;
            // Keep river sandbars and the bank mixture close to the waterline.
            float bank_mix=0.f, river_mix=0.f;
            if (!patches.empty() && !steep && !cold && std::abs(out.height-cfg.sea_level)<5.f) {
                const float river_bank=smooth(.58f,.91f,out.valley) *
                    (1.f-smooth(cfg.river_width*.45f,cfg.river_width*1.6f,std::abs(river[i])));
                const float coast_bank=smooth(.12f,.48f,out.beach_mask);
                const float waterline=1.f-smooth(.5f,5.f,std::abs(out.height-cfg.sea_level));
                river_mix=river_bank*waterline;
                bank_mix=std::max(river_mix,coast_bank*waterline);
            }
            const bool river_sand=out.region!=5 && river_mix>0.f &&
                .70f*patch-.30f*d[i]>.60f-.48f*river_mix;
            if (cfg.deeper_waterbeds && out.height < cfg.sea_level &&
                (out.region==5 || out.river>0.f)) {
                const float depth=cfg.sea_level-out.height;
                const float sediment=d[i]+.35f*waterbed_sample;
                if (bank_mix>0.f && patch<-.72f+.60f*bank_mix)
                    out.surface=registry.dirt;
                else if (river_sand || (out.region==5 && depth<=4.f+2.5f*patch &&
                         out.continentalness>.30f-.012f*patch && !steep && !cold))
                    out.surface=registry.sand;
                else
                    out.surface=sediment+.28f*patch>.04f?id_clay:id_gravel;
                out.sediment_depth=out.surface==registry.dirt?2:2+int(3.f*smooth(-.35f,.65f,sediment));
            } else if(out.region==5 || (out.height<=cfg.sea_level && out.river>.32f))
                out.surface=steep?registry.stone:id_gravel;
            else if(out.beach_mask>.36f+.12f*patch && !steep)
                out.surface=cold?id_gravel:registry.sand;
            else if(steep)
                out.surface=registry.stone;
            else if(cold)
                out.surface=registry.snow;
            else if(active_biome.desert)
                out.surface=registry.sand;
            else
                out.surface=registry.grass;
            if (bank_mix>0.f && out.height>=cfg.sea_level &&
                (out.surface==registry.grass || out.surface==registry.sand ||
                 (river_sand && out.surface==id_gravel))) {
                if (river_sand || patch>.72f-.64f*bank_mix) {
                    out.surface=river_sand?registry.sand:id_gravel;
                    out.sediment_depth=2;
                } else if (out.surface==registry.grass && patch<-.72f+.50f*bank_mix) {
                    out.surface=registry.dirt;
                }
            }
            if(out.surface==registry.snow)
                for(size_t j=0;j<cfg.biomes.size();j++)if(cfg.biomes[j].snow){out.biome=int(j);break;}
            else if(primary.snow && !secondary.snow)
                out.biome=mask.secondary; // A grass patch inside the border belongs to the warm side.
            apply_climate(out,mask,use_secondary_block);
            result[i]=out;
            continue;
        }

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

        // Sustained Piedmont / Foothill Transition Zone:
        // Spans a broad buffer zone (region_raw from ~0.38 to ~0.68) before mountain ranges.
        // It provides a gradual, progressive ramp that lifts the base elevation from low riverlands (46)
        // up to an elevated highland foundation (75..90), while progressively ramping ruggedness and erosion.
        float foothill_ramp = cfg.generator_version == 1
                                  ? 0.f
                                  : smooth(std::max(0.f, cfg.mountain_threshold - .30f), cfg.mountain_threshold, region_raw) *
                                        inland_mask * (1.f - ocean_mask);
        float foothill_zone = foothill_ramp * (1.f - mountain);

        // Elevated flat-topped plateaus with steep escarpment rims:
        // Confined to the elevated piedmont / highland transition zone, NEVER appearing
        // in low river plains to avoid abrupt 35-block mesa chasms in the middle of meadows!
        float raw_plat = plat[i] * .5f + .5f;
        float plateau_mask = cfg.generator_version == 1
                                 ? 0.f
                                 : smooth(.62f, .75f, raw_plat) * smooth(.28f, .72f, foothill_ramp) *
                                       (1.f - smooth(.48f, .82f, mountain)) * smooth(.26f, .50f, er) * inland_mask;
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
        float mountain_stream = 0.f;

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

        // Progressive base lift across the piedmont transition:
        // Smoothly lifts the terrain from river baseline (46) up to ~82 before mountains start
        float piedmont_elev = foothill_ramp * foothill_ramp * 36.f * shape;

        // Foothill rolling relief and spur undulation:
        float foothill = cfg.generator_version == 1
                             ? 0.f
                             : foothill_zone * (.35f + .65f * broad_spur) * rough * 18.f * shape;

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
            (c[i] * .40f * (1.f - .65f * buildable_mask) + plateau_mask * 1.6f + mountain_body * .45f) *
            cfg.continental_amplitude * shape;
        // Depress ocean basins smoothly below sea level and sculpt gentle sandy coastal beaches at the ocean transition!
        float beach_zone = ocean_enabled ? smooth(.19f, .27f, cont_raw) * (1.f - smooth(.29f, .39f, cont_raw)) *
                                               (1.f - smooth(.25f, .55f, mountain))
                                         : 0.f;
        if (ocean_mask > 0.f) {
            float ocean_depth = (cfg.base_height - float(cfg.sea_level) + 16.f) * smooth(.0f, .85f, ocean_mask);
            continental_elev -= ocean_depth;
        }
        float mountain_elev = (chain * .88f + fault_step * .18f * cfg.terrace_strength) * cfg.mountain_amplitude;
        float valley_depth_factor = .25f + .75f * foothill_ramp;
        float valley_carve =
            (valley * (.30f + rough * .70f + mountain * .45f) + gully_erosion * .30f) *
            (cfg.valley_depth * valley_depth_factor) * shape;
        float local_detail =
            (d[i] * (.14f + .86f * rough) * top_flatness + outcrop) * cfg.detail_amplitude * shape;

        float height = cfg.base_height + continental_elev + piedmont_elev + foothill + mountain_elev - valley_carve + local_detail;

        if (ocean_mask < .25f && river_channel < .15f) {
            height = std::max(height, float(cfg.sea_level) + 1.8f);
        }

        // Smoothly grade coastal beaches down to `sea_level + 1..3` so the transition from continent to ocean forms wide walkable beaches
        if (beach_zone > .05f && cfg.valley_depth > 1.f) {
            float beach_target = float(cfg.sea_level) + 1.0f + 2.5f * smooth(.20f, .34f, cont_raw);
            float beach_pull = smooth(.05f, .70f, beach_zone) * (1.f - .85f * river_channel);
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

        // 7b. Natural Water Levels & Fixed Baseline:
        // Mountain streams no longer place elevated water voxels on high mountain summits,
        // preventing physics bugs and world-gen simulation lag. All natural bodies of water
        // (oceans, lakes, river channels) generate cleanly at or below sea_level and start fixed.
        int water_top = cfg.sea_level;
        float waterfall_mask = 0.f;

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
                        : mountain > .45f                              ? 4
                        : plateau_mask > .50f                          ? 2
                        : (valley > .48f || river_channel > .35f)      ? 3
                        : (buildable_mask > .40f && foothill_ramp < .35f) ? 0
                        : (foothill_zone > .25f || rough > .35f)       ? 1
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
        out.river = river_channel;
        float river_flow_x = 0.f, river_flow_z = 0.f;
        if (river_channel > 0.04f) {
            float flow_dx = -float(x[i]);
            float flow_dz = -float(z[i]);
            float dlen = std::sqrt(flow_dx * flow_dx + flow_dz * flow_dz);
            if (dlen > 1.f) {
                flow_dx /= dlen;
                flow_dz /= dlen;
            } else {
                flow_dx = 0.f; flow_dz = 1.f;
            }
            float r_speed = std::clamp(river_channel * 1.5f, 0.2f, 1.0f);
            river_flow_x = flow_dx * r_speed;
            river_flow_z = flow_dz * r_speed;
        }
        out.river_flow_x = river_flow_x;
        out.river_flow_z = river_flow_z;
        out.buildable = buildable_mask;
        out.adventure = adventure_mask;
        out.terrace_step = terrace_zone;
        out.arch_mask = arch_candidate;
        out.lake_mask = lake_mask;
        out.beach_mask = beach_mask;
        out.river_bank_step = bank_step_mask;
        out.waterfall_mask = 0.f;
        out.water_top = cfg.sea_level;
        out.transition_biome = transition_id;
        out.region = region_id;
        apply_climate(out,mask,use_secondary_block);
        result[i] = out;
    }
    auto after_surface=std::chrono::steady_clock::now();
    if (cave_biomes && cave_runtime && include_underground) {
        for(size_t i=0;i<result.size();i++) {
            const float heat=cave_runtime->geothermal(x[i],z[i]);
            if(cfg.caves.biome_variation)result[i].underground=cave_biomes->column(x[i],z[i],result[i].underground.surface,heat);
            else result[i].underground.geothermal=heat;
        }
    }
    if(timings)timings->cave_biome_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-after_surface).count();
    if (timings)
        timings->surface_ms +=
            std::chrono::duration<double, std::milli>(after_surface - after_noise).count();
    return result;
}
Column Generator::column(int x, int z) const {
    return columns({float(x)}, {float(z)})[0];
}
SpawnPoint Generator::find_spawn() const {
    // 0 cold, 1 temperate, 2 hot, from the biome's climate temperature.
    auto climate = [&](const Column &c) {
        if (c.biome < 0 || c.biome >= int(cfg.biomes.size())) return 1;
        const auto &b = cfg.biomes[c.biome];
        return b.snow || b.temperature < .35f ? 0 : b.desert || b.temperature > .75f ? 2 : 1;
    };
    auto grid = [&](int radius, int step, std::vector<float> &x, std::vector<float> &z) {
        x.clear();
        z.clear();
        for (int gz = -radius; gz <= radius; gz += step)
            for (int gx = -radius; gx <= radius; gx += step) {
                x.push_back(float(gx));
                z.push_back(float(gz));
            }
        return columns(x, z, nullptr, false);
    };
    // Coarse climate map: cold and hot regions must lie within reach of spawn.
    constexpr int climate_radius = 4096, climate_step = 128, reach = 2600;
    std::vector<float> cx, cz;
    const auto climate_map = grid(climate_radius, climate_step, cx, cz);
    auto neighbours_vary = [&](float x, float z) {
        if (!cfg.single_biome.empty())
            return true;
        bool cold = false, hot = false;
        for (size_t i = 0; i < climate_map.size() && !(cold && hot); ++i) {
            const auto &c = climate_map[i];
            if (c.height <= cfg.sea_level) continue;
            const float dx = cx[i] - x, dz = cz[i] - z;
            if (dx * dx + dz * dz > float(reach) * reach) continue;
            const int k = climate(c);
            cold |= k == 0;
            hot |= k == 2;
        }
        return cold && hot;
    };
    // Candidate plains in temperate biomes, nearest and flattest first.
    constexpr int search_radius = 2048, search_step = 32;
    std::vector<float> px, pz;
    const auto plains = grid(search_radius, search_step, px, pz);
    struct Candidate { float score; size_t index; };
    std::vector<Candidate> candidates;
    for (size_t i = 0; i < plains.size(); ++i) {
        const auto &c = plains[i];
        if ((cfg.single_biome.empty() && (climate(c) != 1 || c.transition_biome >= 0)) ||
            c.region != 0 || c.height < cfg.sea_level + 3 ||
            c.height > cfg.sea_level + 60 || c.river > .02f || c.lake_mask > .05f || c.slope > .2f)
            continue;
        const float distance = std::sqrt(px[i] * px[i] + pz[i] * pz[i]);
        candidates.push_back({distance / 512.f + c.slope * 4.f + (1.f - c.buildable), i});
    }
    if (candidates.empty() && !cfg.single_biome.empty()) {
        for (size_t i = 0; i < plains.size(); ++i) {
            const auto &c = plains[i];
            if (c.height <= cfg.sea_level + 1 || c.river > .05f || c.lake_mask > .1f)
                continue;
            const float distance = std::sqrt(px[i] * px[i] + pz[i] * pz[i]);
            candidates.push_back({distance / 512.f + c.slope * 4.f + (1.f - c.buildable), i});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.score < b.score; });
    SpawnPoint best;
    float best_score = 1e9f;
    int checked = 0;
    std::vector<float> fx, fz;
    for (const auto &candidate : candidates) {
        if (checked >= 48 || candidate.score > best_score) break;
        const float x0 = px[candidate.index], z0 = pz[candidate.index];
        if (!neighbours_vary(x0, z0)) continue;
        ++checked;
        // Flat ground: a 25 m square whose heights stay within one block and a half.
        fx.clear();
        fz.clear();
        for (int dz = -12; dz <= 12; dz += 3)
            for (int dx = -12; dx <= 12; dx += 3) {
                fx.push_back(x0 + dx);
                fz.push_back(z0 + dz);
            }
        const auto patch = columns(fx, fz, nullptr, false);
        float lo = 1e9f, hi = -1e9f;
        bool dry = true;
        for (const auto &c : patch) {
            lo = std::min(lo, c.height);
            hi = std::max(hi, c.height);
            dry &= c.height > cfg.sea_level + 2 && c.river <= .02f && c.lake_mask <= .05f;
        }
        if (!dry || hi - lo > 1.5f) continue;
        const float score = candidate.score + (hi - lo);
        if (score < best_score) {
            best_score = score;
            const auto &centre = patch[patch.size() / 2];
            best = {x0 + .5f, z0 + .5f, std::floor(centre.height), true};
        }
    }
    return best;
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
std::pair<float, float> Generator::river_current(int x, int z) const {
    const auto c=column(x,z);
    if (c.river < 0.04f || (drainage && c.region==5))
        return {0.f, 0.f};
    if(drainage) {
        auto fields=[&](float sx,float sz) {
            float px=sx+cfg.warp_amplitude*warp->GenSingle2D(sx,sz,offset_seed(seed,101));
            float pz=sz+cfg.warp_amplitude*warp->GenSingle2D(sx,sz,offset_seed(seed,509));
            return std::pair{drainage->GenSingle2D(px+721,pz+391,offset_seed(seed,713)),
                             continent->GenSingle2D(px,pz,seed)};
        };
        const auto west=fields(x-4,z),east=fields(x+4,z),north=fields(x,z-4),south=fields(x,z+4);
        const float tx=north.first-south.first,tz=east.first-west.first;
        const float magnitude=std::hypot(tx,tz);
        if(magnitude<1e-8f)return {0.f,0.f};
        const float uphill=tx*(east.second-west.second)+tz*(south.second-north.second);
        const float speed=std::copysign(.75f*c.river/magnitude,-uphill);
        return {tx*speed,tz*speed};
    }
    return {c.river_flow_x, c.river_flow_z};
}
void Generator::density_samples(std::vector<float> &out, const std::vector<float> &x,
                                const std::vector<float> &y, const std::vector<float> &z) const {
    out.resize(x.size());
    if (!density)
        std::fill(out.begin(), out.end(), 0.f);
    else if (!out.empty())
        density->GenPositionArray3D(out.data(), int(out.size()), x.data(), y.data(), z.data(), 0, 0, 0,
                                    offset_seed(seed, 113));
}
bool Generator::density_solid(const Column &c,int x,int y,int z) const {
    const float band=cfg.cliff_strength*c.cliff;
    if (band == 0.f)
        return float(y) <= c.height;
    if(float(y)<c.height-band)return true;
    if(float(y)>c.height+band)return false;
    return float(y)<=c.height+density->GenSingle3D(float(x),float(y),float(z),offset_seed(seed,113))*band;
}
std::vector<caves::Formation> Generator::cave_formations(const caves::Network &network) const {
    std::vector<caves::Formation> out;
    if(!cave_biomes->has_formations() || network.chambers.empty())return out;
    std::vector<float> x,z;x.reserve(network.chambers.size());z.reserve(x.capacity());
    for(const auto &room:network.chambers){x.push_back(room.center[0]);z.push_back(room.center[2]);}
    auto climate=columns(x,z);
    for(size_t i=0;i<climate.size();i++) {
        if(!cfg.caves.biome_variation)
            climate[i].underground=cave_biomes->column(x[i],z[i],climate[i].underground.surface,
                                                      cave_runtime->geothermal(x[i],z[i]));
        cave_biomes->plan_formations(network.chambers[i],climate[i].underground,climate[i].height,out);
    }
    return out;
}
caves::Outlet Generator::cave_outlet(const caves::Passage &opening) const {
    auto b=caves::bounds(opening);
    int x0=int(std::floor(b.lo[0]))-3,x1=int(std::ceil(b.hi[0]))+3;
    int z0=int(std::floor(b.lo[2]))-3,z1=int(std::ceil(b.hi[2]))+3;
    std::vector<float> xs,zs;xs.reserve((x1-x0+1)*(z1-z0+1));zs.reserve(xs.capacity());
    for(int z=z0;z<=z1;z++)for(int x=x0;x<=x1;x++){xs.push_back(float(x));zs.push_back(float(z));}
    auto cols=columns(xs,zs,nullptr,false);
    caves::Outlet result;
    const int high=std::min(cfg.sea_level,int(std::ceil(b.hi[1]))+2);
    const int width=x1-x0+1;
    auto col=[&](int x,int z)->const Column&{return cols[(z-z0)*width+x-x0];};
    constexpr int neighbors[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for(int z=z0+1;z<z1;z++)for(int x=x0+1;x<x1;x++) {
        const auto &c=col(x,z);
        const int low=int(std::floor(c.height-cfg.cliff_strength*c.cliff));
        // The exterior below sea level is water by the base terrain rule.
        // Dry outlet limits can therefore never lower a connected ocean.
        result.dry_limit=std::min(result.dry_limit,std::max(cfg.sea_level,low-1));
        const int start=std::max({low,cfg.min_y+2,int(std::floor(b.lo[1]))-2});
        for(int y=start;y<=high;y++) {
            if(density_solid(c,x,y,z))continue;
            bool contact=false;
            // Confirm a shared face between exterior water and host rock
            // removed by this actual noisy entrance, below the same plane.
            for(const auto &d:neighbors) {
                int nx=x+d[0],ny=y+d[1],nz=z+d[2];
                if(ny<=cfg.sea_level && cave_runtime->entrance_contains(opening,nx,ny,nz) &&
                   density_solid(col(nx,nz),nx,ny,nz)){contact=true;break;}
            }
            if(!contact)continue;
            if(c.region==5)return {caves::WaterSource::ocean,result.dry_limit};
            result.source=caves::WaterSource::surface_water;break;
        }
    }
    return result;
}
BlockId Generator::terrain_sample(int x, int y, int z) const {
    if (cfg.preset == WorldPreset::Structure)
        return registry.air;
    if (std::abs(int64_t(x))>1000000 || std::abs(int64_t(z))>1000000)
        return registry.air;
    if (y < cfg.min_y || y >= cfg.max_y)
        return registry.air;
    if (y == cfg.min_y)
        return registry.bedrock;
    return terrain_sample(column(x,z),x,y,z);
}
Generator::CaveCell Generator::cave_cell(const Column &c,int x,int y,int z) const {
    CaveCell out;
    if(cave_runtime) {
        const auto &q=cave_runtime->point_query(x,z);
        if(cave_runtime->sample(q,x,y,z,c.height).carved) {
            auto hydro=cave_runtime->hydrology(cave_runtime->basin_at(x,z));
            out.carved=true;out.fluid=hydro.at(y);
            out.decoration=caves::sample_formation(q,{float(x),float(y),float(z)},out.fluid==caves::Fluid::water,
                                                   out.fluid==caves::Fluid::lava,hydro.top);
            // Same rule as the tile fill: dry chamber air inside a lava sea is lava.
            if(cave_noise && out.fluid==caves::Fluid::dry && !out.decoration &&
               cave_noise->at(x,y,z,c.height)==caves::NoiseCell::lava)
                out.fluid=caves::Fluid::lava;
            return out;
        }
    }
    if(cave_noise) {
        auto cell=cave_noise->at(x,y,z,c.height);
        if(cell!=caves::NoiseCell::solid) {
            out.carved=true;
            out.fluid=cell==caves::NoiseCell::lava?caves::Fluid::lava:caves::Fluid::dry;
        }
    }
    return out;
}
int Generator::cave_probe(const Column &c,int x,int y,int z) const {
    if(y<cfg.min_y || y>=cfg.max_y)return 2;
    if(y==cfg.min_y)return 1;
    if(!density_solid(c,x,y,z) || natural_arch_carved(c,x,y,z))return 2;
    auto cell=cave_cell(c,x,y,z);
    if(!cell.carved)return 1;
    if(cell.fluid!=caves::Fluid::dry || (cell.decoration && !registry.get(cell.decoration).root_plant))return 2;
    return 0;
}
BlockId Generator::cave_spike(const Column &c,int x,int y,int z,uint8_t *shape) const {
    if(!cave_noise)return 0;
    int up=0,down=0,roof=INT_MAX,floor=INT_MIN;
    for(int k=1;k<=caves::spike_scan;++k) {
        int p=cave_probe(c,x,y+k,z);
        if(p==0){++up;continue;}
        if(p==1)roof=y+k;
        break;
    }
    for(int k=1;k<=caves::spike_scan;++k) {
        int p=cave_probe(c,x,y-k,z);
        if(p==0){++down;continue;}
        if(p==1)floor=y-k;
        break;
    }
    const auto spikes=cave_noise->spikes(x,z,floor,roof,std::min(up+down+1,caves::spike_scan));
    int form=0;
    if(spikes.pillar)form=int(BlockShape::Pillar);
    else if(spikes.down && roof-y<=spikes.down)form=roof-y==spikes.down?int(BlockShape::MiniPillar):int(BlockShape::Pillar);
    else if(spikes.up && y-floor<=spikes.up)form=y-floor==spikes.up?int(BlockShape::MiniPillar):int(BlockShape::Pillar);
    if(!form)return 0;
    if(shape)*shape=pack_shape(form);
    return rock_material(c,x,y,z);
}
BlockId Generator::terrain_sample(const Column &c,int x,int y,int z,bool spikes) const {
    if (cfg.preset == WorldPreset::Structure)
        return registry.air;
    if(y<cfg.min_y || y>=cfg.max_y || std::abs(int64_t(x))>1000000 || std::abs(int64_t(z))>1000000)
        return registry.air;
    if(y==cfg.min_y)return registry.bedrock;
    int w_top = std::max(cfg.sea_level, c.water_top);
    if (natural_arch_carved(c, x, y, z))
        return y <= w_top ? registry.water : registry.air;
    if (density_solid(c,x,y,z)) {
        if (cave_runtime) {
            const auto cell=cave_cell(c,x,y,z);
            if(cell.carved) {
                const auto fluid=cell.fluid;
                if(cell.decoration)return cell.decoration;
                if(spikes && fluid==caves::Fluid::dry)
                    if(auto spike=cave_spike(c,x,y,z))return spike;
                if (fluid == caves::Fluid::dry && rock_grass_at(c, x, y, z, true) &&
                    y > cfg.min_y + 1 && cave_probe(c,x,y-1,z)==1) {
                    bool headroom = true;
                    for (int dy : {1, 2})
                        if (cave_probe(c,x,y+dy,z)!=0 || cave_cell(c,x,y+dy,z).decoration) {
                            headroom = false;
                            break;
                        }
                    if (headroom)
                        return registry.valid(registry.grass) ? registry.grass :
                            registry.cave_grass ? registry.cave_grass : registry.rock_grass;
                }
                return fluid==caves::Fluid::water?registry.water:fluid==caves::Fluid::lava?registry.lava:registry.air;
            }
        }
        if (natural_arch_carved(c, x, y + 1, z))
            return rock_material(c,x,y,z);
        const bool above_solid = density_solid(c, x, y + 1, z);
        if (cave_runtime && above_solid) {
            const auto above=cave_cell(c,x,y+1,z);
            if(above.carved && !above.decoration)
                return rock_material(c, x, y, z, above.fluid == caves::Fluid::water);
        }
        return material(c,x,y,z,!above_solid);
    }
    if (y > w_top && rock_grass_at(c, x, y, z, false) &&
        density_solid(c, x, y - 1, z) && !natural_arch_carved(c, x, y - 1, z) &&
        (!cave_runtime || !cave_cell(c,x,y-1,z).carved))
        return registry.valid(registry.grass) ? registry.grass : registry.rock_grass;
    return y <= w_top ? registry.water : registry.air;
}
BlockId Generator::sample(int x, int y, int z) const {
    if (std::abs(int64_t(x))>1000000 || std::abs(int64_t(z))>1000000 || y<cfg.min_y || y>=cfg.max_y)
        return registry.air;
    auto terrain = terrain_sample(x, y, z);
    if (terrain != registry.air || !cfg.vegetation)
        return terrain;
    for (const auto &part : trees(x, z, x + 1, z + 1))
        if (part.first.y == y && (terrain == registry.air || registry.get(terrain).foliage))
            terrain = part.second;
    return terrain;
}
BlockId Generator::geological_rock(const Column &c, int x, int y, int z) const {
    if (cfg.generator_version == 1)
        return registry.stone;
    if(cfg.generator_version>=3) {
        if(c.underground.surface.desert>.8f && c.erosion<.36f && c.mountain>.38f)
            return id_sandstone;
        return registry.stone;
    }

    // 1. High Alpine Summits & Crests: Dark Basalt with Crystal Veins
    if (y >= cfg.snow_height - 15 || (c.mountain > .58f && y >= 135)) {
        uint32_t v_hash = uint32_t(x)*73856093u ^ uint32_t(y)*19349663u ^ uint32_t(z)*83492791u;
        if ((v_hash & 1023) < 85 && id_crystal_stone != registry.stone)
            return id_crystal_stone;
        return id_basalt != registry.stone ? id_basalt : registry.stone;
    }

    // 2. Natural Rock Arches: Crystal-bearing or Basalt lintels
    if (c.arch_mask > .35f && y > c.height - 4) {
        uint32_t a_hash = uint32_t(x)*374761393u ^ uint32_t(z)*668265263u;
        if ((a_hash & 7) == 0 && id_crystal_stone != registry.stone)
            return id_crystal_stone;
    }

    // 3. Desert & Arid Mountains: Stratified Sandstone & Limestone
    if (c.underground.surface.desert > .45f) {
        int desert_strata = (y + int(std::sin(float(x) * .04f + float(z) * .03f) * 3.f)) / 5;
        if ((desert_strata & 1) == 0 && id_sandstone != registry.stone)
            return id_sandstone;
        return id_limestone != registry.stone ? id_limestone : (id_sandstone != registry.stone ? id_sandstone : registry.stone);
    }

    // 4. River Gorges & Water Level Cliffs: Wet Stone & Mossy Stone
    if (y <= cfg.sea_level + 4 || (c.river > .30f && y <= cfg.sea_level + 7)) {
        if (y <= cfg.sea_level + 2 && id_wet_stone != registry.stone)
            return id_wet_stone;
        if (id_moss_stone != registry.stone)
            return id_moss_stone;
        return registry.stone;
    }

    // 5. Mid-Altitude Mountain Flanks, Terraces, & Escarpments:
    // Folded geological stratification (Limestone, Fossil Limestone, Standard Stone)
    float strata_warp = std::sin(float(x) * .032f + float(z) * .024f) * 4.2f +
                        std::cos(float(x) * .016f - float(z) * .028f) * 2.8f;
    int strata_layer = int(std::floor((float(y) + strata_warp) / 6.0f)) & 7;

    // Damp, humid temperate slopes get mossy stone
    if (c.underground.surface.humidity > .62f && strata_layer == 2 && id_moss_stone != registry.stone)
        return id_moss_stone;

    switch (strata_layer) {
        case 0:
        case 4:
            return id_limestone != registry.stone ? id_limestone : registry.stone;
        case 1:
            return id_fossil_limestone != registry.stone ? id_fossil_limestone : registry.stone;
        case 3:
        case 6:
            return id_limestone != registry.stone ? id_limestone : registry.stone;
        case 5:
            if (c.mountain > .38f && id_basalt != registry.stone)
                return id_basalt;
            return registry.stone;
        default:
            return registry.stone;
    }
}
BlockId Generator::base_rock(int y) const {
    return cfg.caves.enabled && y <= hellstone_y ? id_hellstone : registry.stone;
}
BlockId Generator::rock_material(const Column &c,int x,int y,int z,bool water) const {
    if (const BlockId deep = base_rock(y); deep != registry.stone)
        return deep;
    if(cave_runtime && !cfg.caves.biome_variation)return registry.stone;
    return cave_biomes && cave_runtime ? cave_biomes->material(c.underground,c.height,x,y,z,water) : geological_rock(c, x, y, z);
}
// Broad, blended bands leave exposed rock between patches of grass.
static float rock_grass_patch(int x, int y, int z) {
    const float px = x + y * .73f, pz = z - y * .41f;
    return std::sin(px * .065f + std::cos(pz * .055f) * 1.4f) * .5f +
           std::cos(pz * .060f + std::sin(px * .050f) * 1.4f) * .5f;
}
bool Generator::rock_grass_at(const Column &c, int x, int y, int z, bool cave) const {
    if (!registry.rock_grass && !(cave && registry.cave_grass))
        return false;
    if (!cave && (c.surface != registry.stone || c.arch_mask > .22f || c.mountain <= .25f ||
                  y >= cfg.snow_height || y <= std::max(cfg.sea_level, c.water_top)))
        return false;
    return rock_grass_patch(x, y, z) > (cave ? .22f : -.10f);
}
BlockId Generator::material(const Column &c,int x,int y,int z,bool exposed) const {
    if (c.sediment_depth) {
        if (y > c.height-c.sediment_depth)
            return c.surface;
        if (c.height>=cfg.sea_level && y>c.height-5.f)
            return registry.dirt;
        return rock_material(c,x,y,z);
    }
    // Cave floor cover is resolved separately after carving and liquid fill.
    if (cfg.caves.enabled && y < c.height - cfg.cliff_strength*c.cliff - 8)
        return rock_material(c,x,y,z);
    if (cfg.generator_version == 1)
        return y > c.height - 3 ? c.surface : (y > c.height - 6 ? registry.dirt : registry.stone);
    if(cfg.generator_version>=3 && id_gravel!=registry.stone && c.surface==id_gravel)
        return y>c.height-3 ? id_gravel : registry.stone;
    if (c.arch_mask > .22f)
        return geological_rock(c, x, y, z);
    if (c.surface == registry.stone)
        return geological_rock(c, x, y, z);
    if (!exposed && (c.slope > .58f || c.cliff > .52f))
        return geological_rock(c, x, y, z);
    if (c.surface == registry.sand) {
        int sand_depth = c.valley > .35f ? 9 : (c.slope > .38f ? 4 : 7);
        return y > c.height - sand_depth ? registry.sand : geological_rock(c, x, y, z);
    }
    if (exposed)
        return c.surface;
    // A snow pack covers the first blocks, so steps on snowy slopes stay white.
    if (c.surface == registry.snow && y > c.height - 3.f)
        return registry.snow;
    int dirt_depth = c.valley > .40f    ? 7
                     : c.plateau > .60f ? 4
                     : (c.mountain > .45f || c.slope > .42f) ? 2
                                                             : 5;
    return y > c.height - dirt_depth ? registry.dirt : geological_rock(c, x, y, z);
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
    int pad = tile.level == 0 ? skylight_reach + 1 : 1 << tile.level;
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
        const auto &def = r.get(id);
        if (def.solid && !def.liquid && !def.water && !def.lava)
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
    return select_tiles({x,z,x,z,s.view_distance,false},s);
}
std::vector<TileKey> select_tiles(const StreamDemand &demand, const Settings &s) {
    const auto [x,z,ahead_x,ahead_z,range,underground,required_detail]=demand;
    std::vector<TileKey> result;
    float splits[] = {
        0, s.near_distance, s.middle_distance, s.coarse_distance, s.distant_distance, s.horizon_distance};
    const bool adaptive=s.adaptive_streaming;
    if (adaptive)
        splits[3]=std::min(s.coarse_distance, 360.f);
    int max_level = underground ? 0 : s.generator_version >= 2 ? 5 : 3;
    int root_size = 32 << max_level;
    std::function<void(TileKey)> visit = [&](TileKey t) {
        float d = tile_distance(t, x, z);
        if (d > range)
            return;
        const float projected=adaptive && t.level<=3
            ? tile_distance(t,ahead_x,ahead_z) : d;
        const bool needs_detail=t.level>0 && required_detail &&
            std::any_of(required_detail->begin(),required_detail->end(),[&](const TileKey &key) {
                return overlaps(t,key);
            });
        if (t.level > 0 && (needs_detail || std::min(d,projected) < splits[t.level])) {
            int half = t.size() / 2;
            for (int dz = 0; dz < 2; dz++)
                for (int dx = 0; dx < 2; dx++)
                    visit({t.x + dx * half, t.z + dz * half, t.level - 1});
        } else
            result.push_back(t);
    };
    int radius = int(std::ceil(range / root_size)) + 1, cx = int(std::floor(x / root_size)),
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
void Volume::compress() {
    if (compressed || data.empty())
        return;
    const size_t layers = size_t(height + 2);
    layer_slot.assign(layers, 0);
    std::vector<BlockId> slices;
    for (size_t y = 0; y < layers; ++y) {
        const BlockId first = data[y * 34];
        bool same = true;
        for (size_t z = 0; z < 34 && same; ++z)
            for (size_t x = 0; x < 34; ++x)
                if (data[(z * layers + y) * 34 + x] != first) { same = false; break; }
        if (same) {
            layer_slot[y] = -int32_t(first) - 1;
            continue;
        }
        layer_slot[y] = int32_t(slices.size() / 1156);
        for (size_t z = 0; z < 34; ++z)
            for (size_t x = 0; x < 34; ++x)
                slices.push_back(data[(z * layers + y) * 34 + x]);
    }
    packed = std::move(slices);
    packed.shrink_to_fit();
    std::vector<BlockId>().swap(data);
    compressed = true;
}
size_t Volume::voxel_bytes() const {
    return compressed ? packed.size() * sizeof(BlockId) + layer_slot.size() * sizeof(int32_t)
                      : data.size() * sizeof(BlockId);
}
TerrainWorkers::TerrainWorkers(std::shared_ptr<const Generator> g, int count,
                               std::function<void(MeshResult &)> finish_hook)
    : generator(std::move(g)), finish(std::move(finish_hook)) {
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
                                     job.include_vegetation,job.window,job.depleted);
                result.vegetation_source = std::move(job.vegetation_volume);
                if (finish)
                    finish(result);
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
                            std::shared_ptr<const AppearanceMap> surfaces, bool include_vegetation, VerticalWindow window,
                            std::shared_ptr<const ResourceDepletions> depleted) {
    {
        std::lock_guard<std::mutex> l(mutex);
        jobs.push_back({k, v, std::move(e), std::move(surfaces), nullptr, std::move(depleted), include_vegetation,window});
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
size_t TerrainWorkers::cancel_queued(const std::function<bool(TileKey, uint64_t, bool, VerticalWindow)> &cancel) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto before=jobs.size();
    jobs.erase(std::remove_if(jobs.begin(),jobs.end(),[&](const Job &job) {
        return cancel(job.key,job.version,bool(job.vegetation_volume),job.window);
    }),jobs.end());
    return before-jobs.size();
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
