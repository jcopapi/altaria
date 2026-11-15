#include "entity_world.h"
#include "game/world_generation/voxel_world.h"
#include <entt/entt.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_map>

using namespace godot;

namespace altaria {
namespace {

constexpr float kPi = 3.14159265358979f;

// ---- Data -------------------------------------------------------------------
// One species per `entities[]` entry with controller "wander_creature".
struct Species {
    std::string key, name;
    Ref<PackedScene> scene;
    // Model materials swapped for the creature toon shader, shared by every instance.
    std::unordered_map<int64_t, Ref<ShaderMaterial>> materials;
    float scale = 1.0f, model_yaw = 0.0f;
    float half_width = 0.2f, height = 0.6f;
    float walk_speed = 1.2f, turn_speed = 4.0f, gravity = 24.0f, step_height = 0.55f;
    float hop_speed = 3.5f, jump_speed = 7.4f, max_health = 4.0f;
    float idle_min = 1.0f, idle_max = 4.0f, walk_min = 1.5f, walk_max = 4.5f;
    float peck_chance = 0.35f, hop_chance = 0.12f, max_fall = 2.5f;
    float waddle = 0.12f, peck_pitch = 0.55f; // animation amplitudes (radians)
    std::vector<BlockId> spawn_blocks;
    bool natural_spawn = true;
    int group_min = 2, group_max = 4, max_near = 8;
    float spawn_min_radius = 10.0f, spawn_max_radius = 28.0f, spawn_interval = 6.0f, despawn_distance = 80.0f;
    std::vector<std::string> aliases; // extra names for commands ("bee", "abelha")
    // Flyers hover `hover_height` above the ground instead of walking under gravity.
    bool flying = false;
    float hover_height = 1.2f, bob_amplitude = 0.06f;
    // Wing nodes: model descendants whose local y >= wing_min_y buzz around their z axis.
    float wing_min_y = 1e9f, wing_speed = 38.0f, wing_angle = 0.35f;
    // Mount: rider feet sit at `saddle` (creature frame: x right, y up, z back).
    bool mountable = false;
    Vector3 saddle;
    float ride_speed = 9.0f, ride_sprint = 2.0f, ride_vertical = 6.0f, ride_accel = 4.0f, rider_height = 1.8f;
};

// ---- Components ---------------------------------------------------------------
struct Kind {
    uint16_t species = 0;
};
struct Transform {
    Vector3 position; // feet, centre of the footprint
    float yaw = 0.0f; // Godot convention: forward = (-sin, 0, -cos)
};
struct Velocity {
    Vector3 value;
};
struct Body {
    bool on_ground = false;
    bool blocked = false;  // a wall stopped the last horizontal move
    float hazard_timer = 0.0f;
};
struct Health {
    float current = 1.0f, max = 1.0f;
};
struct Wander {
    enum State : uint8_t { Idle, Turn, Walk, Peck };
    State state = Idle;
    float timer = 0.0f, target_yaw = 0.0f, action = 0.0f, action_length = 1.0f;
};
struct Rider { // present on the mount the player is riding
    Vector3 wish;
    float vertical = 0.0f, yaw = 0.0f;
    bool sprint = false;
};
struct Render {
    Node3D *pivot = nullptr; // feet + yaw
    Node3D *model = nullptr; // scaled scene instance; bob / waddle / peck
    float visual_y = 0.0f, phase = 0.0f, stride = 0.0f, last_yaw = 0.0f, roll = 0.0f;
    std::vector<Node3D *> wings;
    std::vector<Vector3> wing_rest;
    std::vector<float> wing_side;
};

// Toon-lit like the terrain: ambient is nearly zero in Altaria, so sky-facing light comes
// from the same `sky_fill` emission term terrain.gdshader uses (shadows read lavender, not black).
Ref<Shader> creature_shader(Ref<Shader> (&cache)[2], bool double_sided) {
    Ref<Shader> &shader = cache[double_sided ? 1 : 0];
    if (shader.is_null()) {
        shader.instantiate();
        shader->set_code(String(R"(shader_type spatial;
render_mode diffuse_toon, specular_disabled, )") +
                         (double_sided ? "cull_disabled" : "cull_back") + R"(;
uniform sampler2D albedo_texture : source_color, filter_nearest, repeat_disable;
uniform float alpha_cutoff = 0.05;
uniform vec3 sky_fill = vec3(0.0);
void fragment() {
    vec4 c = texture(albedo_texture, UV);
    if (c.a < alpha_cutoff) discard;
    ALBEDO = c.rgb;
    ROUGHNESS = 1.0;
    float up = (INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).y;
    EMISSION = c.rgb * sky_fill * (0.72 + 0.28 * max(0.0, up));
}
)");
    }
    return shader;
}

Vector3 forward(float yaw) {
    return Vector3(-std::sin(yaw), 0.0f, -std::cos(yaw));
}
float wrap_angle(float a) {
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}
float num(const Dictionary &d, const char *key, float fallback) {
    const Variant v = d.get(key, fallback);
    return (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) ? float(v) : fallback;
}

} // namespace

struct EntityWorld::Impl {
    entt::registry registry;
    std::vector<Species> species;
    VoxelWorld *world = nullptr;
    Node3D *parent = nullptr;
    CollisionQuery solid;
    std::mt19937 rng{0xC41C4E};
    float spawn_timer = 2.0f;
    Ref<Shader> shaders[2];
    entt::entity ridden = entt::null;

    float rand(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }
    int rand_int(int lo, int hi) { return std::uniform_int_distribution<int>(lo, std::max(lo, hi))(rng); }

    int species_index(const std::string &key) const {
        for (size_t i = 0; i < species.size(); i++)
            if (species[i].key == key) return int(i);
        return -1;
    }

    bool collides(const Species &s, Vector3 p) const { return collides_h(s, p, s.height); }
    bool collides_h(const Species &s, Vector3 p, float h) const {
        return solid && solid(Vector3(p.x - s.half_width, p.y + 0.001f, p.z - s.half_width),
                              Vector3(p.x + s.half_width, p.y + h, p.z + s.half_width));
    }
    // A ridden mount also needs room for the rider standing on the saddle.
    float ridden_height(const Species &s) const { return std::max(s.height, s.saddle.y + s.rider_height); }
    Vector3 saddle_world(const Species &s, const Transform &tf) const {
        const float c = std::cos(tf.yaw), sn = std::sin(tf.yaw);
        // Rotate the creature-frame offset by yaw (same convention as Node3D rotation.y).
        return tf.position + Vector3(s.saddle.x * c + s.saddle.z * sn, s.saddle.y, -s.saddle.x * sn + s.saddle.z * c);
    }
    // Highest free feet height in [p.y - max_drop, p.y], or NAN when p is blocked or
    // nothing supports the body within max_drop.
    float ground_below(const Species &s, Vector3 p, float max_drop) const {
        if (collides(s, p)) return NAN;
        const float step = 0.25f;
        float free_y = p.y;
        for (float drop = step; drop <= max_drop + 1e-4f; drop += step) {
            const float y = p.y - drop;
            if (collides(s, Vector3(p.x, y, p.z))) {
                float lo = y, hi = free_y; // lo collides, hi free
                for (int i = 0; i < 10; i++) {
                    const float mid = (lo + hi) * 0.5f;
                    (collides(s, Vector3(p.x, mid, p.z)) ? lo : hi) = mid;
                }
                return hi;
            }
            free_y = y;
        }
        return NAN;
    }
    bool liquid_at(Vector3 p) const {
        if (!world) return false;
        const auto &def = world->registry().get(
            world->block_at(int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))));
        return def.water || def.liquid || def.lava;
    }

    // Top of the column at (x, z) searched downward from `top`; the surface block id and
    // the feet position on it. Only accepts loaded ground with a dry, open cell above.
    bool surface_at(int x, int z, int top, int bottom, BlockId &block, int &surface_y) const {
        const auto &reg = world->registry();
        bottom = std::max(bottom, world->settings().min_y);
        top = std::min(top, world->settings().max_y - 1);
        bool above_open = false, known = false;
        for (int y = top; y >= bottom; y--) {
            // Tile volumes cover a vertical band: skip the unstreamed air above it.
            if (!world->voxel_ready(x, y, z)) {
                if (known) return false;
                continue;
            }
            known = true;
            const BlockId id = world->block_at(x, y, z);
            const auto &def = reg.get(id);
            if (def.water || def.liquid || def.lava) return false;
            if (def.solid && id != reg.air) {
                if (!above_open) return false;
                block = id;
                surface_y = y;
                return true;
            }
            above_open = true;
        }
        return false;
    }

    entt::entity create(int index, Vector3 feet, float yaw) {
        Species &s = species[size_t(index)];
        const auto e = registry.create();
        registry.emplace<Kind>(e, uint16_t(index));
        registry.emplace<Transform>(e, feet, yaw);
        registry.emplace<Velocity>(e);
        registry.emplace<Body>(e);
        registry.emplace<Health>(e, s.max_health, s.max_health);
        Wander w;
        w.timer = rand(0.2f, s.idle_max);
        w.target_yaw = yaw;
        registry.emplace<Wander>(e, w);
        Render r;
        r.visual_y = feet.y;
        r.phase = rand(0.0f, 2.0f * kPi);
        if (parent && s.scene.is_valid()) {
            r.pivot = memnew(Node3D);
            r.pivot->set_name(String(s.name.c_str()));
            if (Node *instance = s.scene->instantiate()) {
                r.model = Object::cast_to<Node3D>(instance);
                if (r.model) {
                    r.model->set_scale(Vector3(s.scale, s.scale, s.scale));
                    r.pivot->add_child(r.model);
                    apply_materials(s, r.model);
                    collect_wings(s, r.model, r);
                } else
                    memdelete(instance);
            }
            parent->add_child(r.pivot);
            r.pivot->set_position(feet);
            r.pivot->set_rotation(Vector3(0.0f, yaw + s.model_yaw, 0.0f));
        }
        registry.emplace<Render>(e, r);
        return e;
    }

    void collect_wings(const Species &s, Node *node, Render &r) {
        std::vector<Node3D *> found;
        std::vector<Node *> stack{node};
        while (!stack.empty()) {
            Node *n = stack.back();
            stack.pop_back();
            for (int i = 0; i < n->get_child_count(); i++) stack.push_back(n->get_child(i));
            auto *n3 = Object::cast_to<Node3D>(n);
            if (n3 && n3 != node && n3->get_position().y >= s.wing_min_y) found.push_back(n3);
        }
        if (found.empty()) return;
        float mid = 0.0f;
        for (auto *w : found) mid += w->get_position().x;
        mid /= float(found.size());
        for (auto *w : found) {
            r.wings.push_back(w);
            r.wing_rest.push_back(w->get_rotation());
            r.wing_side.push_back(w->get_position().x >= mid ? 1.0f : -1.0f);
        }
    }

    void apply_materials(Species &s, Node *node) {
        if (auto *mesh = Object::cast_to<MeshInstance3D>(node)) {
            mesh->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_ON);
            for (int i = 0; i < mesh->get_surface_override_material_count(); i++) {
                Ref<StandardMaterial3D> source = mesh->get_active_material(i);
                if (source.is_null()) continue;
                const int64_t id = int64_t(source->get_instance_id());
                Ref<ShaderMaterial> &material = s.materials[id];
                if (material.is_null()) {
                    material.instantiate();
                    material->set_shader(creature_shader(shaders, source->get_cull_mode() == BaseMaterial3D::CULL_DISABLED));
                    material->set_shader_parameter("albedo_texture", source->get("albedo_texture"));
                    material->set_shader_parameter("alpha_cutoff", source->get_alpha_scissor_threshold());
                }
                mesh->set_surface_override_material(i, material);
            }
        }
        for (int i = 0; i < node->get_child_count(); i++) apply_materials(s, node->get_child(i));
    }

    void destroy(entt::entity e) {
        if (e == ridden) ridden = entt::null;
        if (auto *r = registry.try_get<Render>(e); r && r->pivot) {
            if (Node *p = r->pivot->get_parent()) p->remove_child(r->pivot);
            memdelete(r->pivot);
            r->pivot = nullptr;
        }
        registry.destroy(e);
    }

    // Places a creature on spawnable ground at column (x, z), searching around `y_hint`.
    bool try_spawn_column(int index, int x, int z, float y_hint, bool require_spawn_block, int depth = 32) {
        const Species &s = species[size_t(index)];
        BlockId block = 0;
        int surface_y = 0;
        if (!surface_at(x, z, int(std::floor(y_hint)) + 24, int(std::floor(y_hint)) - depth, block, surface_y))
            return false;
        if (require_spawn_block && !s.spawn_blocks.empty() &&
            std::find(s.spawn_blocks.begin(), s.spawn_blocks.end(), block) == s.spawn_blocks.end())
            return false;
        const Vector3 start(x + 0.5f, float(surface_y) + 1.2f, z + 0.5f);
        if (!world->voxels_ready(start)) return false;
        // One creature per column so a herd does not spawn stacked inside itself.
        for (auto [e, tf] : registry.view<Transform>().each()) {
            const float dx = tf.position.x - start.x, dz = tf.position.z - start.z;
            if (dx * dx + dz * dz < 0.8f * 0.8f && std::abs(tf.position.y - start.y) < 3.0f) return false;
        }
        const float y = ground_below(s, start, 1.5f);
        if (std::isnan(y)) return false;
        create(index, Vector3(start.x, y, start.z), rand(-kPi, kPi));
        return true;
    }

    int spawn_group(int index, Vector3 center, int count, float min_r, float max_r, bool require_spawn_block,
                    int depth = 32) {
        int spawned = 0;
        for (int attempt = 0; attempt < count * 8 && spawned < count; attempt++) {
            const float angle = rand(-kPi, kPi), radius = rand(min_r, std::max(min_r, max_r));
            const int x = int(std::floor(center.x + std::cos(angle) * radius));
            const int z = int(std::floor(center.z + std::sin(angle) * radius));
            spawned += try_spawn_column(index, x, z, center.y, require_spawn_block, depth) ? 1 : 0;
        }
        return spawned;
    }

    // ---- Systems --------------------------------------------------------------
    void natural_spawn_system(float dt, Vector3 viewer) {
        spawn_timer -= dt;
        if (spawn_timer > 0.0f) return;
        spawn_timer = 1e9f;
        for (size_t i = 0; i < species.size(); i++) {
            const Species &s = species[i];
            spawn_timer = std::min(spawn_timer, s.spawn_interval);
            if (!s.natural_spawn || !world->collision_ready(viewer)) continue;
            int near = 0;
            for (auto [e, kind, tf] : registry.view<Kind, Transform>().each())
                near += kind.species == i ? 1 : 0;
            if (near >= s.max_near) continue;
            // Pick a herd anchor on spawnable ground, then scatter the group around it.
            for (int attempt = 0; attempt < 6; attempt++) {
                const float angle = rand(-kPi, kPi), radius = rand(s.spawn_min_radius, s.spawn_max_radius);
                const int x = int(std::floor(viewer.x + std::cos(angle) * radius));
                const int z = int(std::floor(viewer.z + std::sin(angle) * radius));
                BlockId block = 0;
                int surface_y = 0;
                if (!surface_at(x, z, int(viewer.y) + 24, int(viewer.y) - 32, block, surface_y)) continue;
                if (!s.spawn_blocks.empty() &&
                    std::find(s.spawn_blocks.begin(), s.spawn_blocks.end(), block) == s.spawn_blocks.end())
                    continue;
                const int group = std::min(rand_int(s.group_min, s.group_max), s.max_near - near);
                spawn_group(int(i), Vector3(x + 0.5f, float(surface_y), z + 0.5f), group, 0.0f, 3.0f, true);
                break;
            }
        }
        if (spawn_timer > 1e8f) spawn_timer = 6.0f;
    }

    void despawn_system(Vector3 viewer) {
        std::vector<entt::entity> doomed;
        for (auto [e, kind, tf] : registry.view<Kind, Transform>().each()) {
            const Species &s = species[kind.species];
            if (e == ridden) continue;
            const float dx = tf.position.x - viewer.x, dz = tf.position.z - viewer.z;
            if ((s.despawn_distance > 0.0f && dx * dx + dz * dz > s.despawn_distance * s.despawn_distance) ||
                tf.position.y < -256.0f)
                doomed.push_back(e);
        }
        for (auto e : doomed) destroy(e);
    }

    // Is walking forward safe? False in front of a drop deeper than max_fall or water.
    bool path_clear(const Species &s, const Transform &tf) const {
        const Vector3 ahead = tf.position + forward(tf.yaw) * (s.half_width + 0.45f) + Vector3(0, s.step_height, 0);
        if (!world->collision_ready(ahead)) return false;
        if (collides(s, ahead)) return true; // a wall; the mover handles it
        const float g = ground_below(s, ahead, s.step_height + s.max_fall);
        if (std::isnan(g)) return false;
        return !liquid_at(Vector3(ahead.x, g - 0.3f, ahead.z)) && !liquid_at(Vector3(ahead.x, g + 0.1f, ahead.z));
    }

    void wander_system(float dt) {
        auto view = registry.view<Kind, Transform, Velocity, Body, Wander>();
        for (auto [e, kind, tf, vel, body, w] : view.each()) {
            const Species &s = species[kind.species];
            if (e == ridden || !world->collision_ready(tf.position)) continue;
            w.timer -= dt;
            switch (w.state) {
            case Wander::Idle:
                if (w.timer <= 0.0f) {
                    if (rand(0.0f, 1.0f) < s.peck_chance) {
                        w.state = Wander::Peck;
                        w.action = 0.0f;
                        w.action_length = rand(0.5f, 0.9f);
                        w.timer = w.action_length;
                    } else {
                        w.state = Wander::Turn;
                        w.target_yaw = wrap_angle(tf.yaw + rand(-2.4f, 2.4f));
                        w.timer = 2.0f;
                    }
                }
                break;
            case Wander::Turn: {
                const float diff = wrap_angle(w.target_yaw - tf.yaw);
                const float step = s.turn_speed * dt;
                tf.yaw = wrap_angle(tf.yaw + std::clamp(diff, -step, step));
                if (std::abs(diff) <= step || w.timer <= 0.0f) {
                    w.state = Wander::Walk;
                    w.timer = rand(s.walk_min, s.walk_max);
                }
                break;
            }
            case Wander::Walk: {
                // Gentle meander while walking.
                tf.yaw = wrap_angle(tf.yaw + std::sin(w.timer * 1.7f + float(entt::to_integral(e))) * 0.6f * dt);
                body.hazard_timer -= dt;
                bool turn_back = false;
                if (!s.flying && body.hazard_timer <= 0.0f) {
                    body.hazard_timer = 0.15f;
                    turn_back = !path_clear(s, tf);
                }
                if (body.blocked && s.flying) {
                    turn_back = true;
                } else if (body.blocked && body.on_ground) {
                    // A full block in the way: hop over it when there is room, else turn.
                    const Vector3 over = tf.position + Vector3(0, 1.05f, 0) + forward(tf.yaw) * 0.35f;
                    if (!collides(s, over) && !collides(s, tf.position + Vector3(0, 1.05f, 0)))
                        vel.value.y = s.jump_speed;
                    else
                        turn_back = true;
                } else if (body.on_ground && rand(0.0f, 1.0f) < s.hop_chance * dt)
                    vel.value.y = s.hop_speed;
                if (turn_back) {
                    w.state = Wander::Turn;
                    w.target_yaw = wrap_angle(tf.yaw + kPi + rand(-0.8f, 0.8f));
                    w.timer = 2.0f;
                } else if (w.timer <= 0.0f) {
                    w.state = Wander::Idle;
                    w.timer = rand(s.idle_min, s.idle_max);
                }
                break;
            }
            case Wander::Peck:
                w.action = std::clamp(1.0f - w.timer / std::max(0.01f, w.action_length), 0.0f, 1.0f);
                if (w.timer <= 0.0f) {
                    w.state = Wander::Idle;
                    w.action = 0.0f;
                    w.timer = rand(0.3f, s.idle_max * 0.5f);
                }
                break;
            }
        }
    }

    void physics_system(float dt) {
        auto view = registry.view<Kind, Transform, Velocity, Body, Wander>();
        for (auto [e, kind, tf, vel, body, w] : view.each()) {
            const Species &s = species[kind.species];
            if (!world->collision_ready(tf.position)) continue; // freeze until the ground streams in
            if (e == ridden) {
                ridden_physics(s, tf, vel, body, registry.get<Rider>(e), dt);
                continue;
            }
            if (s.flying) {
                flyer_physics(s, tf, vel, body, w, dt);
                continue;
            }
            Vector3 p = tf.position;
            // Pushed out of freshly placed blocks.
            for (int i = 0; i < 4 && collides(s, p); i++) p.y += 0.5f;
            Vector3 v = vel.value;
            const bool in_liquid = liquid_at(p + Vector3(0, s.height * 0.35f, 0));
            v.y = in_liquid ? std::min(v.y + (s.gravity * 0.6f) * dt, 1.5f) : std::max(v.y - s.gravity * dt, -30.0f);
            const Vector3 desired = w.state == Wander::Walk ? forward(tf.yaw) * s.walk_speed : Vector3();
            const float blend = std::min(1.0f, dt * (body.on_ground ? 10.0f : 2.0f));
            v.x += (desired.x - v.x) * blend;
            v.z += (desired.z - v.z) * blend;

            body.blocked = false;
            for (int axis : {0, 2}) {
                const float d = v[axis] * dt;
                if (std::abs(d) < 1e-6f) continue;
                Vector3 np = p;
                np[axis] += d;
                if (!collides(s, np)) {
                    p = np;
                    continue;
                }
                // Auto-step onto half-block terrain steps and slabs.
                const Vector3 up = np + Vector3(0, s.step_height, 0);
                if (body.on_ground && !collides(s, up) && !collides(s, p + Vector3(0, s.step_height, 0))) {
                    const float g = ground_below(s, up, s.step_height);
                    p = std::isnan(g) ? up : Vector3(up.x, g, up.z);
                } else {
                    body.blocked = true;
                    v[axis] = 0.0f;
                }
            }

            const bool was_grounded = body.on_ground;
            Vector3 np = p;
            np.y += v.y * dt;
            if (collides(s, np)) {
                float free_y = p.y, hit_y = np.y;
                for (int i = 0; i < 10; i++) {
                    const float mid = (free_y + hit_y) * 0.5f;
                    (collides(s, Vector3(p.x, mid, p.z)) ? hit_y : free_y) = mid;
                }
                p.y = free_y;
                body.on_ground = v.y < 0.0f;
                v.y = 0.0f;
            } else {
                p = np;
                body.on_ground = false;
                // Stick to the ground walking down half-block steps instead of skipping off.
                if (was_grounded && v.y <= 0.0f && !in_liquid) {
                    const float g = ground_below(s, p, s.step_height + 0.05f);
                    if (!std::isnan(g)) {
                        p.y = g;
                        body.on_ground = true;
                        v.y = 0.0f;
                    }
                }
            }
            tf.position = p;
            vel.value = v;
        }
    }

    // Gravity-free mover shared by flyers and ridden mounts: axis-separated, sub-stepped so
    // fast flight cannot tunnel, and a wall only cancels the blocked component.
    void move_free(const Species &s, Vector3 &p, Vector3 &v, float dt, float h, Body &body) {
        body.blocked = false;
        body.on_ground = false;
        for (int i = 0; i < 4 && collides_h(s, p, h); i++) p.y += 0.5f;
        const int steps = std::clamp(int(std::ceil(v.length() * dt / 0.3f)), 1, 12);
        const float sdt = dt / float(steps);
        for (int step = 0; step < steps; step++)
            for (int axis : {0, 2, 1}) {
                const float d = v[axis] * sdt;
                if (std::abs(d) < 1e-6f) continue;
                Vector3 np = p;
                np[axis] += d;
                if (!collides_h(s, np, h)) {
                    p = np;
                    continue;
                }
                float free_v = p[axis], hit_v = np[axis];
                for (int i = 0; i < 8; i++) {
                    const float mid = (free_v + hit_v) * 0.5f;
                    Vector3 q = p;
                    q[axis] = mid;
                    (collides_h(s, q, h) ? hit_v : free_v) = mid;
                }
                p[axis] = free_v;
                if (axis == 1) body.on_ground = body.on_ground || v.y < 0.0f;
                else body.blocked = true;
                v[axis] = 0.0f;
            }
    }

    void flyer_physics(const Species &s, Transform &tf, Velocity &vel, Body &body, const Wander &w, float dt) {
        Vector3 p = tf.position, v = vel.value;
        // Hold hover_height above whatever is below; sink gently when the ground is out of reach.
        const float ground = ground_below(s, p + Vector3(0, 0.5f, 0), s.hover_height + 8.0f);
        const float target_vy = std::isnan(ground) ? -1.5f : std::clamp((ground + s.hover_height - p.y) * 2.0f, -3.0f, 3.0f);
        const Vector3 desired = w.state == Wander::Walk ? forward(tf.yaw) * s.walk_speed : Vector3();
        const float blend = std::min(1.0f, dt * 3.0f);
        v += (Vector3(desired.x, target_vy, desired.z) - v) * blend;
        move_free(s, p, v, dt, s.height, body);
        tf.position = p;
        vel.value = v;
    }

    void ridden_physics(const Species &s, Transform &tf, Velocity &vel, Body &body, const Rider &rider, float dt) {
        // The mount turns with the rider's view, slightly smoothed.
        tf.yaw = wrap_angle(tf.yaw + wrap_angle(rider.yaw - tf.yaw) * std::min(1.0f, dt * 12.0f));
        const float speed = s.ride_speed * (rider.sprint ? s.ride_sprint : 1.0f);
        const Vector3 desired = rider.wish * speed + Vector3(0, rider.vertical * s.ride_vertical, 0);
        Vector3 v = vel.value;
        v += (desired - v) * std::min(1.0f, dt * s.ride_accel);
        Vector3 p = tf.position;
        move_free(s, p, v, dt, ridden_height(s), body);
        tf.position = p;
        vel.value = v;
    }

    // Creatures do not walk through each other: overlapping footprints are pushed apart
    // horizontally (never into terrain). O(n^2), fine for the few dozen kept near the player.
    void separation_system(float dt) {
        struct Item {
            Transform *tf;
            const Species *s;
            bool fixed;
        };
        std::vector<Item> items;
        for (auto [e, kind, tf] : registry.view<Kind, Transform>().each())
            items.push_back({&tf, &species[kind.species], e == ridden});
        const float rate = std::min(1.0f, dt * 8.0f);
        for (size_t i = 0; i < items.size(); i++)
            for (size_t j = i + 1; j < items.size(); j++) {
                Vector3 &a = items[i].tf->position, &b = items[j].tf->position;
                if (std::abs(a.y - b.y) > std::max(items[i].s->height, items[j].s->height)) continue;
                const float reach = items[i].s->half_width + items[j].s->half_width;
                Vector3 d(b.x - a.x, 0.0f, b.z - a.z);
                const float dist = d.length();
                if (dist >= reach) continue;
                d = dist > 1e-4f ? d / dist : Vector3(std::cos(float(i)), 0.0f, std::sin(float(i)));
                const Vector3 push = d * ((reach - dist) * 0.5f * rate);
                if (!items[i].fixed && !collides(*items[i].s, a - push)) a -= push;
                if (!items[j].fixed && !collides(*items[j].s, b + push)) b += push;
            }
    }

    void render_system(float dt) {
        const Vector3 fill = world->sky_fill();
        for (auto &s : species)
            for (auto &[id, material] : s.materials) material->set_shader_parameter("sky_fill", fill);
        auto view = registry.view<Kind, Transform, Velocity, Body, Wander, Render>();
        for (auto [e, kind, tf, vel, body, w, r] : view.each()) {
            if (!r.pivot) continue;
            const Species &s = species[kind.species];
            // Smooth the vertical snap of steps without lagging falls.
            if (s.flying || std::abs(tf.position.y - r.visual_y) > 1.5f || !body.on_ground)
                r.visual_y = tf.position.y;
            else
                r.visual_y += (tf.position.y - r.visual_y) * (1.0f - std::exp(-dt * 18.0f));
            r.pivot->set_position(Vector3(tf.position.x, r.visual_y, tf.position.z));
            r.pivot->set_rotation(Vector3(0.0f, tf.yaw + s.model_yaw, 0.0f));
            if (!r.model) continue;
            const float speed = Vector2(vel.value.x, vel.value.z).length();
            const float stride_target = std::clamp(speed / std::max(0.1f, s.walk_speed), 0.0f, 1.0f);
            r.stride += (stride_target - r.stride) * std::min(1.0f, dt * 8.0f);
            r.phase = std::fmod(r.phase + dt * (4.0f + 10.0f * r.stride), 2.0f * kPi * 64.0f);
            const float bob = std::abs(std::sin(r.phase)) * 0.035f * r.stride * s.scale;
            const float waddle = std::sin(r.phase) * s.waddle * r.stride;
            float pitch = 0.0f;
            if (w.state == Wander::Peck) {
                // Two quick pecks per action.
                pitch = -s.peck_pitch * std::abs(std::sin(w.action * 2.0f * kPi));
            } else if (!body.on_ground)
                pitch = std::clamp(vel.value.y * -0.04f, -0.25f, 0.25f);
            if (s.flying) {
                // Hover bob, buzzing wings, lean into the flight direction and bank on turns.
                const float t = r.phase;
                const Vector3 local_v = Vector3(vel.value.x, 0, vel.value.z);
                const float fwd = local_v.dot(forward(tf.yaw)) / std::max(1.0f, s.ride_speed * s.ride_sprint);
                const float yaw_rate = wrap_angle(tf.yaw - r.last_yaw) / std::max(dt, 1e-4f);
                r.last_yaw = tf.yaw;
                r.roll += (std::clamp(-yaw_rate * 0.12f, -0.35f, 0.35f) - r.roll) * std::min(1.0f, dt * 6.0f);
                r.model->set_position(Vector3(0.0f, std::sin(t * 0.6f) * s.bob_amplitude * s.scale, 0.0f));
                // Pitch/roll are applied in model space: flip them for models authored facing +Z.
                const float facing = std::cos(s.model_yaw) >= 0.0f ? 1.0f : -1.0f;
                r.model->set_rotation(Vector3(facing * std::clamp(-fwd * 0.35f + vel.value.y * 0.02f, -0.35f, 0.3f),
                                              0.0f, facing * r.roll));
                const float clock = float(Time::get_singleton()->get_ticks_msec()) * 0.001f;
                for (size_t i = 0; i < r.wings.size(); i++)
                    r.wings[i]->set_rotation(r.wing_rest[i] + Vector3(0, 0, r.wing_side[i] * s.wing_angle *
                                                                            std::sin(clock * s.wing_speed + float(i))));
                continue;
            }
            const float facing = std::cos(s.model_yaw) >= 0.0f ? 1.0f : -1.0f;
            r.model->set_position(Vector3(0.0f, bob, 0.0f));
            r.model->set_rotation(Vector3(facing * pitch, 0.0f, waddle));
        }
    }
};

EntityWorld::EntityWorld() : impl(std::make_unique<Impl>()) {}
EntityWorld::~EntityWorld() = default;

void EntityWorld::configure(VoxelWorld *world, Node3D *parent, const Dictionary &entity_data, CollisionQuery solid,
                            uint32_t seed) {
    clear();
    impl->world = world;
    impl->parent = parent;
    impl->solid = std::move(solid);
    impl->rng.seed(seed ^ 0xC41C4Eu);
    impl->spawn_timer = 2.0f;
    impl->species.clear();
    if (!world) return;
    const auto &reg = world->registry();
    Array defs = entity_data.get("entities", Array());
    for (int i = 0; i < defs.size(); i++) {
        if (defs[i].get_type() != Variant::DICTIONARY) continue;
        Dictionary d = defs[i];
        if (String(d.get("controller", String())) != "wander_creature") continue;
        Species s;
        s.key = String(d.get("key", String())).utf8().get_data();
        s.name = String(d.get("name", String(s.key.c_str()))).utf8().get_data();
        const String model = d.get("model", String());
        if (!model.is_empty()) {
            s.scene = ResourceLoader::get_singleton()->load(model);
            if (s.scene.is_null()) UtilityFunctions::push_warning("Entity model failed to load: ", model);
        }
        s.scale = num(d, "scale", s.scale);
        s.model_yaw = num(d, "model_yaw_degrees", 0.0f) * kPi / 180.0f;
        Array size = d.get("size", Array());
        if (size.size() == 2) {
            s.half_width = std::clamp(float(size[0]) * 0.5f, 0.05f, 2.0f);
            s.height = std::clamp(float(size[1]), 0.1f, 4.0f);
        }
        s.walk_speed = num(d, "walk_speed", s.walk_speed);
        s.turn_speed = num(d, "turn_speed", s.turn_speed);
        s.gravity = num(d, "gravity", s.gravity);
        s.step_height = num(d, "step_height", s.step_height);
        s.hop_speed = num(d, "hop_speed", s.hop_speed);
        s.jump_speed = num(d, "jump_speed", s.jump_speed);
        s.max_health = num(d, "health", s.max_health);
        s.max_fall = num(d, "max_fall", s.max_fall);
        Dictionary ai = d.get("wander", Dictionary());
        Array idle = ai.get("idle_seconds", Array()), walk = ai.get("walk_seconds", Array());
        if (idle.size() == 2) s.idle_min = idle[0], s.idle_max = idle[1];
        if (walk.size() == 2) s.walk_min = walk[0], s.walk_max = walk[1];
        s.peck_chance = num(ai, "peck_chance", s.peck_chance);
        s.waddle = num(ai, "waddle", s.waddle);
        s.peck_pitch = num(ai, "peck_pitch", s.peck_pitch);
        s.hop_chance = num(ai, "hop_per_second", s.hop_chance);
        Dictionary sp = d.get("spawn", Dictionary());
        s.natural_spawn = bool(sp.get("natural", true));
        Array blocks = sp.get("blocks", Array());
        for (int b = 0; b < blocks.size(); b++) {
            const BlockId id = reg.find(String(blocks[b]).utf8().get_data());
            if (id != reg.air) s.spawn_blocks.push_back(id);
        }
        Array group = sp.get("group", Array()), radius = sp.get("radius", Array());
        if (group.size() == 2) s.group_min = group[0], s.group_max = group[1];
        if (radius.size() == 2) s.spawn_min_radius = radius[0], s.spawn_max_radius = radius[1];
        s.max_near = int(num(sp, "max_near_player", float(s.max_near)));
        s.spawn_interval = std::max(0.5f, num(sp, "interval_seconds", s.spawn_interval));
        s.despawn_distance = num(sp, "despawn_distance", s.despawn_distance);
        Array aliases = d.get("aliases", Array());
        for (int a = 0; a < aliases.size(); a++) s.aliases.push_back(String(aliases[a]).to_lower().utf8().get_data());
        Dictionary fly = d.get("flight", Dictionary());
        if (!fly.is_empty()) {
            s.flying = true;
            s.hover_height = num(fly, "hover_height", s.hover_height);
            s.bob_amplitude = num(fly, "bob", s.bob_amplitude);
            s.wing_min_y = num(fly, "wing_min_y", s.wing_min_y);
            s.wing_speed = num(fly, "wing_speed", s.wing_speed);
            s.wing_angle = num(fly, "wing_angle", s.wing_angle);
        }
        Dictionary mount = d.get("mount", Dictionary());
        if (!mount.is_empty()) {
            s.mountable = true;
            Array seat = mount.get("saddle", Array());
            if (seat.size() == 3) s.saddle = Vector3(seat[0], seat[1], seat[2]);
            s.ride_speed = num(mount, "speed", s.ride_speed);
            s.ride_sprint = num(mount, "sprint_multiplier", s.ride_sprint);
            s.ride_vertical = num(mount, "vertical_speed", s.ride_vertical);
            s.ride_accel = num(mount, "acceleration", s.ride_accel);
            s.rider_height = num(mount, "rider_height", s.rider_height);
        }
        if (!s.key.empty()) impl->species.push_back(std::move(s));
    }
}

void EntityWorld::clear() {
    std::vector<entt::entity> all;
    for (auto e : impl->registry.view<Kind>()) all.push_back(e);
    for (auto e : all) impl->destroy(e);
    impl->registry.clear();
    impl->ridden = entt::null;
}

void EntityWorld::detach() {
    impl->registry.clear();
    impl->ridden = entt::null;
    impl->world = nullptr;
    impl->parent = nullptr;
    impl->solid = nullptr;
}

bool EntityWorld::has_species(const std::string &key) const { return impl->species_index(key) >= 0; }

std::string EntityWorld::resolve_species(const std::string &raw) const {
    std::string name = raw;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    std::string underscored = name;
    std::replace(underscored.begin(), underscored.end(), ' ', '_');
    for (const auto &s : impl->species) {
        if (s.key == name) return s.key;
        const auto colon = s.key.find(':');
        std::string lower_name = s.name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        if ((colon != std::string::npos && s.key.substr(colon + 1) == underscored) || lower_name == name) return s.key;
        for (const auto &alias : s.aliases)
            if (alias == name || alias == underscored) return s.key;
    }
    return std::string();
}

bool EntityWorld::is_mountable(const std::string &key) const {
    const int index = impl->species_index(key);
    return index >= 0 && impl->species[size_t(index)].mountable;
}

bool EntityWorld::summon_mount(const std::string &key, Vector3 &rider_feet, float yaw) {
    const int index = impl->species_index(key);
    if (index < 0 || !impl->world || !impl->species[size_t(index)].mountable) return false;
    const Species &s = impl->species[size_t(index)];
    if (impl->ridden != entt::null) dismount();
    // Put the mount where the rider stands, then lift until mount + rider fit.
    Vector3 p = rider_feet;
    const float h = impl->ridden_height(s);
    int lift = 0;
    while (impl->collides_h(s, p, h) && lift++ < 16) p.y += 0.25f;
    if (impl->collides_h(s, p, h)) return false;
    const auto e = impl->create(index, p, yaw);
    impl->registry.emplace<Rider>(e, Rider{Vector3(), 0.0f, yaw, false});
    impl->ridden = e;
    rider_feet = impl->saddle_world(s, impl->registry.get<Transform>(e));
    return true;
}

bool EntityWorld::mount_nearest(Vector3 eye, float reach) {
    if (impl->ridden != entt::null) return false;
    entt::entity best = entt::null;
    float best_d = reach;
    for (auto [e, kind, tf] : impl->registry.view<Kind, Transform>().each()) {
        const Species &s = impl->species[kind.species];
        if (!s.mountable) continue;
        const Vector3 center = tf.position + Vector3(0, s.height * 0.5f, 0);
        const float d = std::max(0.0f, eye.distance_to(center) - s.half_width);
        if (d <= best_d) best_d = d, best = e;
    }
    if (best == entt::null) return false;
    auto &tf = impl->registry.get<Transform>(best);
    impl->registry.emplace_or_replace<Rider>(best, Rider{Vector3(), 0.0f, tf.yaw, false});
    impl->registry.get<Velocity>(best).value = Vector3();
    impl->ridden = best;
    return true;
}

void EntityWorld::dismount() {
    if (impl->ridden == entt::null) return;
    if (impl->registry.valid(impl->ridden)) {
        impl->registry.remove<Rider>(impl->ridden);
        impl->registry.get<Velocity>(impl->ridden).value = Vector3();
        auto &w = impl->registry.get<Wander>(impl->ridden);
        w.state = Wander::Idle;
        w.timer = impl->rand(1.0f, 3.0f);
    }
    impl->ridden = entt::null;
}

bool EntityWorld::is_riding() const { return impl->ridden != entt::null && impl->registry.valid(impl->ridden); }

std::string EntityWorld::ridden_species() const {
    if (!is_riding()) return std::string();
    return impl->species[impl->registry.get<Kind>(impl->ridden).species].key;
}

void EntityWorld::set_rider_input(Vector3 wish, float vertical, bool sprint, float yaw) {
    if (!is_riding()) return;
    auto &r = impl->registry.get<Rider>(impl->ridden);
    r.wish = wish;
    r.vertical = std::clamp(vertical, -1.0f, 1.0f);
    r.sprint = sprint;
    r.yaw = yaw;
}

bool EntityWorld::rider_seat(Vector3 &feet) const {
    if (!is_riding()) return false;
    const auto &kind = impl->registry.get<Kind>(impl->ridden);
    feet = impl->saddle_world(impl->species[kind.species], impl->registry.get<Transform>(impl->ridden));
    return true;
}

int EntityWorld::spawn_near(const std::string &key, Vector3 center, int count, float min_radius, float max_radius) {
    const int index = impl->species_index(key);
    if (index < 0 || !impl->world) return 0;
    // Commands spawn on any dry ground; natural spawning is restricted to `spawn.blocks`.
    // Commands search the whole column below, so they work while flying high.
    return impl->spawn_group(index, center, std::clamp(count, 1, 64), min_radius, max_radius, false, 320);
}

bool EntityWorld::spawn_at(const std::string &key, Vector3 position) {
    const int index = impl->species_index(key);
    if (index < 0 || !impl->world) return false;
    return impl->try_spawn_column(index, int(std::floor(position.x)), int(std::floor(position.z)), position.y, false,
                                  320);
}

void EntityWorld::update(float dt, Vector3 viewer) {
    if (!impl->world || impl->species.empty()) return;
    dt = std::clamp(dt, 0.0f, 0.05f);
    impl->natural_spawn_system(dt, viewer);
    impl->wander_system(dt);
    impl->physics_system(dt);
    impl->separation_system(dt);
    impl->despawn_system(viewer);
    impl->render_system(dt);
}

int EntityWorld::count(const std::string &key) const {
    const int index = key.empty() ? -1 : impl->species_index(key);
    if (!key.empty() && index < 0) return 0;
    int n = 0;
    for (auto [e, kind] : impl->registry.view<Kind>().each())
        n += (index < 0 || kind.species == index) ? 1 : 0;
    return n;
}

Dictionary EntityWorld::debug_state() const {
    Dictionary d;
    for (const auto &s : impl->species) d[String(s.key.c_str())] = count(s.key);
    return d;
}

Array EntityWorld::positions(const std::string &key) const {
    const int index = key.empty() ? -1 : impl->species_index(key);
    Array out;
    for (auto [e, kind, tf, body] : impl->registry.view<Kind, Transform, Body>().each()) {
        if (index >= 0 && kind.species != index) continue;
        Dictionary entry;
        entry["position"] = tf.position;
        entry["on_ground"] = body.on_ground;
        entry["frozen"] = !impl->world || !impl->world->collision_ready(tf.position);
        entry["ridden"] = e == impl->ridden;
        out.push_back(entry);
    }
    return out;
}

} // namespace altaria
