#include "physics_blocks.h"
#include "../debug/frame_profile.h"
#include "block_shapes.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/convex_polygon_shape3d.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_material.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <unordered_set>
namespace godot {
using namespace altaria;
void PhysicsBlocks::configure(VoxelWorld *w, const EnvironmentSettings &s) {
    world = w;
    settings = s;
    player_box.instantiate();
    player_query.instantiate();
    player_query->set_shape(player_box);
    player_query->set_collision_mask(2);
    player_query->set_collide_with_areas(false);
    player_query->set_collide_with_bodies(true);
    player_query->set_margin(.001);
}
void PhysicsBlocks::rebuild(Debris &d) {
    for (int i = d.body->get_child_count() - 1; i >= 0; i--) {
        auto node = d.body->get_child(i);
        if (node != d.mesh) {
            d.body->remove_child(node);
            memdelete(node);
        }
    }
    d.mesh->set_mesh(world->block_mesh(d.parts, d.appearances, d.column, true));
    d.mesh->set_instance_shader_parameter("texture_anchor", d.texture_anchor);
    float mass = 0;
    bool has_solid = false;
    for (auto &p : d.parts)
        has_solid |= !world->registry().get(p.second).foliage;
    for (size_t i = 0; i < d.parts.size(); i++) {
        const auto &part = d.parts[i];
        auto &definition = world->registry().get(part.second);
        mass += definition.mass;
        if (definition.foliage && has_solid)
            continue;
        auto &a = d.appearances[i];
        uint8_t packed = pack_shape(a.shape, a.rotation);
        auto cache = hull_cache.find(packed);
        if (cache == hull_cache.end()) {
            std::vector<Ref<Shape3D>> shapes;
            for (auto &hull : shape_hulls(packed)) {
                Ref<ConvexPolygonShape3D> shape;
                shape.instantiate();
                PackedVector3Array points;
                for (auto &p : hull)
                    points.push_back(Vector3(p[0] - .5f, p[1] - .5f, p[2] - .5f));
                shape->set_points(points);
                shape->set_margin(.025f);
                shapes.push_back(shape);
            }
            cache = hull_cache.emplace(packed, std::move(shapes)).first;
        }
        for (auto &shape : cache->second) {
            auto node = memnew(CollisionShape3D);
            node->set_shape(shape);
            node->set_position(Vector3(part.first.x + .5f, part.first.y + .5f, part.first.z + .5f));
            d.body->add_child(node);
        }
    }
    d.body->set_mass(std::max(.1f, mass));
    d.body->set_sleeping(false);
}
bool PhysicsBlocks::add(std::vector<std::pair<IVec3, BlockId>> parts, std::vector<Appearance> appearances,
                        Vector3 origin, const Column &column, Vector3 impulse) {
    if (parts.empty() || parts.size() > size_t(settings.max_parts) ||
        debris.size() >= size_t(settings.max_bodies))
        return false;
    Debris d;
    d.parts = std::move(parts);
    d.appearances = std::move(appearances);
    d.column = column;
    d.texture_anchor = origin.floor();
    d.body = memnew(RigidBody3D);
    d.body->set_name("PhysicsDebris");
    d.body->set_collision_layer(2);
    d.body->set_collision_mask(7);
    d.body->set_linear_damp(.08);
    d.body->set_angular_damp(.10);
    d.body->set_use_continuous_collision_detection(true);
    Ref<PhysicsMaterial> material;
    material.instantiate();
    material->set_friction(.85);
    material->set_bounce(.02);
    d.body->set_physics_material_override(material);
    d.body->set_freeze_enabled(true);
    add_child(d.body);
    d.body->set_position(origin);
    d.mesh = memnew(MeshInstance3D);
    d.body->add_child(d.mesh);
    rebuild(d);
    d.resume_linear = impulse;
    if (d.parts.size() > 1)
        d.resume_angular = Vector3(.35f, .04f, .85f);
    debris.push_back(std::move(d));
    return true;
}
void PhysicsBlocks::_physics_process(double) {
    ALTARIA_PROFILE("physicsblocks.physics");
    for (auto &d : debris) {
        bool far = d.body->get_global_position().distance_to(viewer) > settings.physics_distance;
        bool ready = !far && world->debris_collision_ready(
            d.mesh->get_global_transform().xform(d.mesh->get_aabb()).grow(4.f));
        bool frozen = far || !ready;
        if (d.body->is_freeze_enabled() != frozen) {
            if (frozen) {
                d.resume_linear = d.body->get_linear_velocity();
                d.resume_angular = d.body->get_angular_velocity();
            }
            d.body->set_freeze_enabled(frozen);
            if (!frozen) {
                d.body->set_linear_velocity(d.resume_linear);
                d.body->set_angular_velocity(d.resume_angular);
                d.body->set_sleeping(false);
            }
        }
        d.waiting = !ready;
        d.distant = far;
    }
}
bool PhysicsBlocks::collides_box(Vector3 minimum, Vector3 maximum) {
    if (!is_inside_tree() || debris.empty())
        return false;
    auto size = maximum - minimum;
    if (!player_box->get_size().is_equal_approx(size))
        player_box->set_size(size);
    player_query->set_transform(Transform3D(Basis(), (minimum + maximum) * .5f));
    auto state = get_world_3d()->get_direct_space_state();
    return state && !state->intersect_shape(player_query, 1).is_empty();
}
bool PhysicsBlocks::release_piece(BlockId block, Appearance appearance, Vector3 origin, String &error) {
    if (!world->registry().get(block).solid || debris.size() >= size_t(settings.max_bodies)) {
        error = "Invalid physical material or body budget reached";
        return false;
    }
    auto column = world->generator()->column(int(std::floor(origin.x)), int(std::floor(origin.z)));
    if (!add({{{0, 0, 0}, block}}, {appearance}, origin, column, Vector3()))
        return false;
    debris.back().texture_anchor = origin;
    debris.back().mesh->set_instance_shader_parameter("texture_anchor", origin);
    return true;
}
void PhysicsBlocks::discard_last() {
    if (!debris.empty()) {
        debris.back().body->queue_free();
        debris.pop_back();
    }
}
bool PhysicsBlocks::release(IVec3 p, bool tree, String &error) {
    auto id = world->block_at(p.x, p.y, p.z);
    if (!world->registry().get(id).solid || id == world->registry().bedrock) {
        error = "Select a solid, removable block.";
        return false;
    }
    if (debris.size() >= size_t(settings.max_bodies)) {
        error = "Physics body budget reached. Use physics clear to remove debris.";
        return false;
    }
    std::vector<std::pair<IVec3, BlockId>> parts;
    std::vector<Appearance> appearances;
    std::deque<IVec3> queue;
    std::unordered_set<IVec3, CoordHash> visited;
    bool is_tree = tree && world->registry().get(id).wood;
    std::unordered_set<IVec3, CoordHash> generated_tree;
    if (is_tree) {
        const auto &generator = *world->generator();
        for (const auto &site : generator.tree_sites(p.x - 6, p.z - 6, p.x + 7, p.z + 7, true)) {
            const auto site_parts = generator.tree_parts(site);
            if (std::none_of(site_parts.begin(), site_parts.end(), [&](const TreePart &part) {
                    return part.first == p && part.second == id;
                }))
                continue;
            for (const auto &part : site_parts)
                generated_tree.insert(part.first);
            break;
        }
    }
    queue.push_back(p);
    while (!queue.empty()) {
        auto c = queue.front();
        queue.pop_front();
        if (!visited.insert(c).second)
            continue;
        if (std::abs(c.x - p.x) > 5 || std::abs(c.z - p.z) > 5 || c.y < p.y || c.y > p.y + 18)
            continue;
        if (!generated_tree.empty() && !generated_tree.count(c))
            continue;
        auto block = world->block_at(c.x, c.y, c.z);
        auto &def = world->registry().get(block);
        if (is_tree ? (!def.wood && !def.foliage) : !(c == p))
            continue;
        auto a = world->appearance_at(c);
        uint8_t shape = world->shape_at(c.x, c.y, c.z);
        unpack_shape(shape,a);
        parts.push_back({c, block});
        appearances.push_back(a);
        if (parts.size() > size_t(settings.max_parts)) {
            error = "Connected tree exceeds physics part budget.";
            return false;
        }
        if (is_tree)
            for (auto n :
                 std::array<IVec3, 6>{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}})
                queue.push_back({c.x + n.x, c.y + n.y, c.z + n.z});
    }
    if (parts.empty()) {
        error = "Nothing to release.";
        return false;
    }
    auto column = world->generator()->column(p.x, p.z);
    for (auto &part : parts) {
        if (!world->edit({0x50485953ULL, ++physics_sequence, part.first, world->registry().air}, error))
            return false;
        part.first = {part.first.x - p.x, part.first.y - p.y, part.first.z - p.z};
    }
    return add(std::move(parts), std::move(appearances), Vector3(p.x, p.y, p.z) + Vector3(0, .04f, 0), column,
               Vector3(.35f, 0, .15f));
}
PhysicsBlocks::Hit PhysicsBlocks::pick(Vector3 origin, Vector3 direction, float reach) const {
    Hit result;
    for (size_t i = 0; i < debris.size(); i++) {
        auto &d = debris[i];
        auto inv = d.body->get_global_transform().affine_inverse();
        auto o = inv.xform(origin), dir = inv.basis.xform(direction);
        if (o.length() > reach + 24)
            continue;
        for (size_t j = 0; j < d.parts.size(); j++) {
            auto &p = d.parts[j].first;
            auto &a = d.appearances[j];
            float t = shape_ray(pack_shape(a.shape, a.rotation),
                                {o.x - p.x, o.y - p.y, o.z - p.z}, {dir.x, dir.y, dir.z}, reach);
            if (t >= 0 && t < result.distance)
                result = {int(i), int(j), t};
        }
    }
    return result;
}
bool PhysicsBlocks::selection_for(Hit hit, Transform3D &transform, BlockId &block, uint8_t &shape) const {
    if (hit.body < 0 || hit.body >= int(debris.size()))
        return false;
    const auto &d = debris[hit.body];
    if (hit.part < 0 || hit.part >= int(d.parts.size()))
        return false;
    auto &p = d.parts[hit.part];
    auto &a = d.appearances[hit.part];
    shape = pack_shape(a.shape, a.rotation);
    block = p.second;
    transform =
        d.body->get_global_transform() * Transform3D(Basis(), Vector3(p.first.x, p.first.y, p.first.z));
    return true;
}
bool PhysicsBlocks::detach(Hit hit, Vector3 impulse, String &error) {
    if (hit.body < 0 || hit.body >= int(debris.size()) || hit.part < 0 ||
        hit.part >= int(debris[hit.body].parts.size()))
        return false;
    auto &d = debris[hit.body];
    if (d.parts.size() == 1) {
        d.body->queue_free();
        debris.erase(debris.begin() + hit.body);
        return true;
    }
    if (debris.size() >= size_t(settings.max_bodies)) {
        error = "Physics body budget reached.";
        return false;
    }
    auto part = d.parts[hit.part];
    auto appearance = d.appearances[hit.part];
    auto transform = d.body->get_global_transform();
    auto column = d.column;
    auto texture_anchor = d.texture_anchor + Vector3(part.first.x, part.first.y, part.first.z);
    Vector3 location = transform.xform(Vector3(part.first.x, part.first.y, part.first.z));
    d.parts.erase(d.parts.begin() + hit.part);
    d.appearances.erase(d.appearances.begin() + hit.part);
    rebuild(d);
    if (!add({{{0, 0, 0}, part.second}}, {appearance}, location, column, impulse))
        return false;
    debris.back().texture_anchor = texture_anchor;
    debris.back().mesh->set_instance_shader_parameter("texture_anchor", texture_anchor);
    debris.back().body->set_basis(transform.basis);
    return true;
}
static Array xyz(Vector3 p) {
    Array a;
    a.push_back(p.x);
    a.push_back(p.y);
    a.push_back(p.z);
    return a;
}
static Vector3 vec(const Variant &v) {
    Array a = v;
    return a.size() == 3 ? Vector3(a[0], a[1], a[2]) : Vector3();
}
void PhysicsBlocks::clear() {
    for (auto &d : debris)
        d.body->queue_free();
    debris.clear();
}
Array PhysicsBlocks::save() const {
    Array records;
    for (auto &d : debris) {
        Dictionary r;
        r["position"] = xyz(d.body->get_position());
        r["texture_anchor"] = xyz(d.texture_anchor);
        r["rotation"] = xyz(d.body->get_rotation());
        r["velocity"] = xyz(d.body->is_freeze_enabled() ? d.resume_linear : d.body->get_linear_velocity());
        r["angular_velocity"] =
            xyz(d.body->is_freeze_enabled() ? d.resume_angular : d.body->get_angular_velocity());
        r["tint"] = int64_t(pack_rgb(d.column.grass_tint));
        r["brightness"] = d.column.brightness;
        r["preserve"] = d.column.preserve;
        Array parts;
        for (size_t i = 0; i < d.parts.size(); i++) {
            Array p;
            auto &c = d.parts[i].first;
            auto &a = d.appearances[i];
            for (int value :
                 {c.x, c.y, c.z, int(d.parts[i].second), int(a.tint), int(a.preserve), int(a.brightness),
                  int(a.overlay), int(a.decoration), int(a.painted), int(a.shape), int(a.rotation)})
                p.push_back(value);
            parts.push_back(p);
        }
        r["parts"] = parts;
        records.push_back(r);
    }
    return records;
}
bool PhysicsBlocks::restore(const Array &records, String &error) {
    if (records.size() > settings.max_bodies) {
        error = "Saved debris exceeds configured body budget.";
        return false;
    }
    for (int i = 0; i < records.size(); i++) {
        if (records[i].get_type() != Variant::DICTIONARY) {
            error = "Invalid debris record";
            return false;
        }
        Dictionary r = records[i];
        Array parts = r.get("parts", Array());
        if (parts.is_empty() || parts.size() > settings.max_parts) {
            error = "Invalid debris part count";
            return false;
        }
        std::vector<std::pair<IVec3, BlockId>> blocks;
        std::vector<Appearance> surfaces;
        for (int j = 0; j < parts.size(); j++) {
            Array p = parts[j];
            if (p.size() != 12) {
                error = "Invalid debris part";
                return false;
            }
            int id = p[3], shape = p[10], rotation = p[11];
            if (id < 0 || !world->registry().valid(id) || !world->registry().get(id).solid || shape < 0 ||
                !valid_shape_id(shape) || rotation < 0 || rotation > 3) {
                error = "Invalid debris material or shape";
                return false;
            }
            for (int k = 0; k < 3; k++)
                if (std::abs(int64_t(p[k])) > 32) {
                    error = "Invalid debris local coordinate";
                    return false;
                }
            if (int64_t(p[4]) < 0 || int64_t(p[4]) > 0xffffff || int64_t(p[5]) < 0 || int64_t(p[5]) > 1000 ||
                int64_t(p[6]) < 0 || int64_t(p[6]) > 2000 || int64_t(p[7]) < 0 || int64_t(p[7]) > 65535 ||
                int64_t(p[8]) < 0 || int64_t(p[8]) > 65535 || int64_t(p[9]) < 0 || int64_t(p[9]) > 1) {
                error = "Invalid saved debris appearance";
                return false;
            }
            for (int k : {7, 8}) {
                int64_t id = p[k];
                if (id != 65535 && (id >= int64_t(world->registry().overlays.size()) ||
                                    world->registry().overlays[id].key.empty())) {
                    error = "Unknown saved debris overlay";
                    return false;
                }
            }
            blocks.push_back({{int(p[0]), int(p[1]), int(p[2])}, BlockId(id)});
            surfaces.push_back({uint32_t(int64_t(p[4])), uint16_t(int(p[5])), uint16_t(int(p[6])),
                                uint16_t(int(p[7])), uint16_t(int(p[8])), bool(p[9]), uint8_t(shape),
                                uint8_t(rotation)});
        }
        auto position = vec(r.get("position", Array()));
        auto rotation = vec(r.get("rotation", Array()));
        auto velocity = vec(r.get("velocity", Array()));
        if (!position.is_finite() || !rotation.is_finite() || !velocity.is_finite() ||
            position.length() > 2000000) {
            error = "Invalid debris transform";
            return false;
        }
        Column column;
        column.grass_tint = unpack_rgb(int64_t(r.get("tint", 0xffffff)));
        column.brightness = r.get("brightness", 1.35);
        column.preserve = r.get("preserve", .65);
        if (!std::isfinite(column.brightness) || !std::isfinite(column.preserve) || column.brightness < 0 ||
            column.brightness > 2 || column.preserve < 0 || column.preserve > 1 ||
            !vec(r.get("angular_velocity", Array())).is_finite()) {
            error = "Invalid debris tint or velocity";
            return false;
        }
        if (!add(std::move(blocks), std::move(surfaces), position, column, velocity)) {
            error = "Could not restore debris";
            return false;
        }
        auto anchor = r.has("texture_anchor") ? vec(r["texture_anchor"]) : position;
        if (!anchor.is_finite() || anchor.length() > 2000000) {
            error = "Invalid texture anchor";
            return false;
        }
        debris.back().texture_anchor = anchor;
        debris.back().mesh->set_instance_shader_parameter("texture_anchor", anchor);
        debris.back().body->set_rotation(rotation);
        debris.back().resume_angular = vec(r.get("angular_velocity", Array()));
    }
    return true;
}
std::pair<float, float> PhysicsBlocks::thermal_influence(Vector3 position) const {
    float cold = 0, warm = 0;
    for (auto &d : debris) {
        auto local = d.body->get_global_transform().affine_inverse().xform(position);
        if (local.length() > 40)
            continue;
        for (auto &part : d.parts) {
            const auto &block = world->registry().get(part.second);
            if (block.thermal_radius <= 0)
                continue;
            float distance =
                local.distance_to(Vector3(part.first.x + .5f, part.first.y + .5f, part.first.z + .5f));
            if (distance >= block.thermal_radius)
                continue;
            float falloff = 1 - distance / block.thermal_radius;
            float value = block.thermal_delta * falloff * falloff;
            cold = std::min(cold, value);
            warm = std::max(warm, value);
        }
    }
    return {cold, warm};
}
Dictionary PhysicsBlocks::stats() const {
    Dictionary s;
    int sleeping = 0, frozen = 0, parts = 0;
    Array positions, rotations;
    for (auto &d : debris) {
        sleeping += d.body->is_sleeping();
        frozen += d.body->is_freeze_enabled();
        parts += d.parts.size();
        positions.push_back(d.body->get_position());
        rotations.push_back(d.body->get_rotation());
    }
    s["physics_bodies"] = int(debris.size());
    s["physics_parts"] = parts;
    s["physics_sleeping"] = sleeping;
    s["physics_frozen"] = frozen;
    s["physics_positions"] = positions;
    s["physics_rotations"] = rotations;
    return s;
}
} // namespace godot
