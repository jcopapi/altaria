#include "voxel_world.h"
#include "block_shapes.h"
#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
namespace godot {
using namespace altaria;
VoxelWorld::~VoxelWorld() {
    workers.reset();
}
static Ref<Texture2DArray> build_texture_array(const Registry &r, String &error) {
    TypedArray<Ref<Image>> images;
    for (size_t layer = 0; layer < r.textures.size(); layer++) {
        const auto &definition = r.textures[layer];
        Ref<Image> image;
        if (!definition.path.empty()) {
            Ref<Texture2D> texture = ResourceLoader::get_singleton()->load(
                String(definition.path.c_str()), String(), ResourceLoader::CACHE_MODE_IGNORE);
            if (texture.is_valid())
                image = texture->get_image();
            if (image.is_null()) {
                error = "Cannot load block texture: " + String(definition.path.c_str());
                return {};
            }
            if (image->is_compressed())
                image->decompress();
            image->convert(Image::FORMAT_RGBA8);
            if (image->get_width() != 16 || image->get_height() != 16)
                image->resize(16, 16, Image::INTERPOLATE_NEAREST);
        } else {
            image = Image::create_empty(16, 16, false, Image::FORMAT_RGBA8);
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) {
                    uint32_t h = uint32_t(x + 17 * y + 131 * layer) * 0x9e3779b9u;
                    h ^= h >> 15;
                    float noise = float(h & 65535) / 65535;
                    float factor =
                        definition.generator == "solid"
                            ? 1.f
                            : (definition.generator == "sand" ? .90f + noise * .18f : .86f + noise * .25f);
                    if (definition.generator == "bark")
                        factor = (x % 5 == 0 ? .60f : .94f) + noise * .15f;
                    image->set_pixel(x, y,
                                     Color(std::clamp(definition.color.r * factor, 0.f, 1.f),
                                           std::clamp(definition.color.g * factor, 0.f, 1.f),
                                           std::clamp(definition.color.b * factor, 0.f, 1.f), 1));
                }
        }
        image->generate_mipmaps();
        images.push_back(image);
    }
    Ref<Texture2DArray> array;
    array.instantiate();
    if (array->create_from_images(images) != OK) {
        error = "Could not build block texture array";
        return {};
    }
    return array;
}
bool VoxelWorld::configure(const Settings &s, const Registry &r, int seed, const String &path,
                           String &error) {
    started = std::chrono::steady_clock::now();
    if (s.vegetation && (!r.keys.count(s.bush_block) || r.find(s.bush_block) == r.air)) {
        error = "Unknown bush material: " + String(s.bush_block.c_str());
        return false;
    }
    auto gen = std::make_shared<Generator>(s, r, seed);
    model = std::make_unique<WorldModel>(gen);
    texture_array = build_texture_array(r, error);
    if (texture_array.is_null())
        return false;
    Ref<Shader> shader =
        ResourceLoader::get_singleton()->load("res://game/world_generation/shaders/terrain.gdshader");
    if (shader.is_null()) {
        error = "Could not load terrain shader";
        return false;
    }
    solid_material.instantiate();
    solid_material->set_shader(shader);
    solid_material->set_shader_parameter("fullbright", false);
    solid_material->set_shader_parameter("block_textures", texture_array);
    solid_material->set_shader_parameter("horizon_start", std::min(s.fog_start, s.view_distance * .72f));
    solid_material->set_shader_parameter("horizon_end", s.view_distance * .94f);
    solid_material->set_shader_parameter("horizon_color", Color(.63, .74, .82));
    physical_material = solid_material->duplicate();
    physical_material->set_shader_parameter("local_textures", true);
    Ref<Shader> overlay_shader =
        ResourceLoader::get_singleton()->load("res://game/world_generation/shaders/overlay.gdshader");
    if (overlay_shader.is_null()) {
        error = "Cannot load overlay shader";
        return false;
    }
    overlay_material.instantiate();
    overlay_material->set_shader(overlay_shader);
    overlay_material->set_shader_parameter("block_textures", texture_array);
    overlay_material->set_shader_parameter("fullbright", false);
    grass_material.instantiate();
    Ref<Shader> grass_shader =
        ResourceLoader::get_singleton()->load("res://game/world_generation/shaders/grass.gdshader");
    if (grass_shader.is_null()) {
        error = "Cannot load grass shader";
        return false;
    }
    grass_material->set_shader(grass_shader);
    grass_material->set_shader_parameter("block_textures", texture_array);
    grass_material->set_shader_parameter("fade_start", s.grass_render_distance - s.grass_fade_distance);
    grass_material->set_shader_parameter("fade_end", s.grass_render_distance);
    bush_material = solid_material->duplicate();
    bush_material->set_shader_parameter("distance_limited", true);
    bush_material->set_shader_parameter("vegetation_limit", s.bush_render_distance);
    workers = std::make_unique<TerrainWorkers>(gen, s.workers);
    return true;
}
static void add_surface(const Ref<ArrayMesh> &mesh, const Surface &s, const Ref<Material> &material) {
    if (s.vertices.empty())
        return;
    PackedVector3Array positions, normals;
    PackedVector2Array uv, uv2;
    PackedFloat32Array custom0, custom1;
    PackedColorArray colors;
    PackedInt32Array indices;
    int count = int(s.vertices.size());
    positions.resize(count);
    normals.resize(count);
    uv.resize(count);
    uv2.resize(count);
    custom0.resize(count * 4);
    custom1.resize(count * 4);
    colors.resize(count);
    indices.resize(s.indices.size());
    auto pp = positions.ptrw();
    auto np = normals.ptrw();
    auto up = uv.ptrw();
    auto cp = colors.ptrw();
    auto u2 = uv2.ptrw();
    auto c0 = custom0.ptrw();
    auto c1 = custom1.ptrw();
    auto ip = indices.ptrw();
    for (int i = 0; i < count; i++) {
        const auto &v = s.vertices[i];
        pp[i] = Vector3(v.x, v.y, v.z);
        np[i] = Vector3(v.nx, v.ny, v.nz);
        up[i] = Vector2(v.u, v.v);
        cp[i] = Color(v.tr, v.tg, v.tb, v.preserve);
        u2[i] = Vector2(v.texture, v.overlay);
        c0[i * 4] = v.r;
        c0[i * 4 + 1] = v.g;
        c0[i * 4 + 2] = v.b;
        c0[i * 4 + 3] = v.brightness;
        c1[i * 4] = v.painted;
        c1[i * 4 + 1] = v.overlay_tint;
        c1[i * 4 + 2] = v.decal;
        c1[i * 4 + 3] = v.wind;
    }
    std::copy(s.indices.begin(), s.indices.end(), ip);
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = positions;
    arrays[Mesh::ARRAY_NORMAL] = normals;
    arrays[Mesh::ARRAY_TEX_UV] = uv;
    arrays[Mesh::ARRAY_COLOR] = colors;
    arrays[Mesh::ARRAY_TEX_UV2] = uv2;
    arrays[Mesh::ARRAY_CUSTOM0] = custom0;
    arrays[Mesh::ARRAY_CUSTOM1] = custom1;
    arrays[Mesh::ARRAY_INDEX] = indices;
    BitField<Mesh::ArrayFormat> flags(
        (uint64_t(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT) |
        (uint64_t(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT));
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, TypedArray<Array>(), Dictionary(),
                                  flags);
    mesh->surface_set_material(mesh->get_surface_count() - 1, material);
}
void VoxelWorld::upload(MeshResult result) {
    auto mesh_started=std::chrono::steady_clock::now();
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    Ref<ShaderMaterial> material = solid_material;
    add_surface(mesh, result.solid, material);
    add_surface(mesh, result.water, solid_material);
    add_surface(mesh, result.decorations, overlay_material);

    auto found = tiles.find(result.key);
    MeshInstance3D *node;
    if (found == tiles.end()) {
        node = memnew(MeshInstance3D);
        node->set_position(Vector3(result.key.x, 0, result.key.z));
        node->set_name("Terrain_" + String::num_int64(result.key.x) + "_" + String::num_int64(result.key.z) +
                       "_LOD" + String::num_int64(result.key.level));
        add_child(node);
    } else
        node = found->second.node;
    node->set_mesh(mesh);
    node->set_instance_shader_parameter("terrain_lod", float(result.key.level));
    node->set_cast_shadows_setting(result.key.level <= 1 ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON
                                                         : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    int triangles = int(
        (result.solid.indices.size() + result.water.indices.size() + result.decorations.indices.size()) / 3);
    int overlay_quads = 0;
    for (const auto &v : result.solid.vertices)
        overlay_quads += v.overlay >= 0;
    auto &tile = tiles[result.key];
    // Keep the last complete foliage batch while its replacement is built.
    // Clearing here made an entire chunk flash bare after every block edit.
    tile.node = node;
    tile.material = material;
    tile.volume = std::move(result.volume);
    tile.version = result.version;
    tile.triangles = triangles;
    tile.decoration_quads = int(result.decorations.indices.size() / 6);
    tile.overlay_quads = overlay_quads / 4;
    tile.collider = nullptr;
    max_mesh_upload_ms=std::max(max_mesh_upload_ms,std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-mesh_started).count());
    auto collider_started=std::chrono::steady_clock::now();
    // Jolt only receives close, solid terrain triangles. Grass and water have no colliders.
    // At most the near LOD tiles own collision bodies; distant meshes remain render-only.
    if (result.key.level == 0) {
        StaticBody3D *body = nullptr;
        // Replace only the solid collider; vegetation children have their own lifecycle.
        for (int i = 0; i < node->get_child_count(); i++) {
            auto existing = Object::cast_to<StaticBody3D>(node->get_child(i));
            if (existing) {
                node->remove_child(existing);
                memdelete(existing);
            }
        }
        body = memnew(StaticBody3D);
        body->set_collision_layer(1);
        body->set_collision_mask(2);
        Ref<ConcavePolygonShape3D> shape;
        shape.instantiate();
        PackedVector3Array faces;
        faces.resize(result.solid.indices.size());
        auto *face_data = faces.ptrw();
        for (size_t i = 0; i < result.solid.indices.size(); i++) {
            auto &v = result.solid.vertices[result.solid.indices[i]];
            face_data[i] = Vector3(v.x, v.y, v.z);
        }
        if (!faces.is_empty()) {
            shape->set_faces(faces);
            auto collision = memnew(CollisionShape3D);
            collision->set_shape(shape);
            body->add_child(collision);
            node->add_child(body);
            tiles[result.key].collider = body;
        } else
            memdelete(body);
    }
    stages = result.stages;
    max_collider_ms=std::max(max_collider_ms,std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-collider_started).count());
    last_mesh_ms = last_mesh_ms * .9f + float(result.milliseconds) * .1f;
    if (first_tile_ms < 0)
        first_tile_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (result.foliage_included && needs_vegetation(result.key, tile)) {
        // Edited terrain and foliage become visible in the same frame. The old
        // complete tile stays on screen until this worker result is ready.
        upload_vegetation(std::move(result));
        ++foliage_atomic_swaps;
    }
}
static Ref<ArrayMesh> grass_template(const Ref<Material> &material) {
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    PackedVector3Array vertices, normals;
    PackedVector2Array uv;
    PackedInt32Array indices;
    const Vector3 points[] = {{-.5f, 0, 0}, {.5f, 0, 0}, {.5f, 1, 0}, {-.5f, 1, 0},
                              {0, 0, -.5f}, {0, 0, .5f}, {0, 1, .5f}, {0, 1, -.5f}};
    for (int i = 0; i < 8; i++) {
        vertices.push_back(points[i]);
        normals.push_back(Vector3(0, 1, 0));
        uv.push_back(Vector2(i % 4 == 1 || i % 4 == 2 ? 1 : 0, i % 4 < 2 ? 1 : 0));
    }
    for (int value : {0, 2, 1, 0, 3, 2, 4, 6, 5, 4, 7, 6})
        indices.push_back(value);
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = vertices;
    arrays[Mesh::ARRAY_NORMAL] = normals;
    arrays[Mesh::ARRAY_TEX_UV] = uv;
    arrays[Mesh::ARRAY_INDEX] = indices;
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    mesh->surface_set_material(0, material);
    return mesh;
}
void VoxelWorld::clear_vegetation(Tile &tile) {
    if (tile.grass_node) {
        tile.grass_node->queue_free();
        tile.grass_node = nullptr;
    }
    if (tile.bush_node) {
        tile.bush_node->queue_free();
        tile.bush_node = nullptr;
    }
    tile.grass_quads = 0;
    tile.bush_count = 0;
    tile.bush_triangles = 0;
    tile.vegetation_version = std::numeric_limits<uint64_t>::max();
}
static float bounds_distance(const AABB &box, Vector3 point) {
    auto minimum = box.position, maximum = box.position + box.size;
    Vector3 closest(std::clamp(point.x, minimum.x, maximum.x), std::clamp(point.y, minimum.y, maximum.y),
                    std::clamp(point.z, minimum.z, maximum.z));
    return closest.distance_to(point);
}
bool VoxelWorld::needs_vegetation(const TileKey &, const Tile &) const {
    return false;
}
MultiMeshInstance3D *VoxelWorld::grass_batch(const std::vector<GrassInstance> &plants, Node3D *parent,
                                             AABB &bounds) {
    if (plants.empty())
        return nullptr;

    if (grass_template_mesh.is_null())
        grass_template_mesh = grass_template(grass_material);
    Ref<MultiMesh> instances;
    instances.instantiate();
    instances->set_transform_format(MultiMesh::TRANSFORM_3D);
    instances->set_use_colors(true);
    instances->set_use_custom_data(true);
    instances->set_mesh(grass_template_mesh);
    instances->set_instance_count(int(plants.size()));
    PackedFloat32Array buffer;
    buffer.resize(plants.size() * 20);
    auto data = buffer.ptrw();
    Vector3 minimum(1e10f, 1e10f, 1e10f), maximum(-1e10f, -1e10f, -1e10f);
    for (size_t i = 0; i < plants.size(); i++) {
        const auto &plant = plants[i];
        float c = std::cos(plant.angle) * plant.width, s = std::sin(plant.angle) * plant.width;
        const float row[] = {c,
                             0,
                             s,
                             plant.x,
                             0,
                             plant.height,
                             0,
                             plant.y,
                             -s,
                             0,
                             c,
                             plant.z,
                             plant.tint.r,
                             plant.tint.g,
                             plant.tint.b,
                             plant.preserve,
                             plant.brightness,
                             plant.phase,
                             plant.top_layer,
                             plant.apply_tint};
        std::copy(row, row + 20, data + i * 20);
        minimum = minimum.min(Vector3(plant.x - 1.5f, plant.y, plant.z - 1.5f));
        maximum = maximum.max(Vector3(plant.x + 1.5f, plant.y + plant.height + .1f, plant.z + 1.5f));
    }
    bounds = AABB(minimum, maximum - minimum);
    instances->set_custom_aabb(bounds);
    instances->set_buffer(buffer);
    auto node = memnew(MultiMeshInstance3D);
    node->set_name("GrassInstances");
    node->set_multimesh(instances);
    node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    parent->add_child(node);
    return node;
}
void VoxelWorld::upload_vegetation(MeshResult result) {
    auto started=std::chrono::steady_clock::now();
    auto found = tiles.find(result.key);
    if (found == tiles.end())
        return;
    auto &tile = found->second;
    clear_vegetation(tile);
    if (!result.grass.empty()) {
        tile.grass_node = grass_batch(result.grass, tile.node, tile.grass_bounds);
        tile.grass_quads = int(result.grass.size()) * 2;
    }
    if (!result.bushes.vertices.empty()) {
        Ref<ArrayMesh> mesh;
        mesh.instantiate();
        add_surface(mesh, result.bushes, bush_material);
        tile.bush_node = memnew(MeshInstance3D);
        tile.bush_node->set_name("Bushes");
        tile.bush_node->set_mesh(mesh);
        tile.bush_node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        tile.node->add_child(tile.bush_node);
        tile.bush_bounds = mesh->get_aabb();
        tile.bush_count = result.bush_count;
        tile.bush_triangles = int(result.bushes.indices.size() / 3);
    }
    tile.vegetation_version = result.version;
    max_foliage_upload_ms=std::max(max_foliage_upload_ms,std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-started).count());
}
void VoxelWorld::update_vegetation() {
    grass_material->set_shader_parameter("vegetation_center", viewer);
    bush_material->set_shader_parameter("vegetation_center", viewer);
    for (auto &entry : tiles) {
        auto &tile = entry.second;
        if (tile.grass_node)
            tile.grass_node->set_visible(
                bounds_distance(tile.grass_bounds, viewer - Vector3(entry.first.x, 0, entry.first.z)) <
                settings().grass_render_distance);
        if (tile.bush_node)
            tile.bush_node->set_visible(
                bounds_distance(tile.bush_bounds, viewer - Vector3(entry.first.x, 0, entry.first.z)) <
                settings().bush_render_distance);
        if (!needs_vegetation(entry.first, tile) && (tile.grass_node || tile.bush_node))
            clear_vegetation(tile);
    }
    // Prioritize close foliage before filling the remaining worker slots with distant terrain.
    for (const auto &key : ordered) {
        if (pending.size() + vegetation_pending.size() >= size_t(settings().max_pending))
            break;
        auto found = tiles.find(key);
        if (found == tiles.end())
            continue;
        auto &tile = found->second;
        if (needs_vegetation(key, tile) && tile.version == versions[key] &&
            tile.vegetation_version != tile.version && !vegetation_pending.count(key)) {
            vegetation_pending[key] = tile.version;
            workers->submit_vegetation(key, tile.version, tile.volume);
        }
    }
}
void VoxelWorld::reconcile() {
    std::vector<TileKey> retire;
    for (auto &entry : tiles) {
        if (wanted.count(entry.first))
            continue;
        bool complete = true;
        for (auto &key : ordered)
            if (overlaps(entry.first, key) && !tiles.count(key)) {
                complete = false;
                break;
            }
        if (complete)
            retire.push_back(entry.first);
    }
    retirement_pending=retire.size()>2;
    std::unordered_set<TileKey,TileHash> covered(retire.begin(),retire.end());
    int retired_count=0;
    for (auto &key : retire) {
        tiles[key].node->hide();
        if(retired_count++>=2)continue;
        tiles[key].node->queue_free();
        tiles.erase(key);
    }
    for (auto it = versions.begin(); it != versions.end();) {
        if (!wanted.count(it->first) && !tiles.count(it->first) && !pending.count(it->first) &&
            !vegetation_pending.count(it->first))
            it = versions.erase(it);
        else
            ++it;
    }
    std::vector<TileKey> replacing;
    for(const auto &entry:tiles) if(!wanted.count(entry.first) && !covered.count(entry.first)) replacing.push_back(entry.first);
    for (auto &entry : tiles) {
        bool visible = !covered.count(entry.first);
        if (wanted.count(entry.first))
            for (const auto &other : replacing)
                if (overlaps(entry.first, other)) {
                    visible = false;
                    break;
                }
        if(entry.second.node->is_visible()!=visible) entry.second.node->set_visible(visible);
    }
}
void VoxelWorld::_process(double delta) {
    if (!workers)
        return;
    stream_timer += float(delta);
    if (stream_timer >= .3f) {
        stream_timer = 0;
        ordered = select_tiles(viewer.x, viewer.z, settings());
        wanted.clear();
        for (auto &key : ordered)
            wanted.insert(key);
    }
    MeshResult result;
    int uploaded = 0, processed = 0;
    bool coverage_changed=false;
    auto upload_start=std::chrono::steady_clock::now();
    last_upload_ms=0;
    while (uploaded < settings().upload_budget && processed < settings().max_pending &&
           workers->pop(result)) {
        ++processed;
        auto &queue = result.vegetation_only ? vegetation_pending : pending;
        auto it = queue.find(result.key);
        if (it != queue.end() && it->second == result.version)
            queue.erase(it);
        if (wanted.count(result.key) && versions[result.key] == result.version) {
            if (result.vegetation_only) {
                auto tile = tiles.find(result.key);
                if (tile != tiles.end() && tile->second.version == result.version &&
                    needs_vegetation(result.key, tile->second))
                    upload_vegetation(std::move(result));
            } else {
                coverage_changed|=!tiles.count(result.key);
                upload(std::move(result));
            }
            ++uploaded;
            last_upload_ms=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-upload_start).count();
            max_upload_ms=std::max(max_upload_ms,last_upload_ms);
            if(last_upload_ms>=settings().upload_time_budget_ms)break;
        }
    }
    if (coverage_changed || stream_timer == 0 || retirement_pending)
        reconcile();
    update_vegetation();
    for (auto &key : ordered) {
        if (pending.size() + vegetation_pending.size() >= size_t(settings().max_pending))
            break;
        auto it = tiles.find(key);
        uint64_t version = versions[key];
        if (pending.count(key) || (it != tiles.end() && it->second.version == version))
            continue;
        pending[key] = version;
        bool refresh_foliage = key.level == 0 && it != tiles.end() &&
                               (it->second.grass_node || it->second.bush_node) &&
                               needs_vegetation(key, it->second);
        workers->submit(key, version, model->snapshot_for(key), render_surfaces(key), refresh_foliage);
    }
    if (world_ready_ms < 0 && !ordered.empty() && pending.empty() && vegetation_pending.empty()) {
        bool ready = true;
        for (const auto &key : ordered) {
            auto entry = tiles.find(key);
            if (entry == tiles.end() || entry->second.version != versions[key] ||
                (needs_vegetation(key, entry->second) &&
                 entry->second.vegetation_version != entry->second.version)) {
                ready = false;
                break;
            }
        }
        if (ready)
            world_ready_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    }
}
BlockId VoxelWorld::block_at(int x, int y, int z) const {
    if (!model)
        return 0;
    auto edits = model->snapshot();
    auto changed = edits->find({x, y, z});
    if (changed != edits->end())
        return changed->second;
    TileKey key{int(std::floor(double(x) / 32)) * 32, int(std::floor(double(z) / 32)) * 32, 0};
    auto it = tiles.find(key);
    if (it != tiles.end() && it->second.volume) {
        auto v = it->second.volume;
        int iy = y - v->min_y;
        if (iy < 0 || iy >= v->height)
            return 0;
        return v->at(x - key.x, iy, z - key.z);
    }
    return model->generator->sample(x, y, z);
}
bool VoxelWorld::collision_ready(Vector3 p) const {
    for (int dz : {-4, 4})
        for (int dx : {-4, 4}) {
            TileKey key{int(std::floor((p.x + dx) / 32)) * 32, int(std::floor((p.z + dz) / 32)) * 32, 0};
            auto t = tiles.find(key);
            auto v = versions.find(key);
            if (t == tiles.end() || !t->second.collider ||
                (v != versions.end() && v->second != t->second.version))
                return false;
        }
    return true;
}
uint8_t VoxelWorld::shape_at(int x, int y, int z) const {
    auto connected=ramp_connections.find({x,y,z});
    if(connected!=ramp_connections.end())return connected->second;
    return natural_shape_at(x,y,z);
}
uint8_t VoxelWorld::natural_shape_at(int x,int y,int z) const {
    auto base=base_shape_at(x,y,z);
    auto source=ramp_spec(base);
    if(!source.valid)return base;
    return connect_ramp(base,[&](int dx,int dz) {
        for(int y2:{y,y-1,y+1})if(registry().get(block_at(x+dx,y2,z+dz)).solid) {
            auto code=base_shape_at(x+dx,y2,z+dz);auto r=ramp_spec(code);
            if(r.valid && std::abs(y2+r.low()-y-source.low())<.001f)return code;
        }
        return uint8_t(0);
    });
}
uint8_t VoxelWorld::base_shape_at(int x, int y, int z) const {
    if (!registry().get(block_at(x, y, z)).solid)
        return 0;
    auto a = model->appearance_at({x, y, z});
    if (a.shape != 255)
        return pack_shape(a.shape, a.rotation);
    if (model->snapshot()->count({x, y, z}))
        return 0;
    auto original=ramp_base_shapes.find({x,y,z});
    if(original!=ramp_base_shapes.end())return original->second;
    TileKey key{int(std::floor(double(x) / 32)) * 32, int(std::floor(double(z) / 32)) * 32, 0};
    auto t = tiles.find(key);
    if (t != tiles.end() && t->second.volume) {
        auto &v = *t->second.volume;
        size_t index = ((z - key.z + 1) * (v.height + 2) + (y - v.min_y + 1)) * 34 + x - key.x + 1;
        auto shape = v.shapes.find(index);
        return shape == v.shapes.end() ? 0 : shape->second;
    }
    auto c = generator()->column(x, z);
    auto shape = generator()->surface_shape(x, y, z, c);
    return shape && (!registry().get(generator()->terrain_sample(x, y, z)).solid ||
                     registry().get(generator()->sample(x, y + 1, z)).solid)
               ? 0
               : shape;
}
bool VoxelWorld::collides_box(Vector3 lo, Vector3 hi) const {
    for (int z = int(std::floor(lo.z)); z <= int(std::floor(hi.z)); z++)
        for (int y = int(std::floor(lo.y)); y <= int(std::floor(hi.y)); y++)
            for (int x = int(std::floor(lo.x)); x <= int(std::floor(hi.x)); x++)
                if (registry().get(block_at(x, y, z)).solid &&
                    shape_intersects(shape_at(x, y, z), {lo.x - x, lo.y - y, lo.z - z},
                                     {hi.x - x, hi.y - y, hi.z - z}))
                    return true;
    return false;
}
Ref<ArrayMesh> VoxelWorld::surface_mesh(const Surface &surface) const {
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    add_surface(mesh, surface, solid_material);
    return mesh;
}
Ref<ArrayMesh> VoxelWorld::block_mesh(const std::vector<std::pair<IVec3, BlockId>> &parts,
                                      const std::vector<Appearance> &appearances, const Column &c,
                                      bool physical) const {
    Surface surface;
    for (size_t i = 0; i < parts.size(); i++) {
        auto &p = parts[i].first;
        auto a = i < appearances.size() ? appearances[i] : Appearance();
        append_block_surface(surface, {float(p.x), float(p.y), float(p.z)},
                             pack_shape(a.shape, a.rotation), parts[i].second, a, c,
                             registry());
    }
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    add_surface(mesh, surface, physical ? physical_material : solid_material);
    return mesh;
}
void VoxelWorld::set_visual_environment(Color fog, float time, float strength, float scale, float variation,
                                        float fog_start, float fog_end) {
    for (auto material : {solid_material, physical_material, bush_material}) {
        material->set_shader_parameter("horizon_start", fog_start);
        material->set_shader_parameter("horizon_end", fog_end);
    }
    bush_material->set_shader_parameter("horizon_color", fog);
    physical_material->set_shader_parameter("horizon_color", fog);
    physical_material->set_shader_parameter("grass_noise_scale", scale);
    physical_material->set_shader_parameter("grass_noise_strength", variation);
    solid_material->set_shader_parameter("horizon_color", fog);
    solid_material->set_shader_parameter("grass_noise_scale", scale);
    solid_material->set_shader_parameter("grass_noise_strength", variation);
    grass_material->set_shader_parameter("wind_time", time);
    grass_material->set_shader_parameter("wind_strength", strength);
    grass_material->set_shader_parameter("grass_noise_scale", scale);
    grass_material->set_shader_parameter("grass_noise_strength", variation);
}
bool VoxelWorld::edit(const EditCommand &cmd, String &error) {
    std::string e;
    if (!model->validate(cmd, e)) {
        error = String(e.c_str());
        return false;
    }
    if (!model->apply(cmd, e)) {
        error = String(e.c_str());
        return false;
    }
    ramp_base_shapes.erase(cmd.position);
    ramp_connections.erase(cmd.position);
    invalidate(cmd.position);
    return true;
}
bool VoxelWorld::edit_surface(const SurfaceCommand &command, String &error) {
    std::string e;
    if (!model->validate_surface(command, e)) {
        error = String(e.c_str());
        return false;
    }
    if (!model->apply_surface(command, e)) {
        error = String(e.c_str());
        return false;
    }
    ramp_base_shapes.erase(command.position);
    ramp_connections.erase(command.position);
    invalidate(command.position);
    return true;
}
std::shared_ptr<const AppearanceMap> VoxelWorld::render_surfaces(const TileKey &key) const {
    auto base=model->surfaces_for(key);
    if(ramp_connections.empty())return base;
    auto result=std::make_shared<AppearanceMap>(*base);int pad=1<<key.level;
    for(auto &[p,shape]:ramp_connections)if(p.x>=key.x-pad && p.x<key.x+key.size()+pad && p.z>=key.z-pad && p.z<key.z+key.size()+pad) {
        auto a=model->appearance_at(p);unpack_shape(shape,a);a.derived_ramp=true;(*result)[p]=a;
    }
    return result;
}
void VoxelWorld::set_ramp_connection(IVec3 p,uint8_t shape) {
    auto it=ramp_connections.find(p);
    if(shape==255) {if(it==ramp_connections.end())return;ramp_connections.erase(it);}
    else {
        if(it!=ramp_connections.end() && it->second==shape)return;
        if(!ramp_base_shapes.count(p))ramp_base_shapes[p]=base_shape_at(p.x,p.y,p.z);
        ramp_connections[p]=shape;
    }
    invalidate(p,false);
}
void VoxelWorld::invalidate(IVec3 position,bool notify) {
    if(notify && edit_callback)edit_callback(position);
    // Invalidate all affected LODs and their meshing halos, including unloaded jobs.
    std::unordered_set<TileKey, TileHash> affected;
    for (auto &key : ordered)
        affected.insert(key);
    for (auto &entry : tiles)
        affected.insert(entry.first);
    for (auto &entry : pending)
        affected.insert(entry.first);
    for (auto &entry : vegetation_pending)
        affected.insert(entry.first);
    for (auto &key : affected) {
        int pad = 1 << key.level;
        if (position.x >= key.x - pad && position.x < key.x + key.size() + pad && position.z >= key.z - pad &&
            position.z < key.z + key.size() + pad)
            ++versions[key];
    }
}
void VoxelWorld::set_fullbright(bool value) {
    fullbright = value;
    bush_material->set_shader_parameter("fullbright", value);
    physical_material->set_shader_parameter("fullbright", value);
    solid_material->set_shader_parameter("fullbright", value);
    overlay_material->set_shader_parameter("fullbright", value);
    grass_material->set_shader_parameter("fullbright", value);
    for (auto &entry : tiles)
        entry.second.material->set_shader_parameter("fullbright", value);
}
Dictionary VoxelWorld::stats() const {
    Dictionary d;
    d["first_tile_ms"] = first_tile_ms;
    d["world_ready_ms"] = world_ready_ms;
    int counts[6] = {}, triangles = 0, decorations = 0, overlays = 0, grass = 0;
    int64_t bytes = 0;
    int foliage_tiles = 0, visible_grass_tiles = 0, bush_count = 0;
    for (auto &entry : tiles) {
        counts[entry.first.level]++;
        triangles += entry.second.triangles + entry.second.grass_quads * 2 + entry.second.bush_triangles;
        foliage_tiles += entry.second.grass_node != nullptr || entry.second.bush_node != nullptr;
        visible_grass_tiles += entry.second.grass_node && entry.second.grass_node->is_visible_in_tree();
        bush_count += entry.second.bush_count;
        decorations += entry.second.decoration_quads;
        grass += entry.second.grass_quads;
        overlays += entry.second.overlay_quads;
        if (entry.second.volume)
            bytes += int64_t(entry.second.volume->data.size() * sizeof(BlockId));
    }
    for (int i = 0; i < 6; i++)
        d["lod" + String::num_int64(i)] = counts[i];
    d["tiles"] = int(tiles.size());
    d["wanted"] = int(wanted.size());
    d["pending"] = int(pending.size() + vegetation_pending.size());
    d["vegetation_pending"] = int(vegetation_pending.size());
    d["vegetation_tiles"] = foliage_tiles;
    d["grass_visible_tiles"] = visible_grass_tiles;
    d["grass_instances"] = grass / 2;
    d["grass_instance_bytes"] = int64_t(grass / 2) * 20 * sizeof(float);
    d["grass_render_distance"] = settings().grass_render_distance;
    d["foliage_atomic_swaps"] = foliage_atomic_swaps;
    d["upload_ms"] = last_upload_ms;
    d["max_upload_ms"] = max_upload_ms;
    d["max_mesh_upload_ms"] = max_mesh_upload_ms;
    d["max_collider_ms"] = max_collider_ms;
    d["max_foliage_upload_ms"] = max_foliage_upload_ms;
    d["bushes"] = bush_count;
    d["triangles"] = triangles;
    d["decorations"] = decorations;
    d["grass_quads"] = grass;
    d["overlay_quads"] = overlays;
    d["texture_layers"] = int(registry().textures.size());
    d["voxel_bytes"] = bytes;
    d["mesh_ms"] = last_mesh_ms;
    d["biome_stage_ms"] = stages.biomes_ms;
    d["terrain_stage_ms"] = stages.terrain_ms;
    d["surface_stage_ms"] = stages.surface_ms;
    d["mesh_stage_ms"] = stages.mesh_ms;
    d["surface_edits"] = int(model->painted_count());
    d["view_distance"] = settings().view_distance;
    d["edits"] = model ? int(model->snapshot()->size()) : 0;
    d["revision"] = model ? int64_t(model->revision) : 0;
    int colliders = 0;
    for (auto &t : tiles)
        colliders += t.second.collider != nullptr;
    d["terrain_colliders"] = colliders;
    d["fullbright"] = fullbright;
    return d;
}
} // namespace godot
