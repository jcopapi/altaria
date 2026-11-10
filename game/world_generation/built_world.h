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
    std::unordered_map<altaria::IVec3, std::vector<uint64_t>, altaria::CoordHash> regions;
    std::unordered_map<altaria::IVec3, Region, altaria::CoordHash> meshes;
    std::unordered_set<altaria::IVec3, altaria::CoordHash> dirty;
    Vector3 viewer;
    float timer = 0;
    std::function<bool(const altaria::BuiltBlock &)> guard;
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
    bool configure(VoxelWorld *world, const String &path, String &error);
    void _process(double delta) override;
    const altaria::ConstructionStore &construction() const {return store;}
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
    bool remove(uint64_t id, String &error);
    bool appearance(uint64_t id, const altaria::Appearance &value, String &error);
    bool collides_box(Vector3 minimum, Vector3 maximum) const;
    std::pair<float, float> thermal(Vector3 position) const;
    Dictionary stats() const;
    Array describe() const;
};
} // namespace godot
