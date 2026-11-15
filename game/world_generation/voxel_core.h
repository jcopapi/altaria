#pragma once
#include <FastNoise/FastNoise.h>
#include "caves/cave_runtime.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace altaria {
using BlockId = uint16_t;
inline constexpr int skylight_reach = 14;
inline constexpr int hellstone_y = -420;
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
RGB grass_on_support(RGB grass, RGB support);
struct ResourceRule {
    std::string block;
    std::vector<std::string> hosts;
    RGB tint;
    float min_depth = 0, max_depth = 0, chance = 0, size = .2f;
    int pieces = 1;
    bool all_faces = false;
    float embed = 0.f;
    bool irregular = false, outline = true;
};
struct ResourcePiece {
    IVec3 support;
    std::array<float, 3> position;
    std::array<float, 3> dimensions;
    std::array<std::array<float, 3>, 3> axes;
    IVec3 exposed_face;
    BlockId block;
    uint8_t slot;
};
using ResourceDepletions = std::unordered_map<IVec3, uint32_t, CoordHash>;
struct BlockDef {
    BlockId id = 0;
    std::string key, name, name_pt;
    std::string deposit_model;
    RGB color;
    bool solid = true, water = false, placeable = true, collectible = true, instant_break = false;
    bool liquid = false, lava = false;
    int texture_layer = 0, texture_end_layer = -1, overlay = 0, transition_overlay = 0;
    float thermal_delta = 0, thermal_radius = 0, mass = 20;
    bool wood = false, foliage = false, tintable = false, alpha_cutout = false;
    bool root_plant = false;
    bool distant_outline = true, grass_on_rock = false, rock_variation = false;
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
    float temperature = .6f, humidity = .5f, terrain_multiplier = 1, mountain_multiplier = 1, pebbles = 0;
    float preserve_saturation = .65f, brightness = 1.35f;
    bool desert = false, snow = false, volcanic = false;
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
    BlockId air = 0, stone = 1, dirt = 2, grass = 3, sand = 4, water = 5, snow = 6, bedrock = 7, lava = 0, rock_grass = 0, cave_grass = 0;
    bool add(BlockDef def, std::string &error);
    bool valid(BlockId id) const { return id < blocks.size() && !blocks[id].key.empty(); }
    const BlockDef &get(BlockId id) const { return blocks[valid(id) ? id : air]; }
    BlockId find(const std::string &key) const;
};
enum class WorldPreset { Default, Plain, Structure };
struct Settings {
    caves::Config caves;
    std::vector<ResourceRule> resources;
    bool generate_resource_deposits = true;
    WorldPreset preset = WorldPreset::Default;
    int generator_version = 2, min_y = -64, max_y = 320, sea_level = 42;
    float base_height = 46, continental_amplitude = 12, mountain_amplitude = 210;
    float continental_scale = 1800, mountain_scale = 480, erosion_scale = 710;
    float detail_scale = 48, detail_amplitude = 4, warp_scale = 310, warp_amplitude = 55;
    float valley_depth = 20, valley_width = .11f, cliff_strength = 14, terrace_strength = .24f;
    float ocean_depth = 56, river_scale = 2300, river_width = .026f, beach_height = 2;
    float meadow_hill_amplitude = 0;
    // Distinct bell-shaped hills on lowlands (0 = off; old worlds keep their relief).
    float hill_spacing = 64, hill_chance = 0, hill_height_min = 5, hill_height_max = 22;
    float hill_width_min = 1.1f, hill_width_max = 2.0f;
    float plains_threshold = .55f, snow_height = 185;
    float density_scale = 43, terrace_height = 18, ridge_power = 1.45f;
    int octaves = 5, workers = 3, upload_budget = 2, max_pending = 24;
    float near_distance = 96, middle_distance = 224, coarse_distance = 480, view_distance = 6144;
    float autosave_seconds = 15;
    float distant_distance = 1100, horizon_distance = 2600, fog_start = 4500;
    float biome_scale = 520, biome_blend = 130, altitude_cooling = .004f, cold_start_altitude = 110,
          snow_temperature = .22f;
    float mountain_region_scale = 1900, mountain_threshold = .68f, mountain_blend = .20f;
    bool testing_biomes = true;
    bool vegetation = false, surface_shapes = false; // Old saved worlds retain their original surface.
    bool smooth_lowland_relief = false;
    bool deeper_waterbeds = false, mixed_sediments = false, mountain_ledges = false;
    bool far_vertical_trim=false, adaptive_streaming=false;
    float upload_time_budget_ms = 2;
    // Vertices handed to the GPU per frame; a tile above it waits for the next
    // frame unless it is the first upload of that frame.
    int upload_vertex_budget = 160000;
    int tree_spacing = 14, tree_lod_max = 2, grass_tufts = 9;
    int far_heightfield_lod = 3; // LOD from which plain far tiles mesh as a heightfield (0 = never).
    float grass_height = .68f, grass_height_variation = .35f, grass_tall_chance = .13f,
          grass_tall_multiplier = 1.8f;
    float grass_render_distance = 48, grass_fade_distance = 10, bush_render_distance = 64;
    std::string bush_block = "altaria:leaves";
    std::string single_biome;
    std::vector<BiomeDef> biomes = default_biomes();
};
struct Column {
    caves::BiomeColumn underground;
    float height = 0, erosion = 0, ridge = 0, cliff = 0;
    BlockId surface = 0;
    int biome = 0;
    RGB grass_tint;
    float temperature = .6f, mountain = 0;
    float day_c = 24, night_c = 10;
    float plateau = 0, valley = 0, slope = 0, chain_axis = 0;
    float continentalness = .5f, river = 0, buildable = 0, adventure = 0, terrace_step = 0, arch_mask = 0;
    float lake_mask = 0, beach_mask = 0, river_bank_step = 0, waterfall_mask = 0;
    float river_flow_x = 0, river_flow_z = 0;
    int water_top = -1000;
    int sediment_depth = 0;
    int transition_biome = -1;
    int region = 0; // 0 = buildable_plains, 1 = foothills, 2 = plateau, 3 = valley/river, 4 = mountain_range, 5 = ocean
    float preserve = .65f, brightness = 1.35f, pebbles = .1f;
};
// Where a new default world places the player: flat dry plains in a
// temperate biome with both cold and hot regions within reach.
struct SpawnPoint {
    float x = .5f, z = .5f, height = 0;
    bool found = false;
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
    double biomes_ms = 0, terrain_ms = 0, surface_ms = 0, mesh_ms = 0, cave_plan_ms = 0, cave_field_ms = 0, cave_biome_ms = 0;
};
enum class TreeForm { Round, Spire, Open, Broad, Shrub };
struct TreeSite {
    IVec3 base;
    int height, radius, branch_axis;
    Appearance canopy;
    TreeForm form;
    uint64_t pattern;
    int crown_depth() const;
};
struct TreePart {
    IVec3 first;
    BlockId second;
    uint8_t shape;
    Appearance appearance;
    bool operator==(const TreePart &other) const {
        return first == other.first && second == other.second && shape == other.shape && appearance == other.appearance;
    }
};
class Generator {
    Settings cfg;
    Registry registry;
    int seed;
    FastNoise::SmartNode<> continent, erosion, ridges, spur_ridges, chain_spine, plateau_noise, detail, warp,
        density, mountain_regions, drainage, meadow_relief, waterbed_relief, sediment_patches,
        snow_patches;
    BiomeStage biome_stage;
    std::unique_ptr<caves::Runtime> cave_runtime;
    std::unique_ptr<caves::BiomeStage> cave_biomes;
    std::unique_ptr<caves::NoiseField> cave_noise;
    std::string generation_error;
    caves::Outlet cave_outlet(const caves::Passage &opening) const;
    std::vector<caves::Formation> cave_formations(const caves::Network &network) const;
    bool density_solid(const Column &c,int x,int y,int z) const;
    BlockId id_basalt = 1, id_limestone = 1, id_sandstone = 1, id_moss_stone = 1,
            id_crystal_stone = 1, id_fossil_limestone = 1, id_wet_stone = 1, id_gravel = 1, id_clay = 1, id_hellstone = 1;

  public:
    Generator(const Settings &settings, const Registry &blocks, int seed_value);
    Generator(const Generator &) = delete;
    Generator &operator=(const Generator &) = delete;
    const caves::Runtime *underground() const {return cave_runtime.get();}
    const caves::BiomeStage *underground_biomes() const {return cave_biomes.get();}
    const caves::NoiseField *noise_caves() const {return cave_noise.get();}
    struct CaveCell {bool carved=false;caves::Fluid fluid=caves::Fluid::dry;BlockId decoration=0;};
    // Graph chambers and passages first, then the organic noise caves. The
    // host cell must already be natural rock (density solid, no arch).
    CaveCell cave_cell(const Column &column,int x,int y,int z) const;
    // 0 open dry cave air, 1 natural rock, 2 anything else.
    int cave_probe(const Column &column,int x,int y,int z) const;
    // Dripstone and thin columns grown in open cave air; shape receives the
    // Pillar/MiniPillar form. Returns 0 when the cell stays open.
    BlockId cave_spike(const Column &column,int x,int y,int z,uint8_t *shape=nullptr) const;
    bool valid_generation(std::string &error) const {error=generation_error;return error.empty();}
    const Settings &settings() const {
        return cfg;
    }
    const Registry &blocks() const {
        return registry;
    }
    std::vector<Column> columns(const std::vector<float> &x, const std::vector<float> &z,
                                StageTimings *timings = nullptr,bool include_underground=true) const;
    Column column(int x, int z) const;
    SpawnPoint find_spawn() const;
    BlockId geological_rock(const Column &column, int x, int y, int z) const;
    BlockId material(const Column &column,int x,int y,int z,bool exposed) const;
    BlockId base_rock(int y) const;
    BlockId rock_material(const Column &column,int x,int y,int z,bool water=false) const;
    int seed_value() const {
        return seed;
    }
    BlockId sample(int x, int y, int z) const;
    BlockId terrain_sample(int x, int y, int z) const;
    // spikes=false skips dripstone (a 48-cell scan); for coarse opacity queries.
    BlockId terrain_sample(const Column &column, int x, int y, int z, bool spikes = true) const;
    bool natural_arch_carved(const Column &c, int x, int y, int z) const;
    // Natural rock before caves: inside the terrain and outside any arch.
    bool host_rock(const Column &c, int x, int y, int z) const {
        return y == cfg.min_y || (y > cfg.min_y && y < cfg.max_y && density_solid(c, x, y, z) && !natural_arch_carved(c, x, y, z));
    }
    std::pair<float, float> river_current(int x, int z) const;
    std::vector<TreeSite> tree_sites(int minx, int minz, int maxx, int maxz, bool check_caves) const;
    std::vector<TreeSite> shrub_sites(int minx, int minz, int maxx, int maxz, bool check_caves) const;
    std::vector<TreePart> tree_parts(const TreeSite &site) const;
    std::vector<TreePart> trees(int minx, int minz, int maxx, int maxz) const;
    bool rock_grass_at(const Column &column, int x, int y, int z, bool cave) const;
    uint8_t surface_shape(int x, int y, int z, const Column &c) const;
    void density_samples(std::vector<float> &out, const std::vector<float> &x, const std::vector<float> &y,
                         const std::vector<float> &z) const;
  private:
    std::vector<TreeSite> plant_sites(int minx, int minz, int maxx, int maxz, bool check_caves, bool shrubs) const;
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
    bool validate_surface(const SurfaceCommand &command, std::string &error,
                          std::optional<BlockId> support = {}) const;
    bool apply_surface(const SurfaceCommand &command, std::string &error,
                       std::optional<BlockId> support = {});
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
struct StreamDemand {
    float x, z, ahead_x, ahead_z, radius;
    bool underground;
    const std::vector<TileKey> *required_detail=nullptr;
};
std::vector<TileKey> select_tiles(const StreamDemand &demand, const Settings &settings);
struct VerticalWindow {
    enum Mode { full, surface, underground } mode = full;
    int floor = 8192, ceiling = 8192;
    bool operator==(const VerticalWindow &other) const {
        return mode == other.mode && floor == other.floor && ceiling == other.ceiling;
    }
};
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
                          int transition_overlay = 0, bool foliage_cards = true);
struct Volume {
    std::vector<ResourcePiece> resources;
    bool has_root_plants = false;
    struct RiverSegment {caves::Point from,to;float radius;};
    bool unknown_below=false, unknown_above=false;
    BlockId bottom_fill=0; // Uniform rock below a compact volume.
    std::vector<std::shared_ptr<const caves::IndexedNetwork>> cave_plans;
    std::vector<caves::HydroPlan> cave_basins;
    std::vector<bool> static_cave_liquid;
    std::vector<RiverSegment> cave_rivers;
    int cave_basin_span=0;
    int step = 1, y_step = 1, min_y = 0, height = 0, water_surface = 43;
    std::vector<Column> columns;
    std::array<int16_t, 34 * 34> sky_floor{};
    std::unordered_map<size_t, uint8_t> indirect_sky;
    std::unordered_map<size_t, Appearance> surfaces;
    std::vector<BlockId> data;
    std::unordered_map<size_t, uint8_t> shapes;
    // Retained near volumes are compressed after meshing: a layer of one
    // material is stored as that id alone, the rest as packed 34x34 slices.
    std::vector<BlockId> packed;
    std::vector<int32_t> layer_slot; // >=0: packed slice index; <0: -(id+1)
    bool compressed = false;
    void compress();
    size_t voxel_bytes() const;
    inline BlockId at(int x, int y, int z) const;
    bool cave_liquid_at(int x,int y,int z) const;
    uint8_t sky_exposure(int x, int y, int z) const;
};
inline BlockId Volume::at(int x, int y, int z) const {
    if ((data.empty() && !compressed) || x < -1 || x > 32 || z < -1 || z > 32 || y < -1 || y > height)
        return 0;
    if(y==-1 && bottom_fill)return bottom_fill;
    if (compressed) {
        const int32_t slot = layer_slot[size_t(y + 1)];
        return slot < 0 ? BlockId(-slot - 1) : packed[size_t(slot) * 1156 + size_t((z + 1) * 34 + x + 1)];
    }
    return data[((z + 1) * (height + 2) + (y + 1)) * 34 + (x + 1)];
}
struct GrassInstance {
    float x = 0, y = 0, z = 0, width = .42f, height = .4f, angle = 0;
    float lean_x = 0, lean_z = 0;
    RGB tint;
    float preserve = .65f, brightness = 1.35f, phase = 0;
    float top_layer = 3, apply_tint = 1;
    uint8_t sky_exposure = 0;
};
std::optional<GrassInstance> grass_for_block(std::array<float, 3> origin, uint8_t shape, const Appearance &appearance,
                                             const Column &column, const BlockDef &block, const Settings &settings,
                                             int seed, int tuft);
struct CollisionRect {
    std::array<int16_t,3> corner{};
    uint16_t width=0,height=0;
    uint8_t axis=0;
    bool positive=false;
    std::array<std::array<float,3>,6> faces() const {
        std::array<std::array<float,3>,4> p;
        p.fill({float(corner[0]),float(corner[1]),float(corner[2])});
        int u=(axis+1)%3,w=(axis+2)%3;
        p[1][u]+=width;p[2][u]+=width;p[2][w]+=height;p[3][w]+=height;
        constexpr int cw[]={0,2,1,0,3,2},ccw[]={0,1,2,0,2,3};
        const int *order=positive?cw:ccw;
        return {p[order[0]],p[order[1]],p[order[2]],p[order[3]],p[order[4]],p[order[5]]};
    }
};
struct TerrainEdgeSpan {
    float bottom = 0, top = 0;
    bool solid = false;
    Vertex material{};
    std::optional<float> end_top;
};
struct TerrainEdgeColumn {
    float floor = 0;
    std::vector<TerrainEdgeSpan> spans;
};
using TerrainEdges = std::array<std::array<TerrainEdgeColumn, 32>, 4>;
void append_terrain_seam(Surface &surface, int axis, float boundary, float from, float to,
                         const TerrainEdgeColumn &first, const TerrainEdgeColumn &second);
struct MeshResult {
    int volume_min_y=0,volume_height=0;
    size_t voxel_bytes=0;
    TileKey key;
    VerticalWindow window;
    uint64_t version = 0;
    // leaf_cards: canopy cards, drawn without casting shadows (leaf_shadows does that).
    Surface solid, water, decorations, bushes, leaf_shadows, sky_shadows, leaf_cards;
    TerrainEdges edges;
    std::vector<CollisionRect> collision_rects;
    std::vector<std::array<float,3>> partial_collision_faces;
    std::vector<GrassInstance> grass;
    // Glow anchors over lava pools (tile-local x, y, z, pool area in m²).
    std::vector<std::array<float, 4>> lava_lights;
    std::vector<std::array<float, 3>> root_lights;
    int bush_count = 0;
    int grass_core = 0; // Grass instances drawn at distance (a prefix of grass).
    bool vegetation_only = false;
    bool foliage_included = false;
    StageTimings stages;
    std::vector<uint8_t> texture;
    int texture_size = 0;
    std::shared_ptr<Volume> volume;
    std::shared_ptr<const Volume> vegetation_source;
    // Engine-side vertex buffers, filled on the worker thread by the
    // TerrainWorkers finish hook so the main thread only hands them over.
    std::shared_ptr<void> prepared;
    double milliseconds = 0;
};
MeshResult build_tile(const TileKey &key, uint64_t version, const Generator &generator,
                      std::shared_ptr<const EditMap> edits,
                      std::shared_ptr<const AppearanceMap> surfaces = std::make_shared<AppearanceMap>(),
                      bool include_vegetation = true, VerticalWindow window = {},
                      std::shared_ptr<const ResourceDepletions> depleted = {});
MeshResult build_vegetation(const TileKey &key, uint64_t version, const Generator &generator,
                            const Volume &volume);
class TerrainWorkers {
    struct Job {
        TileKey key;
        uint64_t version;
        std::shared_ptr<const EditMap> edits;
        std::shared_ptr<const AppearanceMap> surfaces;
        std::shared_ptr<const Volume> vegetation_volume;
        std::shared_ptr<const ResourceDepletions> depleted;
        bool include_vegetation = false;
        VerticalWindow window;
    };
    std::shared_ptr<const Generator> generator;
    std::vector<std::thread> threads;
    std::deque<Job> jobs;
    std::deque<MeshResult> completed;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    std::function<void(MeshResult &)> finish;

  public:
    // finish runs on the worker thread after each tile is built.
    TerrainWorkers(std::shared_ptr<const Generator> gen, int count,
                   std::function<void(MeshResult &)> finish = {});
    ~TerrainWorkers();
    void submit(TileKey key, uint64_t version, std::shared_ptr<const EditMap> edits,
                std::shared_ptr<const AppearanceMap> surfaces = std::make_shared<AppearanceMap>(),
                bool include_vegetation = false, VerticalWindow window = {},
                std::shared_ptr<const ResourceDepletions> depleted = {});
    size_t cancel_queued(const std::function<bool(TileKey, uint64_t, bool, VerticalWindow)> &cancel);
    void submit_vegetation(TileKey key, uint64_t version, std::shared_ptr<const Volume> volume);
    bool pop(MeshResult &result);
    size_t queued();
};
} // namespace altaria
