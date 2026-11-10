#include "built_world.h"
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
static uint64_t hash_value(uint64_t v) {
    v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ULL;
    v = (v ^ (v >> 27)) * 0x94d049bb133111ebULL;
    return v ^ (v >> 31);
}
static uint64_t checksum(char op, const BuiltBlock &b) {
    uint64_t h = hash_value(b.id) ^ uint64_t(op);
    uint64_t values[] = {
        uint32_t(b.origin8.x),   uint32_t(b.origin8.y), uint32_t(b.origin8.z),   b.block,
        b.appearance.tint,       b.appearance.preserve, b.appearance.brightness, b.appearance.overlay,
        b.appearance.decoration, b.appearance.painted,  b.appearance.shape,      b.appearance.rotation};
    for (auto value : values)
        h = hash_value(h ^ value);
    if (b.appearance.terrain != -1) h = hash_value(h ^ (0x54455200ULL + unsigned(b.appearance.terrain)));
    return h;
}
bool BuiltWorld::record(char op, const BuiltBlock &b, String &error) {
    FILE *f = std::fopen(journal.c_str(), "ab");
    if (!f) {
        error = "Cannot open construction journal";
        return false;
    }
    auto &a = b.appearance;
    int n =
        std::fprintf(f, "B2 %c %llu %d %d %d %u %u %u %u %u %u %u %u %u %u %llu\n", op, (unsigned long long)b.id,
                     b.origin8.x, b.origin8.y, b.origin8.z, unsigned(b.block), a.tint, unsigned(a.preserve),
                     unsigned(a.brightness), unsigned(a.overlay), unsigned(a.decoration), unsigned(a.painted),
                     unsigned(a.shape), unsigned(a.rotation), unsigned(a.terrain+1), (unsigned long long)checksum(op, b));
    bool ok = n > 0 && std::fflush(f) == 0;
    if (std::fclose(f) != 0)
        ok = false;
    if (!ok)
        error = "Cannot persist construction change";
    return ok;
}
void BuiltWorld::apply(char op, const BuiltBlock &b) {
    if(op!='P' || b.appearance.terrain<=0)meshed_terrain.erase(b.id);
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
    } else
        store.erase(b.id);
    store.next_id = std::max(store.next_id, b.id + 1);
    refresh_world_ramps(b.position());
    store.refresh_connections(b.position());
    mark_shape_changes();
    if(terrain_callback){auto p=b.position();terrain_callback(Vector3(p[0],p[1],p[2]));}
}
void BuiltWorld::refresh_world_ramps(Point center) {
    int cx=int(std::floor(center[0])),cz=int(std::floor(center[2])),cy=int(std::floor(center[1]));
    for(int z=cz-1;z<=cz+1;z++)for(int x=cx-1;x<=cx+1;x++)for(int y=cy-1;y<=cy+1;y++) {
        IVec3 p{x,y,z};
        if(world->appearance_at(p).terrain>0){world->set_ramp_connection(p,255);continue;}
        auto id=world->block_at(x,y,z);
        if(!world->registry().get(id).solid) {world->set_ramp_connection(p,255);continue;}
        auto base=world->base_shape_at(x,y,z);
        if (shape_type(base) == 8) {
            auto shape = connect_terrain([&](int dx, int dy, int dz) {
                int qx = x + dx, qy = y + dy, qz = z + dz;
                BlockId tid = world->block_at(qx, qy, qz);
                const auto &def = world->registry().get(tid);
                if (def.solid && !def.foliage && !def.wood)
                    return true;
                for (auto bid : store.nearby({float(qx) + .1f, float(qy) + .1f, float(qz) + .1f},
                                             {float(qx) + .9f, float(qy) + .9f, float(qz) + .9f}))
                    if (world->registry().get(store.blocks.at(bid).block).solid)
                        return true;
                return false;
            });
            for(auto other:store.nearby({float(x),float(y),float(z)},{float(x+1),float(y+1),float(z+1)})) {
                auto &block=store.blocks.at(other);
                if(shapes_overlap(shape,{float(x),float(y),float(z)},block.shape(),block.position())) {shape=world->shape_at(x,y,z);break;}
            }
            world->set_ramp_connection(p,shape==world->natural_shape_at(x,y,z) ? uint8_t(255) : shape);
            continue;
        }
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
    journal = ProjectSettings::get_singleton()->globalize_path(path).utf8().get_data();
    FILE *f = std::fopen(journal.c_str(), "rb");
    if (!f) {
        if (errno == ENOENT)
            return true;
        error = "Cannot read construction journal";
        return false;
    }
    char line[768];
    uintmax_t valid = 0;
    while (std::fgets(line, sizeof(line), f)) {
        size_t length = std::strlen(line);
        if (!length || line[length - 1] != '\n') {
            bool torn = std::feof(f);
            std::fclose(f);
            if (!torn) {
                error = "Oversized construction record";
                return false;
            }
            std::error_code ec;
            std::filesystem::resize_file(journal, valid, ec);
            if (ec) {
                error = "Cannot recover interrupted construction";
                return false;
            }
            return true;
        }
        valid += length;
        char op, extra;
        unsigned long long id, sum;
        long long x, y, z;
        unsigned block, tint, preserve, brightness, overlay, deco, paint, shape, rotation, terrain=0;
        bool v2 = std::strncmp(line,"B2 ",3)==0;
        int count;
        if (v2) count=std::sscanf(line,"B2 %c %llu %lld %lld %lld %u %u %u %u %u %u %u %u %u %u %llu %c", &op,
                                  &id,&x,&y,&z,&block,&tint,&preserve,&brightness,&overlay,&deco,&paint,&shape,&rotation,&terrain,&sum,&extra);
        else count = std::sscanf(line, "B1 %c %llu %lld %lld %lld %u %u %u %u %u %u %u %u %u %llu %c", &op,
                                &id, &x, &y, &z, &block, &tint, &preserve, &brightness, &overlay, &deco,
                                &paint, &shape, &rotation, &sum, &extra);
        if (count != (v2 ? 16 : 15) || terrain > 2 || (op != 'P' && op != 'D') || !id || id > 1000000000000ULL ||
            std::abs(x) > 8000000 || std::abs(z) > 8000000 || y <= world->settings().min_y * 8 ||
            y >= world->settings().max_y * 8 || block > 4095 || !world->registry().valid(block) ||
            block == world->registry().air || tint > 0xffffff || preserve > 1000 || brightness > 2000 ||
            overlay > 65535 || deco > 65535 || paint > 1 || !valid_shape_id(shape) || rotation > 3) {
            std::fclose(f);
            error = "Corrupt construction journal";
            return false;
        }
        for (auto value : {overlay, deco})
            if (value != 65535 && (value >= world->registry().overlays.size() ||
                                   world->registry().overlays[value].key.empty())) {
                std::fclose(f);
                error = "Unknown construction overlay";
                return false;
            }
        BuiltBlock b{id,
                     {int(x), int(y), int(z)},
                     BlockId(block),
                     {tint, uint16_t(preserve), uint16_t(brightness), uint16_t(overlay), uint16_t(deco),
                      paint != 0, uint8_t(shape), uint8_t(rotation)}};
        b.appearance.terrain = int8_t(terrain)-1;
        if (checksum(op, b) != sum) {
            std::fclose(f);
            error = "Construction checksum mismatch";
            return false;
        }
        if (!v2 && b.appearance.shape==8) { b.appearance.shape=0; b.appearance.terrain=1; }
        apply(op, b);
    }
    bool ok = !std::ferror(f);
    std::fclose(f);
    if (!ok)
        error = "Cannot read construction journal";
    return ok;
}
const BuiltBlock *BuiltWorld::get(uint64_t id) const {
    auto it = store.blocks.find(id);
    return it == store.blocks.end() ? nullptr : &it->second;
}
uint64_t BuiltWorld::place(Vector3 p, int block, int shape, int rotation, String &error) {
    if (!p.is_finite() || std::abs(p.x) > 1000000 || std::abs(p.z) > 1000000 ||
        p.y <= world->settings().min_y || p.y >= world->settings().max_y || block < 0 || block > 4095 ||
        !valid_shape_id(shape) || rotation < 0 || rotation > 3) {
        error = "Invalid construction position or form";
        return 0;
    }
    BuiltBlock b;
    b.id = store.next_id;
    b.origin8 = {int(std::round(p.x * 8)), int(std::round(p.y * 8)), int(std::round(p.z * 8))};
    b.block = block;
    b.appearance.shape = shape;
    b.appearance.rotation = rotation;
    if (shape==8) { b.appearance.shape=0; b.appearance.terrain=1; }
    b.connected_shape=store.connection_shape(b,&b);
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
    if (!record('P', b, error))
        return 0;
    apply('P', b);
    return b.id;
}
uint64_t BuiltWorld::attach(Vector3 hit, Vector3 normal, Vector3 preferred, Vector3 source,
                            uint8_t source_shape, int block, int shape, int rotation, String &error) {
    int placement_shape=shape==int(BlockShape::Terrain) ? 0 : shape;
    if(shape_type(pack_shape(shape,rotation))==3 && shape_type(source_shape)==1) {
        bool top=normal.y>.99f;
        shape=ramp_shape(top ? RampScale::Half : RampScale::Raised,RampCorner::Straight,0);
        preferred.y=source.y+(top ? .5f : 0.f);
        if(std::abs(normal.x)>.5f || std::abs(normal.z)>.5f)
            rotation=normal.x>.5f ? 0 : normal.z>.5f ? 1 : normal.x<-.5f ? 2 : 3;
    }
    if(shape!=int(BlockShape::Terrain))placement_shape=shape;
    // Try centered support first, then nearby half-block offsets for narrow stair
    // treads and pillars. Contact is solved from the clicked face's actual plane.
    int axis = 0;
    for (int j = 1; j < 3; j++)
        if (std::abs(normal[j]) > std::abs(normal[axis]))
            axis = j;
    int a = (axis + 1) % 3, b = (axis + 2) % 3;
    for (float radius : {0.f, .125f, .25f, .5f})
        for (int direction = 0; direction < (radius == 0 ? 1 : 8); direction++) {
            Vector3 candidate = preferred;
            const int dx[] = {1, -1, 0, 0, 1, -1, 1, -1}, dz[] = {0, 0, 1, -1, 1, -1, -1, 1};
            if (radius > 0) {
                candidate[a] += dx[direction] * radius;
                candidate[b] += dz[direction] * radius;
            }
            auto origin =
                snap_placement({hit.x, hit.y, hit.z}, {normal.x, normal.y, normal.z},
                               pack_shape(placement_shape, rotation), {candidate.x, candidate.y, candidate.z});
            Vector3 contact = Vector3(origin.x / 8.f, origin.y / 8.f, origin.z / 8.f) - normal * .002f;
            if (!shapes_overlap(pack_shape(placement_shape, rotation), {contact.x, contact.y, contact.z},
                                source_shape, {source.x, source.y, source.z}))
                continue;
            auto id =
                place(Vector3(origin.x / 8.f, origin.y / 8.f, origin.z / 8.f), block, shape, rotation, error);
            if (id)
                return id;
        }
    return 0;
}
bool BuiltWorld::remove(uint64_t id, String &error) {
    auto b = get(id);
    if (!b) {
        error = "Building piece no longer exists";
        return false;
    }
    auto copy = *b;
    if (!record('D', copy, error))
        return false;
    apply('D', copy);
    return true;
}
bool BuiltWorld::appearance(uint64_t id, const Appearance &value, String &error) {
    auto b = get(id);
    if (!b)
        return false;
    auto copy = *b;
    copy.appearance = value;
    if (value.terrain < -1 || value.terrain > 1 || value.tint > 0xffffff || value.preserve > 1000 || value.brightness > 2000 || !valid_shape_id(value.shape) ||
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
    if (!record('P', copy, error))
        return false;
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
        if(b.appearance.terrain>0 && terrain_meshed(b.id))continue;
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
        if(!world->registry().get(b.block).solid || (b.appearance.terrain>0 && terrain_meshed(id)))continue;
        auto p=b.position();
        if(shape_intersects(b.shape(),{lo.x-p[0],lo.y-p[1],lo.z-p[2]},{hi.x-p[0],hi.y-p[1],hi.z-p[2]}))return true;
    }
    return false;
}
bool BuiltWorld::source_rendered(Vector3 p) const {
    return !dirty.count({int(std::floor(p.x/32))*32,0,int(std::floor(p.z/32))*32});
}
void BuiltWorld::clear_terrain_meshed(uint64_t id) {
    if(meshed_terrain.erase(id))if(auto source=get(id))dirty.insert(region_of(*source));
}
void BuiltWorld::set_terrain_meshed(uint64_t id) {
    if(meshed_terrain.insert(id).second)if(auto source=get(id))dirty.insert(region_of(*source));
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
    for (auto id : ids->second) {
        const auto &b = store.blocks.at(id);
        if(b.appearance.terrain>0 && terrain_meshed(b.id))continue;
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
        if (world->registry().get(b.block).solid) {
            int offset = collision.vertices.size();
            collision.vertices.insert(collision.vertices.end(), surface.vertices.begin() + start,
                                      surface.vertices.end());
            for (size_t i = index_start; i < surface.indices.size(); i++)
                collision.indices.push_back(offset + surface.indices[i] - int(start));
        }
    }
    region.mesh = memnew(MeshInstance3D);
    region.mesh->set_name("ConstructionRegion");
    region.mesh->set_position(Vector3(key.x, 0, key.z));
    region.mesh->set_mesh(world->surface_mesh(surface));
    region.mesh->set_visibility_range_end(world->settings().view_distance);
    add_child(region.mesh);
    region.grass = nullptr;
    region.grass_near = false;
    bool near = tile_distance({key.x, key.z, 0}, viewer.x, viewer.z) < world->settings().near_distance;
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
        d["terrain"] = b.appearance.terrain>0;
        d["shape_code"] = int(b.shape());
        d["rotation"] = b.appearance.rotation;
        auto ramp=ramp_spec(b.shape());
        if(ramp.valid) {d["ramp_corner"]=ramp_corner_name(ramp.corner);d["ramp_rise"]=ramp.rise();d["ramp_low"]=ramp.low();}
        if (shape_type(b.shape()) == 8) {
            Array th;
            for (float h : terrain_heights(b.shape()))
                th.push_back(h);
            d["terrain_heights"] = th;
        }
        d["painted"] = b.appearance.painted;
        d["color"] = int64_t(b.appearance.tint);
        values.push_back(d);
    }
    return values;
}
} // namespace godot
