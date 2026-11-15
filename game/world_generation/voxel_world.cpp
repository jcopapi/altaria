#include "voxel_world.h"
#include "../debug/frame_profile.h"
#include <cstring>
#include <future>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <cstdlib>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include "block_shapes.h"
#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
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
    for (const auto &rule : s.resources) {
        if (!r.get(r.find(rule.block)).solid ||
            std::any_of(rule.hosts.begin(), rule.hosts.end(),
                        [&](const std::string &host) { return !r.get(r.find(host)).solid; })) {
            error = "Resource deposit requires registered solid blocks: " + String(rule.block.c_str());
            return false;
        }
        const auto &block = r.get(r.find(rule.block));
        if (s.preset == WorldPreset::Default && !block.deposit_model.empty() &&
            !deposit_models.count(block.id) && !load_deposit_model(block, error))
            return false;
    }
    if (s.preset == WorldPreset::Default && s.vegetation &&
        (!r.keys.count(s.bush_block) || r.find(s.bush_block) == r.air)) {
        error = "Unknown bush material: " + String(s.bush_block.c_str());
        return false;
    }
    auto gen = std::make_shared<Generator>(s, r, seed);
    std::string generation_error;
    if(!gen->valid_generation(generation_error)){error=generation_error.c_str();return false;}
    model = std::make_unique<WorldModel>(gen);
    fog_range=settings().view_distance*.94f;
    stream_radius=settings().view_distance;
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
    if (const auto *noise = gen->noise_caves()) {
        // The underworld is lit by its lava ocean: a warm glow from below that
        // fades out above the roof, so the open layer reads as a world.
        const auto &n = noise->settings();
        solid_material->set_shader_parameter("underworld_top", float(n.hell_ceiling) + n.hell_ceiling_amplitude + 12.f);
        solid_material->set_shader_parameter("underworld_lava", float(n.hell_lava));
        solid_material->set_shader_parameter("underworld_glow", Vector3(.62f, .2f, .07f));
    }
    physical_material = solid_material->duplicate();
    {
        Ref<Shader> leaf_shader =
            ResourceLoader::get_singleton()->load("res://game/world_generation/shaders/leaf_cards.gdshader");
        if (leaf_shader.is_null()) {
            error = "Cannot load leaf card shader";
            return false;
        }
        leaf_material.instantiate();
        leaf_material->set_shader(leaf_shader);
        leaf_material->set_shader_parameter("fullbright", false);
        leaf_material->set_shader_parameter("block_textures", texture_array);
    }
    physical_material->set_shader_parameter("local_textures", true);
    Ref<Shader> water_shader =
        ResourceLoader::get_singleton()->load("res://game/world_generation/shaders/water.gdshader");
    if (water_shader.is_null()) {
        error = "Cannot load water shader";
        return false;
    }
    water_material.instantiate();
    water_material->set_shader(water_shader);
    water_material->set_render_priority(-10);
    Ref<Shader> lava_shader =
        ResourceLoader::get_singleton()->load("res://game/world_generation/shaders/lava.gdshader");
    if (lava_shader.is_null()) {
        error = "Cannot load lava shader";
        return false;
    }
    lava_material.instantiate();
    lava_material->set_shader(lava_shader);
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
    Ref<Texture2D> sgt_tex = ResourceLoader::get_singleton()->load("res://game/assets/blocks/grass.png");
    if (sgt_tex.is_valid()) {
        grass_material->set_shader_parameter("grass_texture", sgt_tex);
    }
    grass_material->set_shader_parameter("fade_start", s.grass_render_distance - s.grass_fade_distance);
    grass_material->set_shader_parameter("fade_end", s.grass_render_distance);
    bush_material = solid_material->duplicate();
    bush_material->set_shader_parameter("distance_limited", true);
    bush_material->set_shader_parameter("vegetation_limit", s.bush_render_distance);
    held_result.reset();
    canopy_queue.clear();
    // workers = 0 sizes the pool to the machine: all but two hardware threads
    // (main thread and renderer), between 2 and 8.
    const int hardware = int(std::thread::hardware_concurrency());
    if (s.preset != WorldPreset::Structure) {
        worker_count = s.preset == WorldPreset::Plain ? 2 :
            s.workers > 0 ? s.workers : std::clamp(hardware - 2, 2, 8);
        workers = std::make_unique<TerrainWorkers>(gen, worker_count, [](MeshResult &result) { prepare_upload(result); });
    }
    liquid_sequence=0;
    liquid_sim.configure(r, gen, [this](int x, int y, int z) {
        if((settings().caves.enabled || settings().adaptive_streaming) && !voxel_ready(x,y,z))return registry().bedrock;
        return block_at(x, y, z);
    }, [this](int x, int y, int z, altaria::BlockId b) {
        if (model) {
            std::string err;
            constexpr uint64_t liquid_actor=std::numeric_limits<uint64_t>::max();
            if(model->apply({liquid_actor,++liquid_sequence,{x,y,z},b},err))invalidate({x,y,z},true);
        }
    },[this](int x,int y,int z){return cave_liquid_at(x,y,z);});
    configure_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (s.preset == WorldPreset::Structure)
        world_ready_ms = configure_ms;
    return true;
}
namespace {
// Converts a mesher surface into the engine's array layout. Runs on worker
// threads for streamed tiles and on the main thread for small edits.
// IEEE half precision, rounded to nearest, for the compact color attribute.
uint16_t half_float(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    int exponent = int((bits >> 23) & 0xff) - 127 + 15;
    uint32_t mantissa = bits & 0x7fffffu;
    if (exponent <= 0) {
        if (exponent < -10)
            return uint16_t(sign);
        mantissa |= 0x800000u;
        const int shift = 14 - exponent;
        uint32_t half = mantissa >> shift;
        if ((mantissa >> (shift - 1)) & 1u)
            ++half;
        return uint16_t(sign | half);
    }
    if (exponent >= 31)
        return uint16_t(sign | 0x7c00u);
    uint32_t half = sign | uint32_t(exponent) << 10 | mantissa >> 13;
    if (mantissa & 0x1000u)
        ++half; // may carry into the exponent, which is still correct rounding
    return uint16_t(half);
}
uint8_t unorm8(float value) { return uint8_t(std::clamp(int(std::lround(value * 255.f)), 0, 255)); }
// Packs a mesher surface straight into the engine's vertex buffers, in the
// layout the renderer keeps for a compressed surface (what Mesh would produce
// from arrays with ARRAY_FLAG_COMPRESS_ATTRIBUTES). Doing it here keeps the
// conversion on worker threads: the main thread only hands the bytes over.
//   vertex buffer    per vertex: position as 3 x uint16 within the bounds and
//                    the tangent-frame angle (uint16); then, per vertex, the
//                    frame's axis, octahedral, 2 x uint16
//   attribute buffer COLOR   RGBA8   tint rgb, preserve
//                    UV      2 x uint16, (uv / uv_scale + 0.5)
//                    CUSTOM0 RGBA16F base color rgb, brightness
//                    CUSTOM1 RG32F   flags (integer bit field up to 2^22), side top
//                    CUSTOM2 RGBA8   painted, overlay tint, texture layer / 255,
//                                    (overlay layer + 1) / 255
// The renderer rebuilds the normal by rotating +Z around the axis by the
// angle (negated), so the axis is normal x Z and the angle acos(normal.z).
Dictionary pack_surface(const Surface &s) {
    Dictionary out;
    const int count = int(s.vertices.size());
    if (count == 0 || s.indices.empty())
        return out;
    constexpr uint64_t format =
        uint64_t(Mesh::ARRAY_FORMAT_VERTEX) | uint64_t(Mesh::ARRAY_FORMAT_NORMAL) | uint64_t(Mesh::ARRAY_FORMAT_TANGENT) |
        uint64_t(Mesh::ARRAY_FORMAT_COLOR) | uint64_t(Mesh::ARRAY_FORMAT_TEX_UV) | uint64_t(Mesh::ARRAY_FORMAT_CUSTOM0) |
        uint64_t(Mesh::ARRAY_FORMAT_CUSTOM1) | uint64_t(Mesh::ARRAY_FORMAT_CUSTOM2) | uint64_t(Mesh::ARRAY_FORMAT_INDEX) |
        (uint64_t(Mesh::ARRAY_CUSTOM_RGBA_HALF) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT) |
        (uint64_t(Mesh::ARRAY_CUSTOM_RG_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT) |
        (uint64_t(Mesh::ARRAY_CUSTOM_RGBA8_UNORM) << Mesh::ARRAY_FORMAT_CUSTOM2_SHIFT) |
        uint64_t(Mesh::ARRAY_FLAG_COMPRESS_ATTRIBUTES) | (uint64_t(1) << 35); // current surface format version
    float lo[3] = {s.vertices[0].x, s.vertices[0].y, s.vertices[0].z}, hi[3] = {lo[0], lo[1], lo[2]};
    float uv_reach[2] = {0, 0};
    for (const auto &v : s.vertices) {
        const float p[3] = {v.x, v.y, v.z};
        for (int a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], p[a]);
            hi[a] = std::max(hi[a], p[a]);
        }
        uv_reach[0] = std::max(uv_reach[0], std::abs(v.u));
        uv_reach[1] = std::max(uv_reach[1], std::abs(v.v));
    }
    float size[3], uv_scale[2];
    for (int a = 0; a < 3; ++a)
        size[a] = std::max(hi[a] - lo[a], 1e-4f);
    for (int a = 0; a < 2; ++a)
        uv_scale[a] = std::max(2.f * uv_reach[a], 1e-4f);
    const auto unit16 = [](float value) { return uint16_t(std::clamp(value * 65535.f, 0.f, 65535.f)); };
    PackedByteArray vertex_data, attribute_data, index_data;
    vertex_data.resize(int64_t(count) * 12);
    attribute_data.resize(int64_t(count) * 28);
    uint8_t *positions = vertex_data.ptrw(), *frames = positions + size_t(count) * 8, *attributes = attribute_data.ptrw();
    for (int i = 0; i < count; ++i) {
        const auto &v = s.vertices[i];
        float nx = v.nx, ny = v.ny, nz = v.nz;
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (length > 1e-6f)
            nx /= length, ny /= length, nz /= length;
        else
            nx = 0, ny = 1, nz = 0;
        float ax = ny, ay = -nx;
        const float planar = std::sqrt(ax * ax + ay * ay);
        if (planar > 1e-5f)
            ax /= planar, ay /= planar;
        else
            ax = 1, ay = 0;
        const float angle = std::acos(std::clamp(nz, -1.f, 1.f));
        const float turn = std::clamp(angle / 3.14159265f * .5f + .5f, .500008f, 1.f);
        const uint16_t position[4] = {unit16((v.x - lo[0]) / size[0]), unit16((v.y - lo[1]) / size[1]),
                                      unit16((v.z - lo[2]) / size[2]), unit16(turn)};
        std::memcpy(positions + size_t(i) * 8, position, 8);
        const float spread = std::abs(ax) + std::abs(ay);
        const uint16_t frame[2] = {unit16(ax / spread * .5f + .5f), unit16(ay / spread * .5f + .5f)};
        std::memcpy(frames + size_t(i) * 4, frame, 4);
        uint8_t *a = attributes + size_t(i) * 28;
        const auto byte = [](float value) { return uint8_t(std::clamp(value * 255.f, 0.f, 255.f)); };
        a[0] = byte(v.tr), a[1] = byte(v.tg), a[2] = byte(v.tb), a[3] = byte(v.preserve);
        const uint16_t uv[2] = {unit16(v.u / uv_scale[0] + .5f), unit16(v.v / uv_scale[1] + .5f)};
        std::memcpy(a + 4, uv, 4);
        const uint16_t base[4] = {half_float(v.r), half_float(v.g), half_float(v.b), half_float(v.brightness)};
        std::memcpy(a + 8, base, 8);
        const float flags[2] = {v.decal, v.wind};
        std::memcpy(a + 16, flags, 8);
        a[24] = unorm8(v.painted), a[25] = unorm8(v.overlay_tint);
        a[26] = uint8_t(std::clamp(int(std::lround(v.texture)), 0, 255));
        a[27] = uint8_t(std::clamp(int(std::lround(v.overlay)) + 1, 0, 255));
    }
    const int index_count = int(s.indices.size());
    if (count <= 65536) {
        index_data.resize(int64_t(index_count) * 2);
        auto *indices = reinterpret_cast<uint16_t *>(index_data.ptrw());
        for (int i = 0; i < index_count; ++i)
            indices[i] = uint16_t(s.indices[i]);
    } else {
        index_data.resize(int64_t(index_count) * 4);
        std::memcpy(index_data.ptrw(), s.indices.data(), size_t(index_count) * 4);
    }
    out["primitive"] = int(Mesh::PRIMITIVE_TRIANGLES);
    out["format"] = int64_t(format);
    out["vertex_data"] = vertex_data;
    out["attribute_data"] = attribute_data;
    out["vertex_count"] = count;
    out["index_data"] = index_data;
    out["index_count"] = index_count;
    out["aabb"] = AABB(Vector3(lo[0], lo[1], lo[2]), Vector3(size[0], size[1], size[2]));
    out["uv_scale"] = Vector4(uv_scale[0], uv_scale[1], 0, 0);
    return out;
}
// Liquid meshes mix water, lava and opaque seam quads in one surface.
// Returns true when every quad is water, leaving the parts untouched.
bool split_liquids(const Surface &surface, Surface &opaque, Surface &lava, Surface &water) {
    const auto kind = [](const Vertex &vertex) { return int(vertex.decal) & 3; };
    if (std::all_of(surface.vertices.begin(), surface.vertices.end(), [&](const Vertex &v) { return kind(v) == 2; }))
        return true;
    for (size_t vertex = 0, index = 0; vertex < surface.vertices.size(); vertex += 4, index += 6) {
        const int k = kind(surface.vertices[vertex]);
        auto &part = k == 2 ? water : k == 3 ? lava : opaque;
        const int base = int(part.vertices.size());
        part.vertices.insert(part.vertices.end(), surface.vertices.begin() + vertex, surface.vertices.begin() + vertex + 4);
        for (size_t corner = index; corner < index + 6; ++corner)
            part.indices.push_back(base + surface.indices[corner] - int(vertex));
    }
    return false;
}
} // namespace
// Arrays for every surface a streamed tile uploads, built off the main thread.
struct VoxelWorld::PreparedTile {
    Dictionary solid, decorations, leaf_shadows, leaf_cards, sky_shadows, bushes;
    bool all_water = false;
    Dictionary liquid_opaque, liquid_lava, liquid_water;
    struct Section {
        int band = 0;
        Dictionary solid, liquid_opaque, liquid_lava, liquid_water;
    };
    bool sectioned = false;
    std::vector<Section> sections;
};
void VoxelWorld::prepare_upload(MeshResult &result) {
    auto prepared = std::make_shared<PreparedTile>();
    if (result.key.level == 0 && result.volume && result.volume->y_step == 1 && result.volume->height > 0) {
        // Split the ground into 32 m sections: the renderer culls each against
        // the view on its own instead of the tile's whole column.
        prepared->sectioned = true;
        // Separate the liquid kinds first: that split reads whole quads, which
        // section clipping may cut into triangles.
        Surface opaque, lava, water;
        if (!result.water.vertices.empty() && split_liquids(result.water, opaque, lava, water))
            water = result.water;
        std::map<int, PreparedTile::Section> bands;
        auto section = [&](int band) -> PreparedTile::Section & {
            auto &entry = bands[band];
            entry.band = band;
            return entry;
        };
        for (auto &mesh : altaria::split_sections(result.solid, opaque)) {
            auto &entry = section(mesh.band);
            entry.solid = pack_surface(mesh.solid);
            entry.liquid_opaque = pack_surface(mesh.water);
        }
        for (auto &mesh : altaria::split_sections(lava, water)) {
            auto &entry = section(mesh.band);
            entry.liquid_lava = pack_surface(mesh.solid);
            entry.liquid_water = pack_surface(mesh.water);
        }
        for (auto &[band, entry] : bands)
            prepared->sections.push_back(std::move(entry));
        prepared->decorations = pack_surface(result.decorations);
        prepared->leaf_shadows = pack_surface(result.leaf_shadows);
        prepared->leaf_cards = pack_surface(result.leaf_cards);
        prepared->sky_shadows = pack_surface(result.sky_shadows);
        prepared->bushes = pack_surface(result.bushes);
        result.prepared = std::move(prepared);
        return;
    }
    prepared->solid = pack_surface(result.solid);
    prepared->decorations = pack_surface(result.decorations);
    prepared->leaf_shadows = pack_surface(result.leaf_shadows);
    prepared->leaf_cards = pack_surface(result.leaf_cards);
    prepared->sky_shadows = pack_surface(result.sky_shadows);
    prepared->bushes = pack_surface(result.bushes);
    if (!result.water.vertices.empty()) {
        Surface opaque, lava, water;
        prepared->all_water = split_liquids(result.water, opaque, lava, water);
        if (prepared->all_water) {
            prepared->liquid_water = pack_surface(result.water);
        } else {
            prepared->liquid_opaque = pack_surface(opaque);
            prepared->liquid_lava = pack_surface(lava);
            prepared->liquid_water = pack_surface(water);
        }
    }
    result.prepared = std::move(prepared);
}
const VoxelWorld::PreparedTile *VoxelWorld::prepared_of(const MeshResult &result) {
    return static_cast<const PreparedTile *>(result.prepared.get());
}
Ref<ArrayMesh> VoxelWorld::MeshBuild::finish() const {
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    // Same entry point the scene loader uses for stored meshes: the packed
    // buffers go to the renderer without any conversion.
    if (!surfaces.is_empty())
        mesh->set("_surfaces", surfaces);
    return mesh;
}
void VoxelWorld::add_arrays(MeshBuild &mesh, const Dictionary &packed, const Ref<Material> &material) const {
    if (packed.is_empty())
        return;
    Dictionary surface = packed.duplicate(false); // buffers are shared, not copied
    surface["material"] = material;
    mesh.surfaces.push_back(surface);
}
void VoxelWorld::add_surface(MeshBuild &mesh,const Surface &s,const Ref<Material> &material) const {
    if (s.vertices.empty())
        return;
    add_arrays(mesh, pack_surface(s), material);
}
void VoxelWorld::add_liquid_surfaces(MeshBuild &mesh, const Surface &surface,
                                     const PreparedTile *prepared) const {
    if (surface.vertices.empty())
        return;
    if (prepared) {
        add_arrays(mesh, prepared->liquid_opaque, solid_material);
        add_arrays(mesh, prepared->liquid_lava, lava_material);
        add_arrays(mesh, prepared->liquid_water, water_material);
        return;
    }
    Surface water, lava, opaque;
    if (split_liquids(surface, opaque, lava, water)) {
        add_surface(mesh, surface, water_material);
        return;
    }
    add_surface(mesh, opaque, solid_material);
    add_surface(mesh, lava, lava_material);
    add_surface(mesh, water, water_material);
}

void VoxelWorld::upload(MeshResult result) {
    auto mesh_started=std::chrono::steady_clock::now();
    static altaria::profile::Slot &surfaces_slot = altaria::profile::slot("world.upload.surfaces");
    std::optional<altaria::profile::Scope> surfaces_scope;
    surfaces_scope.emplace(surfaces_slot);
    MeshBuild mesh;
    Ref<ShaderMaterial> material = solid_material;
    const PreparedTile *prepared = prepared_of(result);
    if (prepared) {
        add_arrays(mesh, prepared->solid, material);
        add_liquid_surfaces(mesh, result.water, prepared);
        add_arrays(mesh, prepared->decorations, overlay_material);
    } else {
        add_surface(mesh,result.solid,material);
        add_liquid_surfaces(mesh, result.water);
        add_surface(mesh,result.decorations,overlay_material);
    }

    surfaces_scope.reset();
    auto found = tiles.find(result.key);
    MeshInstance3D *node;
    if (found == tiles.end()) {
        node = memnew(MeshInstance3D);
        node->set_position(Vector3(result.key.x, 0, result.key.z));
        node->set_name("Terrain_" + String::num_int64(result.key.x) + "_" + String::num_int64(result.key.z) +
                       "_LOD" + String::num_int64(result.key.level));
        node->hide();
        add_child(node);
    } else
        node = found->second.node;
    node->set_mesh(mesh.finish());
    // Lava pools light their surroundings with a few warm, shadowless lights.
    for (int i = node->get_child_count() - 1; i >= 0; --i)
        if (auto *old = Object::cast_to<OmniLight3D>(node->get_child(i))) {
            node->remove_child(old);
            old->queue_free();
        }
    // The underworld is lit by its own glow, and a light only matters near the
    // viewer: lights stay hidden beyond light_radius (see update_lights).
    float underworld_top = -1e9f;
    if (const auto *noise = generator()->noise_caves())
        underworld_top = float(noise->settings().hell_ceiling) + noise->settings().hell_ceiling_amplitude;
    const bool lights_near = tile_distance(result.key, viewer.x, viewer.z) < light_radius;
    for (const auto &glow : result.lava_lights) {
        if (glow[1] < underworld_top)
            continue;
        auto *light = memnew(OmniLight3D);
        light->set_visible(lights_near);
        light->set_name("LavaGlow");
        const float deep_heat = Math::smoothstep(110.f, 480.f, -glow[1]);
        light->set_color(Color(1.f, .46f, .16f).lerp(Color(1.f, .19f, .10f), deep_heat));
        const float size = std::sqrt(std::max(1.f, glow[3]));
        light->set_param(Light3D::PARAM_ENERGY, std::min(3.4f, 1.4f + .3f * size));
        light->set_param(Light3D::PARAM_RANGE, std::min(20.f, 8.f + 1.4f * size));
        light->set_param(Light3D::PARAM_ATTENUATION, 1.4f);
        light->set_param(Light3D::PARAM_VOLUMETRIC_FOG_ENERGY, 0.f);
        light->set_shadow(false);
        light->set_position(Vector3(glow[0], glow[1] + 1.2f, glow[2]));
        node->add_child(light);
    }
    for (const auto &glow : result.root_lights) {
        auto *light = memnew(OmniLight3D);
        light->set_visible(lights_near);
        light->set_name("RootGlow");
        light->set_color(Color(1.f, .79f, .35f));
        light->set_param(Light3D::PARAM_ENERGY, .28f);
        light->set_param(Light3D::PARAM_RANGE, 3.f);
        light->set_param(Light3D::PARAM_ATTENUATION, 1.6f);
        light->set_param(Light3D::PARAM_VOLUMETRIC_FOG_ENERGY, 0.f);
        light->set_shadow(false);
        light->set_position(Vector3(glow[0], glow[1], glow[2]));
        node->add_child(light);
    }
    {
        auto &previous = tiles[result.key];
        // A new tile has no node yet: the per-tile settings below (LOD,
        // shadows, cull margin) must reach this node, not only its sections.
        previous.node = node;
        for (auto *section : previous.sections) {
            node->remove_child(section);
            section->queue_free();
        }
        previous.sections.clear();
        previous.section_bands.clear();
        if (prepared && prepared->sectioned) {
            ALTARIA_PROFILE("world.upload.sections");
            for (const auto &part : prepared->sections) {
                MeshBuild section_mesh;
                add_arrays(section_mesh, part.solid, material);
                add_arrays(section_mesh, part.liquid_opaque, solid_material);
                add_arrays(section_mesh, part.liquid_lava, lava_material);
                add_arrays(section_mesh, part.liquid_water, water_material);
                if (section_mesh.surfaces.is_empty())
                    continue;
                auto *section = memnew(MeshInstance3D);
                section->set_name("Section_" + String::num_int64(part.band));
                section->set_mesh(section_mesh.finish());
                section->set_instance_shader_parameter("seam_edges", previous.seam_edges);
                section->set_instance_shader_parameter("lod_opacity", previous.node ? previous.opacity : 1.f);
                node->add_child(section);
                previous.sections.push_back(section);
                previous.section_bands.push_back(part.band);
            }
        }
        for_meshes(previous, [&](MeshInstance3D *mesh_node) {
            if (result.key.level == 0)
                mesh_node->set_extra_cull_margin(.6f);
            mesh_node->set_instance_shader_parameter("terrain_lod", float(result.key.level));
            mesh_node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        });
    }
    int triangles = int(
        (result.solid.indices.size() + result.water.indices.size() + result.decorations.indices.size()) / 3);
    int overlay_quads = 0;
    for (const auto &v : result.solid.vertices)
        overlay_quads += v.overlay >= 0;
    auto &tile = tiles[result.key];
    const bool new_volume=result.volume && (!tile.volume || !(tile.window==result.window));
    // Keep the last complete foliage batch while its replacement is built.
    // Clearing here made an entire chunk flash bare after every block edit.
    // The canopy meshes cost as much to hand over as the terrain itself, so
    // they follow in a later frame (see apply_canopy) rather than stacking.
    for (auto it = canopy_queue.begin(); it != canopy_queue.end();)
        it = it->key == result.key ? canopy_queue.erase(it) : it + 1;
    if (settings().preset == WorldPreset::Default || !result.leaf_shadows.vertices.empty() ||
        !result.leaf_cards.vertices.empty() || !result.sky_shadows.vertices.empty())
        canopy_queue.push_back({result.key, result.version, std::move(result.leaf_shadows), std::move(result.leaf_cards),
                                std::move(result.sky_shadows), result.prepared});
    tile.node = node;
    tile.lights_visible = lights_near;
    tile.edges = std::make_shared<const altaria::TerrainEdges>(std::move(result.edges));
    if (result.window.mode == VerticalWindow::underground && tile.seam_edges != Vector4i()) {
        tile.seam_edges = Vector4i();
        for_meshes(tile, [&](MeshInstance3D *mesh_node) { mesh_node->set_instance_shader_parameter("seam_edges", tile.seam_edges); });
    }
    seams_dirty |= tile.ground_visible;
    tile.material = material;
    tile.volume = std::move(result.volume);
    if (tile.volume)
        upload_deposits(tile, tile.volume->resources);
    tile.version = result.version;
    tile.window = result.window;
    if(new_volume) {
        const auto &volume=*tile.volume;
        const int y0=volume.unknown_below ? volume.min_y : settings().min_y;
        const int y1=volume.unknown_above ? volume.min_y+volume.height : settings().max_y;
        liquid_sim.wake_region({result.key.x,y0,result.key.z},
                               {result.key.x+32,y1,result.key.z+32});
    }
    tile.triangles = triangles;
    const size_t collider_vertices=result.collision_rects.size()*6+result.partial_collision_faces.size();
    tile.collision_triangles=int(collider_vertices/3);
    tile.decoration_quads = int(result.decorations.indices.size() / 6);
    tile.overlay_quads = overlay_quads / 4;
    if(tile.collider) {
        node->remove_child(tile.collider);
        memdelete(tile.collider);
        tile.collider=nullptr;
    }
    tile.collision_rects=std::move(result.collision_rects);
    tile.partial_collision_faces=std::move(result.partial_collision_faces);
    max_mesh_upload_ms=std::max(max_mesh_upload_ms,std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-mesh_started).count());
    stages = result.stages;
    {
        auto &g = generation_stats[size_t(std::clamp(result.key.level, 0, 5))];
        ++g.tiles;
        g.total_ms += result.milliseconds;
        g.biomes_ms += result.stages.biomes_ms;
        g.terrain_ms += result.stages.terrain_ms;
        g.caves_ms += result.stages.cave_plan_ms + result.stages.cave_field_ms + result.stages.cave_biome_ms;
        g.surface_ms += result.stages.surface_ms;
        g.mesh_ms += result.stages.mesh_ms;
        g.vertices += result.solid.vertices.size() + result.water.vertices.size();
    }
    last_mesh_ms = last_mesh_ms * .9f + float(result.milliseconds) * .1f;
    if (first_tile_ms < 0) {
        first_tile_stages = result.stages;
        first_tile_build_ms = result.milliseconds;
    }
    if (first_tile_ms < 0)
        first_tile_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (new_volume && !result.foliage_included)
        clear_vegetation(tile);
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
bool VoxelWorld::needs_vegetation(const TileKey &key, const Tile &tile) const {
    if (key.level != 0 || !tile.volume || settings().preset != WorldPreset::Default)
        return false;
    float max_dist = std::max(settings().grass_render_distance + 12.f, settings().bush_render_distance);
    return tile_distance(key, viewer.x, viewer.z) <= max_dist;
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
                             plant.lean_x,
                             s,
                             plant.x,
                             0,
                             plant.height,
                             0,
                             plant.y,
                             -s,
                             plant.lean_z,
                             c,
                             plant.z,
                             plant.tint.r,
                             plant.tint.g,
                             plant.tint.b,
                             plant.preserve,
                             plant.brightness,
                             plant.phase,
                             plant.top_layer,
                             plant.apply_tint + 2.f * plant.sky_exposure};
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
    if (result.vegetation_only && result.vegetation_source != tile.volume)
        return;
    clear_vegetation(tile);
    if (!result.grass.empty()) {
        tile.grass_node = grass_batch(result.grass, tile.node, tile.grass_bounds);
        tile.grass_node->set_instance_shader_parameter("lod_opacity", tile.opacity);
        tile.grass_quads = int(result.grass.size()) * 2;
        tile.grass_core = result.grass_core > 0 ? result.grass_core : int(result.grass.size());
    }
    if (!result.bushes.vertices.empty()) {
        MeshBuild mesh;
        if (const auto *prepared = prepared_of(result))
            add_arrays(mesh, prepared->bushes, bush_material);
        else
            add_surface(mesh, result.bushes, bush_material);
        tile.bush_node = memnew(MeshInstance3D);
        tile.bush_node->set_name("Bushes");
        const Ref<ArrayMesh> bush_mesh = mesh.finish();
        tile.bush_node->set_mesh(bush_mesh);
        tile.bush_node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        tile.node->add_child(tile.bush_node);
        tile.bush_bounds = bush_mesh->get_aabb();
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
        if (tile.grass_node) {
            const float distance = bounds_distance(tile.grass_bounds, viewer - Vector3(entry.first.x, 0, entry.first.z));
            tile.grass_node->set_visible(distance < settings().grass_render_distance);
            // Past 32 m only the core two thirds of the tufts are drawn.
            const int visible = distance > 32.f ? tile.grass_core : -1;
            auto multimesh = tile.grass_node->get_multimesh();
            if (multimesh.is_valid() && multimesh->get_visible_instance_count() != visible)
                multimesh->set_visible_instance_count(visible);
        }
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
bool VoxelWorld::build_requested_collider() {
    while(!collider_requests.empty()) {
        const auto key=*collider_requests.begin();
        collider_requests.erase(key);
        if(!wanted.count(key))continue;
        auto found=tiles.find(key);
        auto revision=versions.find(key);
        if(found==tiles.end() || (revision!=versions.end() && revision->second!=found->second.version))continue;
        auto &tile=found->second;
        if(tile.collider || tile.collision_triangles==0)continue;
        const auto started=std::chrono::steady_clock::now();
        PackedVector3Array faces;
        faces.resize(tile.collision_triangles*3);
        auto *data=faces.ptrw();
        size_t index=0;
        for(const auto &rect:tile.collision_rects)
            for(const auto &point:rect.faces())data[index++]=Vector3(point[0],point[1],point[2]);
        for(const auto &point:tile.partial_collision_faces)data[index++]=Vector3(point[0],point[1],point[2]);
        Ref<ConcavePolygonShape3D> shape;
        shape.instantiate();
        shape->set_faces(faces);
        auto *collision=memnew(CollisionShape3D);
        collision->set_shape(shape);
        tile.collider=memnew(StaticBody3D);
        tile.collider->set_collision_layer(1);
        tile.collider->set_collision_mask(2);
        tile.collider->add_child(collision);
        tile.node->add_child(tile.collider);
        std::vector<altaria::CollisionRect>().swap(tile.collision_rects);
        std::vector<std::array<float,3>>().swap(tile.partial_collision_faces);
        max_collider_ms=std::max(max_collider_ms,std::chrono::duration<float,std::milli>(
            std::chrono::steady_clock::now()-started).count());
        return true;
    }
    return false;
}
// Seam geometry and the edge cells it replaces, for one snapshot of the
// visible surface tiles. Runs on a background thread.
VoxelWorld::SeamResult VoxelWorld::compute_seams(std::vector<SeamTile> snapshot) {
    SeamResult out;
    Surface surface;
    std::unordered_map<TileKey, const altaria::TerrainEdges *, TileHash> visible;
    visible.reserve(snapshot.size() * 2);
    for (const auto &tile : snapshot)
        visible.emplace(tile.key, tile.edges.get());
    const auto at = [&](int x, int z, int max_level) -> std::optional<TileKey> {
        for (int level = 0; level <= max_level; ++level) {
            const int span = 32 << level;
            TileKey key{int(std::floor(double(x) / span)) * span, int(std::floor(double(z) / span)) * span, level};
            if (visible.count(key))
                return key;
        }
        return std::nullopt;
    };
    const auto replace = [&](const TileKey &key, int edge, int cell) {
        auto &mask = out.masks[key][edge];
        mask = int32_t(uint32_t(mask) | (1u << cell));
    };
    for (const auto &tile : snapshot) {
        const auto &key = tile.key;
        if (key.level == 0)
            continue;
        const int step = 1 << key.level;
        for (int edge = 0; edge < 4; ++edge) {
            const bool forward = (edge & 1) != 0;
            const bool along_x = edge < 2;
            const int edge_start = along_x ? key.x : key.z;
            const int boundary = (along_x ? key.z : key.x) + (forward ? key.size() : 0);
            const int across = boundary - (forward ? 0 : 1);
            for (int i = 0; i < 32; ++i) {
                const int start = edge_start + i * step;
                const auto neighbor = at(along_x ? start : across, along_x ? across : start, key.level - 1);
                if (!neighbor)
                    continue;
                replace(key, edge, i);
                const int neighbor_step = 1 << neighbor->level;
                const int neighbor_start = along_x ? neighbor->x : neighbor->z;
                const auto &other = (*visible.at(*neighbor))[edge ^ 1];
                for (int along = start; along < start + step; along += neighbor_step) {
                    const int cell = (along - neighbor_start) / neighbor_step;
                    replace(*neighbor, edge ^ 1, cell);
                    const auto &a = (*tile.edges)[edge][i];
                    const auto &b = other[cell];
                    append_terrain_seam(surface, along_x ? 2 : 0, float(boundary), float(along),
                                        float(along + neighbor_step), forward ? a : b, forward ? b : a);
                }
            }
        }
    }
    if (!surface.vertices.empty()) {
        Surface opaque, lava, water;
        if (split_liquids(surface, opaque, lava, water))
            out.water = pack_surface(surface);
        else {
            out.opaque = pack_surface(opaque);
            out.lava = pack_surface(lava);
            out.water = pack_surface(water);
        }
    }
    return out;
}
// LOD seams follow the visible tiles a frame or two behind: the work happens
// off the main thread, which only snapshots the tile list and swaps the mesh.
void VoxelWorld::update_seams() {
    if (seam_task.valid() && seam_task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        ALTARIA_PROFILE("world.seams_apply");
        SeamResult result = seam_task.get();
        if (!underground_stream) {
            for (auto &[key, tile] : tiles) {
                const auto it = result.masks.find(key);
                const Vector4i mask = it == result.masks.end() ? Vector4i() : it->second;
                if (mask != tile.seam_edges) {
                    tile.seam_edges = mask;
                    for_meshes(tile, [&](MeshInstance3D *mesh_node) { mesh_node->set_instance_shader_parameter("seam_edges", mask); });
                }
            }
            if (!terrain_seams) {
                terrain_seams = memnew(MeshInstance3D);
                terrain_seams->set_name("TerrainLodSeams");
                terrain_seams->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
                add_child(terrain_seams);
            }
            MeshBuild mesh;
            add_arrays(mesh, result.opaque, solid_material);
            add_arrays(mesh, result.lava, lava_material);
            add_arrays(mesh, result.water, water_material);
            terrain_seams->set_mesh(mesh.finish());
            terrain_seams->show();
        }
    }
    if (seams_dirty && !seam_task.valid() && !underground_stream) {
        ALTARIA_PROFILE("world.seams_snapshot");
        std::vector<SeamTile> snapshot;
        snapshot.reserve(tiles.size());
        for (const auto &[key, tile] : tiles)
            if (tile.ground_visible && tile.edges && tile.window.mode != VerticalWindow::underground)
                snapshot.push_back({key, tile.edges});
        seam_task = std::async(std::launch::async, compute_seams, std::move(snapshot));
        seams_dirty = false;
    }
}

void VoxelWorld::reconcile() {
    constexpr double fade_seconds = .32;
    std::vector<TileKey> retire;
    std::vector<TileKey> waiting;
    std::vector<TileKey> fading;
    auto set_opacity = [](Tile &tile, float opacity) {
        if (tile.opacity == opacity)
            return;
        tile.opacity = opacity;
        for_meshes(tile, [&](MeshInstance3D *mesh_node) { mesh_node->set_instance_shader_parameter("lod_opacity", opacity); });
        if (tile.grass_node)
            tile.grass_node->set_instance_shader_parameter("lod_opacity", opacity);
        if (tile.leaf_shadow)
            tile.leaf_shadow->set_instance_shader_parameter("lod_opacity", opacity);
        if (tile.leaf_cards)
            tile.leaf_cards->set_instance_shader_parameter("lod_opacity", opacity);
        if (tile.sky_shadow)
            tile.sky_shadow->set_instance_shader_parameter("lod_opacity", opacity);
    };
    const auto set_ground = [&](Tile &tile, bool visible) {
        seams_dirty |= tile.ground_visible != visible;
        tile.ground_visible = visible;
    };
    lod_fading = false;
    for (auto &[key, tile] : tiles) {
        if (wanted.count(key)) {
            tile.retiring_since = -1;
            continue;
        }
        if (underground_stream || !tile.node->is_visible()) {
            retire.push_back(key);
            continue;
        }
        if (!tile.ground_visible && tile.retiring_since >= 0) {
            // A later plan can request a different replacement while this
            // tile's foliage is still fading. Only committed ground can
            // provide coverage while a replacement is pending.
            const float progress = float((stream_clock - tile.retiring_since) / fade_seconds);
            if (progress < 1.f) {
                set_opacity(tile, -1.f - progress);
                fading.push_back(key);
                lod_fading = true;
            } else
                retire.push_back(key);
            continue;
        }
        bool overlaps_replacement = false;
        bool complete = true;
        for (const auto &replacement : ordered) {
            if (!overlaps(key, replacement))
                continue;
            overlaps_replacement = true;
            if (!tiles.count(replacement)) {
                complete = false;
                break;
            }
        }
        if (!complete) {
            tile.retiring_since = -1;
            set_opacity(tile, 1.f);
            waiting.push_back(key);
        } else if (overlaps_replacement) {
            if (tile.retiring_since < 0)
                tile.retiring_since = stream_clock;
            const float progress = float((stream_clock - tile.retiring_since) / fade_seconds);
            if (progress < 1.f) {
                set_ground(tile, false);
                set_opacity(tile, -1.f - progress);
                fading.push_back(key);
                lod_fading = true;
            } else
                retire.push_back(key);
        } else
            retire.push_back(key);
    }
    retirement_pending=false;
    int retired_count=0;
    for (const auto &key : retire) {
        auto tile=tiles.find(key);
        set_ground(tile->second, false);
        tile->second.node->hide();
        const bool distant=underground_stream ||
            tile_distance(key,viewer.x,viewer.z)>stream_radius*.5f;
        if(distant) {
            if(tile->second.collider) {
                tile->second.collider->set_collision_layer(0);
                tile->second.collider->set_collision_mask(0);
            }
            retired_nodes.push_back(tile->second.node);
        } else if(retired_count++<2)
            tile->second.node->queue_free();
        else {
            retirement_pending=true;
            continue;
        }
        tiles.erase(tile);
    }
    for (auto &[key, tile] : tiles) {
        if (!wanted.count(key))
            continue;
        const bool blocked = std::any_of(waiting.begin(), waiting.end(),
                                        [&](const TileKey &old) { return overlaps(key, old); });
        set_ground(tile, !blocked);
        tile.node->set_visible(!blocked);
        if (blocked)
            continue;
        float opacity = 1.f;
        for (const auto &old : fading)
            if (overlaps(key, old))
                opacity = std::min(opacity, float((stream_clock - tiles.at(old).retiring_since) / fade_seconds));
        set_opacity(tile, opacity);
    }
    if (underground_stream) {
        if (terrain_seams)
            terrain_seams->hide();
    } else if (terrain_seams && !terrain_seams->is_visible())
        seams_dirty = true;
    for (auto it = versions.begin(); it != versions.end();) {
        if (!wanted.count(it->first) && !tiles.count(it->first) && !pending.count(it->first) &&
            !vegetation_pending.count(it->first))
            it = versions.erase(it);
        else
            ++it;
    }

}
VerticalWindow VoxelWorld::window_for(const TileKey &key) const {
    if (settings().preset != WorldPreset::Default ||
        (!settings().adaptive_streaming && !underground_stream) || key.level > 2)
        return {};
    VerticalWindow window=underground_stream
        ? VerticalWindow{VerticalWindow::underground,cave_band-cave_vertical_half_span,cave_band+cave_vertical_half_span}
        : VerticalWindow{VerticalWindow::surface,key.level<=1 ? shallow_band : 8192,8192};
    const auto demand=debris_demand.find(key);
    if(demand!=debris_demand.end()) {
        window.floor=std::min(window.floor,demand->second.floor);
        window.ceiling=std::max(window.ceiling,demand->second.ceiling);
    }
    return window;
}

bool VoxelWorld::has_open_roof() const {
    // Favor keeping surface coverage for long, straight shafts. The sample
    // stays inside the loaded band and never asks the generator to fill it.
    const int x=int(std::floor(viewer.x)),y=int(std::floor(viewer.y))+2,z=int(std::floor(viewer.z));
    if (!voxel_ready(x, y, z))
        return false;
    const int rise_to_sky = std::max(4, int(std::ceil(local_ground)) + 4 - y);
    if (rise_to_sky > 128)
        return false;
    constexpr int slopes[][2]={{0,0},{1,0},{-1,0},{0,1},{0,-1}};
    for(const auto &slope:slopes) {
        bool open=true;
        for (int rise = 0; rise <= rise_to_sky; ++rise) {
            const int sx=x+slope[0]*rise/8,sz=z+slope[1]*rise/8;
            const auto &block = registry().get(block_at(sx, y + rise, sz));
            if (!voxel_ready(sx, y + rise, sz) || (block.solid && !block.foliage && !block.wood)) {
                open=false;
                break;
            }
        }
        if(open)return true;
    }
    return false;
}

void VoxelWorld::plan_stream(float delta) {
    const auto &s=settings();
    stream_timer+=delta;
    stream_clock+=delta;
    if(s.adaptive_streaming) {
        const Vector3 shift=viewer-last_viewer;
        last_viewer=viewer;
        if(!viewer_initialized || shift.length_squared()>64*64)travel_velocity=Vector3();
        else if(delta>0)travel_velocity=travel_velocity.lerp(shift/delta,1-std::exp(-delta*8));
        viewer_initialized=true;
    }
    const bool moved=s.adaptive_streaming && stream_timer>=.075f &&
        viewer.distance_squared_to(planned_viewer)>8*8;
    if(stream_timer<.3f && !moved)return;
    stream_timer=0;
    planned_viewer=viewer;
    {
        Vector2i cell(int(std::floor(viewer.x)), int(std::floor(viewer.z)));
        if (cell != height_sample_key) {
            height_sample_key = cell;
            const auto column = generator()->columns({float(cell.x)}, {float(cell.y)}, nullptr, false).front();
            local_ground = column.height + s.cliff_strength * column.cliff;
        }
        // Only open sky counts as outside. Detect an enclosed passage near
        // the surface with the same rule as a deep chamber.
        const bool deep = s.caves.enabled && viewer.y < local_ground - (underground_stream ? 2.f : 4.f) &&
                          !has_open_roof();
        if(deep && (!underground_stream || std::abs(viewer.y-cave_band)>48.f))
            cave_band=int(std::floor((viewer.y+32)/64))*64;
        underground_stream=deep;
        if(!deep && viewer.y<local_ground-36)
            shallow_band=int(std::floor(viewer.y/64))*64-64;
        else
            shallow_band=8192;
        const float horizon=std::ceil((fog_range+128)/128)*128;
        // Caves stream only as far as light lets you see; the underworld's
        // haze reaches further, so it keeps a wider ring.
        const float cave_radius = in_underworld() ? 208.f : std::clamp(underground_sight() + 40.f, 96.f, 288.f);
        stream_radius = deep ? std::min(s.view_distance, cave_radius) :
                        s.adaptive_streaming ? std::clamp(horizon, 256.f, s.view_distance) : s.view_distance;
        Vector3 lead=travel_velocity;
        lead.y=0;
        lead=(lead*.65f).limit_length(deep?72.f:240.f);
        std::vector<TileKey> required;
        for(auto it=debris_demand.begin();it!=debris_demand.end();) {
            if(it->second.expires<stream_clock)it=debris_demand.erase(it);
            else {
                required.push_back(it->first);
                ++it;
            }
        }
        if (s.preset == WorldPreset::Plain) {
            // A uniform horizon needs only a small detail area and coarse outer tiles.
            ordered = select_tiles({viewer.x,viewer.z,viewer.x,viewer.z,stream_radius,false,&required},s);
        } else {
            ordered=select_tiles({viewer.x,viewer.z,viewer.x+lead.x,viewer.z+lead.z,stream_radius,deep,&required},s);
        }
    }
    wanted.clear();
    wanted.insert(ordered.begin(),ordered.end());
}

size_t VoxelWorld::upload_vertices(const MeshResult &r) {
    // Canopy meshes are staged separately and do not count here.
    return r.solid.vertices.size() + r.water.vertices.size() + r.decorations.vertices.size() +
           r.bushes.vertices.size() + r.grass.size() * 4;
}
void VoxelWorld::apply_canopy(Tile &tile, CanopyUpload &canopy) {
    const auto *prepared = static_cast<const PreparedTile *>(canopy.prepared.get());
    MeshInstance3D *node = tile.node;
    const auto stage = [&](const Surface &surface, const Dictionary *ready, MeshInstance3D *&mesh_node, const char *name,
                           uint32_t layers, GeometryInstance3D::ShadowCastingSetting casting,
                           const Ref<ShaderMaterial> &surface_material) {
        if (surface.vertices.empty()) {
            if (mesh_node) {
                mesh_node->hide();
                mesh_node->set_mesh(Ref<Mesh>());
            }
            return;
        }
        if (!mesh_node) {
            mesh_node = memnew(MeshInstance3D);
            mesh_node->set_name(name);
            mesh_node->set_layer_mask(layers);
            mesh_node->set_cast_shadows_setting(casting);
            mesh_node->set_extra_cull_margin(.6f);
            node->add_child(mesh_node);
        }
        MeshBuild mesh;
        if (ready)
            add_arrays(mesh, *ready, surface_material);
        else
            add_surface(mesh, surface, surface_material);
        mesh_node->set_mesh(mesh.finish());
        mesh_node->set_instance_shader_parameter("lod_opacity", tile.opacity);
        mesh_node->show();
    };
    stage(canopy.leaf_shadows, prepared ? &prepared->leaf_shadows : nullptr, tile.leaf_shadow, "CanopyShadows", 1,
          GeometryInstance3D::SHADOW_CASTING_SETTING_SHADOWS_ONLY, solid_material);
    // Canopy cards are visible-only: CanopyShadows already casts the canopy's
    // shadow, so the cards stay out of every shadow cascade.
    stage(canopy.leaf_cards, prepared ? &prepared->leaf_cards : nullptr, tile.leaf_cards, "LeafCards",
          node->get_layer_mask(), GeometryInstance3D::SHADOW_CASTING_SETTING_OFF, leaf_material);
    stage(canopy.sky_shadows, prepared ? &prepared->sky_shadows : nullptr, tile.sky_shadow, "CaveRoofShadows",
          TERRAIN_SKY_SHADOW_LAYER, GeometryInstance3D::SHADOW_CASTING_SETTING_SHADOWS_ONLY, solid_material);
}
// One staged canopy per frame, before new tiles: a heavy canopy uses up the
// frame's time budget so no terrain upload stacks on top of it.
void VoxelWorld::upload_canopy() {
    while (!canopy_queue.empty()) {
        auto canopy = std::move(canopy_queue.front());
        canopy_queue.pop_front();
        auto tile = tiles.find(canopy.key);
        if (tile == tiles.end() || !tile->second.node || tile->second.version != canopy.version)
            continue;
        apply_canopy(tile->second, canopy);
        return;
    }
}
// Frame pacing is judged by its worst frames: an average hides the hitches.
void VoxelWorld::record_frame(float milliseconds) {
    if (frame_count > 0 && milliseconds > frame_spike_ms)
        ++frame_spikes;
    frame_times[frame_cursor] = milliseconds;
    frame_cursor = (frame_cursor + 1) % frame_times.size();
    frame_count = std::min(frame_count + 1, frame_times.size());
}
void VoxelWorld::frame_stats(float &worst, float &p99, float &average) const {
    worst = p99 = average = 0;
    if (!frame_count)
        return;
    std::vector<float> recent(frame_times.begin(), frame_times.begin() + frame_count);
    for (float t : recent) {
        worst = std::max(worst, t);
        average += t;
    }
    average /= float(recent.size());
    const size_t rank = std::min(recent.size() - 1, size_t(std::ceil(.99 * double(recent.size()))) - 1);
    std::nth_element(recent.begin(), recent.begin() + rank, recent.end());
    p99 = recent[rank];
}
// Shows the glow lights of tiles near the viewer and hides the rest. Hundreds
// of distant lights cost render time without lighting anything on screen.
void VoxelWorld::update_lights() {
    for (auto &[key, tile] : tiles) {
        if (key.level != 0 || !tile.node)
            continue;
        const bool near = tile_distance(key, viewer.x, viewer.z) < light_radius;
        if (near == tile.lights_visible)
            continue;
        tile.lights_visible = near;
        for (int i = tile.node->get_child_count() - 1; i >= 0; --i)
            if (auto *light = Object::cast_to<OmniLight3D>(tile.node->get_child(i)))
                light->set_visible(near);
    }
}
void VoxelWorld::process_structure(float delta) {
    record_frame(float(delta * 1000.0));
    liquid_sim.step(float(delta));
    build_requested_collider();
    if (structure_dirty.empty())
        return;
    const auto it = structure_dirty.begin();
    const TileKey key = *it;
    structure_dirty.erase(it);
    auto edits = model->snapshot_for(key);
    const bool occupied = std::any_of(edits->begin(), edits->end(), [&](const auto &entry) {
        const auto &p = entry.first;
        return entry.second != registry().air && p.x >= key.x && p.x < key.x + key.size() &&
               p.z >= key.z && p.z < key.z + key.size();
    });
    if (!occupied) {
        if (auto tile = tiles.find(key); tile != tiles.end()) {
            tile->second.node->queue_free();
            tiles.erase(tile);
        }
        wanted.erase(key);
        versions.erase(key);
        return;
    }
    wanted.insert(key);
    upload(build_tile(key, versions[key], *generator(), edits, render_surfaces(key), false));
    auto &tile = tiles.at(key);
    tile.node->show();
    tile.ground_visible = true;
}

void VoxelWorld::_process(double delta) {
    if (model && settings().preset == WorldPreset::Structure) {
        process_structure(delta);
        return;
    }
    if (!workers)
        return;
    ALTARIA_PROFILE("world.process");
    record_frame(float(delta * 1000.0));
    for(int count=0;count<6 && !retired_nodes.empty();count++) {
        retired_nodes.front()->queue_free();
        retired_nodes.pop_front();
    }
    { ALTARIA_PROFILE("world.plan_stream"); plan_stream(float(delta)); }
    if(stream_timer==0) {
        // A change in vertical demand also invalidates outstanding requests
        // for an unchanged XZ tile.
        cancelled_jobs+=workers->cancel_queued([this](TileKey key,uint64_t version,bool vegetation,VerticalWindow window) {
            auto &queue=vegetation?vegetation_pending:pending;
            auto it=queue.find(key);
            if(it==queue.end() || it->second!=version)return true;
            if(wanted.count(key) && versions[key]==version &&
               (vegetation || window==window_for(key)))return false;
            queue.erase(it);
            return true;
        });
    }
    MeshResult result;
    int uploaded = 0, processed = 0;
    bool coverage_changed=false;
    auto upload_start=std::chrono::steady_clock::now();
    last_upload_ms=0;
    bool collider_built;
    { ALTARIA_PROFILE("world.collider"); collider_built = build_requested_collider(); }
    if(collider_built) {
        last_upload_ms=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-upload_start).count();
        max_upload_ms=std::max(max_upload_ms,last_upload_ms);
    }
    if (!canopy_queue.empty() && last_upload_ms < settings().upload_time_budget_ms) {
        { ALTARIA_PROFILE("world.canopy_upload"); upload_canopy(); }
        last_upload_ms=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-upload_start).count();
        max_upload_ms=std::max(max_upload_ms,last_upload_ms);
    }
    size_t frame_vertices = 0;
    while (last_upload_ms<settings().upload_time_budget_ms && uploaded < settings().upload_budget && processed < settings().max_pending) {
        if (held_result) {
            result = std::move(*held_result);
            held_result.reset();
        } else if (!workers->pop(result)) {
            break;
        }
        ++processed;
        auto &queue = result.vegetation_only ? vegetation_pending : pending;
        auto it = queue.find(result.key);
        if (wanted.count(result.key) && versions[result.key] == result.version &&
            (result.vegetation_only || result.window==window_for(result.key))) {
            // Bound what reaches the GPU in one frame: a large tile after
            // another upload waits for the next frame instead of stacking.
            const size_t vertices = upload_vertices(result);
            if (uploaded > 0 && frame_vertices + vertices > size_t(settings().upload_vertex_budget)) {
                held_result = std::move(result);
                ++deferred_uploads;
                break;
            }
            if (it != queue.end() && it->second == result.version)
                queue.erase(it);
            frame_vertices += vertices;
            if (result.vegetation_only) {
                auto tile = tiles.find(result.key);
                if (tile != tiles.end() && tile->second.version == result.version &&
                    needs_vegetation(result.key, tile->second))
                    { ALTARIA_PROFILE("world.vegetation_upload"); upload_vegetation(std::move(result)); }
            } else {
                coverage_changed = true;
                { ALTARIA_PROFILE("world.tile_upload"); upload(std::move(result)); }
            }
            ++uploaded;
            last_upload_ms=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-upload_start).count();
            max_upload_ms=std::max(max_upload_ms,last_upload_ms);
            if(last_upload_ms>=settings().upload_time_budget_ms)break;
        } else {
            if (it != queue.end() && it->second == result.version)
                queue.erase(it);
            ++discarded_jobs;
        }
    }
    last_upload_vertices = frame_vertices;
    max_upload_vertices = std::max(max_upload_vertices, frame_vertices);
    { ALTARIA_PROFILE("world.liquids"); liquid_sim.step(float(delta)); }
    { ALTARIA_PROFILE("world.update_vegetation"); update_vegetation(); }
    if (coverage_changed || stream_timer == 0 || retirement_pending || lod_fading)
        { ALTARIA_PROFILE("world.reconcile"); reconcile(); }
    update_seams();
    light_timer += float(delta);
    if (light_timer >= .25f) {
        light_timer = 0;
        update_lights();
    }
    ALTARIA_PROFILE("world.submit");
    for (auto &key : ordered) {
        if (pending.size() + vegetation_pending.size() >= size_t(settings().max_pending))
            break;
        auto it = tiles.find(key);
        uint64_t version = versions[key];
        const auto window=window_for(key);
        if (pending.count(key) || (it != tiles.end() && it->second.version == version &&
                                  it->second.window==window))
            continue;
        pending[key] = version;
        bool refresh_foliage = key.level == 0 && it != tiles.end() &&
                               (it->second.grass_node || it->second.bush_node) &&
                               needs_vegetation(key, it->second);
        workers->submit(key, version, model->snapshot_for(key), render_surfaces(key), refresh_foliage, window,
                        resource_snapshot(key));
    }
    if (world_ready_ms < 0 && !ordered.empty() && pending.empty() && vegetation_pending.empty()) {
        bool ready = true;
        for (const auto &key : ordered) {
            auto entry = tiles.find(key);
            if (entry == tiles.end() || entry->second.version != versions[key] || !(entry->second.window==window_for(key)) ||
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
    if (settings().preset == WorldPreset::Structure)
        return registry().air;
    TileKey key{int(std::floor(double(x) / 32)) * 32, int(std::floor(double(z) / 32)) * 32, 0};
    auto it = tiles.find(key);
    if (it != tiles.end() && it->second.volume) {
        auto v = it->second.volume;
        int iy = y - v->min_y;
        if (iy < 0 && v->bottom_fill && y >= settings().min_y)
            return y == settings().min_y ? registry().bedrock : generator()->base_rock(y);
        if (iy < 0 || iy >= v->height)
            return 0;
        return v->at(x - key.x, iy, z - key.z);
    }
    // Unknown streaming space must never plan on main. Physics and interaction
    // use explicit readiness gates, rather than treating this as known empty air.
    if (settings().caves.enabled)
        return registry().air;
    return model->generator->sample(x, y, z);
}
std::shared_ptr<const ResourceDepletions> VoxelWorld::resource_snapshot(TileKey key) const {
    const auto found = depleted_resources.find(key);
    return found == depleted_resources.end() ? nullptr : found->second;
}
std::optional<ResourcePiece> VoxelWorld::pick_resource(Vector3 origin, Vector3 direction, float &distance) const {
    if (settings().resources.empty() || settings().preset != WorldPreset::Default)
        return {};
    const auto tile_start = [](float coord) { return int(std::floor(coord / 32.f)) * 32; };
    const Vector3 end = origin + direction * distance;
    const auto edits = model->snapshot();
    std::optional<ResourcePiece> hit;
    for (int z = tile_start(std::min(origin.z, end.z) - 1.f); z <= tile_start(std::max(origin.z, end.z) + 1.f); z += 32)
        for (int x = tile_start(std::min(origin.x, end.x) - 1.f); x <= tile_start(std::max(origin.x, end.x) + 1.f); x += 32) {
            const TileKey key{x, z, 0};
            const auto tile = tiles.find(key);
            if (tile == tiles.end() || !tile->second.volume || !tile->second.ground_visible)
                continue;
            const auto depleted = resource_snapshot(key);
            for (auto piece : tile->second.volume->resources) {
                Vector3 position(piece.position[0] + x, piece.position[1], piece.position[2] + z);
                const auto axis = [&](int i) {
                    return Vector3(piece.axes[i][0], piece.axes[i][1], piece.axes[i][2]);
                };
                const Basis inverse = Basis(axis(0), axis(1), axis(2)).transposed();
                const Vector3 local_origin = inverse.xform(origin - position);
                Vector3 intersection;
                const Vector3 dimensions(piece.dimensions[0], piece.dimensions[1], piece.dimensions[2]);
                if (!AABB(-dimensions * .5f, dimensions)
                         .intersects_segment(local_origin, local_origin + inverse.xform(direction) * distance, &intersection))
                    continue;
                const auto &p = piece.support;
                const auto &face = piece.exposed_face;
                if (edits->count(p) || block_at(p.x + face.x, p.y + face.y, p.z + face.z) != registry().air)
                    continue;
                if (depleted) {
                    const auto mask = depleted->find(p);
                    if (mask != depleted->end() && (mask->second & (1u << piece.slot)))
                        continue;
                }
                distance = local_origin.distance_to(intersection);
                piece.position = {position.x, position.y, position.z};
                hit = piece;
            }
        }
    return hit;
}
void VoxelWorld::mine_resource(const ResourcePiece &piece) {
    const TileKey key{int(std::floor(piece.support.x / 32.0)) * 32,
                      int(std::floor(piece.support.z / 32.0)) * 32, 0};
    const auto previous = resource_snapshot(key);
    auto updated = previous ? std::make_shared<ResourceDepletions>(*previous) : std::make_shared<ResourceDepletions>();
    (*updated)[piece.support] |= 1u << piece.slot;
    depleted_resources[key] = std::move(updated);
    if (auto tile = tiles.find(key); tile != tiles.end() && tile->second.volume) {
        std::vector<ResourcePiece> active;
        active.reserve(tile->second.volume->resources.size());
        const auto &depletions = *depleted_resources[key];
        for (const auto &candidate : tile->second.volume->resources) {
            auto dep = depletions.find(candidate.support);
            if (dep != depletions.end() && (dep->second & (1u << candidate.slot)))
                continue;
            active.push_back(candidate);
        }
        upload_deposits(tile->second, active);
    }
    ++versions[key];
}
bool VoxelWorld::voxels_ready(Vector3 p) const {
    if (settings().preset == WorldPreset::Structure)
        return true;
    for (int dz : {-4, 4})
        for (int dx : {-4, 4}) {
            TileKey key{int(std::floor((p.x + dx) / 32)) * 32, int(std::floor((p.z + dz) / 32)) * 32, 0};
            auto t = tiles.find(key);
            const int x = int(std::floor(p.x + dx)), z = int(std::floor(p.z + dz));
            if (t == tiles.end() || !t->second.volume ||
                !voxel_ready(x, int(std::floor(p.y)) - 2, z) ||
                !voxel_ready(x, int(std::floor(p.y)) + 3, z))
                return false;
        }
    return true;
}
bool VoxelWorld::collision_ready(Vector3 p) const {
    if (settings().preset == WorldPreset::Structure)
        return true;
    for (int dz : {-4, 4})
        for (int dx : {-4, 4}) {
            TileKey key{int(std::floor((p.x + dx) / 32)) * 32, int(std::floor((p.z + dz) / 32)) * 32, 0};
            auto t = tiles.find(key);
            auto v = versions.find(key);
            const int x=int(std::floor(p.x+dx)),z=int(std::floor(p.z+dz));
            if (t == tiles.end() || !t->second.volume ||
                !voxel_ready(x,int(std::floor(p.y))-2,z) ||
                !voxel_ready(x,int(std::floor(p.y))+3,z) ||
                (v != versions.end() && v->second != t->second.version))
                return false;
        }
    return true;
}
bool VoxelWorld::debris_collision_ready(const AABB &bounds) {
    const auto hi=bounds.get_end();
    auto grid=[](float value){return int(std::floor(value/32))*32;};
    bool ready=true;
    for(int z=grid(bounds.position.z);z<=grid(hi.z);z+=32)for(int x=grid(bounds.position.x);x<=grid(hi.x);x+=32) {
        TileKey key{x,z,0};
        auto tile=tiles.find(key);
        auto revision=versions.find(key);
        if (settings().preset == WorldPreset::Structure) {
            if (structure_dirty.count(key))
                ready = false;
            else if (tile != tiles.end() && !tile->second.collider && tile->second.collision_triangles > 0) {
                collider_requests.insert(key);
                ready = false;
            }
            continue;
        }
        if(settings().adaptive_streaming) {
            // Rigid bodies can be active outside the player's visual LOD0.
            // Hold their tiles and request more depth only when needed.
            const int lo_y=int(std::floor(bounds.position.y)),hi_y=int(std::floor(hi.y));
            const bool needs_y=tile!=tiles.end() && (!voxel_ready(x,lo_y,z) || !voxel_ready(x,hi_y,z));
            auto [request,inserted]=debris_demand.try_emplace(key,DebrisDemand{0,8192,-8192});
            auto &demand=request->second;
            demand.expires=stream_clock+.6;
            if(needs_y) {
                const int floor=int(std::floor((lo_y-8)/32.0))*32;
                const int ceiling=int(std::ceil((hi_y+8)/32.0))*32;
                if(floor<demand.floor || ceiling>demand.ceiling)stream_timer=.3f;
                demand.floor=std::min(demand.floor,floor);
                demand.ceiling=std::max(demand.ceiling,ceiling);
            }
            if(inserted && !wanted.count(key))stream_timer=.3f;
        }
        if(tile==tiles.end() || !tile->second.volume ||
           !voxel_ready(x,int(std::floor(bounds.position.y)),z) ||
           !voxel_ready(x,int(std::floor(hi.y)),z) ||
           (revision!=versions.end() && revision->second!=tile->second.version)) {
            ready=false;
            continue;
        }
        if(!tile->second.collider && tile->second.collision_triangles>0) {
            collider_requests.insert(key);
            ready=false;
        }
    }
    return ready;
}
bool VoxelWorld::voxel_ready(int x,int y,int z) const {
    if (settings().preset == WorldPreset::Structure)
        return true;
    if(y<settings().min_y || y>=settings().max_y)return true;
    TileKey key{int(std::floor(double(x)/32))*32,int(std::floor(double(z)/32))*32,0};
    auto tile=tiles.find(key);
    if(tile==tiles.end() || !tile->second.volume)return false;
    const auto &v=*tile->second.volume;
    return (!v.unknown_below || y>=v.min_y) && (!v.unknown_above || y<v.min_y+v.height);
}
bool VoxelWorld::cave_liquid_at(int x,int y,int z) const {
    if(!model || !settings().caves.hydrology.basin_regions)return false;
    TileKey key{int(std::floor(double(x)/32))*32,int(std::floor(double(z)/32))*32,0};
    auto tile=tiles.find(key);
    if(tile==tiles.end() || !tile->second.volume)return false;
    const auto &v=*tile->second.volume;
    if(!v.cave_liquid_at(x-key.x,y-v.min_y,z-key.z))return false;
    return model->snapshot()->count({x,y,z})==0;
}
float VoxelWorld::sky_exposure_at(Vector3 world_pos) const {
    if (settings().preset == WorldPreset::Structure)
        return 1.f;
    const int px = int(std::floor(world_pos.x)), py = int(std::floor(world_pos.y)), pz = int(std::floor(world_pos.z));
    const TileKey key{int(std::floor(double(px) / 32)) * 32, int(std::floor(double(pz) / 32)) * 32, 0};
    const auto tile = tiles.find(key);
    if (tile == tiles.end() || !tile->second.volume)
        return underground_stream ? 0.f : 1.f;
    const auto &v = *tile->second.volume;
    return std::clamp(float(v.sky_exposure(px - key.x, py - v.min_y, pz - key.z)) / 15.f, 0.f, 1.f);
}
const caves::HydroPlan *VoxelWorld::cave_hydrology_at(int x,int z) const {
    TileKey key{int(std::floor(double(x)/32))*32,int(std::floor(double(z)/32))*32,0};
    auto tile=tiles.find(key);
    if(tile==tiles.end() || !tile->second.volume)return nullptr;
    const auto &v=*tile->second.volume;
    if(!v.cave_basin_span)return nullptr;
    auto basin=caves::basin_of({x,z},v.cave_basin_span);
    for(const auto &h:v.cave_basins)if(h.basin==basin)return &h;
    return nullptr;
}
Vector3 VoxelWorld::cave_river_at(Vector3 p) const {
    int x=int(std::floor(p.x)),y=int(std::floor(p.y)),z=int(std::floor(p.z));
    if(!cave_liquid_at(x,y,z) || block_at(x,y,z)!=registry().water)return {};
    TileKey key{int(std::floor(double(x)/32))*32,int(std::floor(double(z)/32))*32,0};
    auto it=tiles.find(key);
    if(it==tiles.end() || !it->second.volume)return {};
    Vector3 current;
    float best=1;
    for(const auto &s:it->second.volume->cave_rivers) {
        Vector3 a(s.from[0]+.5f,s.from[1]+.5f,s.from[2]+.5f),b(s.to[0]+.5f,s.to[1]+.5f,s.to[2]+.5f);
        Vector3 d=b-a;float length_sq=d.length_squared();
        if(length_sq<.001f)continue;
        float t=std::clamp(float((p-a).dot(d))/length_sq,0.f,1.f);
        float distance=float(p.distance_squared_to(a+d*t))/(s.radius*s.radius);
        if(distance>=best)continue;
        best=distance;d.y=0;
        current=d.normalized()*(1.8f*(1-distance));
    }
    return current;
}
std::shared_ptr<const caves::IndexedNetwork> VoxelWorld::cave_plan_at(caves::Region region) const {
    if(!model || !model->generator->underground())return {};
    auto plan=model->generator->underground()->resident(region);if(plan)return plan;
    // Uncached oversized/tight-budget plans still belong to loaded near chunks.
    // This path never starts planning and never waits for a worker mutex.
    for(const auto &entry:tiles)if(entry.second.volume)
        for(const auto &loaded:entry.second.volume->cave_plans)if(loaded->network.region==region)return loaded;
    return {};
}
Appearance VoxelWorld::appearance_at(IVec3 p) const {
    auto authored = model->appearance_at(p);
    if (authored.painted || !registry().get(block_at(p.x, p.y, p.z)).foliage ||
        model->snapshot()->count(p))
        return authored;
    const TileKey key{int(std::floor(p.x / 32.0)) * 32, int(std::floor(p.z / 32.0)) * 32, 0};
    auto tile = tiles.find(key);
    if (tile != tiles.end() && tile->second.volume) {
        const auto &volume = *tile->second.volume;
        if (p.y >= volume.min_y && p.y < volume.min_y + volume.height) {
            const size_t index = ((p.z - key.z + 1) * (volume.height + 2) +
                                  (p.y - volume.min_y + 1)) * 34 + p.x - key.x + 1;
            auto leaf = volume.surfaces.find(index);
            if (leaf != volume.surfaces.end() && leaf->second.painted) {
                authored.painted = true;
                authored.tint = leaf->second.tint;
                authored.preserve = leaf->second.preserve;
                authored.brightness = leaf->second.brightness;
            }
        }
    }
    return authored;
}
uint8_t VoxelWorld::shape_at(int x, int y, int z) const {
    auto connected=ramp_connections.find({x,y,z});
    if(connected!=ramp_connections.end())return connected->second;
    return natural_shape_at(x,y,z);
}
uint8_t VoxelWorld::natural_shape_at(int x,int y,int z) const {
    auto base=base_shape_at(x,y,z);
    const int type = shape_type(base);
    if (type == int(BlockShape::Stairs) || type == int(BlockShape::Roof))
        return connect_building_corner(base, [&](int dx, int dz) { return base_shape_at(x + dx, y, z + dz); });
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
        if(y<v.min_y || y>=v.min_y+v.height)return 0;
        size_t index = ((z - key.z + 1) * (v.height + 2) + (y - v.min_y + 1)) * 34 + x - key.x + 1;
        auto shape = v.shapes.find(index);
        return shape == v.shapes.end() ? 0 : shape->second;
    }
    if (settings().caves.enabled)
        return 0;
    auto c = generator()->column(x, z);
    auto shape = generator()->surface_shape(x, y, z, c);
    return shape && (!registry().get(generator()->terrain_sample(x, y, z)).solid ||
                     registry().get(generator()->sample(x, y + 1, z)).solid)
               ? 0
               : shape;
}
bool VoxelWorld::collides_box(Vector3 lo, Vector3 hi) const {
    if ((settings().caves.enabled || settings().adaptive_streaming) && hi.y>=settings().min_y && lo.y<settings().max_y) {
        for(int z=int(std::floor(lo.z/32));z<=int(std::floor(hi.z/32));z++)
            for(int x=int(std::floor(lo.x/32));x<=int(std::floor(hi.x/32));x++)
                if(!voxel_ready(x*32,int(std::floor(lo.y)),z*32) ||
                   !voxel_ready(x*32,int(std::floor(hi.y)),z*32))return true;
    }
    for (int z = int(std::floor(lo.z)); z <= int(std::floor(hi.z)); z++)
        for (int y = int(std::floor(lo.y)); y <= int(std::floor(hi.y)); y++)
            for (int x = int(std::floor(lo.x)); x <= int(std::floor(hi.x)); x++) {
                const auto &bdef = registry().get(block_at(x, y, z));
                if (bdef.solid && !bdef.foliage &&
                    shape_intersects(shape_at(x, y, z), {lo.x - x, lo.y - y, lo.z - z},
                                     {hi.x - x, hi.y - y, hi.z - z}))
                    return true;
            }
    return false;
}
bool VoxelWorld::in_foliage_box(Vector3 lo, Vector3 hi) const {
    if ((settings().caves.enabled || settings().adaptive_streaming) && hi.y >= settings().min_y && lo.y < settings().max_y) {
        for (int z = int(std::floor(lo.z / 32)); z <= int(std::floor(hi.z / 32)); z++)
            for (int x = int(std::floor(lo.x / 32)); x <= int(std::floor(hi.x / 32)); x++)
                if (!voxel_ready(x * 32, int(std::floor(lo.y)), z * 32) ||
                    !voxel_ready(x * 32, int(std::floor(hi.y)), z * 32))
                    return false;
    }
    for (int z = int(std::floor(lo.z)); z <= int(std::floor(hi.z)); z++)
        for (int y = int(std::floor(lo.y)); y <= int(std::floor(hi.y)); y++)
            for (int x = int(std::floor(lo.x)); x <= int(std::floor(hi.x)); x++)
                if (registry().get(block_at(x, y, z)).foliage &&
                    shape_intersects(shape_at(x, y, z), {lo.x - x, lo.y - y, lo.z - z},
                                     {hi.x - x, hi.y - y, hi.z - z}))
                    return true;
    return false;
}
Ref<ArrayMesh> VoxelWorld::surface_mesh(const Surface &surface) const {
    MeshBuild mesh;
    add_surface(mesh, surface, solid_material);
    return mesh.finish();
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
    MeshBuild mesh;
    add_surface(mesh, surface, physical ? physical_material : solid_material);
    return mesh.finish();
}
void VoxelWorld::set_visual_environment(float time, float strength, float scale, float variation, float fog_end) {
    fog_range=fog_end;
    physical_material->set_shader_parameter("grass_noise_scale", scale);
    physical_material->set_shader_parameter("grass_noise_strength", variation);
    solid_material->set_shader_parameter("grass_noise_scale", scale);
    solid_material->set_shader_parameter("grass_noise_strength", variation);
    leaf_material->set_shader_parameter("grass_noise_scale", scale);
    leaf_material->set_shader_parameter("grass_noise_strength", variation);
    set_wind_time(time);
    for (auto material : {solid_material, physical_material, grass_material, leaf_material})
        material->set_shader_parameter("wind_strength", strength);
    grass_material->set_shader_parameter("grass_noise_scale", scale);
    grass_material->set_shader_parameter("grass_noise_strength", variation);
}
void VoxelWorld::set_sky_fill(Vector3 fill) {
    current_sky_fill = fill;
    for (auto material : {solid_material, physical_material, grass_material, bush_material, water_material, leaf_material})
        material->set_shader_parameter("sky_fill", fill);
}
void VoxelWorld::set_grass_hit(const Vector3 &box_min, const Vector3 &box_max, float flash,
                               const Vector3 &root_shift, const Vector3 &blade_scale) {
    if (grass_material.is_null())
        return;
    grass_material->set_shader_parameter("hit_box_min", box_min);
    grass_material->set_shader_parameter("hit_box_max", box_max);
    grass_material->set_shader_parameter("hit_flash", flash);
    grass_material->set_shader_parameter("hit_root_shift", root_shift);
    grass_material->set_shader_parameter("hit_blade_scale", blade_scale);
}
void VoxelWorld::set_terrain_hit(const Vector3 &box_min, const Vector3 &box_max, float flash) {
    if (solid_material.is_null())
        return;
    solid_material->set_shader_parameter("hit_box_min", box_min);
    solid_material->set_shader_parameter("hit_box_max", box_max);
    solid_material->set_shader_parameter("hit_flash", flash);
}
void VoxelWorld::set_player_foliage_fade(Vector3 center, float opacity) {
    opacity = std::clamp(opacity, 0.1f, 1.0f);
    if (leaf_material.is_valid()) {
        leaf_material->set_shader_parameter("leaf_fade_center", center);
        leaf_material->set_shader_parameter("leaf_fade_opacity", opacity);
    }
    if (solid_material.is_valid()) {
        solid_material->set_shader_parameter("leaf_fade_center", center);
        solid_material->set_shader_parameter("leaf_fade_opacity", opacity);
    }
}
void VoxelWorld::set_wind_time(float time) {
    for (auto material : {solid_material, physical_material, grass_material, water_material, lava_material, leaf_material})
        material->set_shader_parameter("wind_time", time);
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
    // This sample already includes session edits and the loaded generated volume.
    // Passing it prevents model validation from initiating a cold scalar plan.
    auto support=block_at(command.position.x,command.position.y,command.position.z);
    if (!model->apply_surface(command, e,support)) {
        error = String(e.c_str());
        return false;
    }
    ramp_base_shapes.erase(command.position);
    ramp_connections.erase(command.position);
    invalidate(command.position,true,command.appearance.shape!=255);
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
    if(shape==255) {
        if(it==ramp_connections.end())return;
        ramp_connections.erase(it);
        ramp_base_shapes.erase(p);
    }
    else {
        if(it!=ramp_connections.end() && it->second==shape)return;
        if(!ramp_base_shapes.count(p))ramp_base_shapes[p]=base_shape_at(p.x,p.y,p.z);
        ramp_connections[p]=shape;
    }
    invalidate(p,false,false);
}
void VoxelWorld::invalidate(IVec3 position,bool notify,bool wake_liquids) {
    if(wake_liquids)liquid_sim.wake_neighbors(position.x, position.y, position.z);
    if(notify && edit_callback)edit_callback(position);
    if (settings().preset == WorldPreset::Structure) {
        const auto grid = [](int coordinate) { return int(std::floor(coordinate / 32.0)) * 32; };
        const TileKey edited{grid(position.x), grid(position.z), 0};
        structure_dirty.insert(edited);
        ++versions[edited];
        for (const auto &[key, tile] : tiles) {
            if (key == edited)
                continue;
            const int pad = skylight_reach + 1;
            if (position.x >= key.x - pad && position.x < key.x + key.size() + pad &&
                position.z >= key.z - pad && position.z < key.z + key.size() + pad) {
                structure_dirty.insert(key);
                ++versions[key];
            }
        }
        return;
    }
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
        int pad = key.level == 0 ? skylight_reach + 1 : 1 << key.level;
        if (position.x >= key.x - pad && position.x < key.x + key.size() + pad && position.z >= key.z - pad &&
            position.z < key.z + key.size() + pad)
            ++versions[key];
    }
}
void VoxelWorld::probe_liquid_box(Vector3 minimum, Vector3 maximum, bool &in_water, bool &in_lava,
                                  float &submergence, float &surface_y, bool &has_surface, Vector3 &current) const {
    in_water = false;
    in_lava = false;
    submergence = 0.0f;
    surface_y = 0.0f;
    has_surface = false;
    current = Vector3();

    const int x0 = int(std::floor(minimum.x)), x1 = int(std::floor(maximum.x));
    const int y0 = int(std::floor(minimum.y)), y1 = int(std::floor(maximum.y));
    const int z0 = int(std::floor(minimum.z)), z1 = int(std::floor(maximum.z));
    const float body_h = std::max(0.01f, maximum.y - minimum.y);
    const auto &reg = registry();
    constexpr uint8_t kMaxLiquid = 255;

    float min_overlap_y = maximum.y;
    float max_overlap_y = minimum.y;
    bool any_liquid = false;
    bool water_hit = false;
    bool lava_hit = false;
    float highest_surface = -1e9f;

    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            for (int y = y0; y <= y1; ++y) {
                const altaria::BlockId t = liquid_sim.get_type(x, y, z);
                if (!liquid_sim.is_liquid(t))
                    continue;
                const uint8_t amt = liquid_sim.get_amount(x, y, z);
                if (amt == 0)
                    continue;

                any_liquid = true;
                const bool is_lava_cell = (t == reg.lava) || reg.get(t).lava;
                if (is_lava_cell)
                    lava_hit = true;
                else
                    water_hit = true;

                const float fill_h = amt >= kMaxLiquid
                    ? 1.0f
                    : std::clamp(float(amt) / float(kMaxLiquid), 0.15f, 1.0f);
                const float cell_top = float(y) + fill_h;
                const float ov_lo = std::max(minimum.y, float(y));
                const float ov_hi = std::min(maximum.y, cell_top);
                if (ov_hi > ov_lo) {
                    min_overlap_y = std::min(min_overlap_y, ov_lo);
                    max_overlap_y = std::max(max_overlap_y, ov_hi);
                }

                // Walk up contiguous liquid column above this cell (capped to keep cost tiny)
                float col_top = cell_top;
                if (fill_h >= 0.99f) {
                    int cy = y;
                    for (int step = 0; step < 6; ++step) {
                        const altaria::BlockId above_t = liquid_sim.get_type(x, cy + 1, z);
                        if (!liquid_sim.is_liquid(above_t))
                            break;
                        const uint8_t above_amt = liquid_sim.get_amount(x, cy + 1, z);
                        if (above_amt == 0)
                            break;
                        ++cy;
                        const float above_fill = above_amt >= kMaxLiquid
                            ? 1.0f
                            : std::clamp(float(above_amt) / float(kMaxLiquid), 0.15f, 1.0f);
                        col_top = float(cy) + above_fill;
                        if (above_fill < 0.99f)
                            break;
                    }
                }
                highest_surface = std::max(highest_surface, col_top);
            }
        }
    }

    if (!any_liquid)
        return;

    in_water = water_hit;
    in_lava = lava_hit && !water_hit;
    if (max_overlap_y > min_overlap_y)
        submergence = std::clamp((max_overlap_y - min_overlap_y) / body_h, 0.0f, 1.0f);
    if (highest_surface > -1e8f) {
        surface_y = highest_surface;
        has_surface = true;
    }

    const Vector3 center = (minimum + maximum) * 0.5f;
    const Vector3 cave_flow = cave_river_at(center);
    if (cave_flow.length_squared() > 0.0f) {
        current = cave_flow;
    } else {
        auto cur = liquid_sim.current_at(center.x, center.y, center.z);
        current = Vector3(cur[0], cur[1], cur[2]);
    }
}
Vector3 VoxelWorld::liquid_current_box(Vector3 minimum, Vector3 maximum, bool &in_liquid) const {
    bool in_water = false, in_lava = false, has_surface = false;
    float sub = 0.0f, surf_y = 0.0f;
    Vector3 current;
    probe_liquid_box(minimum, maximum, in_water, in_lava, sub, surf_y, has_surface, current);
    in_liquid = in_water || in_lava;
    return current;
}
void VoxelWorld::set_fullbright(bool value) {
    fullbright = value;
    bush_material->set_shader_parameter("fullbright", value);
    physical_material->set_shader_parameter("fullbright", value);
    solid_material->set_shader_parameter("fullbright", value);
    overlay_material->set_shader_parameter("fullbright", value);
    grass_material->set_shader_parameter("fullbright", value);
    water_material->set_shader_parameter("fullbright", value);
    lava_material->set_shader_parameter("fullbright", value);
    leaf_material->set_shader_parameter("fullbright", value);
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
            bytes += int64_t(entry.second.volume->voxel_bytes());
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
    {
        float worst = 0, p99 = 0, average = 0;
        frame_stats(worst, p99, average);
        d["frame_worst_ms"] = worst;   // over the last frame_times window
        d["frame_p99_ms"] = p99;
        d["frame_average_ms"] = average;
        d["frame_spikes"] = int(frame_spikes); // frames slower than frame_spike_ms since start
        d["upload_vertices"] = int(last_upload_vertices);
        d["max_upload_vertices"] = int(max_upload_vertices);
        d["deferred_uploads"] = int(deferred_uploads);
    }
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
    d["cave_plan_ms"] = stages.cave_plan_ms;
    d["cave_field_ms"] = stages.cave_field_ms;
    d["cave_biome_ms"] = stages.cave_biome_ms;
    d["caves_enabled"] = settings().caves.enabled;
    if (generator()->underground()) {
        auto cache=generator()->underground()->stats();
        d["cave_cache_regions"]=int64_t(cache.regions);
        d["cave_cache_estimated_bytes"]=int64_t(cache.bytes);
        d["cave_roof_estimated_bytes"]=int64_t(cache.roof_bytes);
        d["cave_cache_hits"]=int64_t(cache.hits);
        d["cave_cache_misses"]=int64_t(cache.misses);
        d["cave_support_entries"]=int64_t(cache.support_entries);
        d["cave_support_estimated_bytes"]=int64_t(cache.support_bytes);
        d["cave_pinned_cache_regions"]=int64_t(cache.pinned_regions);
    }
    d["surface_edits"] = int(model->painted_count());
    d["view_distance"] = settings().view_distance;
    d["stream_radius"] = stream_radius;
    d["stream_underground"] = underground_stream;
    d["lod_fading"] = lod_fading;
    d["stream_cancelled_jobs"] = cancelled_jobs;
    d["stream_discarded_jobs"] = discarded_jobs;
    d["edits"] = model ? int(model->snapshot()->size()) : 0;
    d["revision"] = model ? int64_t(model->revision) : 0;
    int colliders = 0;
    int64_t collider_triangles=0;
    for (auto &t : tiles) {
        colliders += t.second.collider != nullptr;
        if(t.second.collider)collider_triangles+=t.second.collision_triangles;
    }
    d["terrain_colliders"] = colliders;
    d["collider_triangles"]=collider_triangles;
    {
        Dictionary profile;
        for (const auto *slot : altaria::profile::slots()) {
            Array row;
            row.push_back(slot->calls ? slot->total_ms / double(slot->calls) : 0.0);
            row.push_back(slot->max_ms);
            row.push_back(int64_t(slot->calls));
            row.push_back(slot->total_ms);
            profile[slot->name] = row;
        }
        d["profile"] = profile; // name -> [average ms, worst ms, calls, total ms]
    }
    d["workers"] = worker_count;
    d["first_tile_build_ms"] = first_tile_build_ms;
    d["first_tile_cave_plan_ms"] = first_tile_stages.cave_plan_ms;
    d["first_tile_cave_field_ms"] = first_tile_stages.cave_field_ms;
    d["first_tile_surface_ms"] = first_tile_stages.surface_ms;
    d["first_tile_mesh_ms"] = first_tile_stages.mesh_ms;
    d["first_tile_biomes_ms"] = first_tile_stages.biomes_ms;
    d["first_tile_terrain_ms"] = first_tile_stages.terrain_ms;
    d["configure_ms"] = configure_ms;
    {
        // Per LOD level: [tiles, average ms per tile, biomes, terrain, caves, surface, mesh, vertices per tile].
        Array levels;
        for (const auto &g : generation_stats) {
            Array row;
            const double n = double(std::max<uint64_t>(1, g.tiles));
            row.push_back(int64_t(g.tiles));
            for (double value : {g.total_ms, g.biomes_ms, g.terrain_ms, g.caves_ms, g.surface_ms, g.mesh_ms})
                row.push_back(value / n);
            row.push_back(int64_t(double(g.vertices) / n));
            levels.push_back(row);
        }
        d["generation_stats"] = levels;
    }
    d["fullbright"] = fullbright;
    return d;
}

bool VoxelWorld::get_tile_info(int x, int z, int level, Dictionary &info) const {
    altaria::TileKey key{x, z, level};
    auto it = tiles.find(key);
    if (it == tiles.end())
        return false;
    info["x"] = x;
    info["z"] = z;
    info["level"] = level;
    info["triangles"] = it->second.triangles;
    info["has_mesh"] = (it->second.node != nullptr && it->second.node->get_mesh().is_valid());
    info["has_collider"] = (it->second.collider != nullptr);
    info["has_volume"] = (it->second.volume != nullptr);
    info["grass_quads"] = it->second.grass_quads;
    info["bush_count"] = it->second.bush_count;
    info["version"] = (int64_t)it->second.version;
    return true;
}

Array VoxelWorld::get_loaded_tile_keys() const {
    Array arr;
    for (const auto &kv : tiles) {
        Dictionary d;
        d["x"] = kv.first.x;
        d["z"] = kv.first.z;
        d["level"] = kv.first.level;
        arr.push_back(d);
    }
    return arr;
}

bool VoxelWorld::reload_tile(int x, int z, int level) {
    if (!workers || !model)
        return false;
    altaria::TileKey key{x, z, level};
    auto it = tiles.find(key);
    if (it != tiles.end()) {
        versions[key]++;
        pending[key] = versions[key];
        bool refresh_foliage = key.level == 0 && (it->second.grass_node || it->second.bush_node);
        workers->submit(key, versions[key], model->snapshot_for(key), render_surfaces(key), refresh_foliage,
                        window_for(key), resource_snapshot(key));
        return true;
    }
    return false;
}

Dictionary VoxelWorld::get_chunk_queue_info() const {
    Dictionary d;
    d["pending"] = int(pending.size());
    d["vegetation_pending"] = int(vegetation_pending.size());
    d["wanted"] = int(wanted.size());
    d["loaded_tiles"] = int(tiles.size());
    return d;
}

} // namespace godot
