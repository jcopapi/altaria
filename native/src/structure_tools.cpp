#include "altaria_game.h"
#include "game/world_generation/block_shapes.h"
#include "game/world_generation/world_data.h"
#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

using namespace altaria;

namespace godot {
namespace {
constexpr int64_t max_fill_cells = 4096;
constexpr int64_t max_export_cells = 262144;
constexpr int64_t max_undo_entries = 16;
constexpr float wand_reach = 24.0f;
constexpr float wand_air_distance = 6.0f;

String corner_text(Vector3i p) {
    return String::num_int64(p.x) + " " + String::num_int64(p.y) + " " + String::num_int64(p.z);
}

int64_t volume(Vector3i size) {
    return int64_t(size.x) * size.y * size.z;
}

Array coordinates(Vector3 p) {
    return Array::make(p.x, p.y, p.z);
}

Dictionary describe_piece(const Registry &registry, BlockId id, Vector3 offset, uint8_t shape,
                          const Appearance &appearance, const char *kind) {
    Dictionary piece;
    piece["kind"] = kind;
    piece["block"] = String(registry.get(id).key.c_str());
    piece["position"] = coordinates(offset);
    piece["shape"] = shape_name(shape_type(shape));
    piece["shape_code"] = int(shape);
    piece["rotation"] = altaria::shape_rotation(shape);
    if (appearance.shape != 255) {
        piece["authored_shape"] = shape_name(appearance.shape);
        piece["authored_shape_id"] = appearance.shape;
        piece["authored_rotation"] = appearance.rotation;
    }
    if (appearance.painted) {
        const auto tint = unpack_rgb(appearance.tint);
        Dictionary paint;
        paint["color"] = String("#") + Color(tint.r, tint.g, tint.b).to_html(false);
        paint["preserve"] = appearance.preserve / 1000.;
        paint["brightness"] = appearance.brightness / 1000.;
        piece["paint"] = paint;
    }
    for (auto [field, index] : {std::pair{"overlay", appearance.overlay},
                               std::pair{"decoration", appearance.decoration}}) {
        if (index == 0)
            piece[field] = "";
        else if (index < registry.overlays.size() && !registry.overlays[index].key.empty())
            piece[field] = String(registry.overlays[index].key.c_str());
    }
    return piece;
}

AABB piece_bounds(const BuiltBlock &block) {
    const auto p = block.position();
    const auto [low, high] = shape_bounds(block.shape());
    return AABB(Vector3(p[0] + low[0], p[1] + low[1], p[2] + low[2]),
                Vector3(high[0] - low[0], high[1] - low[1], high[2] - low[2]));
}

Vector3i rotate_local_cell(Vector3i p, Vector3i size, int rot) {
    rot = ((rot % 4) + 4) % 4;
    if (rot == 1)
        return {size.z - 1 - p.z, p.y, p.x};
    if (rot == 2)
        return {size.x - 1 - p.x, p.y, size.z - 1 - p.z};
    if (rot == 3)
        return {p.z, p.y, size.x - 1 - p.x};
    return p;
}

Vector3 rotate_local_pos(Vector3 p, Vector3i size, int rot) {
    rot = ((rot % 4) + 4) % 4;
    if (rot == 1)
        return {float(size.z) - 1.0f - p.z, p.y, p.x};
    if (rot == 2)
        return {float(size.x) - 1.0f - p.x, p.y, float(size.z) - 1.0f - p.z};
    if (rot == 3)
        return {p.z, p.y, float(size.x) - 1.0f - p.x};
    return p;
}

Vector3i rotated_box_size(Vector3i size, int rot) {
    return (rot & 1) ? Vector3i(size.z, size.y, size.x) : size;
}

Appearance rotate_appearance(Appearance a, int rot) {
    rot = ((rot % 4) + 4) % 4;
    if (rot != 0 && a.shape != 255)
        a.rotation = uint8_t((a.rotation + rot) & 3);
    return a;
}

String short_block_name(const Registry &registry, BlockId id) {
    if (id == 0 || id >= registry.blocks.size())
        return "";
    const String key(registry.get(id).key.c_str());
    const int colon = key.find(":");
    return colon >= 0 ? key.substr(colon + 1) : key;
}

bool resolve_block_key(const Registry &registry, const String &material, BlockId &out_block, bool allow_air) {
    const String key = material.contains(":") ? material : String("altaria:") + material;
    const auto found = registry.keys.find(key.utf8().get_data());
    if (found == registry.keys.end())
        return false;
    if (found->second == registry.air) {
        if (!allow_air)
            return false;
        out_block = registry.air;
        return true;
    }
    if (!registry.get(found->second).placeable)
        return false;
    out_block = found->second;
    return true;
}

MeshInstance3D *make_marker_cube(const Color &color, float pad) {
    auto *m = memnew(MeshInstance3D);
    m->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    Ref<BoxMesh> box;
    box.instantiate();
    box->set_size(Vector3(1.0f + pad, 1.0f + pad, 1.0f + pad));
    m->set_mesh(box);
    Ref<StandardMaterial3D> mat;
    mat.instantiate();
    mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
    mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
    mat->set_albedo(color);
    m->set_material_override(mat);
    m->hide();
    return m;
}
}

void AltariaGame::create_structure_outline() {
    if (structure_outline)
        return;
    structure_outline = memnew(MeshInstance3D);
    structure_outline->set_name("StructureSelection");
    structure_outline->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    Ref<ImmediateMesh> lines;
    lines.instantiate();
    structure_outline->set_mesh(lines);
    Ref<StandardMaterial3D> material;
    material.instantiate();
    material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
    material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
    structure_outline->set_material_override(material);

    structure_volume = make_marker_cube(Color(0.14f, 0.90f, 0.82f, 0.13f), -0.004f);
    structure_volume->set_name("StructureVolume");
    structure_outline->add_child(structure_volume);

    structure_corner_markers[0] = make_marker_cube(Color(0.16f, 0.96f, 0.85f, 0.34f), 0.035f);
    structure_corner_markers[0]->set_name("StructurePos1");
    structure_outline->add_child(structure_corner_markers[0]);

    structure_corner_markers[1] = make_marker_cube(Color(1.00f, 0.68f, 0.20f, 0.34f), 0.035f);
    structure_corner_markers[1]->set_name("StructurePos2");
    structure_outline->add_child(structure_corner_markers[1]);

    stage->add_child(structure_outline);
    structure_outline->hide();

    structure_paste_preview = memnew(MeshInstance3D);
    structure_paste_preview->set_name("StructurePastePreview");
    structure_paste_preview->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    Ref<ImmediateMesh> paste_lines;
    paste_lines.instantiate();
    structure_paste_preview->set_mesh(paste_lines);
    Ref<StandardMaterial3D> paste_mat;
    paste_mat.instantiate();
    paste_mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    paste_mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    paste_mat->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
    paste_mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
    structure_paste_preview->set_material_override(paste_mat);
    stage->add_child(structure_paste_preview);
    structure_paste_preview->hide();
}

bool AltariaGame::wand_aim_cell(bool adjacent_for_place, Vector3i &out_cell, BlockId *out_block,
                                bool *out_has_block, Vector3 *out_world_pt) const {
    if (out_has_block)
        *out_has_block = false;
    if (out_block)
        *out_block = world ? world->registry().air : 0;
    if (!world || !player || !player->get_camera())
        return false;
    const auto camera = player->get_camera();
    const Vector3 origin = camera->get_global_position();
    const Vector3 direction = -camera->get_global_basis().get_column(2);
    const float max_d = current_equipped_tool() == "world_wand" ? wand_reach : reach;
    const float air_d = current_equipped_tool() == "world_wand" ? wand_air_distance : 4.0f;

    bool unknown = false;
    const RayHit voxel = raycast_shapes(
        {origin.x, origin.y, origin.z}, {direction.x, direction.y, direction.z}, max_d,
        [this, &unknown](int x, int y, int z) {
            if ((world->settings().caves.enabled || world->settings().adaptive_streaming) &&
                y >= world->settings().min_y && y < world->settings().max_y && !world->voxel_ready(x, y, z))
                unknown = true;
            return unknown ? world->registry().air : world->block_at(x, y, z);
        },
        [this](int x, int y, int z) { return world->shape_at(x, y, z); }, world->registry());
    const BuiltWorld::Hit placed = pick_built(origin, direction, max_d);

    if (placed.id && (unknown || !voxel.hit || placed.distance < voxel.distance)) {
        const Vector3 hit_pt = origin + direction * placed.distance;
        const Vector3 hit_norm(placed.normal[0], placed.normal[1], placed.normal[2]);
        const Vector3 step = adjacent_for_place ? (hit_pt + hit_norm * 0.05f) : (hit_pt + direction * 0.002f);
        out_cell = Vector3i(step.floor());
        if (out_has_block)
            *out_has_block = true;
        if (out_block && built_world) {
            if (const auto *piece = built_world->get(placed.id))
                *out_block = piece->block;
        }
        if (out_world_pt)
            *out_world_pt = Vector3(out_cell) + Vector3(0.5f, 0.5f, 0.5f);
        return true;
    }
    if (!unknown && voxel.hit) {
        out_cell = adjacent_for_place ? Vector3i(voxel.previous.x, voxel.previous.y, voxel.previous.z)
                                      : Vector3i(voxel.cell.x, voxel.cell.y, voxel.cell.z);
        if (out_has_block)
            *out_has_block = true;
        if (out_block)
            *out_block = voxel.block;
        if (out_world_pt)
            *out_world_pt = Vector3(out_cell) + Vector3(0.5f, 0.5f, 0.5f);
        return true;
    }

    const Vector3 air_pt = origin + direction * air_d;
    out_cell = Vector3i(air_pt.floor());
    if (out_world_pt)
        *out_world_pt = Vector3(out_cell) + Vector3(0.5f, 0.5f, 0.5f);
    return true;
}

String AltariaGame::mark_structure_corner(int corner, const std::vector<std::string> &args) {
    Vector3i cell;
    const auto &settings = world->settings();
    const auto valid = [&](int64_t value, int axis) {
        return axis == 1 ? value > settings.min_y && value < settings.max_y
                         : value >= -1000000 && value <= 1000000;
    };
    if (args.empty()) {
        wand_aim_cell(false, cell);
    } else {
        if (args.size() != 3)
            return "Usage: structure pos1|pos2 [x y z]";
        for (int axis = 0; axis < 3; ++axis) {
            const String number(args[axis].c_str());
            if (!number.is_valid_int() || !valid(number.to_int(), axis))
                return "Selection coordinates must be integers within world bounds.";
            cell[axis] = number.to_int();
        }
    }
    if (!valid(cell.x, 0) || !valid(cell.y, 1) || !valid(cell.z, 2))
        return "Selection is outside world bounds.";
    structure_corners[corner] = cell;
    refresh_structure_outline();
    refresh_wand_hud();
    return "Corner " + String::num_int64(corner + 1) + String(": ") + corner_text(cell);
}

void AltariaGame::refresh_structure_outline() {
    if (!structure_outline)
        return;
    Ref<ImmediateMesh> lines = structure_outline->get_mesh();
    lines->clear_surfaces();
    const AABB bounds = [&]() {
        if (structure_corners[0] && structure_corners[1]) {
            const auto a = *structure_corners[0], b = *structure_corners[1];
            const auto low = a.min(b);
            return AABB(Vector3(low), Vector3(a.max(b) - low + Vector3i(1, 1, 1)));
        }
        const auto corner = structure_corners[0] ? structure_corners[0] : structure_corners[1];
        return corner ? AABB(Vector3(*corner), Vector3(1, 1, 1)) : AABB();
    }();
    if (!bounds.has_volume()) {
        structure_outline->hide();
        if (structure_volume)
            structure_volume->hide();
        for (auto *m : structure_corner_markers)
            if (m)
                m->hide();
        return;
    }

    structure_outline->set_position(bounds.position);
    const AABB local(Vector3(), bounds.size);
    structure_outline->set_custom_aabb(local);
    lines->surface_begin(Mesh::PRIMITIVE_LINES);
    lines->surface_set_color(Color(.21f, .93f, .83f));
    for (int i = 0; i < 12; ++i) {
        Vector3 a, b;
        local.get_edge(i, a, b);
        lines->surface_add_vertex(a);
        lines->surface_add_vertex(b);
    }
    lines->surface_end();

    if (structure_volume) {
        if (structure_corners[0] && structure_corners[1]) {
            structure_volume->set_scale(bounds.size);
            structure_volume->set_position(bounds.size * 0.5f);
            structure_volume->show();
        } else {
            structure_volume->hide();
        }
    }
    for (int i = 0; i < 2; ++i) {
        if (!structure_corner_markers[i])
            continue;
        if (structure_corners[i]) {
            structure_corner_markers[i]->set_position(
                Vector3(*structure_corners[i]) - bounds.position + Vector3(0.5f, 0.5f, 0.5f));
            structure_corner_markers[i]->show();
        } else {
            structure_corner_markers[i]->hide();
        }
    }
    structure_outline->show();
}

String AltariaGame::structure_selection_status() const {
    if (!structure_outline)
        return "";
    String status = "\nV: pos1 | B: pos2 | structure";
    if (structure_corners[0] && structure_corners[1]) {
        const auto a = *structure_corners[0], b = *structure_corners[1];
        const auto size = a.max(b) - a.min(b) + Vector3i(1, 1, 1);
        status += " | " + String::num_int64(size.x) + String::utf8(" × ") + String::num_int64(size.y) +
                  String::utf8(" × ") + String::num_int64(size.z);
    }
    return status;
}

String AltariaGame::structure_command(const std::vector<std::string> &args) {
    if (!world || !built_world)
        return "Open a world first.";
    if (!structure_outline)
        create_structure_outline();
    const String usage =
        "Usage: structure pos1|pos2 [x y z] | clear | fill [block|air] | replace <from|*> <to|air> | "
        "walls [block] | hollow [block] | copy | paste [rot] | stack [count] | undo | export <name>";
    if (args.size() < 2)
        return usage;
    const String action = String(args[1].c_str()).to_lower();
    if (action == "pos1" || action == "pos2")
        return mark_structure_corner(action == "pos1" ? 0 : 1, {args.begin() + 2, args.end()});
    if (action == "clear" && args.size() == 2) {
        for (auto &corner : structure_corners)
            corner.reset();
        refresh_structure_outline();
        refresh_wand_hud();
        return "Selection cleared.";
    }
    if (action == "undo" && args.size() == 2)
        return undo_structure();
    if (action == "paste" && args.size() <= 3) {
        int rot = wand_paste_rot;
        if (args.size() == 3) {
            const String rstr(args[2].c_str());
            if (!rstr.is_valid_int())
                return "Rotation must be 0, 1, 2, or 3 (or 90/180/270).";
            int rv = rstr.to_int();
            rot = (rv == 90 || rv == 180 || rv == 270) ? (rv / 90) : (rv & 3);
        }
        Vector3i anchor;
        wand_aim_cell(true, anchor);
        return paste_structure(anchor, rot);
    }

    if (!structure_corners[0] || !structure_corners[1])
        return "Mark both corners with V and B or 'structure pos1' and 'structure pos2'.";
    const auto a = *structure_corners[0], b = *structure_corners[1];
    const auto low = a.min(b), size = a.max(b) - low + Vector3i(1, 1, 1);

    if (action == "fill" && args.size() <= 3)
        return fill_structure(args.size() == 3 ? String(args[2].c_str()) : String(), low, size);
    if (action == "replace" && args.size() >= 3 && args.size() <= 4) {
        const String from_mat = args.size() == 4 ? String(args[2].c_str()) : String("*");
        const String to_mat = args.size() == 4 ? String(args[3].c_str()) : String(args[2].c_str());
        return replace_structure(from_mat, to_mat, low, size);
    }
    if (action == "walls" && args.size() <= 3)
        return walls_structure(args.size() == 3 ? String(args[2].c_str()) : String(), low, size);
    if (action == "hollow" && args.size() <= 3)
        return hollow_structure(args.size() == 3 ? String(args[2].c_str()) : String(), low, size);
    if (action == "copy" && args.size() == 2)
        return copy_structure(low, size);
    if (action == "stack" && args.size() <= 3) {
        int count = 1;
        if (args.size() == 3) {
            const String cstr(args[2].c_str());
            if (!cstr.is_valid_int() || cstr.to_int() < 1 || cstr.to_int() > 16)
                return "Stack count must be between 1 and 16.";
            count = int(cstr.to_int());
        }
        return stack_structure(count);
    }
    if (action == "export" && args.size() == 3)
        return export_structure(String(args[2].c_str()), low, size);
    return usage;
}

String AltariaGame::export_structure(const String &name, Vector3i low, Vector3i size) const {
    const String stem = name.ends_with(".json") ? name.trim_suffix(".json") : name;
    if (stem.is_empty() || stem.length() > 64)
        return "Use a name of 1–64 letters, digits, underscores or hyphens.";
    for (int i = 0; i < stem.length(); ++i) {
        const auto c = stem[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return "Use letters, digits, underscores or hyphens in the export name.";
    }
    if (volume(size) > max_export_cells)
        return "Selection exceeds the 262144-cell export limit.";
    Array pieces;
    const auto &registry = world->registry();
    const auto end = low + size;
    for (int y = low.y; y < end.y; ++y)
        for (int z = low.z; z < end.z; ++z)
            for (int x = low.x; x < end.x; ++x) {
                const auto id = world->block_at(x, y, z);
                if (id != registry.air)
                    pieces.push_back(describe_piece(registry, id, Vector3(x - low.x, y - low.y, z - low.z),
                                                    world->shape_at(x, y, z), world->appearance_at({x, y, z}),
                                                    "voxel"));
            }

    const AABB region{Vector3(low), Vector3(size)};
    const auto &construction = built_world->construction();
    auto candidates = construction.nearby({float(low.x), float(low.y), float(low.z)},
                                          {float(end.x), float(end.y), float(end.z)});
    std::sort(candidates.begin(), candidates.end());
    for (auto id : candidates) {
        const auto &piece = construction.blocks.at(id);
        if (!region.intersects(piece_bounds(piece)))
            continue;
        const auto p = piece.position();
        pieces.push_back(describe_piece(registry, piece.block, Vector3(p[0], p[1], p[2]) - Vector3(low),
                                        piece.shape(), piece.appearance, "construction"));
    }
    if (pieces.is_empty())
        return "The selection contains no blocks.";

    Dictionary data;
    data["format"] = "altaria.structure";
    data["version"] = 1;
    data["size"] = Array::make(size.x, size.y, size.z);
    data["blocks"] = pieces;
    const String folder = "user://structures";
    if (DirAccess::make_dir_recursive_absolute(folder) != OK)
        return "Could not create the structures folder.";
    const String path = folder.path_join(stem + String(".json"));
    String error;
    if (!write_json_atomic(path, data, error))
        return "Could not export structure: " + error;
    return "Exported " + String::num_int64(pieces.size()) + String(" blocks to ") +
           ProjectSettings::get_singleton()->globalize_path(path);
}

String AltariaGame::fill_structure(const String &material, Vector3i low, Vector3i size) {
    if (volume(size) > max_fill_cells)
        return "Selection exceeds the 4096-cell fill limit.";
    const auto &registry = world->registry();
    BlockId block = registry.air;
    if (material.is_empty()) {
        if (!inventory.has_active_block())
            return "Select a block first.";
        block = inventory.active_slot().block;
    } else if (!resolve_block_key(registry, material, block, true)) {
        return "Unknown or unavailable fill block.";
    }

    const AABB region{Vector3(low), Vector3(size)};
    if (registry.get(block).solid && region.intersects(player->body_bounds()))
        return "Move outside the selection before filling it with solid blocks.";
    const auto end = low + size;
    const auto &construction = built_world->construction();
    std::vector<uint64_t> remove;
    std::vector<StructureBuiltRecord> removed_records;
    for (auto id : construction.nearby({float(low.x), float(low.y), float(low.z)},
                                       {float(end.x), float(end.y), float(end.z)})) {
        const auto &piece = construction.blocks.at(id);
        const auto bounds = piece_bounds(piece);
        if (!region.intersects(bounds))
            continue;
        if (!region.encloses(bounds))
            return "A construction piece crosses the selection boundary. Enlarge the selection before filling.";
        remove.push_back(id);
        const auto p = piece.position();
        removed_records.push_back({{p[0], p[1], p[2]}, piece.block, piece.shape(), piece.appearance});
    }

    record_structure_undo(low, size, removed_records);
    String error;
    for (auto id : remove)
        if (!built_world->remove(id, error))
            return error;
    for (int y = low.y; y < end.y; ++y)
        for (int z = low.z; z < end.z; ++z)
            for (int x = low.x; x < end.x; ++x) {
                if (world->block_at(x, y, z) != block &&
                    !world->edit({1, ++sequence, {x, y, z}, block}, error))
                    return error;
                if (block != registry.air && !(world->appearance_at({x, y, z}) == Appearance()) &&
                    !world->edit_surface({1, ++sequence, {x, y, z}, Appearance()}, error))
                    return error;
            }
    return "Filled " + String::num_int64(volume(size)) + String(" cells with ") +
           String(registry.get(block).key.c_str()) + String(".");
}

void AltariaGame::record_structure_undo(Vector3i low, Vector3i size,
                                        const std::vector<StructureBuiltRecord> &removed_built) {
    if (!world || volume(size) <= 0 || volume(size) > max_fill_cells * 4)
        return;
    StructureUndoEntry entry;
    entry.low = low;
    entry.size = size;
    const size_t total = size_t(volume(size));
    entry.blocks.reserve(total);
    entry.appearances.reserve(total);
    const auto end = low + size;
    for (int y = low.y; y < end.y; ++y)
        for (int z = low.z; z < end.z; ++z)
            for (int x = low.x; x < end.x; ++x) {
                entry.blocks.push_back(world->block_at(x, y, z));
                entry.appearances.push_back(world->appearance_at({x, y, z}));
            }
    entry.removed_built = removed_built;
    structure_undo_stack.push_back(std::move(entry));
    if (int64_t(structure_undo_stack.size()) > max_undo_entries)
        structure_undo_stack.erase(structure_undo_stack.begin());
}

String AltariaGame::undo_structure() {
    if (!world || !built_world)
        return "Open a world first.";
    if (structure_undo_stack.empty())
        return "Nothing to undo.";
    const StructureUndoEntry entry = std::move(structure_undo_stack.back());
    structure_undo_stack.pop_back();
    const auto &registry = world->registry();
    const AABB player_box = player ? player->body_bounds() : AABB();
    const Vector3i end = entry.low + entry.size;
    size_t idx = 0;
    for (int y = entry.low.y; y < end.y; ++y)
        for (int z = entry.low.z; z < end.z; ++z)
            for (int x = entry.low.x; x < end.x; ++x, ++idx) {
                if (player && registry.get(entry.blocks[idx]).solid &&
                    player_box.intersects(AABB(Vector3(x, y, z), Vector3(1, 1, 1)))) {
                    structure_undo_stack.push_back(entry);
                    return "Move outside the undo region before restoring solid blocks.";
                }
            }

    const AABB region{Vector3(entry.low), Vector3(entry.size)};
    const auto &construction = built_world->construction();
    std::vector<uint64_t> clear_ids;
    for (auto id : construction.nearby({float(entry.low.x), float(entry.low.y), float(entry.low.z)},
                                       {float(end.x), float(end.y), float(end.z)})) {
        if (region.encloses(piece_bounds(construction.blocks.at(id))))
            clear_ids.push_back(id);
    }
    String error;
    for (auto id : clear_ids)
        built_world->remove(id, error);

    idx = 0;
    int restored = 0;
    for (int y = entry.low.y; y < end.y; ++y)
        for (int z = entry.low.z; z < end.z; ++z)
            for (int x = entry.low.x; x < end.x; ++x, ++idx) {
                const BlockId target_block = entry.blocks[idx];
                const Appearance target_app = entry.appearances[idx];
                if (world->block_at(x, y, z) != target_block) {
                    if (!world->edit({1, ++sequence, {x, y, z}, target_block}, error))
                        return error;
                    ++restored;
                }
                if (target_block != registry.air && !(world->appearance_at({x, y, z}) == target_app)) {
                    if (!world->edit_surface({1, ++sequence, {x, y, z}, target_app}, error))
                        return error;
                }
            }
    for (const auto &piece : entry.removed_built) {
        const auto id = built_world->place(piece.pos, piece.block, shape_type(piece.shape),
                                           altaria::shape_rotation(piece.shape), error);
        if (id && !(piece.appearance == Appearance()))
            built_world->appearance(id, piece.appearance, error);
    }
    refresh_wand_hud();
    return "Undid last edit (" + String::num_int64(restored) + String(" changed).");
}

BlockId AltariaGame::resolve_wand_material() const {
    if (!world)
        return 0;
    const auto &registry = world->registry();
    if (wand_material != 0 && registry.get(wand_material).placeable)
        return wand_material;
    for (int i = 0; i < HOTBAR_SLOT_COUNT; ++i) {
        const auto &slot = inventory.slot(i);
        if (slot.has_block() && registry.get(slot.block).placeable)
            return slot.block;
    }
    return registry.stone;
}

String AltariaGame::replace_structure(const String &from_mat, const String &to_mat, Vector3i low, Vector3i size) {
    if (volume(size) > max_fill_cells)
        return "Selection exceeds the 4096-cell limit.";
    const auto &registry = world->registry();
    bool match_any_non_air = (from_mat == "*" || from_mat.to_lower() == "any" || from_mat.to_lower() == "solid");
    BlockId from_block = registry.air;
    if (!match_any_non_air && !resolve_block_key(registry, from_mat, from_block, true))
        return "Unknown source block: " + from_mat;
    BlockId to_block = registry.air;
    if (!resolve_block_key(registry, to_mat, to_block, true))
        return "Unknown replacement block: " + to_mat;

    const auto end = low + size;
    const AABB player_box = player ? player->body_bounds() : AABB();
    std::vector<Vector3i> targets;
    for (int y = low.y; y < end.y; ++y)
        for (int z = low.z; z < end.z; ++z)
            for (int x = low.x; x < end.x; ++x) {
                const BlockId cur = world->block_at(x, y, z);
                const bool matches = match_any_non_air ? (cur != registry.air) : (cur == from_block);
                if (!matches || cur == to_block)
                    continue;
                if (player && registry.get(to_block).solid &&
                    player_box.intersects(AABB(Vector3(x, y, z), Vector3(1, 1, 1))))
                    return "Move outside target cells before replacing with solid blocks.";
                targets.push_back({x, y, z});
            }
    if (targets.empty())
        return "No matching blocks found to replace.";

    record_structure_undo(low, size);
    String error;
    for (const auto &p : targets) {
        if (!world->edit({1, ++sequence, {p.x, p.y, p.z}, to_block}, error))
            return error;
        if (to_block != registry.air && !(world->appearance_at({p.x, p.y, p.z}) == Appearance()) &&
            !world->edit_surface({1, ++sequence, {p.x, p.y, p.z}, Appearance()}, error))
            return error;
    }
    return "Replaced " + String::num_int64(targets.size()) + String(" blocks with ") +
           String(registry.get(to_block).key.c_str()) + String(".");
}

String AltariaGame::walls_structure(const String &material, Vector3i low, Vector3i size) {
    if (volume(size) > max_fill_cells)
        return "Selection exceeds the 4096-cell limit.";
    const auto &registry = world->registry();
    BlockId block = resolve_wand_material();
    if (!material.is_empty() && !resolve_block_key(registry, material, block, false))
        return "Unknown or non-placeable block for walls.";

    const auto end = low + size;
    const AABB player_box = player ? player->body_bounds() : AABB();
    std::vector<Vector3i> cells;
    for (int y = low.y; y < end.y; ++y)
        for (int z = low.z; z < end.z; ++z)
            for (int x = low.x; x < end.x; ++x) {
                if (x == low.x || x == end.x - 1 || z == low.z || z == end.z - 1) {
                    if (player && registry.get(block).solid &&
                        player_box.intersects(AABB(Vector3(x, y, z), Vector3(1, 1, 1))))
                        return "Move clear of the perimeter before creating walls.";
                    cells.push_back({x, y, z});
                }
            }
    record_structure_undo(low, size);
    String error;
    for (const auto &p : cells) {
        if (world->block_at(p.x, p.y, p.z) != block &&
            !world->edit({1, ++sequence, {p.x, p.y, p.z}, block}, error))
            return error;
        if (!(world->appearance_at({p.x, p.y, p.z}) == Appearance()) &&
            !world->edit_surface({1, ++sequence, {p.x, p.y, p.z}, Appearance()}, error))
            return error;
    }
    return "Built " + String::num_int64(cells.size()) + String(" wall blocks (") +
           short_block_name(registry, block) + String(").");
}

String AltariaGame::hollow_structure(const String &material, Vector3i low, Vector3i size) {
    if (volume(size) > max_fill_cells)
        return "Selection exceeds the 4096-cell limit.";
    const auto &registry = world->registry();
    BlockId shell_block = resolve_wand_material();
    if (!material.is_empty() && !resolve_block_key(registry, material, shell_block, false))
        return "Unknown or non-placeable shell block.";

    const auto end = low + size;
    const AABB region{Vector3(low), Vector3(size)};
    const AABB player_box = player ? player->body_bounds() : AABB();
    for (int y = low.y; y < end.y; ++y)
        for (int z = low.z; z < end.z; ++z)
            for (int x = low.x; x < end.x; ++x) {
                const bool on_shell = (x == low.x || x == end.x - 1 || y == low.y || y == end.y - 1 ||
                                       z == low.z || z == end.z - 1);
                if (on_shell && player && registry.get(shell_block).solid &&
                    player_box.intersects(AABB(Vector3(x, y, z), Vector3(1, 1, 1))))
                    return "Move clear of the outer shell before hollowing.";
            }

    const auto &construction = built_world->construction();
    std::vector<uint64_t> remove;
    std::vector<StructureBuiltRecord> removed_records;
    for (auto id : construction.nearby({float(low.x), float(low.y), float(low.z)},
                                       {float(end.x), float(end.y), float(end.z)})) {
        const auto &piece = construction.blocks.at(id);
        const auto bounds = piece_bounds(piece);
        if (!region.intersects(bounds))
            continue;
        if (!region.encloses(bounds))
            return "A construction piece crosses the selection boundary.";
        remove.push_back(id);
        const auto p = piece.position();
        removed_records.push_back({{p[0], p[1], p[2]}, piece.block, piece.shape(), piece.appearance});
    }

    record_structure_undo(low, size, removed_records);
    String error;
    for (auto id : remove)
        built_world->remove(id, error);
    int shell_count = 0;
    for (int y = low.y; y < end.y; ++y)
        for (int z = low.z; z < end.z; ++z)
            for (int x = low.x; x < end.x; ++x) {
                const bool on_shell = (x == low.x || x == end.x - 1 || y == low.y || y == end.y - 1 ||
                                       z == low.z || z == end.z - 1);
                const BlockId target = on_shell ? shell_block : registry.air;
                if (on_shell)
                    ++shell_count;
                if (world->block_at(x, y, z) != target &&
                    !world->edit({1, ++sequence, {x, y, z}, target}, error))
                    return error;
                if (target != registry.air && !(world->appearance_at({x, y, z}) == Appearance()) &&
                    !world->edit_surface({1, ++sequence, {x, y, z}, Appearance()}, error))
                    return error;
            }
    return "Hollowed shell (" + String::num_int64(shell_count) + String(" shell blocks, ") +
           short_block_name(registry, shell_block) + String(").");
}

String AltariaGame::copy_structure(Vector3i low, Vector3i size) {
    if (volume(size) > max_fill_cells * 4)
        return "Selection exceeds the 16384-cell copy limit.";
    const auto &registry = world->registry();
    StructureClipboard clip;
    clip.size = size;
    const auto end = low + size;
    for (int y = low.y; y < end.y; ++y)
        for (int z = low.z; z < end.z; ++z)
            for (int x = low.x; x < end.x; ++x) {
                const BlockId id = world->block_at(x, y, z);
                if (id != registry.air)
                    clip.voxels.push_back({{x - low.x, y - low.y, z - low.z}, id, world->appearance_at({x, y, z})});
            }
    const AABB region{Vector3(low), Vector3(size)};
    const auto &construction = built_world->construction();
    for (auto id : construction.nearby({float(low.x), float(low.y), float(low.z)},
                                       {float(end.x), float(end.y), float(end.z)})) {
        const auto &piece = construction.blocks.at(id);
        if (!region.encloses(piece_bounds(piece)))
            continue;
        const auto p = piece.position();
        clip.built.push_back({Vector3(p[0] - low.x, p[1] - low.y, p[2] - low.z),
                              piece.block, piece.shape(), piece.appearance});
    }
    if (clip.voxels.empty() && clip.built.empty())
        return "Selection contains no blocks to copy.";
    structure_clipboard = std::move(clip);
    refresh_wand_hud();
    return "Copied " + String::num_int64(structure_clipboard.voxels.size() + structure_clipboard.built.size()) +
           String(" blocks (") + String::num_int64(size.x) + String::utf8("×") + String::num_int64(size.y) +
           String::utf8("×") + String::num_int64(size.z) + String(").");
}

String AltariaGame::paste_structure(Vector3i anchor, int rot) {
    if (!structure_clipboard.valid())
        return "Clipboard is empty. Copy a selection first.";
    const Vector3i out_size = rotated_box_size(structure_clipboard.size, rot);
    if (volume(out_size) > max_fill_cells * 4)
        return "Clipboard exceeds paste limit.";
    const auto &settings = world->settings();
    if (anchor.y <= settings.min_y || anchor.y + out_size.y >= settings.max_y)
        return "Paste region exceeds vertical world bounds.";

    const auto &registry = world->registry();
    const AABB player_box = player ? player->body_bounds() : AABB();
    for (const auto &v : structure_clipboard.voxels) {
        if (!registry.get(v.block).solid)
            continue;
        const Vector3i wp = anchor + rotate_local_cell(v.pos, structure_clipboard.size, rot);
        if (player_box.intersects(AABB(Vector3(wp), Vector3(1, 1, 1))))
            return "Move clear of the paste footprint before pasting solid blocks.";
    }

    const Vector3i end = anchor + out_size;
    const AABB region{Vector3(anchor), Vector3(out_size)};
    const auto &construction = built_world->construction();
    std::vector<uint64_t> clear_ids;
    std::vector<StructureBuiltRecord> removed_records;
    for (auto id : construction.nearby({float(anchor.x), float(anchor.y), float(anchor.z)},
                                       {float(end.x), float(end.y), float(end.z)})) {
        const auto &piece = construction.blocks.at(id);
        if (!region.encloses(piece_bounds(piece)))
            continue;
        clear_ids.push_back(id);
        const auto p = piece.position();
        removed_records.push_back({{p[0], p[1], p[2]}, piece.block, piece.shape(), piece.appearance});
    }

    record_structure_undo(anchor, out_size, removed_records);
    String error;
    for (auto id : clear_ids)
        built_world->remove(id, error);

    for (const auto &v : structure_clipboard.voxels) {
        const Vector3i wp = anchor + rotate_local_cell(v.pos, structure_clipboard.size, rot);
        if (world->block_at(wp.x, wp.y, wp.z) != v.block &&
            !world->edit({1, ++sequence, {wp.x, wp.y, wp.z}, v.block}, error))
            return error;
        const Appearance rot_app = rotate_appearance(v.appearance, rot);
        if (!(world->appearance_at({wp.x, wp.y, wp.z}) == rot_app) &&
            !world->edit_surface({1, ++sequence, {wp.x, wp.y, wp.z}, rot_app}, error))
            return error;
    }
    for (const auto &b : structure_clipboard.built) {
        const Vector3 wp = Vector3(anchor) + rotate_local_pos(b.pos, structure_clipboard.size, rot);
        const int r = (altaria::shape_rotation(b.shape) + rot) & 3;
        const auto id = built_world->place(wp, b.block, shape_type(b.shape), r, error);
        if (id) {
            const Appearance rot_app = rotate_appearance(b.appearance, rot);
            if (!(rot_app == Appearance()))
                built_world->appearance(id, rot_app, error);
        }
    }
    return "Pasted " + String::num_int64(structure_clipboard.voxels.size() + structure_clipboard.built.size()) +
           String(" blocks at ") + corner_text(anchor) + String(".");
}

String AltariaGame::stack_structure(int count) {
    if (count < 1 || count > 16)
        return "Stack count must be between 1 and 16.";
    if (!structure_corners[0] || !structure_corners[1])
        return "Mark both corners before stacking.";
    const auto a = *structure_corners[0], b = *structure_corners[1];
    const auto low = a.min(b), size = a.max(b) - low + Vector3i(1, 1, 1);
    if (volume(size) * count > max_fill_cells * 2)
        return "Stacked volume exceeds limit.";

    Vector3i dir(0, 0, -1);
    if (player && player->get_camera()) {
        const Vector3 fwd = -player->get_camera()->get_global_basis().get_column(2);
        const float ax = std::abs(fwd.x), ay = std::abs(fwd.y), az = std::abs(fwd.z);
        if (ax >= ay && ax >= az)
            dir = Vector3i(fwd.x >= 0 ? 1 : -1, 0, 0);
        else if (ay > ax && ay >= az)
            dir = Vector3i(0, fwd.y >= 0 ? 1 : -1, 0);
        else
            dir = Vector3i(0, 0, fwd.z >= 0 ? 1 : -1);
    }

    const Vector3i step(dir.x * size.x, dir.y * size.y, dir.z * size.z);
    const Vector3i first_low = low + step;
    const Vector3i last_low = low + step * count;
    const Vector3i union_low = first_low.min(last_low);
    const Vector3i union_high = first_low.max(last_low) + size;
    const Vector3i union_size = union_high - union_low;

    const auto &settings = world->settings();
    if (union_low.y <= settings.min_y || union_high.y >= settings.max_y)
        return "Stack target exceeds vertical world bounds.";

    const auto &registry = world->registry();
    std::vector<StructureVoxelRecord> src_voxels;
    const auto end = low + size;
    for (int y = low.y; y < end.y; ++y)
        for (int z = low.z; z < end.z; ++z)
            for (int x = low.x; x < end.x; ++x) {
                const BlockId id = world->block_at(x, y, z);
                if (id != registry.air)
                    src_voxels.push_back({{x - low.x, y - low.y, z - low.z}, id, world->appearance_at({x, y, z})});
            }

    std::vector<StructureBuiltRecord> src_built;
    const AABB src_region{Vector3(low), Vector3(size)};
    const auto &construction = built_world->construction();
    for (auto id : construction.nearby({float(low.x), float(low.y), float(low.z)},
                                       {float(end.x), float(end.y), float(end.z)})) {
        const auto &piece = construction.blocks.at(id);
        if (!src_region.encloses(piece_bounds(piece)))
            continue;
        const auto p = piece.position();
        src_built.push_back({Vector3(p[0] - low.x, p[1] - low.y, p[2] - low.z),
                             piece.block, piece.shape(), piece.appearance});
    }
    if (src_voxels.empty() && src_built.empty())
        return "Selection contains no blocks to stack.";

    const AABB player_box = player ? player->body_bounds() : AABB();
    for (int k = 1; k <= count; ++k) {
        const Vector3i dst = low + step * k;
        for (const auto &v : src_voxels) {
            if (!registry.get(v.block).solid)
                continue;
            const Vector3i wp = dst + v.pos;
            if (player_box.intersects(AABB(Vector3(wp), Vector3(1, 1, 1))))
                return "Move clear of the stacked path.";
        }
    }

    record_structure_undo(union_low, union_size);
    String error;
    for (int k = 1; k <= count; ++k) {
        const Vector3i dst = low + step * k;
        for (const auto &v : src_voxels) {
            const Vector3i wp = dst + v.pos;
            if (world->block_at(wp.x, wp.y, wp.z) != v.block &&
                !world->edit({1, ++sequence, {wp.x, wp.y, wp.z}, v.block}, error))
                return error;
            if (!(world->appearance_at({wp.x, wp.y, wp.z}) == v.appearance) &&
                !world->edit_surface({1, ++sequence, {wp.x, wp.y, wp.z}, v.appearance}, error))
                return error;
        }
        for (const auto &b : src_built) {
            const auto id = built_world->place(Vector3(dst) + b.pos, b.block, shape_type(b.shape),
                                               altaria::shape_rotation(b.shape), error);
            if (id && !(b.appearance == Appearance()))
                built_world->appearance(id, b.appearance, error);
        }
    }
    structure_corners[0] = last_low;
    structure_corners[1] = last_low + size - Vector3i(1, 1, 1);
    refresh_structure_outline();
    refresh_wand_hud();
    return "Stacked selection " + String::num_int64(count) + String::utf8("× along ") + corner_text(dir) + String(".");
}

void AltariaGame::handle_wand_action(bool secondary) {
    if (!world || !built_world || !player)
        return;
    if (!structure_outline)
        create_structure_outline();

    const auto &registry = world->registry();
    const auto feedback = [&](const String &msg, bool warn = false) {
        (void)warn;
        if (arc_hotbar)
            arc_hotbar->show_wand_feedback(msg);
        print_console(msg);
        refresh_wand_hud();
    };
    const auto cast_anim = [&](Vector3 aim_world) {
        if (first_person_hands && player->get_camera()) {
            auto *cam = player->get_camera();
            const Vector2 screen = cam->is_position_behind(aim_world)
                                       ? get_viewport()->get_visible_rect().size * 0.5f
                                       : cam->unproject_position(aim_world);
            first_person_hands->call("play_world_action", StringName("place"), screen,
                                     cam->get_global_position().distance_to(aim_world));
        }
    };

    Vector3i cell;
    BlockId hit_block = registry.air;
    bool has_hit_block = false;
    Vector3 aim_world;
    wand_aim_cell(wand_mode == int(WandMode::CopyPaste) && secondary, cell, &hit_block, &has_hit_block, &aim_world);

    const bool has_region = structure_corners[0].has_value() && structure_corners[1].has_value();
    Vector3i low, size;
    if (has_region) {
        const auto a = *structure_corners[0], b = *structure_corners[1];
        low = a.min(b);
        size = a.max(b) - low + Vector3i(1, 1, 1);
    }

    switch (WandMode(wand_mode)) {
    case WandMode::Select: {
        const int corner = secondary ? 1 : 0;
        const std::vector<std::string> coords = {
            std::to_string(cell.x), std::to_string(cell.y), std::to_string(cell.z)};
        const String msg = mark_structure_corner(corner, coords);
        cast_anim(aim_world);
        feedback(msg);
        return;
    }
    case WandMode::Fill:
    case WandMode::Walls:
    case WandMode::Hollow: {
        if (secondary) {
            if (has_hit_block && hit_block != registry.air && registry.get(hit_block).placeable) {
                wand_material = hit_block;
                wand_material_is_air = false;
                cast_anim(aim_world);
                feedback("Brush: " + short_block_name(registry, wand_material));
            } else {
                feedback(language == "pt_BR" ? "Mire em um bloco para copiar o material." : "Aim at a block to pick brush.", true);
            }
            return;
        }
        if (!has_region) {
            feedback(language == "pt_BR" ? "Marque Pos1 e Pos2 primeiro (modo Selecionar ou V/B)."
                                         : "Mark Pos1 & Pos2 first (Select mode or V/B).", true);
            return;
        }
        const BlockId mat = resolve_wand_material();
        const String mat_key = wand_material_is_air ? String("altaria:air")
                                                    : String(registry.get(mat).key.c_str());
        cast_anim(aim_world);
        if (wand_mode == int(WandMode::Fill))
            feedback(fill_structure(mat_key, low, size));
        else if (wand_mode == int(WandMode::Walls))
            feedback(walls_structure(mat_key, low, size));
        else
            feedback(hollow_structure(mat_key, low, size));
        return;
    }
    case WandMode::Replace: {
        if (secondary) {
            if (has_hit_block && hit_block != registry.air) {
                wand_replace_target = hit_block;
                wand_replace_target_is_air = false;
                cast_anim(aim_world);
                feedback("Target: " + short_block_name(registry, wand_replace_target));
            } else {
                wand_replace_target = 0;
                wand_replace_target_is_air = false;
                feedback(String::utf8(language == "pt_BR" ? "Alvo: Qualquer bloco sólido (*)" : "Target: Any solid block (*)"));
            }
            return;
        }
        if (!has_region) {
            feedback(language == "pt_BR" ? "Marque Pos1 e Pos2 primeiro." : "Mark Pos1 & Pos2 first.", true);
            return;
        }
        const String from_key = wand_replace_target_is_air
                                    ? String("altaria:air")
                                    : ((wand_replace_target != 0) ? String(registry.get(wand_replace_target).key.c_str())
                                                                  : String("*"));
        const BlockId to_mat = resolve_wand_material();
        const String to_key = wand_material_is_air ? String("altaria:air")
                                                   : String(registry.get(to_mat).key.c_str());
        cast_anim(aim_world);
        feedback(replace_structure(from_key, to_key, low, size));
        return;
    }
    case WandMode::CopyPaste: {
        if (!secondary) {
            if (!has_region) {
                feedback(language == "pt_BR" ? "Marque Pos1 e Pos2 para copiar." : "Mark Pos1 & Pos2 to copy.", true);
                return;
            }
            cast_anim(aim_world);
            feedback(copy_structure(low, size));
        } else {
            cast_anim(aim_world);
            feedback(paste_structure(cell, wand_paste_rot));
        }
        return;
    }
    case WandMode::Stack: {
        if (secondary) {
            for (auto &corner : structure_corners)
                corner.reset();
            refresh_structure_outline();
            feedback(String::utf8(language == "pt_BR" ? "Seleção limpa." : "Selection cleared."));
            return;
        }
        if (!has_region) {
            feedback(language == "pt_BR" ? "Marque Pos1 e Pos2 para empilhar." : "Mark Pos1 & Pos2 to stack.", true);
            return;
        }
        cast_anim(aim_world);
        feedback(stack_structure(1));
        return;
    }
    case WandMode::ClearUndo: {
        if (secondary) {
            cast_anim(aim_world);
            feedback(undo_structure());
            return;
        }
        if (!has_region) {
            feedback(language == "pt_BR" ? "Marque Pos1 e Pos2 para apagar." : "Mark Pos1 & Pos2 to clear.", true);
            return;
        }
        cast_anim(aim_world);
        feedback(fill_structure("altaria:air", low, size));
        return;
    }
    }
}

void AltariaGame::refresh_wand_hud() {
    if (!arc_hotbar || !world)
        return;
    const BlockId brush = resolve_wand_material();
    const bool has_sel = structure_corners[0].has_value() && structure_corners[1].has_value();
    Vector3i sel_size;
    if (has_sel) {
        const auto a = *structure_corners[0], b = *structure_corners[1];
        sel_size = a.max(b) - a.min(b) + Vector3i(1, 1, 1);
    }
    const int clip_blocks = int(structure_clipboard.voxels.size() + structure_clipboard.built.size());
    arc_hotbar->set_wand_state(wand_mode, brush, wand_replace_target, has_sel, sel_size,
                               structure_clipboard.valid(), clip_blocks, wand_paste_rot,
                               wand_material_is_air, wand_replace_target_is_air);
}
}
