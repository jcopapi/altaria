#include "built_world.h"
#include "../debug/frame_profile.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
namespace godot {
using namespace altaria;
static IVec3 region_of(const BuiltBlock &b) {
    auto p = b.position();
    return {int(std::floor(p[0] / 32)) * 32, 0, int(std::floor(p[2] / 32)) * 32};
}
void BuiltWorld::apply(char op, const BuiltBlock &b) {
    if (auto previous = get(b.id)) {
        auto region = region_of(*previous);
        auto &ids = regions[region];
        ids.erase(std::remove(ids.begin(), ids.end(), b.id), ids.end());
        dirty.insert(region);
        if (ids.empty())
            regions.erase(region);
    }
    if (op == 'P') {
        store.put(b);
        auto region = region_of(b);
        regions[region].push_back(b.id);
        dirty.insert(region);
    } else {
        animated_pieces.erase(b.id);
        store.erase(b.id);
    }
    store.next_id = std::max(store.next_id, b.id + 1);
    refresh_world_ramps(b.position());
    store.refresh_connections(b.position());
    mark_shape_changes();
}
void BuiltWorld::refresh_world_ramps(Point center) {
    int cx=int(std::floor(center[0])),cz=int(std::floor(center[2])),cy=int(std::floor(center[1]));
    for(int z=cz-1;z<=cz+1;z++)for(int x=cx-1;x<=cx+1;x++)for(int y=cy-1;y<=cy+1;y++) {
        IVec3 p{x,y,z};
        auto id=world->block_at(x,y,z);
        if(!world->registry().get(id).solid) {world->set_ramp_connection(p,255);continue;}
        auto base=world->base_shape_at(x,y,z);
        auto source=ramp_spec(base);
        if(!source.valid) {world->set_ramp_connection(p,255);continue;}
        auto shape=connect_ramp(base,[&](int dx,int dz){return store.neighbor_ramp((x+dx)*8,(z+dz)*8,y+source.low());});
        for(auto other:store.nearby({float(x),float(y),float(z)},{float(x+1),float(y+1),float(z+1)})) {
            auto &block=store.blocks.at(other);
            if(shapes_overlap(shape,{float(x),float(y),float(z)},block.shape(),block.position())) {shape=world->shape_at(x,y,z);break;}
        }
        world->set_ramp_connection(p,shape==world->natural_shape_at(x,y,z) ? uint8_t(255) : shape);
    }
}
void BuiltWorld::mark_shape_changes() {
    for(auto id:store.shape_changes)if(auto b=get(id))dirty.insert(region_of(*b));
    store.shape_changes.clear();
}
bool BuiltWorld::configure(VoxelWorld *w, const String &path, String &error) {
    world = w;
    store.terrain_lookup=[this](int x,int y,int z) {
        auto id=world->block_at(x,y,z);
        return std::make_pair(id,world->registry().get(id).solid ? world->shape_at(x,y,z) : uint8_t(255));
    };
    world->set_edit_callback([this](IVec3 p) {
        Point center{float(p.x),float(p.y),float(p.z)};
        refresh_world_ramps(center);store.refresh_connections(center);mark_shape_changes();
    });
    return true;
}
const BuiltBlock *BuiltWorld::get(uint64_t id) const {
    auto it = store.blocks.find(id);
    return it == store.blocks.end() ? nullptr : &it->second;
}
uint64_t BuiltWorld::place(Vector3 p, int block, int shape, int rotation, String &error, uint64_t replace, BuiltBlock *preview) {
    if (!p.is_finite() || std::abs(p.x) > 1000000 || std::abs(p.z) > 1000000 ||
        p.y <= world->settings().min_y || p.y >= world->settings().max_y || block < 0 || block > 4095 ||
        !valid_shape_id(shape) || rotation < 0 || rotation > 3) {
        error = "Invalid construction position or form";
        return 0;
    }
    BuiltBlock b;
    b.id = replace ? replace : store.next_id;
    b.origin8 = {int(std::round(p.x * 8)), int(std::round(p.y * 8)), int(std::round(p.z * 8))};
    b.block = block;
    b.appearance.shape = shape;
    b.appearance.rotation = rotation;
    b.connected_shape=store.connection_shape(b,&b);
    if (b.block == world->registry().grass) {
        const auto &registry = world->registry();
        const auto origin = b.position();
        const auto x = int(std::floor(origin[0] + .5f));
        const auto y = int(std::floor(origin[1] - .001f));
        const auto z = int(std::floor(origin[2] + .5f));
        auto support = world->block_at(x, y, z);
        if (const auto *previous = get(replace); previous && previous->block != registry.grass)
            support = previous->block;
        if (support == registry.air) {
            const auto below = pick(Vector3(origin[0] + .5f, origin[1] + .001f, origin[2] + .5f),
                                    Vector3(0, -1, 0), .01f);
            if (const auto *piece = get(below.id); piece && piece->id != replace)
                support = piece->block;
        }
        if (support != registry.air && support != registry.grass) {
            b.appearance.painted = true;
            b.appearance.tint = pack_rgb(grass_on_support(registry.get(b.block).color, registry.get(support).color));
        }
    }
    if (guard && !guard(b)) {
        error = "Building intersects player";
        return 0;
    }
    std::string message;
    if (!store.can_place(
            b, world->registry(), world->settings(),
            [this](int x, int y, int z) {
                return std::make_pair(world->block_at(x, y, z), world->shape_at(x, y, z));
            },
            message)) {
        error = String(message.c_str());
        return 0;
    }
    if (preview)
        *preview = b;
    else
        apply('P', b);
    return b.id;
}
uint64_t BuiltWorld::attach(Vector3 hit, Vector3 normal, Vector3 preferred, Vector3 source,
                            uint8_t source_shape, int block, int shape, int rotation, String &error, BuiltBlock *preview) {
    const bool pillar = shape == int(BlockShape::Pillar) || shape == int(BlockShape::MiniPillar);
    const int axis = std::abs(normal.y) >= std::max(std::abs(normal.x), std::abs(normal.z)) ? 1 :
                     std::abs(normal.x) >= std::abs(normal.z) ? 0 : 2;
    if (pillar && axis != 1) {
        const int horizontal_axis = axis == 0 ? 0 : 1;
        shape = shape == int(BlockShape::Pillar) ? horizontal_pillar(horizontal_axis)
                                                : horizontal_mini_pillar(horizontal_axis);
        rotation = 0;
    }
    const bool inverted = normal.y < -.5f || (axis != 1 && hit.y - source.y > .5f);
    if (shape == int(BlockShape::Ramp) && inverted) {
        shape = upside_down_ramp(pack_shape(shape, rotation));
        rotation = 0;
    } else if (shape_type(pack_shape(shape, rotation)) == int(BlockShape::Ramp) &&
        shape_type(source_shape) == int(BlockShape::Slab)) {
        shape = ramp_shape(axis == 1 && normal.y > 0 ? RampScale::Half : RampScale::Raised,
                           RampCorner::Straight);
        if (axis != 1)
            preferred.y = source.y;
    }

    if (shape == int(BlockShape::Stairs) && inverted) {
        shape = upside_down_stairs(pack_shape(shape, rotation));
        rotation = 0;
    }
    const bool wall = shape == int(BlockShape::VerticalSlab);
    if (wall && axis != 1) {
        rotation = axis == 0 ? (normal.x > 0 ? 0 : 2) : (normal.z > 0 ? 1 : 3);
    }
    const auto packed = pack_shape(shape, rotation);
    // Side attachments align to the face center. Across the height of a
    // vertical face, use the source's cell origin so slabs share its grid.
    if (axis != 1)
        preferred.y = pillar ? source.y + (shape_bounds(source_shape).second[1] +
                                            shape_bounds(source_shape).first[1] - 1.f) * .5f : source.y;
    for (int i : {0, 2})
        if (i != axis && (!pillar || axis != 1))
            preferred[i] = std::round(preferred[i] * 2.f) * .5f;
    if (wall) {
        const auto [lo, hi] = shape_bounds(packed);
        for (int i : {0, 2}) {
            if (i == axis)
                continue;
            preferred[i] = source[i];
            if (hi[i] - lo[i] < .75f)
                preferred[i] += (hit[i] - source[i] >= .5f ? .5f : 0.f) - lo[i];
        }
    }
    if (pillar && axis == 1) {
        preferred.x = source.x;
        preferred.z = source.z;
    }
    const auto origin = snap_placement({hit.x, hit.y, hit.z}, {normal.x, normal.y, normal.z}, packed,
                                       {preferred.x, preferred.y, preferred.z});
    const Vector3 position(origin.x / 8.f, origin.y / 8.f, origin.z / 8.f);
    const Vector3 contact = position - normal * .002f;
    if (!shapes_overlap(packed, {contact.x, contact.y, contact.z}, source_shape,
                        {source.x, source.y, source.z})) {
        error = "No centered grid attachment on this face";
        return 0;
    }
    return place(position, block, shape, rotation, error, 0, preview);
}
bool BuiltWorld::remove(uint64_t id, String &error) {
    auto b = get(id);
    if (!b) {
        error = "Building piece no longer exists";
        return false;
    }
    auto copy = *b;
    apply('D', copy);
    return true;
}
bool BuiltWorld::appearance(uint64_t id, const Appearance &value, String &error) {
    auto b = get(id);
    if (!b)
        return false;
    auto copy = *b;
    copy.appearance = value;
    if (value.tint > 0xffffff || value.preserve > 1000 || value.brightness > 2000 || !valid_shape_id(value.shape) ||
        value.rotation > 3) {
        error = "Invalid construction appearance";
        return false;
    }
    for (auto v : {value.overlay, value.decoration})
        if (v != 65535 &&
            (v >= world->registry().overlays.size() || world->registry().overlays[v].key.empty())) {
            error = "Unknown construction overlay";
            return false;
        }
    std::string e;
    if (!store.can_place(
            copy, world->registry(), world->settings(),
            [this](int x, int y, int z) {
                return std::make_pair(world->block_at(x, y, z), world->shape_at(x, y, z));
            },
            e)) {
        error = String(e.c_str());
        return false;
    }
    apply('P', copy);
    return true;
}
BuiltWorld::Hit BuiltWorld::pick(Vector3 origin, Vector3 direction, float reach) const {
    Point lo{std::min(origin.x, origin.x + direction.x * reach) - 1,
             std::min(origin.y, origin.y + direction.y * reach) - 1,
             std::min(origin.z, origin.z + direction.z * reach) - 1};
    Point hi{std::max(origin.x, origin.x + direction.x * reach) + 1,
             std::max(origin.y, origin.y + direction.y * reach) + 1,
             std::max(origin.z, origin.z + direction.z * reach) + 1};
    Hit hit;
    if (store.blocks.empty())
        return hit;
    for (auto id : store.nearby(lo, hi)) {
        const auto &b = store.blocks.at(id);
        auto p = b.position();
        Point normal;
        float d = shape_ray(b.shape(), {origin.x - p[0], origin.y - p[1], origin.z - p[2]},
                            {direction.x, direction.y, direction.z}, reach, &normal);
        if (d >= 0 && d < hit.distance)
            hit = {id, d, normal};
    }
    return hit;
}
bool BuiltWorld::collides_box(Vector3 lo, Vector3 hi) const {
    for(auto id:store.nearby({lo.x,lo.y,lo.z},{hi.x,hi.y,hi.z})) {
        const auto &b=store.blocks.at(id);
        const auto &def=world->registry().get(b.block);
        if(!def.solid || def.foliage)continue;
        auto p=b.position();
        if(shape_intersects(b.shape(),{lo.x-p[0],lo.y-p[1],lo.z-p[2]},{hi.x-p[0],hi.y-p[1],hi.z-p[2]}))return true;
    }
    return false;
}
bool BuiltWorld::in_foliage_box(Vector3 lo, Vector3 hi) const {
    for (auto id : store.nearby({lo.x, lo.y, lo.z}, {hi.x, hi.y, hi.z})) {
        const auto &b = store.blocks.at(id);
        if (!world->registry().get(b.block).foliage)
            continue;
        auto p = b.position();
        if (shape_intersects(b.shape(), {lo.x - p[0], lo.y - p[1], lo.z - p[2]},
                             {hi.x - p[0], hi.y - p[1], hi.z - p[2]}))
            return true;
    }
    return false;
}
void BuiltWorld::set_placement_animated(uint64_t id, bool animated) {
    if (animated)
        animated_pieces.insert(id);
    else if (!animated_pieces.erase(id))
        return;
    if (const auto *piece = get(id))
        dirty.insert(region_of(*piece));
}
void BuiltWorld::rebuild(IVec3 key) {
    auto &region = meshes[key];
    if (region.mesh) {
        region.grass = nullptr;
        region.mesh->queue_free();
        region.mesh = nullptr;
        region.collider = nullptr;
    }
    auto ids = regions.find(key);
    if (ids == regions.end()) {
        meshes.erase(key);
        return;
    }
    Surface surface, collision;
    std::vector<GrassInstance> grass;
    const auto &blocks = world->registry();
    const bool near = tile_distance({key.x, key.z, 0}, viewer.x, viewer.z) < world->settings().near_distance;
    for (auto id : ids->second) {
        const auto &b = store.blocks.at(id);
        auto p = b.position();
        auto c = world->generator()->column(int(std::floor(p[0])), int(std::floor(p[2])));
        int transition_overlay = 0;
        for (auto other_id : store.nearby({p[0] + .01f, p[1] + .99f, p[2] + .01f},
                                          {p[0] + .99f, p[1] + 1.02f, p[2] + .99f})) {
            if (other_id == id)
                continue;
            const auto &other = store.blocks.at(other_id);
            const auto &above = world->registry().get(other.block);
            if (other.block != b.block && above.solid && above.transition_overlay > 0 &&
                shapes_overlap(b.shape(), {p[0], p[1] + .002f, p[2]}, other.shape(), other.position())) {
                transition_overlay = above.transition_overlay;
                break;
            }
        }
        if (transition_overlay == 0) {
            int tx = int(std::floor(p[0] + .5f)), ty = int(std::floor(p[1] + 1.f)), tz = int(std::floor(p[2] + .5f));
            BlockId above_id = world->block_at(tx, ty, tz);
            const auto &above = world->registry().get(above_id);
            if (above_id != b.block && above.solid && above.transition_overlay > 0 &&
                shapes_overlap(b.shape(), {p[0], p[1] + .002f, p[2]}, world->shape_at(tx, ty, tz),
                               {float(tx), float(ty), float(tz)}))
                transition_overlay = above.transition_overlay;
        }
        size_t start = surface.vertices.size(), index_start = surface.indices.size();
        append_block_surface(surface, {p[0] - key.x, p[1], p[2] - key.z}, b.shape(), b.block, b.appearance, c,
                             world->registry(), transition_overlay);
        for (size_t i = start; i < surface.vertices.size(); i++)
            surface.vertices[i].wind += p[1];
        const auto &def = blocks.get(b.block);
        const bool animated = animated_pieces.count(id);
        if (!animated && near && (b.block == blocks.grass || def.grass_on_rock)) {
            const auto &settings = world->settings();
            for (int i = 0; i < settings.grass_tufts; ++i) {
                auto tuft = grass_for_block(p, b.shape(), b.appearance, c, def, settings, world->last_seed(), i);
                if (!tuft)
                    continue;
                const Vector3 root(tuft->x, tuft->y, tuft->z);
                const Vector3 lo = root + Vector3(-.025f, .05f, -.025f);
                const Vector3 hi = root + Vector3(.025f, .30f, .025f);
                if (!collides_box(lo, hi) && !world->collides_box(lo, hi)) {
                    tuft->x -= key.x;
                    tuft->z -= key.z;
                    grass.push_back(*tuft);
                }
            }
        }
        if (def.solid && !def.foliage) {
            int offset = collision.vertices.size();
            collision.vertices.insert(collision.vertices.end(), surface.vertices.begin() + start,
                                      surface.vertices.end());
            for (size_t i = index_start; i < surface.indices.size(); i++) {
                const int vertex = surface.indices[i];
                if ((int(surface.vertices[vertex].decal) & 32) == 0)
                    collision.indices.push_back(offset + vertex - int(start));
            }
        }
        if (animated) {
            surface.vertices.resize(start);
            surface.indices.resize(index_start);
        }
    }
    region.mesh = memnew(MeshInstance3D);
    region.mesh->set_name("ConstructionRegion");
    region.mesh->set_position(Vector3(key.x, 0, key.z));
    region.mesh->set_mesh(world->surface_mesh(surface));
    region.mesh->set_visibility_range_end(world->settings().view_distance);
    add_child(region.mesh);
    AABB grass_bounds;
    region.grass = world->grass_batch(grass, region.mesh, grass_bounds);
    region.collision_near = near;
    region.mesh->set_cast_shadows_setting(near ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON
                                               : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    if (near && !collision.indices.empty()) {
        Ref<ConcavePolygonShape3D> shape;
        shape.instantiate();
        PackedVector3Array faces;
        faces.resize(collision.indices.size());
        auto *face_data = faces.ptrw();
        for (size_t i = 0; i < collision.indices.size(); i++) {
            auto &v = collision.vertices[collision.indices[i]];
            face_data[i] = Vector3(v.x, v.y, v.z);
        }
        shape->set_faces(faces);
        region.collider = memnew(StaticBody3D);
        region.collider->set_collision_layer(4);
        region.collider->set_collision_mask(2);
        auto node = memnew(CollisionShape3D);
        node->set_shape(shape);
        region.collider->add_child(node);
        region.mesh->add_child(region.collider);
    }
}
void BuiltWorld::_process(double delta) {
    ALTARIA_PROFILE("built.process");
    timer += delta;
    if (timer > .3) {
        timer = 0;
        for (auto &[key, region] : meshes) {
            float distance = tile_distance({key.x, key.z, 0}, viewer.x, viewer.z);
            bool near = distance < world->settings().near_distance;
            if (near != region.collision_near)
                dirty.insert(key);
            region.mesh->set_instance_shader_parameter(
                "terrain_lod",
                std::clamp(float(std::floor(std::log2(std::max(1.f, distance / 96)))), 0.f, 5.f));
        }
    }
    for (int n = 0; n < 2 && !dirty.empty(); n++) {
        auto key = *dirty.begin();
        dirty.erase(key);
        rebuild(key);
    }
}
std::pair<float, float> BuiltWorld::thermal(Vector3 p) const {
    if (store.blocks.empty())
        return {0, 0};
    float cold = 0, warm = 0, radius = 0;
    for (auto &b : world->registry().blocks)
        radius = std::max(radius, b.thermal_radius);
    for (auto id : store.nearby({p.x - radius, p.y - radius, p.z - radius},
                                {p.x + radius, p.y + radius, p.z + radius})) {
        const auto &b = store.blocks.at(id);
        const auto &def = world->registry().get(b.block);
        if (def.thermal_radius <= 0)
            continue;
        auto v = b.position();
        float d = p.distance_to(Vector3(v[0] + .5f, v[1] + .5f, v[2] + .5f));
        if (d >= def.thermal_radius)
            continue;
        float t = 1 - d / def.thermal_radius, value = def.thermal_delta * t * t;
        cold = std::min(cold, value);
        warm = std::max(warm, value);
    }
    return {cold, warm};
}
Dictionary BuiltWorld::stats() const {
    Dictionary d;
    d["placed_blocks"] = int(store.blocks.size());
    d["building_regions"] = int(meshes.size());
    d["building_pending"] = int(dirty.size());
    return d;
}
Array BuiltWorld::describe() const {
    Array values;
    for (auto &[id, b] : store.blocks) {
        Dictionary d;
        auto p = b.position();
        d["id"] = int64_t(id);
        d["position"] = Vector3(p[0], p[1], p[2]);
        d["block"] = b.block;
        d["shape"] = shape_name(shape_type(b.shape()));
        d["original_shape"] = shape_name(b.appearance.shape);
        d["shape_code"] = int(b.shape());
        d["rotation"] = b.appearance.rotation;
        auto ramp=ramp_spec(b.shape());
        if(ramp.valid) {d["ramp_corner"]=ramp_corner_name(ramp.corner);d["ramp_rise"]=ramp.rise();d["ramp_low"]=ramp.low();}
        d["painted"] = b.appearance.painted;
        d["color"] = int64_t(b.appearance.tint);
        values.push_back(d);
    }
    return values;
}
} // namespace godot
