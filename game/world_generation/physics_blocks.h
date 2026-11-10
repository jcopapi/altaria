#pragma once
#include "environment_core.h"
#include "voxel_world.h"
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/physics_shape_query_parameters3d.hpp>
#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/classes/shape3d.hpp>
namespace godot {
class PhysicsBlocks : public Node3D {
    GDCLASS(PhysicsBlocks, Node3D)
    struct Debris {
        RigidBody3D *body = nullptr;
        MeshInstance3D *mesh = nullptr;
        std::vector<std::pair<altaria::IVec3, altaria::BlockId>> parts;
        std::vector<altaria::Appearance> appearances;
        altaria::Column column;
        bool waiting = true, distant = false;
        Vector3 resume_linear, resume_angular, texture_anchor;
    };
    VoxelWorld *world = nullptr;
    altaria::EnvironmentSettings settings;
    std::vector<Debris> debris;
    std::unordered_map<uint8_t, std::vector<Ref<Shape3D>>> hull_cache;
    Vector3 viewer;
    Ref<BoxShape3D> player_box;
    Ref<PhysicsShapeQueryParameters3D> player_query;
    uint64_t physics_sequence = 0;
    void rebuild(Debris &d);
    bool add(std::vector<std::pair<altaria::IVec3, altaria::BlockId>> parts,
             std::vector<altaria::Appearance> appearances, Vector3 origin, const altaria::Column &column,
             Vector3 impulse = Vector3());

  protected:
    static void _bind_methods() {}

  public:
    struct Hit {
        int body = -1, part = -1;
        float distance = 1e30f;
    };
    void configure(VoxelWorld *world, const altaria::EnvironmentSettings &settings);
    void _physics_process(double delta) override;
    void update_viewer(Vector3 p) {
        viewer = p;
    }
    bool release_piece(altaria::BlockId block, altaria::Appearance appearance, Vector3 origin, String &error);
    void discard_last();
    bool release(altaria::IVec3 position, bool tree, String &error);
    Hit pick(Vector3 origin, Vector3 direction, float reach) const;
    bool selection_for(Hit hit, Transform3D &transform, altaria::BlockId &block, uint8_t &shape) const;
    bool detach(Hit hit, Vector3 impulse, String &error);
    bool collides_box(Vector3 minimum, Vector3 maximum);
    void clear();
    Array save() const;
    bool restore(const Array &records, String &error);
    Dictionary stats() const;
    std::pair<float, float> thermal_influence(Vector3 position) const;
};
} // namespace godot
