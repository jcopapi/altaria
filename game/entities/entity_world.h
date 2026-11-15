#pragma once
// Creature layer built on EnTT (header-only ECS, vendored in native/third_party/entt).
// The registry, components and systems live in entity_world.cpp so only that
// translation unit pays for the EnTT header; AltariaGame owns one EntityWorld.
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <functional>
#include <memory>
#include <string>

namespace godot {
class VoxelWorld;
}

namespace altaria {

class EntityWorld {
  public:
    using CollisionQuery = std::function<bool(godot::Vector3, godot::Vector3)>;

    EntityWorld();
    ~EntityWorld();
    EntityWorld(const EntityWorld &) = delete;
    EntityWorld &operator=(const EntityWorld &) = delete;

    // Reads every `entities[]` definition whose controller is "wander_creature".
    void configure(godot::VoxelWorld *world, godot::Node3D *parent, const godot::Dictionary &entity_data,
                   CollisionQuery solid, uint32_t seed);
    // Destroys every creature and frees its node. Call while the parent is still alive.
    void clear();
    // Drops the world/parent pointers without touching nodes (they die with the stage).
    void detach();

    bool has_species(const std::string &key) const;
    // Accepts a full key ("altaria:chicken") or a short name ("chicken").
    std::string resolve_species(const std::string &name) const;
    // Spawns up to `count` creatures on spawnable ground around `center`; returns how many spawned.
    int spawn_near(const std::string &key, godot::Vector3 center, int count, float min_radius, float max_radius);
    // Spawns one creature standing on the ground below/around `position`.
    bool spawn_at(const std::string &key, godot::Vector3 position);

    // Runs the systems: natural spawning, AI, physics, despawn, render sync.
    void update(float dt, godot::Vector3 viewer);

    // ---- Mounts (species with a "mount" block) ----
    bool is_mountable(const std::string &key) const;
    // Spawns a mount under the rider's feet and mounts it. `rider_feet` is moved to the saddle.
    bool summon_mount(const std::string &key, godot::Vector3 &rider_feet, float yaw);
    // Mounts the closest mountable creature within `reach` of `eye`.
    bool mount_nearest(godot::Vector3 eye, float reach);
    void dismount();
    bool is_riding() const;
    std::string ridden_species() const;
    // Rider controls for this frame: horizontal world-space wish (unit or zero), vertical -1..1.
    void set_rider_input(godot::Vector3 wish, float vertical, bool sprint, float yaw);
    // Where the rider's feet go (saddle in world space); false when not riding.
    bool rider_seat(godot::Vector3 &feet) const;

    int count(const std::string &key = std::string()) const;
    godot::Dictionary debug_state() const;
    // Positions (feet) of live creatures, for tests and captures.
    godot::Array positions(const std::string &key = std::string()) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace altaria
