#pragma once
#include "voxel_core.h"
#include <chrono>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <limits>
#include <unordered_set>
namespace godot {
class VoxelWorld : public Node3D {
    GDCLASS(VoxelWorld, Node3D)
    struct Tile {
        MeshInstance3D *node = nullptr;
        Ref<ShaderMaterial> material;
        std::shared_ptr<altaria::Volume> volume;
        uint64_t version = 0;
        int triangles = 0, decoration_quads = 0, overlay_quads = 0, grass_quads = 0;
        StaticBody3D *collider = nullptr;
        MultiMeshInstance3D *grass_node = nullptr;
        MeshInstance3D *bush_node = nullptr;
        uint64_t vegetation_version = std::numeric_limits<uint64_t>::max();
        int bush_count = 0, bush_triangles = 0;
        AABB grass_bounds, bush_bounds;
    };
    std::unordered_map<altaria::TileKey, Tile, altaria::TileHash> tiles;
    std::unordered_map<altaria::TileKey, uint64_t, altaria::TileHash> pending, versions, vegetation_pending;
    std::unordered_set<altaria::TileKey, altaria::TileHash> wanted;
    std::vector<altaria::TileKey> ordered;
    std::unique_ptr<altaria::TerrainWorkers> workers;
    std::unique_ptr<altaria::WorldModel> model;
    Ref<Texture2DArray> texture_array;
    Ref<ShaderMaterial> overlay_material;
    Ref<ShaderMaterial> grass_material, bush_material;
    Ref<ArrayMesh> grass_template_mesh;
    altaria::StageTimings stages;
    std::function<void(altaria::IVec3)> edit_callback;
    std::unordered_map<altaria::IVec3,uint8_t,altaria::CoordHash> ramp_connections;
    std::unordered_map<altaria::IVec3,uint8_t,altaria::CoordHash> ramp_base_shapes;
    void invalidate(altaria::IVec3 position,bool notify=true);
    std::shared_ptr<const altaria::AppearanceMap> render_surfaces(const altaria::TileKey &key) const;
    Ref<ShaderMaterial> solid_material, physical_material;
    Vector3 viewer;
    bool fullbright = false;
    bool retirement_pending=false;
    float stream_timer = 1, last_mesh_ms = 0;
    std::chrono::steady_clock::time_point started;
    double first_tile_ms = -1, world_ready_ms = -1;
    int foliage_atomic_swaps = 0;
    float last_upload_ms=0, max_upload_ms=0;
    float max_mesh_upload_ms=0, max_collider_ms=0, max_foliage_upload_ms=0;
    void upload(altaria::MeshResult result);
    void reconcile();
    void upload_vegetation(altaria::MeshResult result);
    void clear_vegetation(Tile &tile);
    bool needs_vegetation(const altaria::TileKey &key, const Tile &tile) const;
    void update_vegetation();

  protected:
    static void _bind_methods() {}

  public:
    void set_edit_callback(std::function<void(altaria::IVec3)> callback) {edit_callback=std::move(callback);}
    ~VoxelWorld();
    bool configure(const altaria::Settings &settings, const altaria::Registry &blocks, int seed,
                   const String &journal_path, String &error);
    void _process(double delta) override;
    void update_viewer(Vector3 value) {
        viewer = value;
    }
    Vector3 viewer_position() const {
        return viewer;
    }
    altaria::BlockId block_at(int x, int y, int z) const;
    bool collision_ready(Vector3 position) const;
    uint8_t shape_at(int x, int y, int z) const;
    uint8_t base_shape_at(int x,int y,int z) const;
    uint8_t natural_shape_at(int x,int y,int z) const;
    void set_ramp_connection(altaria::IVec3 position,uint8_t shape);
    bool collides_box(Vector3 minimum, Vector3 maximum) const;
    MultiMeshInstance3D *grass_batch(const std::vector<altaria::GrassInstance> &plants, Node3D *parent,
                                     AABB &bounds);
    Ref<ArrayMesh> surface_mesh(const altaria::Surface &surface) const;
    Ref<ArrayMesh> block_mesh(const std::vector<std::pair<altaria::IVec3, altaria::BlockId>> &parts,
                              const std::vector<altaria::Appearance> &appearances,
                              const altaria::Column &column, bool physical = false) const;
    void set_visual_environment(Color fog, float wind_time, float wind_strength, float noise_scale,
                                float noise_strength, float fog_start = 4500, float fog_end = 5775);
    bool edit(const altaria::EditCommand &command, String &error);
    bool edit_surface(const altaria::SurfaceCommand &command, String &error);
    altaria::Appearance appearance_at(altaria::IVec3 p) const {
        return model->appearance_at(p);
    }
    int last_seed() const {
        return model->generator->seed_value();
    }
    void set_fullbright(bool value);
    bool is_fullbright() const {
        return fullbright;
    }
    Dictionary stats() const;
    const altaria::Generator *generator() const {
        return model ? model->generator.get() : nullptr;
    }
    const altaria::Registry &registry() const {
        return model->generator->blocks();
    }
    const altaria::Settings &settings() const {
        return model->generator->settings();
    }
};
} // namespace godot
