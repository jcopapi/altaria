#pragma once
#include <FastNoise/FastNoise.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace altaria {
using BlockId = uint16_t;
struct IVec3 {
    int x = 0, y = 0, z = 0;
    bool operator==(const IVec3 &v) const {
        return x == v.x && y == v.y && z == v.z;
    }
};
struct CoordHash {
    size_t operator()(const IVec3 &p) const;
};
using EditMap = std::unordered_map<IVec3, BlockId, CoordHash>;
struct RGB {
    float r = 1, g = 1, b = 1;
};
struct BlockDef {
    BlockId id = 0;
    std::string key, name, name_pt;
    RGB color;
    bool solid = true, water = false, placeable = true;
    int texture_layer = 0, overlay = 0, transition_overlay = 0;
    float thermal_delta = 0, thermal_radius = 0, mass = 20;
    bool wood = false, foliage = false, tintable = false;
    bool distant_outline = true;
};
struct TextureDef {
    std::string path, generator;
    RGB color;
};
struct OverlayDef {
    uint16_t id = 0;
    std::string key, name;
    int top = -1, side = -1, bottom = -1;
    bool decal = false, tintable = true;
    float density = .10f;
};
struct BiomeDef {
    std::string key, name;
    RGB grass;
    float temperature = .6f, humidity = .5f, terrain_multiplier = 1, mountain_multiplier = 1, pebbles = .10f;
    float preserve_saturation = .65f, brightness = 1.35f;
    bool desert = false, snow = false;
    float day_c = 22, night_c = 10, grass_density = 0.f, tree_density = .45f, bush_density = 0.f;
    RGB fog_day = {.52f, .70f, .94f}, fog_night = {.25f, .14f, .43f};
    float fog_start = 1800, fog_end = 5600;
};
std::vector<BiomeDef> default_biomes();
uint32_t pack_rgb(RGB color);
RGB unpack_rgb(uint32_t color);
struct Appearance {
    uint32_t tint = 0xffffff;
    uint16_t preserve = 350, brightness = 1000;
    uint16_t overlay = 65535, decoration = 65535;
    bool painted = false;
    uint8_t shape = 255, rotation = 0; // 255 = generated shape; material IDs remain independent.
    bool derived_ramp = false; // Render snapshot only; never written to authoritative journals.
    bool operator==(const Appearance &v) const {
        return tint == v.tint && preserve == v.preserve && brightness == v.brightness &&
               overlay == v.overlay && decoration == v.decoration && painted == v.painted &&
               shape == v.shape && rotation == v.rotation;
    }
};
using AppearanceMap = std::unordered_map<IVec3, Appearance, CoordHash>;
struct SurfaceCommand {
    uint64_t actor = 1, sequence = 0;
    IVec3 position;
    Appearance appearance;
};
struct Registry {
    std::vector<BlockDef> blocks;
    std::unordered_map<std::string, BlockId> keys;
    std::vector<TextureDef> textures;
    std::vector<OverlayDef> overlays;
    std::unordered_map<std::string, uint16_t> overlay_keys;
    BlockId air = 0, stone = 1, dirt = 2, grass = 3, sand = 4, water = 5, snow = 6, bedrock = 7;
    bool add(BlockDef def, std::string &error);
    bool valid(BlockId id) const;
    const BlockDef &get(BlockId id) const;
    BlockId find(const std::string &key) const;
};
struct Settings {
    int generator_version = 2, min_y = -64, max_y = 320, sea_level = 42;
    float base_height = 60, continental_amplitude = 24, mountain_amplitude = 220;
    float continental_scale = 1800, mountain_scale = 480, erosion_scale = 710;
    float detail_scale = 48, detail_amplitude = 4, warp_scale = 310, warp_amplitude = 55;
    float valley_depth = 20, valley_width = .11f, cliff_strength = 14, terrace_strength = .24f;
    float plains_threshold = .55f, snow_height = 185;
    float density_scale = 43, terrace_height = 18, ridge_power = 1.45f;
    int octaves = 5, workers = 3, upload_budget = 2, max_pending = 24;
    float near_distance = 96, middle_distance = 224, coarse_distance = 480, view_distance = 6144;
    float autosave_seconds = 15;
    float distant_distance = 1100, horizon_distance = 2600, fog_start = 4500;
    float biome_scale = 520, biome_blend = 130, altitude_cooling = .004f, cold_start_altitude = 110,
          snow_temperature = .22f;
    float mountain_region_scale = 1900, mountain_threshold = .72f, mountain_blend = .18f;
    bool testing_biomes = true;
    bool vegetation = false, surface_shapes = false; // Old saved worlds retain their original surface.
    float upload_time_budget_ms = 2;
    int tree_spacing = 14, tree_lod_max = 2, grass_tufts = 9;
    float grass_height = .38f, grass_height_variation = .50f, grass_tall_chance = .13f,
          grass_tall_multiplier = 1.8f;
    float grass_render_distance = 48, grass_fade_distance = 10, bush_render_distance = 64;
    std::string bush_block = "altaria:leaves";
    std::vector<BiomeDef> biomes = default_biomes();
};
struct Column {
    float height = 0, erosion = 0, ridge = 0, cliff = 0;
    BlockId surface = 0;
    int biome = 0;
    RGB grass_tint;
    float temperature = .6f, mountain = 0;
    float plateau = 0, valley = 0, slope = 0, chain_axis = 0;
    float continentalness = .5f, river = 0, buildable = 0, adventure = 0, terrace_step = 0, arch_mask = 0;
    float lake_mask = 0, beach_mask = 0, river_bank_step = 0, waterfall_mask = 0;
    int water_top = -1000;
    int transition_biome = -1;
    int region = 0; // 0 = buildable_plains, 1 = foothills, 2 = plateau, 3 = valley/river, 4 = mountain_range, 5 = ocean
    float preserve = .65f, brightness = 1.35f, pebbles = .1f;
};
struct BiomeSample {
    int primary = 0, secondary = 0;
    float primary_weight = 1;
    float border_distance = 1000.f, climate_heat = .5f, climate_humidity = .5f;
};
class BiomeStage {
    Settings cfg;
    int seed;
    FastNoise::SmartNode<> temperature, humidity, warp;

  public:
    BiomeStage(const Settings &settings, int seed);
    std::vector<BiomeSample> regions(const std::vector<float> &x, const std::vector<float> &z) const;
};
struct StageTimings {
    double biomes_ms = 0, terrain_ms = 0, surface_ms = 0, mesh_ms = 0;
};
class Generator {
    Settings cfg;
    Registry registry;
    int seed;
    FastNoise::SmartNode<> continent, erosion, ridges, spur_ridges, chain_spine, plateau_noise, detail, warp,
        density, mountain_regions;
    BiomeStage biome_stage;

  public:
    Generator(const Settings &settings, const Registry &blocks, int seed_value);
    const Settings &settings() const {
        return cfg;
    }
    const Registry &blocks() const {
        return registry;
    }
    std::vector<Column> columns(const std::vector<float> &x, const std::vector<float> &z,
                                StageTimings *timings = nullptr) const;
    Column column(int x, int z) const;
    BlockId material(const Column &column, int y, bool exposed) const;
    int seed_value() const {
        return seed;
    }
    BlockId sample(int x, int y, int z) const;
    BlockId terrain_sample(int x, int y, int z) const;
    bool natural_arch_carved(const Column &c, int x, int y, int z) const;
    std::vector<std::pair<IVec3, BlockId>> trees(int minx, int minz, int maxx, int maxz) const;
    uint8_t surface_shape(int x, int y, int z, const Column &c) const;
    void density_samples(std::vector<float> &out, const std::vector<float> &x, const std::vector<float> &y,
                         const std::vector<float> &z) const;
};
struct EditCommand {
    uint64_t actor = 1, sequence = 0;
    IVec3 position;
    BlockId block = 0;
};
struct TileKey;
class WorldModel {
    std::unordered_map<uint64_t, uint64_t> sequences;
    std::shared_ptr<EditMap> edits = std::make_shared<EditMap>();
    std::unordered_map<IVec3, std::vector<IVec3>, CoordHash> edit_regions;
    AppearanceMap appearances;
    std::unordered_map<IVec3, std::vector<IVec3>, CoordHash> surface_regions;

  public:
    std::shared_ptr<const Generator> generator;
    uint64_t revision = 0;
    explicit WorldModel(std::shared_ptr<const Generator> gen) : generator(std::move(gen)) {}
    std::shared_ptr<const EditMap> snapshot() const {
        return edits;
    }
    std::shared_ptr<const EditMap> snapshot_for(const TileKey &tile) const;
    void restore(EditMap data);
    void restore_surfaces(AppearanceMap data);
    Appearance appearance_at(IVec3 position) const;
    const AppearanceMap &all_surfaces() const { return appearances; }
    std::shared_ptr<const AppearanceMap> surfaces_for(const TileKey &tile) const;
    bool validate_surface(const SurfaceCommand &command, std::string &error) const;
    bool apply_surface(const SurfaceCommand &command, std::string &error);
    size_t painted_count() const {
        return appearances.size();
    }
    bool validate(const EditCommand &command, std::string &error) const;
    bool apply(const EditCommand &command, std::string &error);
    BlockId block_at(int x, int y, int z) const;
};
struct RayHit {
    bool hit = false;
    IVec3 cell, previous;
    float distance = 0;
    BlockId block = 0;
};
RayHit raycast(const std::array<float, 3> &origin, const std::array<float, 3> &direction, float reach,
               const std::function<BlockId(int, int, int)> &query, const Registry &registry);
struct TileKey {
    int x = 0, z = 0, level = 0;
    int size() const {
        return 32 << level;
    }
    bool operator==(const TileKey &v) const {
        return x == v.x && z == v.z && level == v.level;
    }
};
struct TileHash {
    size_t operator()(const TileKey &p) const;
};
float tile_distance(const TileKey &tile, float x, float z);
bool overlaps(const TileKey &a, const TileKey &b);
std::vector<TileKey> select_tiles(float x, float z, const Settings &settings);
struct Vertex {
    float x, y, z, nx, ny, nz, u, v, r, g, b;
    float tr = 1, tg = 1, tb = 1, preserve = .35f, brightness = 1;
    float texture = 0, overlay = -1, painted = 0, overlay_tint = 1, decal = 0;
    float wind = 0;
};
struct Surface {
    std::vector<Vertex> vertices;
    std::vector<int32_t> indices;
};
void append_block_surface(Surface &out, std::array<float, 3> position, uint8_t shape, BlockId id,
                          const Appearance &appearance, const Column &column, const Registry &registry,
                          int transition_overlay = 0);
struct Volume {
    int step = 1, y_step = 1, min_y = 0, height = 0, water_surface = 43;
    std::vector<Column> columns;
    std::unordered_map<size_t, Appearance> surfaces;
    std::vector<BlockId> data;
    std::unordered_map<size_t, uint8_t> shapes;
    BlockId at(int x, int y, int z) const;
};
struct GrassInstance {
    float x = 0, y = 0, z = 0, width = .42f, height = .4f, angle = 0;
    RGB tint;
    float preserve = .65f, brightness = 1.35f, phase = 0;
    float top_layer = 3, apply_tint = 1;
};
std::vector<GrassInstance> grass_for_block(std::array<float, 3> origin, uint8_t shape,
                                           const Appearance &appearance, const Column &column,
                                           const Registry &registry, const Settings &settings, int seed);
struct MeshResult {
    TileKey key;
    uint64_t version = 0;
    Surface solid, water, decorations, bushes;
    std::vector<GrassInstance> grass;
    int bush_count = 0;
    bool vegetation_only = false;
    bool foliage_included = false;
    StageTimings stages;
    std::vector<uint8_t> texture;
    int texture_size = 0;
    std::shared_ptr<Volume> volume;
    double milliseconds = 0;
};
MeshResult build_tile(const TileKey &key, uint64_t version, const Generator &generator,
                      std::shared_ptr<const EditMap> edits,
                      std::shared_ptr<const AppearanceMap> surfaces = std::make_shared<AppearanceMap>(),
                      bool include_vegetation = true);
MeshResult build_vegetation(const TileKey &key, uint64_t version, const Generator &generator,
                            const Volume &volume);
class TerrainWorkers {
    struct Job {
        TileKey key;
        uint64_t version;
        std::shared_ptr<const EditMap> edits;
        std::shared_ptr<const AppearanceMap> surfaces;
        std::shared_ptr<const Volume> vegetation_volume;
        bool include_vegetation = false;
    };
    std::shared_ptr<const Generator> generator;
    std::vector<std::thread> threads;
    std::deque<Job> jobs;
    std::deque<MeshResult> completed;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;

  public:
    TerrainWorkers(std::shared_ptr<const Generator> gen, int count);
    ~TerrainWorkers();
    void submit(TileKey key, uint64_t version, std::shared_ptr<const EditMap> edits,
                std::shared_ptr<const AppearanceMap> surfaces = std::make_shared<AppearanceMap>(),
                bool include_vegetation = false);
    void submit_vegetation(TileKey key, uint64_t version, std::shared_ptr<const Volume> volume);
    bool pop(MeshResult &result);
    size_t queued();
};
} // namespace altaria
