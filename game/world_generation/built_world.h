#pragma once
#include "construction_core.h"
#include "voxel_world.h"
#include <godot_cpp/classes/static_body3d.hpp>
namespace godot {
class BuiltWorld : public Node3D {
    GDCLASS(BuiltWorld, Node3D)
    struct Region {
        MeshInstance3D *mesh = nullptr;
        StaticBody3D *collider = nullptr;
        bool collision_near = false, grass_near = false;
        MultiMeshInstance3D *grass = nullptr;
    };
    VoxelWorld *world = nullptr;
    altaria::ConstructionStore store;
    std::string journal;
    std::unordered_map<altaria::IVec3, std::vector<uint64_t>, altaria::CoordHash> regions;
    std::unordered_map<altaria::IVec3, Region, altaria::CoordHash> meshes;
    std::unordered_set<altaria::IVec3, altaria::CoordHash> dirty;
    Vector3 viewer;
    float timer = 0;
    std::function<bool(const altaria::BuiltBlock &)> guard;
    std::function<void(Vector3)> terrain_callback;
    std::unordered_set<uint64_t> meshed_terrain;
    bool record(char operation, const altaria::BuiltBlock &block, String &error);
    void apply(char operation, const altaria::BuiltBlock &block);
    void rebuild(altaria::IVec3 region);
    void mark_shape_changes();
    void refresh_world_ramps(altaria::Point center);

  protected:
    static void _bind_methods() {}

  public:
    struct Hit {
        uint64_t id = 0;
        float distance = 1e30f;
        altaria::Point normal{};
    };
    bool source_rendered(Vector3 position) const;
    bool terrain_meshed(uint64_t id) const {return meshed_terrain.count(id);}
    void set_terrain_meshed(uint64_t id);
    void clear_terrain_meshed(uint64_t id);
    bool configure(VoxelWorld *world, const String &path, String &error);
    void _process(double delta) override;
    const altaria::ConstructionStore &construction() const {return store;}
    void set_terrain_callback(std::function<void(Vector3)> callback) {terrain_callback=std::move(callback);}
    void set_guard(std::function<bool(const altaria::BuiltBlock &)> value) {
        guard = std::move(value);
    }
    void update_viewer(Vector3 p) {
        viewer = p;
    }
    Hit pick(Vector3 origin, Vector3 direction, float reach) const;
    const altaria::BuiltBlock *get(uint64_t id) const;
    uint64_t place(Vector3 position, int block, int shape, int rotation, String &error);
    uint64_t attach(Vector3 hit, Vector3 normal, Vector3 preferred, Vector3 source, uint8_t source_shape,
                    int block, int shape, int rotation, String &error);
    bool replace_cap(uint64_t id,int block,int shape,int rotation,String &error);
    bool remove(uint64_t id, String &error);
    bool appearance(uint64_t id, const altaria::Appearance &value, String &error);
    bool collides_box(Vector3 minimum, Vector3 maximum) const;
    std::pair<float, float> thermal(Vector3 position) const;
    Dictionary stats() const;
    Array describe() const;
};
} // namespace godot
