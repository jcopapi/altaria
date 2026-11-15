#pragma once
#include "voxel_core.h"
#include "sections.h"
#include "liquid/liquid_system.h"
#include <chrono>
#include <future>
#include <optional>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <limits>
#include <deque>
#include <unordered_set>
namespace godot {
inline constexpr uint32_t TERRAIN_SKY_SHADOW_LAYER = 1u << 19;
class VoxelWorld : public Node3D {
    GDCLASS(VoxelWorld, Node3D)
    struct Tile {
        MeshInstance3D *node = nullptr;
        MeshInstance3D *leaf_shadow = nullptr, *sky_shadow = nullptr, *leaf_cards = nullptr;
        double retiring_since = -1;
        float opacity = 1.f;
        std::shared_ptr<const altaria::TerrainEdges> edges;
        Vector4i seam_edges;
        bool ground_visible = false;
        Ref<ShaderMaterial> material;
        std::shared_ptr<altaria::Volume> volume;
        uint64_t version = 0;
        altaria::VerticalWindow window;
        int triangles = 0, collision_triangles = 0, decoration_quads = 0, overlay_quads = 0, grass_quads = 0, grass_core = 0;
        StaticBody3D *collider = nullptr;
        std::vector<altaria::CollisionRect> collision_rects;
        std::vector<std::array<float,3>> partial_collision_faces;
        MultiMeshInstance3D *grass_node = nullptr;
        MeshInstance3D *bush_node = nullptr;
        uint64_t vegetation_version = std::numeric_limits<uint64_t>::max();
        int bush_count = 0, bush_triangles = 0;
        AABB grass_bounds, bush_bounds;
        // Full-resolution tiles draw their ground as stacked 32 m sections.
        bool lights_visible = false;
        std::vector<MeshInstance3D *> sections;
        std::vector<int> section_bands;
        std::vector<MultiMeshInstance3D *> deposit_batches;
    };
    std::unordered_map<altaria::TileKey, Tile, altaria::TileHash> tiles;
  public:
    struct DepositModel {
        struct Part {
            Ref<Mesh> mesh;
            Transform3D transform;
        };
        std::vector<Part> parts;
        AABB bounds;
    };

  private:
    std::unordered_map<altaria::BlockId, DepositModel> deposit_models;
    bool load_deposit_model(const altaria::BlockDef &block, String &error);
    void upload_deposits(Tile &tile, const std::vector<altaria::ResourcePiece> &pieces);
    std::unordered_map<altaria::TileKey, std::shared_ptr<const altaria::ResourceDepletions>, altaria::TileHash> depleted_resources;
    std::shared_ptr<const altaria::ResourceDepletions> resource_snapshot(altaria::TileKey key) const;
    std::unordered_map<altaria::TileKey, uint64_t, altaria::TileHash> pending, versions, vegetation_pending;
    std::unordered_set<altaria::TileKey, altaria::TileHash> wanted;
    std::unordered_set<altaria::TileKey, altaria::TileHash> collider_requests;
    std::deque<MeshInstance3D *> retired_nodes;
    // Packed surfaces collected for one mesh; finish() creates it.
    struct MeshBuild {
        Array surfaces;
        Ref<ArrayMesh> finish() const;
    };
    std::vector<altaria::TileKey> ordered;
    std::unique_ptr<altaria::TerrainWorkers> workers;
    std::unique_ptr<altaria::WorldModel> model;
    altaria::LiquidSimulation liquid_sim;
    uint64_t liquid_sequence=0;
    Ref<Texture2DArray> texture_array;
    Ref<ShaderMaterial> overlay_material;
    Ref<ShaderMaterial> grass_material, bush_material;
    Ref<ArrayMesh> grass_template_mesh;
    altaria::StageTimings stages;
    std::function<void(altaria::IVec3)> edit_callback;
    std::unordered_map<altaria::IVec3,uint8_t,altaria::CoordHash> ramp_connections;
    std::unordered_map<altaria::IVec3,uint8_t,altaria::CoordHash> ramp_base_shapes;
    void invalidate(altaria::IVec3 position,bool notify=true,bool wake_liquids=true);
    std::shared_ptr<const altaria::AppearanceMap> render_surfaces(const altaria::TileKey &key) const;
    Ref<ShaderMaterial> solid_material, physical_material, water_material, lava_material, leaf_material;
    Vector3 viewer, last_viewer, planned_viewer, travel_velocity;
    Vector3 current_sky_fill;
    bool viewer_initialized=false, underground_stream=false;
    static constexpr int cave_vertical_half_span = 128;
    int cave_band=0, shallow_band=8192;
    float fog_range=0, stream_radius=0;
    Vector2i height_sample_key{INT32_MIN,INT32_MIN};
    float local_ground=0, cave_light_range=0;
    struct DebrisDemand {
        double expires;
        int floor, ceiling;
    };
    std::unordered_map<altaria::TileKey,DebrisDemand,altaria::TileHash> debris_demand;
    double stream_clock=0;
    int64_t cancelled_jobs=0, discarded_jobs=0;
    altaria::VerticalWindow window_for(const altaria::TileKey &key) const;
    void plan_stream(float delta);
    void process_structure(float delta);
    std::unordered_set<altaria::TileKey, altaria::TileHash> structure_dirty;
    bool has_open_roof() const;
    bool fullbright = false;
    bool retirement_pending=false, lod_fading=false;
    bool seams_dirty = true;
    int worker_count = 0;
    altaria::StageTimings first_tile_stages;
    double first_tile_build_ms = 0, configure_ms = 0;
    struct GenerationStats {
        uint64_t tiles = 0, vertices = 0;
        double total_ms = 0, biomes_ms = 0, terrain_ms = 0, caves_ms = 0, surface_ms = 0, mesh_ms = 0;
    };
    std::array<GenerationStats, 6> generation_stats{};
    MeshInstance3D *terrain_seams = nullptr;
    struct SeamTile {
        altaria::TileKey key;
        std::shared_ptr<const altaria::TerrainEdges> edges;
    };
    struct SeamResult {
        std::unordered_map<altaria::TileKey, Vector4i, altaria::TileHash> masks;
        Dictionary opaque, lava, water;
    };
    static SeamResult compute_seams(std::vector<SeamTile> snapshot);
    std::future<SeamResult> seam_task;
    void update_seams();
    void update_lights();
    static constexpr float light_radius = 72.f;
    float light_timer = 0;
    float stream_timer = 1, last_mesh_ms = 0;
    std::chrono::steady_clock::time_point started;
    double first_tile_ms = -1, world_ready_ms = -1;
    int foliage_atomic_swaps = 0;
    float last_upload_ms=0, max_upload_ms=0;
    float max_mesh_upload_ms=0, max_collider_ms=0, max_foliage_upload_ms=0;
    // A finished tile that did not fit this frame's vertex budget.
    std::optional<altaria::MeshResult> held_result;
    size_t last_upload_vertices=0, max_upload_vertices=0, deferred_uploads=0;
    static constexpr float frame_spike_ms=33.4f; // Below 30 fps.
    std::array<float,600> frame_times{};
    size_t frame_cursor=0, frame_count=0, frame_spikes=0;
    static size_t upload_vertices(const altaria::MeshResult &result);
    struct CanopyUpload {
        altaria::TileKey key;
        uint64_t version = 0;
        altaria::Surface leaf_shadows, leaf_cards, sky_shadows;
        std::shared_ptr<void> prepared;
    };
    std::deque<CanopyUpload> canopy_queue;
    void apply_canopy(Tile &tile, CanopyUpload &canopy);
    void upload_canopy();
    void record_frame(float milliseconds);
    void frame_stats(float &worst, float &p99, float &average) const;
    void upload(altaria::MeshResult result);
    void add_surface(MeshBuild &mesh,const altaria::Surface &surface,const Ref<Material> &material) const;
    struct PreparedTile;
    static void prepare_upload(altaria::MeshResult &result);
    template <class F> static void for_meshes(Tile &tile, F &&apply) {
        if (tile.node)
            apply(tile.node);
        for (auto *section : tile.sections)
            apply(section);
    }
    static const PreparedTile *prepared_of(const altaria::MeshResult &result);
    void add_arrays(MeshBuild &mesh, const Dictionary &packed, const Ref<Material> &material) const;
    void add_liquid_surfaces(MeshBuild &mesh, const altaria::Surface &surface,
                             const PreparedTile *prepared = nullptr) const;
    bool build_requested_collider();
    void reconcile();
    void upload_vegetation(altaria::MeshResult result);
    void clear_vegetation(Tile &tile);
    bool needs_vegetation(const altaria::TileKey &key, const Tile &tile) const;
    void update_vegetation();

  protected:
    static void _bind_methods() {}

  public:
    const std::unordered_map<altaria::BlockId, DepositModel> &get_deposit_models() const { return deposit_models; }
    void set_edit_callback(std::function<void(altaria::IVec3)> callback) {edit_callback=std::move(callback);}
    ~VoxelWorld();
    bool configure(const altaria::Settings &settings, const altaria::Registry &blocks, int seed,
                   const String &journal_path, String &error);
    void _process(double delta) override;
    void update_viewer(Vector3 value) {
        if(model) {
            const float distance=settings().adaptive_streaming ? 64.f : settings().view_distance*.5f;
            const Vector3 shift=value-viewer;
            if((settings().adaptive_streaming && std::abs(shift.y)>64.f) ||
               shift.x*shift.x+shift.z*shift.z>distance*distance)
                stream_timer=.3f;
        }
        viewer = value;
    }
    Vector3 viewer_position() const {
        return viewer;
    }
    altaria::BlockId block_at(int x, int y, int z) const;
    std::optional<altaria::ResourcePiece> pick_resource(Vector3 origin, Vector3 direction, float &distance) const;
    void mine_resource(const altaria::ResourcePiece &piece);
    bool collision_ready(Vector3 position) const;
    bool voxels_ready(Vector3 position) const;
    bool debris_collision_ready(const AABB &bounds);
    bool voxel_ready(int x,int y,int z) const;
    bool cave_liquid_at(int x,int y,int z) const;
    const altaria::caves::HydroPlan *cave_hydrology_at(int x,int z) const;
    Vector3 cave_river_at(Vector3 position) const;
    std::shared_ptr<const altaria::caves::IndexedNetwork> cave_plan_at(altaria::caves::Region region) const;
    uint8_t shape_at(int x, int y, int z) const;
    uint8_t base_shape_at(int x,int y,int z) const;
    uint8_t natural_shape_at(int x,int y,int z) const;
    void set_ramp_connection(altaria::IVec3 position,uint8_t shape);
    bool collides_box(Vector3 minimum, Vector3 maximum) const;
    bool in_foliage_box(Vector3 minimum, Vector3 maximum) const;
    void set_player_foliage_fade(Vector3 center, float opacity);
    altaria::LiquidSimulation &liquids() { return liquid_sim; }
    const altaria::LiquidSimulation &liquids() const { return liquid_sim; }
    Vector3 liquid_current_box(Vector3 minimum, Vector3 maximum, bool &in_liquid) const;
    void probe_liquid_box(Vector3 minimum, Vector3 maximum, bool &in_water, bool &in_lava,
                          float &submergence, float &surface_y, bool &has_surface, Vector3 &current) const;
    MultiMeshInstance3D *grass_batch(const std::vector<altaria::GrassInstance> &plants, Node3D *parent,
                                     AABB &bounds);
    Ref<ArrayMesh> surface_mesh(const altaria::Surface &surface) const;
    Ref<ArrayMesh> block_mesh(const std::vector<std::pair<altaria::IVec3, altaria::BlockId>> &parts,
                              const std::vector<altaria::Appearance> &appearances,
                              const altaria::Column &column, bool physical = false) const;
    void set_visual_environment(float wind_time, float wind_strength, float noise_scale,
                                float noise_strength, float fog_end);
    void set_wind_time(float time);
    void set_sky_fill(Vector3 fill);
    // Last fill pushed to the world materials; creatures reuse it so their shadows match.
    Vector3 sky_fill() const { return current_sky_fill; }
    void set_grass_hit(const Vector3 &box_min, const Vector3 &box_max, float flash,
                       const Vector3 &root_shift, const Vector3 &blade_scale);
    void set_terrain_hit(const Vector3 &box_min, const Vector3 &box_max, float flash);
    bool edit(const altaria::EditCommand &command, String &error);
    bool edit_surface(const altaria::SurfaceCommand &command, String &error);
    altaria::Appearance appearance_at(altaria::IVec3 p) const;
    const altaria::AppearanceMap *all_surfaces() const { return model ? &model->all_surfaces() : nullptr; }
    int last_seed() const {
        return model->generator->seed_value();
    }
    void set_fullbright(bool value);
    void set_cave_light_range(float range) { cave_light_range = range; }
    // Underground, sight ends where light does: fog closes at this distance and
    // full-resolution streaming stops a little beyond it.
    float underground_sight() const { return std::clamp(std::max(56.f, cave_light_range), 56.f, 160.f); }
    // Inside the open underworld layer (a hot haze replaces the cave darkness).
    bool in_underworld() const {
        const auto *noise = model ? generator()->noise_caves() : nullptr;
        return underground_stream && noise &&
               viewer.y < float(noise->settings().hell_ceiling) + noise->settings().hell_ceiling_amplitude + 6.f;
    }
    bool surface_in_streamed_height() const {
        return !underground_stream || local_ground <= cave_band + cave_vertical_half_span;
    }
    bool is_underground_streaming() const { return underground_stream; }
    float surface_ground_height() const { return local_ground; }
    float sky_exposure_at(Vector3 world_pos) const;
    bool initial_stream_ready() const {
        return world_ready_ms >= 0 || (settings().preset == altaria::WorldPreset::Plain && collision_ready(viewer));
    }
    bool is_fullbright() const {
        return fullbright;
    }
    Dictionary stats() const;
    bool get_tile_info(int x, int z, int level, Dictionary &info) const;
    Array get_loaded_tile_keys() const;
    size_t loaded_tile_count() const { return tiles.size(); }
    bool has_loaded_tile_at(int wx, int wz) const {
        if (settings().preset == altaria::WorldPreset::Structure)
            return true;
        for (int level = 0; level <= 3; ++level) {
            const int sz = 32 << level;
            const int tx = int(std::floor(double(wx) / sz)) * sz;
            const int tz = int(std::floor(double(wz) / sz)) * sz;
            auto it = tiles.find(altaria::TileKey{tx, tz, level});
            if (it != tiles.end() &&
                (it->second.node != nullptr || it->second.volume != nullptr || !it->second.sections.empty()))
                return true;
        }
        return false;
    }
    bool reload_tile(int x, int z, int level = 0);
    Dictionary get_chunk_queue_info() const;
    const altaria::Generator *generator() const {
        return model ? model->generator.get() : nullptr;
    }
    const altaria::Registry &registry() const {
        return model->generator->blocks();
    }
    const altaria::Settings &settings() const {
        return model->generator->settings();
    }
    uint64_t world_revision() const {
        return model ? model->revision : 0;
    }
    std::shared_ptr<const altaria::EditMap> edits_snapshot() const {
        return model ? model->snapshot() : nullptr;
    }
};
} // namespace godot
