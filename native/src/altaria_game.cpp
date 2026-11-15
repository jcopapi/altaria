#include "altaria_game.h"
#include <map>
#include "../../game/debug/frame_profile.h"
#include "crab_console_bridge.h"
#include "game/debug/command_registry.h"
#include "game/world_generation/block_shapes.h"
#include "game/world_generation/world_data.h"
#include <godot_cpp/classes/immediate_mesh.hpp>

namespace godot {
AltariaGame *AltariaGame::active_game = nullptr;

CrabConsole *AltariaGame::get_console() {
    if (cached_console && cached_console->is_inside_tree()) {
        return cached_console;
    }
    if (CrabConsole::get_singleton()) {
        cached_console = CrabConsole::get_singleton();
        return cached_console;
    }
    if (is_inside_tree()) {
        Node *n = get_node_or_null("/root/CrabbyConsole");
        cached_console = Object::cast_to<CrabConsole>(n);
        return cached_console;
    }
    return nullptr;
}

void AltariaGame::print_console(const String &text) {
    CrabConsole *c = get_console();
    if (c) {
        c->print(text);
    } else {
        UtilityFunctions::print(text);
    }
}

bool AltariaGame::is_console_open() {
    CrabConsole *c = get_console();
    return c ? c->is_enabled() : false;
}
} // namespace godot
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/cpu_particles3d.hpp>
#include <godot_cpp/classes/curve.hpp>
#include <godot_cpp/classes/gradient.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/procedural_sky_material.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/sky.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/tween.hpp>
#include <godot_cpp/classes/callback_tweener.hpp>
#include <godot_cpp/classes/interval_tweener.hpp>
#include <godot_cpp/classes/property_tweener.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <sstream>
using namespace altaria;
namespace godot {
static std::optional<Vector2i> parse_resolution(const String &value) {
    const auto dimensions = value.to_lower().split("x");
    if (dimensions.size() != 2 || !dimensions[0].is_valid_int() || !dimensions[1].is_valid_int())
        return {};
    const int64_t width = dimensions[0].to_int(), height = dimensions[1].to_int();
    if (width < 640 || width > 7680 || height < 360 || height > 4320)
        return {};
    return Vector2i(width, height);
}
void AltariaGame::_bind_methods() {
    ClassDB::bind_method(D_METHOD("place_block", "position", "block", "shape", "rotation"),
                         &AltariaGame::place_block, DEFVAL("cube"), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("get_built_blocks"), &AltariaGame::get_built_blocks);
    ClassDB::bind_method(D_METHOD("remove_built_block", "id"), &AltariaGame::remove_built_block);
    ClassDB::bind_method(D_METHOD("start_world", "name", "seed", "preset", "biome"), &AltariaGame::start_world,
                         DEFVAL("default"), DEFVAL(""));
    ClassDB::bind_method(D_METHOD("start_random_world"), &AltariaGame::start_random_world);
    ClassDB::bind_method(D_METHOD("shape_voxel", "x", "y", "z", "shape", "rotation"),
                         &AltariaGame::shape_voxel, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("get_temperature", "position"), &AltariaGame::get_temperature);
    ClassDB::bind_method(D_METHOD("release_voxel", "x", "y", "z", "tree"), &AltariaGame::release_voxel,
                         DEFVAL(true));
    ClassDB::bind_method(D_METHOD("set_voxel", "x", "y", "z", "block"), &AltariaGame::set_voxel);
    ClassDB::bind_method(D_METHOD("get_voxel", "x", "y", "z"), &AltariaGame::get_voxel);
    ClassDB::bind_method(D_METHOD("get_shape_type", "x", "y", "z"), &AltariaGame::get_shape_type);
    ClassDB::bind_method(D_METHOD("paint_voxel", "x", "y", "z", "color", "preserve", "brightness"),
                         &AltariaGame::paint_voxel, DEFVAL(.35), DEFVAL(1.0));
    ClassDB::bind_method(D_METHOD("overlay_voxel", "x", "y", "z", "key"), &AltariaGame::overlay_voxel);
    ClassDB::bind_method(D_METHOD("get_voxel_appearance", "x", "y", "z"), &AltariaGame::get_voxel_appearance);
    ClassDB::bind_method(D_METHOD("execute_command", "text"), &AltariaGame::execute_command);
    ClassDB::bind_method(D_METHOD("execute_legacy_command", "text"), &AltariaGame::execute_legacy_command);
    ClassDB::bind_method(D_METHOD("set_debug_overlay", "value"), &AltariaGame::set_debug_overlay);
    ClassDB::bind_method(D_METHOD("is_debug_overlay"), &AltariaGame::is_debug_overlay);
    ClassDB::bind_method(D_METHOD("get_debug_state"), &AltariaGame::get_debug_state);
    ClassDB::bind_method(D_METHOD("get_cave_environment", "position"), &AltariaGame::get_cave_environment);
    ClassDB::bind_method(D_METHOD("get_cave_formations"), &AltariaGame::get_cave_formations);
    ClassDB::bind_method(D_METHOD("get_world_path"), &AltariaGame::get_world_path);
    ClassDB::bind_method(D_METHOD("get_console"), &AltariaGame::get_console);
    ClassDB::bind_method(D_METHOD("get_hotbar_slots"), &AltariaGame::get_hotbar_slots);
    ClassDB::bind_method(D_METHOD("get_active_hotbar_slot"), &AltariaGame::get_active_hotbar_slot);
    ClassDB::bind_method(D_METHOD("get_selected_slot"), &AltariaGame::get_selected_slot);
    ClassDB::bind_method(D_METHOD("select_hotbar_slot", "index"), &AltariaGame::select_hotbar_slot);
    ClassDB::bind_method(D_METHOD("collect_block_to_hotbar", "block_id", "amount"),
                         &AltariaGame::collect_block_to_hotbar, DEFVAL(1));
    ClassDB::bind_method(D_METHOD("is_alt_inventory_open"), &AltariaGame::is_alt_inventory_open);
    ClassDB::bind_method(D_METHOD("is_hotbar_idle_dimmed"), &AltariaGame::is_hotbar_idle_dimmed);
    ClassDB::bind_method(D_METHOD("is_full_map_open"), &AltariaGame::is_full_map_open);
    ClassDB::bind_method(D_METHOD("toggle_full_map"), &AltariaGame::toggle_full_map);
    ClassDB::bind_method(D_METHOD("get_crosshair_darkness"), &AltariaGame::get_crosshair_darkness);
    ClassDB::bind_method(D_METHOD("get_break_stage"), &AltariaGame::get_break_stage);
    ClassDB::bind_method(D_METHOD("get_dropped_item_count"), &AltariaGame::get_dropped_item_count);
    ClassDB::bind_method(D_METHOD("get_creature_count", "key"), &AltariaGame::get_creature_count, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("get_creature_positions", "key"), &AltariaGame::get_creature_positions, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("on_hand_break_impact"), &AltariaGame::on_hand_break_impact);
}
void AltariaGame::_ready() {
    active_game = this;
    if (Engine::get_singleton()->is_editor_hint())
        return;
    game_log.instantiate();
    OS::get_singleton()->add_logger(game_log);
    get_tree()->set_auto_accept_quit(false);
    String error;
    generation_data = load_generation_data(error);
    if (error.is_empty())
        blocks_data = load_block_data(error);
    if (error.is_empty())
        entity_data = read_json("res://game/player/data/entities.json", error);
    data_ready = error.is_empty();
    String preferences_error;
    const auto preferences = read_json("user://preferences.json", preferences_error);
    if (preferences.get("language", "en") == String("pt_BR"))
        language = "pt_BR";
    if (const auto size = parse_resolution(preferences.get("resolution", "")); size && get_window())
        get_window()->set_size(*size);
    overlay = memnew(CanvasLayer);
    overlay->set_layer(20);
    add_child(overlay);
    build_hud();
    build_cheat_inventory();
    auto panel = memnew(PanelContainer);
    panel->set_name("WorldLoading");
    loading_panel = panel;
    overlay->add_child(panel);
    panel->set_anchor(SIDE_RIGHT, 1.f);
    panel->set_anchor(SIDE_LEFT, .76f);
    panel->set_offset(SIDE_LEFT, 0.f);
    panel->set_offset(SIDE_RIGHT, -24.f);
    panel->set_offset(SIDE_TOP, 24.f);
    auto margin = memnew(MarginContainer);
    margin->add_theme_constant_override("margin_left", 12);
    margin->add_theme_constant_override("margin_right", 12);
    margin->add_theme_constant_override("margin_top", 8);
    margin->add_theme_constant_override("margin_bottom", 8);
    panel->add_child(margin);
    auto box = memnew(VBoxContainer);
    margin->add_child(box);
    auto title = memnew(Label);
    title->set_text("Loading world");
    box->add_child(title);
    loading_status = memnew(Label);
    box->add_child(loading_status);
    panel->hide();
    if (!data_ready)
        print_console(error);
    else
        print_console("Enter world new to generate a world.");
    if (auto console = get_console())
        console->set_enabled(true);
}
void AltariaGame::build_hud() {
    if (hud)
        hud->queue_free();
    hud = memnew(Control);
    hud->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    hud->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    overlay->add_child(hud);
    first_person_hands = nullptr;
    Ref<PackedScene> hands_scene = ResourceLoader::get_singleton()->load(
        "res://game/player/first_person/first_person_hands.tscn");
    if (hands_scene.is_valid()) {
        first_person_hands = Object::cast_to<Control>(hands_scene->instantiate());
        hud->add_child(first_person_hands);
    }
    telemetry = memnew(Label);
    telemetry->set_position(Vector2(14, 12));
    telemetry->add_theme_font_size_override("font_size", 15);
    telemetry->add_theme_color_override("font_outline_color", Color(0, 0, 0, .8));
    telemetry->add_theme_constant_override("outline_size", 4);
    hud->add_child(telemetry);
    crosshair = memnew(CrosshairDot);
    hud->add_child(crosshair);
    arc_hotbar = memnew(ArcHotbar);
    hud->add_child(arc_hotbar);
    arc_hotbar->bind_sources(&inventory, world, language == "pt_BR");
    diorama_minimap = memnew(DioramaMinimap);
    hud->add_child(diorama_minimap);
    diorama_minimap->bind_sources(world, player, built_world, language == "pt_BR");
    diorama_minimap->set_on_full_map_changed([this](bool open) {
        if (open && hud && diorama_minimap)
            hud->move_child(diorama_minimap, -1);
        set_game_input(!open);
    });
    stamina_ring = memnew(StaminaRing);
    hud->add_child(stamina_ring);
    stamina_ring->bind_player(player);
    hotbar_label = memnew(Label);
    hotbar_label->set_anchors_and_offsets_preset(Control::PRESET_BOTTOM_LEFT);
    hotbar_label->set_offset(SIDE_LEFT, 14);
    hotbar_label->set_offset(SIDE_TOP, -72);
    hotbar_label->set_offset(SIDE_BOTTOM, -12);
    hotbar_label->add_theme_font_size_override("font_size", 12);
    hotbar_label->add_theme_color_override("font_color", Color(0.87f, 0.81f, 0.93f, 0.82f));
    hotbar_label->add_theme_color_override("font_outline_color", Color(0.063f, 0.071f, 0.110f, 0.9f));
    hotbar_label->add_theme_constant_override("outline_size", 4);
    hotbar_label->set_visible(debug_overlay_visible);
    hud->add_child(hotbar_label);
    hud->hide();
}
void AltariaGame::toggle_full_map() {
    if (!world || loading_world || is_console_open() || cheat_inventory_open() || !diorama_minimap)
        return;
    diorama_minimap->set_full_map_open(!diorama_minimap->is_full_map_open());
}
void AltariaGame::set_game_input(bool active) {
    active = active && !loading_world && !cheat_inventory_open() && !is_full_map_open();
    if (!active) {
        close_alt_inventory();
        if (placement_preview)
            placement_preview->hide();
    }
    if (player)
        player->set_active(active);
    bool capture = active && get_window()->has_focus();
    Input::get_singleton()->set_mouse_mode(capture ? Input::MOUSE_MODE_CAPTURED : Input::MOUSE_MODE_VISIBLE);
    if (hud)
        hud->set_visible(world && !loading_world && !is_console_open() && !cheat_inventory_open());
    if (first_person_hands) {
        first_person_hands->set_visible(active);
        if (!active)
            first_person_hands->call("set_running", false);
    }
}
void AltariaGame::on_console_visibility_changed(bool open) {
    if (open)
        close_cheat_inventory();
    set_game_input(world && !open);
    if (open && selection)
        selection->hide();
    for (const auto &entry : damaged_targets) {
        if (entry.overlay)
            entry.overlay->set_visible(!open && entry.hits >= 1);
    }
}
void AltariaGame::_notification(int what) {
    if (what == NOTIFICATION_WM_CLOSE_REQUEST && is_inside_tree())
        quit_game();
    if (world && player && what == NOTIFICATION_APPLICATION_FOCUS_OUT) {
        player->set_active(false);
        Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
    }
    if (world && player && what == NOTIFICATION_APPLICATION_FOCUS_IN &&
        !is_console_open())
        set_game_input(true);
}
void AltariaGame::quit_game() {
    stop_world();
    get_tree()->quit();
}
void AltariaGame::stop_world() {
    loading_world = false;
    if (loading_panel)
        loading_panel->hide();
    if (player)
        player->set_active(false);
    world = nullptr;
    player = nullptr;
    if (first_person_hands)
        first_person_hands->call("reset_pose");
    close_cheat_inventory();
    structure_outline = nullptr;
    structure_volume = nullptr;
    structure_corner_markers[0] = nullptr;
    structure_corner_markers[1] = nullptr;
    structure_paste_preview = nullptr;
    structure_clipboard = {};
    structure_undo_stack.clear();
    wand_mode = 0;
    wand_material = 0;
    wand_material_is_air = false;
    wand_replace_target = 0;
    wand_replace_target_is_air = false;
    wand_paste_rot = 0;
    for (auto &corner : structure_corners)
        corner.reset();
    selection_is_physical = false;
    atmosphere = nullptr;
    physics_blocks = nullptr;
    built_world = nullptr;
    selected_built_id = 0;
    selection_shape = -1;
    selection = nullptr;
    for (auto &mat : break_materials)
        mat.unref();
    damaged_targets.clear();
    lmb_cooldown_timer = 0.0f;
    lmb_held = false;
    rmb_held = false;
    lmb_repeat_timer = 0.0f;
    rmb_repeat_timer = 0.0f;
    pending_break_hit = false;
    pending_break_hit_timer = 0.0f;
    placement_preview = nullptr;
    preview_block = -1;
    hand_held_block = -1;
    hand_held_shape = -1;
    hand_held_rot = -1;
    hand_equipped_tool = "pickaxe";
    console_tool_override = false;
    if (first_person_hands) {
        first_person_hands->call("set_tool", String("left"), String());
        first_person_hands->call("set_tool", String("right"), String("pickaxe"));
    }
    prev_player_pos_valid = false;
    last_action_world_timer = 0.0f;
    cave_map=nullptr;cave_map_plan.reset();cave_map_enabled=false;cave_map_loaded=false;
    fly_hotkey_enabled = false;
    noclip_hotkey_enabled = false;
    grass_hit_anim = GrassHitAnim();
    clear_dropped_items();
    creatures.clear();
    creatures.detach();
    sun = nullptr;
    world_environment = nullptr;
    if (stage) {
        remove_child(stage);
        memdelete(stage);
        stage = nullptr;
    }
    get_viewport()->set_disable_3d(false);
    world_path = String();
    metadata.clear();
    inventory.clear();
    if (arc_hotbar) {
        arc_hotbar->close_alt_menu();
        arc_hotbar->bind_sources(&inventory, nullptr, language == "pt_BR");
    }
    if (diorama_minimap)
        diorama_minimap->bind_sources(nullptr, nullptr, nullptr, language == "pt_BR");
    if (stamina_ring)
        stamina_ring->bind_player(nullptr);
    set_game_input(false);
    if (get_console())
        get_console()->set_enabled(false);
}
bool AltariaGame::start_random_world() {
    int64_t seed = UtilityFunctions::randi_range(INT32_MIN, INT32_MAX);
    return start_world("World " + String::num_int64(seed), seed);
}
bool AltariaGame::start_world(const String &name, int64_t seed, const String &preset, const String &biome) {
    if (!data_ready || name.is_empty() || name.length() > 80 || seed < INT32_MIN || seed > INT32_MAX) {
        print_console("Invalid world name or seed.");
        return false;
    }
    String error;
    auto config = load_generation_data(error);
    config["preset"] = preset.to_lower();
    const String clean_biome = biome.strip_edges().to_lower();
    if (!clean_biome.is_empty())
        config["single_biome"] = clean_biome;
    if (error.is_empty())
        blocks_data = load_block_data(error);
    if (error.is_empty())
        entity_data = read_json("res://game/player/data/entities.json", error);
    Settings s;
    Registry r;
    if (!error.is_empty() || !parse_settings(config, s, error) || !parse_registry(blocks_data, r, error)) {
        print_console(error);
        return false;
    }
    Dictionary meta;
    meta["format_version"] = 1;
    meta["name"] = name;
    meta["seed"] = seed;
    meta["generation"] = config;
    meta["created_at"] = Time::get_singleton()->get_datetime_string_from_system(true);
    Dictionary ids;
    for (auto &b : r.blocks)
        if (!b.key.empty())
            ids[String(b.key.c_str())] = b.id;
    meta["block_ids"] = ids;
    Dictionary overlay_ids;
    for (const auto &o : r.overlays)
        if (!o.key.empty())
            overlay_ids[String(o.key.c_str())] = o.id;
    meta["overlay_ids"] = overlay_ids;
    meta["entities"] = Array();
    return load_world(meta);
}
bool AltariaGame::load_world(const Dictionary &input) {
    if (!data_ready)
        return false;
    String error;
    Dictionary data=input;
    String path;
    Settings settings;
    Registry registry;
    auto current_blocks = load_block_data(error);
    if (!error.is_empty() || int(data.get("format_version", 0)) != 1 ||
        data.get("generation", Variant()).get_type() != Variant::DICTIONARY ||
        !parse_settings(data.get("generation", Dictionary()), settings, error) ||
        !parse_registry(current_blocks, registry, error)) {
        print_console(error.is_empty() ? String("Unsupported or corrupt world file.") : error);
        return false;
    }
    auto candidate = memnew(VoxelWorld);
    if (!candidate->configure(settings, registry, int(data.get("seed", 0)), path + String("/edits.log"),
                              error)) {
        memdelete(candidate);
        print_console(error);
        return false;
    }
    auto candidate_building = memnew(BuiltWorld);
    if (!candidate_building->configure(candidate, path + String("/construction.log"), error)) {
        memdelete(candidate_building);
        memdelete(candidate);
        print_console(error);
        return false;
    }
    stop_world();
    built_world = candidate_building;
    world_path = String();
    metadata = data;
    world = candidate;
    sequence = 0;
    generation_reported = false;
    stage = memnew(Node3D);
    stage->set_name("WorldSession");
    add_child(stage);
    stage->add_child(world);
    stage->add_child(built_world);
    create_stage();
    inventory.clear();
    inventory.give_tool("pickaxe");
    inventory.give_tool("axe");
    if (settings.preset == WorldPreset::Structure) {
        inventory.collect_block(registry.stone, 64);
        inventory.give_tool("world_wand");
    }
    wand_mode = 0;
    wand_material = 0;
    wand_material_is_air = false;
    wand_replace_target = 0;
    wand_replace_target_is_air = false;
    wand_paste_rot = 0;
    hand_equipped_tool = "pickaxe";
    console_tool_override = false;
    if (first_person_hands) {
        first_person_hands->call("set_tool", String("left"), String());
        first_person_hands->call("set_tool", String("right"), String("pickaxe"));
    }
    if (arc_hotbar) {
        arc_hotbar->close_alt_menu();
        arc_hotbar->bind_sources(&inventory, world, language == "pt_BR");
    }
    player = memnew(FlyingPlayer);
    Dictionary pd = entity_data.get("player", Dictionary());
    reach = std::clamp(float(pd.get("reach", 9)), 1.f, 16.f);
    player->configure(pd, [this](Vector3 minimum, Vector3 maximum) {
        return world && (world->collides_box(minimum, maximum) ||
                         (physics_blocks && physics_blocks->collides_box(minimum, maximum)) ||
                         (built_world && built_world->collides_box(minimum, maximum)));
    });
    player->set_liquid_query([this](Vector3 minimum, Vector3 maximum, bool &in_liquid) -> Vector3 {
        return world ? world->liquid_current_box(minimum, maximum, in_liquid) : Vector3();
    });
    player->set_liquid_state_query([this](Vector3 minimum, Vector3 maximum) -> FlyingPlayer::LiquidState {
        FlyingPlayer::LiquidState out{};
        if (world) {
            bool has_surface = false;
            world->probe_liquid_box(minimum, maximum, out.in_water, out.in_lava,
                                    out.submergence, out.surface_y, has_surface, out.current);
            out.has_surface = has_surface;
        }
        return out;
    });
    player->set_foliage_query([this](Vector3 minimum, Vector3 maximum) {
        return world && (world->in_foliage_box(minimum, maximum) ||
                         (built_world && built_world->in_foliage_box(minimum, maximum)));
    });
    player->set_stream_query([this](Vector3 position){return world && world->voxels_ready(position);});
    player->set_minimum_y(float(world->settings().min_y + 1));
    stage->add_child(player);
    if (diorama_minimap)
        diorama_minimap->bind_sources(world, player, built_world, language == "pt_BR");
    if (stamina_ring)
        stamina_ring->bind_player(player);
    built_world->set_guard([this](const BuiltBlock &b) {
        if (!player)
            return true;
        auto box = player->body_bounds();
        auto p = b.position();
        auto hi = box.position + box.size;
        return !shape_intersects(b.shape(),
                                 {box.position.x - p[0], box.position.y - p[1], box.position.z - p[2]},
                                 {hi.x - p[0], hi.y - p[1], hi.z - p[2]});
    });
    player->get_camera()->set_far(settings.view_distance * 1.25f + 2048);
    atmosphere->attach_camera(player->get_camera());
    Dictionary saved = data.get("player", Dictionary());
    Array position = saved.get("position", Array());
    Vector3 spawn;
    if (position.size() == 3) {
        spawn = Vector3(position[0], position[1], position[2]);
        if (!spawn.is_finite())
            spawn = Vector3();
    } else if (settings.preset != WorldPreset::Default) {
        const auto column = world->generator()->column(0, 0);
        const float height = settings.preset == WorldPreset::Plain
            ? column.height + 5.f : std::clamp(64, settings.min_y + 8, settings.max_y - 8);
        spawn = Vector3(.5f, height, .5f);
    } else {
        // Flat temperate plains with cold and hot lands nearby; otherwise any
        // dry scenic spot near the origin.
        const SpawnPoint found = world->generator()->find_spawn();
        float best = -1e9;
        if (found.found) {
            best = 1e9;
            spawn = Vector3(found.x, found.height + 1.5f, found.z);
        }
        for (int z = -256; z <= 256; z += 64)
            for (int x = -256; x <= 256; x += 64) {
                auto c = world->generator()->column(x, z);
                float score = -std::abs(c.height - 120);
                if (!found.found && c.height > settings.sea_level + 5 && score > best) {
                    best = score;
                    spawn = Vector3(x, c.height + 1.5f, z);
                }
            }
    }
    player->set_position(spawn);
    const bool structure = settings.preset == WorldPreset::Structure;
    player->set_view(float(saved.get("yaw", structure ? 0. : -.65)),
                     float(saved.get("pitch", structure ? -.65 : -.28)));
    if (structure) {
        create_structure_outline();
        // Leave the procedural preset empty and give builders a nearby anchor.
        const int starter_y = int(spawn.y) - 2;
        place_block(Vector3(0, starter_y, -4), registry.stone, "cube", 0);
    }
    player->set_flying(saved.get("flying", structure));
    world->update_viewer(spawn);
    built_world->update_viewer(spawn);
    physics_blocks = memnew(PhysicsBlocks);
    physics_blocks->configure(world, atmosphere->get_settings());
    stage->add_child(physics_blocks);
    physics_blocks->update_viewer(spawn);
    creatures.configure(world, stage, entity_data, [this](Vector3 minimum, Vector3 maximum) {
        return world && (world->collides_box(minimum, maximum) ||
                         (physics_blocks && physics_blocks->collides_box(minimum, maximum)) ||
                         (built_world && built_world->collides_box(minimum, maximum)));
    }, uint32_t(int64_t(metadata.get("seed", 0))));
    Array entities = metadata.get("entities", Array());
    for (int i = 0; i < entities.size(); i++)
        if (entities[i].get_type() == Variant::DICTIONARY) {
            Dictionary entity = entities[i];
            spawn_entity(entity.get("key", String()), entity);
        }
    loading_world = !world->initial_stream_ready();
    if (loading_world)
        loading_status->set_text("Preparing chunks...");
    loading_panel->set_visible(loading_world);
    atmosphere->set_loading_world(loading_world);
    set_game_input(!is_console_open());
    update_hud();
    UtilityFunctions::print("WORLD_OPEN ", metadata["name"], " seed=", metadata["seed"]);
    return true;
}
void AltariaGame::create_stage() {
    atmosphere = memnew(WorldAtmosphere);
    stage->add_child(atmosphere);
    String error;
    Dictionary generation = metadata.get("generation", Dictionary());
    Dictionary config = generation.get("environment", Dictionary());
    if (config.is_empty())
        config = read_json("res://game/world_generation/data/environment.json", error);
    if (!atmosphere->configure(world, config, metadata.get("atmosphere", Dictionary()), error)) {
        print_console(error);
        UtilityFunctions::push_error(error);
    }
    selection = memnew(MeshInstance3D);
    selection->set_name("BlockSelection");
    selection->set_mesh(build_selection_outline_mesh(0));
    selection_shape = 0;
    Ref<Shader> outline_shader;
    outline_shader.instantiate();
    outline_shader->set_code(
        "shader_type spatial;\n"
        "render_mode unshaded, blend_mix, depth_draw_never, cull_disabled;\n"
        "uniform vec4 albedo : source_color = vec4(0.04, 0.04, 0.06, 0.88);\n"
        "void vertex() {\n"
        "    vec4 view_pos = MODELVIEW_MATRIX * vec4(VERTEX, 1.0);\n"
        "    view_pos.z += 0.004;\n"
        "    POSITION = PROJECTION_MATRIX * view_pos;\n"
        "}\n"
        "void fragment() {\n"
        "    ALBEDO = albedo.rgb;\n"
        "    ALPHA = albedo.a;\n"
        "}\n");
    Ref<ShaderMaterial> mat;
    mat.instantiate();
    mat->set_shader(outline_shader);
    selection->set_material_override(mat);
    selection->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    stage->add_child(selection);
    selection->hide();

    for (int i = 0; i < 5; ++i) {
        if (break_textures[i].is_null()) {
            const String path = "res://game/assets/hud/break" + String::num_int64(i + 1) + ".png";
            Ref<Image> img = Image::load_from_file(ProjectSettings::get_singleton()->globalize_path(path));
            if (img.is_valid() && !img->is_empty()) {
                break_textures[i] = ImageTexture::create_from_image(img);
            } else {
                break_textures[i] = ResourceLoader::get_singleton()->load(path);
            }
        }
        break_materials[i].instantiate();
        break_materials[i]->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        break_materials[i]->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        break_materials[i]->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_NEAREST);
        break_materials[i]->set_flag(BaseMaterial3D::FLAG_UV1_USE_TRIPLANAR, true);
        break_materials[i]->set_uv1_triplanar_blend_sharpness(64.0f);
        break_materials[i]->set_albedo(Color(1.0f, 1.0f, 1.0f, 0.92f));
        break_materials[i]->set_grow_enabled(true);
        break_materials[i]->set_grow(0.005f);
        if (break_textures[i].is_valid())
            break_materials[i]->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, break_textures[i]);
    }
    damaged_targets.clear();

    placement_preview = memnew(MeshInstance3D);
    placement_preview->set_name("PlacementPreview");
    placement_preview->set_transparency(.55f);
    placement_preview->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    stage->add_child(placement_preview);
    placement_preview->hide();
    create_structure_outline();
}

void AltariaGame::show_placement(const BuiltBlock &piece, bool animate) {
    auto p = piece.position();
    const Vector3 position(p[0], p[1], p[2]);
    auto appearance = piece.appearance;
    unpack_shape(piece.shape(), appearance);
    const auto make_mesh = [&]() {
        return world->block_mesh({{{0, 0, 0}, piece.block}}, {appearance}, world->generator()->column(p[0], p[2]));
    };
    if (!animate) {
        if (preview_block != piece.block || preview_shape != piece.shape() || !(preview_appearance == appearance)) {
            placement_preview->set_mesh(make_mesh());
            preview_block = piece.block;
            preview_shape = piece.shape();
            preview_appearance = appearance;
        }
        placement_preview->set_position(position);
        placement_preview->show();
        return;
    }

    auto effect = memnew(MeshInstance3D);
    effect->set_name("PlacementPulse");
    effect->set_mesh(make_mesh());
    effect->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    if (placement_preview)
        placement_preview->hide();
    if (first_person_hands && player && player->get_camera()) {
        auto *cam = player->get_camera();
        const Vector3 world_center = position + Vector3(0.5f, 0.5f, 0.5f);
        last_action_world_pos = world_center;
        last_action_world_timer = 0.28f;
        if (!cam->is_position_behind(world_center)) {
            const Vector2 screen_pt = cam->unproject_position(world_center);
            const float dist = cam->get_global_position().distance_to(world_center);
            first_person_hands->call("play_world_action", StringName("place"), screen_pt, dist);
        }
    }
    Ref<StandardMaterial3D> flash;
    flash.instantiate();
    flash->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    flash->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    flash->set_albedo(Color(1, 1, 1, 1));
    flash->set_grow_enabled(true);
    flash->set_grow(.008f);
    effect->set_material_overlay(flash);
    const auto bounds = effect->get_mesh()->get_aabb();
    const Vector3 pivot(bounds.get_center().x, bounds.position.y, bounds.get_center().z);
    const Vector3 squish(1.16f, .58f, 1.16f);
    const Vector3 rebound(.96f, 1.08f, .96f);
    effect->set_scale(squish);
    effect->set_position(position + pivot - pivot * squish);
    stage->add_child(effect, true);
    const uint64_t id = piece.id;
    if (id)
        built_world->set_placement_animated(id, true);
    auto tween = effect->create_tween();
    tween->set_parallel(true)->set_trans(Tween::TRANS_QUAD)->set_ease(Tween::EASE_OUT);
    tween->tween_property(effect, "scale", rebound, .12);
    tween->tween_property(effect, "position", position + pivot - pivot * rebound, .12);
    tween->tween_property(flash.ptr(), "albedo_color", Color(1, 1, 1, .24f), .12);
    tween->chain()->tween_property(effect, "scale", Vector3(1, 1, 1), .10);
    tween->parallel()->tween_property(effect, "position", position, .10);
    tween->parallel()->tween_property(flash.ptr(), "albedo_color", Color(1, 1, 1, 0), .10);
    if (id) {
        tween->chain()->tween_callback(callable_mp(built_world, &BuiltWorld::set_placement_animated).bind(id, false));
        tween->chain()->tween_interval(.04);
    } else {
        tween->chain()->tween_property(effect, "transparency", 1.f, .05);
    }
    tween->chain()->tween_callback(Callable(effect, "queue_free"));
}

int64_t AltariaGame::place_block(Vector3 position, int block, const String &name, int rotation) {
    if (!built_world)
        return 0;
    int shape = shape_id(name.utf8().get_data());
    String error;
    auto id = built_world->place(position, block, shape, rotation, error);
    if (!id)
        print_console(error);
    return int64_t(id);
}
Array AltariaGame::get_built_blocks() const {
    return built_world ? built_world->describe() : Array();
}
bool AltariaGame::remove_built_block(int64_t id) {
    if (!built_world || id <= 0)
        return false;
    String error;
    bool result = built_world->remove(uint64_t(id), error);
    if (!result)
        print_console(error);
    return result;
}
uint64_t AltariaGame::target_built() const {
    if (!built_world || !player)
        return 0;
    auto camera = player->get_camera();
    auto direction = -camera->get_global_basis().get_column(2);
    auto hit = pick_built(camera->get_global_position(), direction, reach);
    auto terrain = target_block();
    auto physics = physics_blocks->pick(camera->get_global_position(), direction, reach);
    return hit.id && (!terrain.hit || hit.distance < terrain.distance) && hit.distance < physics.distance
               ? hit.id
               : 0;
}
bool AltariaGame::collect_block(altaria::BlockId block, int amount) {
    if (!world || block == world->registry().air || amount <= 0)
        return false;
    const auto &blk = world->registry().get(block);
    if (!blk.collectible)
        return false;
    int slot_idx = -1;
    if (!inventory.collect_block(block, amount, "block_shapes", &slot_idx)) {
        if (arc_hotbar)
            arc_hotbar->trigger_shake();
        return false;
    }
    if (arc_hotbar)
        arc_hotbar->trigger_pickup_anim(slot_idx);
    return true;
}

void AltariaGame::clear_dropped_items() {
    for (auto &item : dropped_items) {
        if (item.root) {
            if (item.root->get_parent())
                item.root->get_parent()->remove_child(item.root);
            memdelete(item.root);
        }
    }
    dropped_items.clear();
}

void AltariaGame::spawn_item_drop(altaria::BlockId block, const Vector3 &world_pos,
                                  const altaria::Appearance &appearance, int amount) {
    if (!world || !stage || block == world->registry().air || amount <= 0)
        return;
    const auto &blk = world->registry().get(block);
    if (!blk.collectible)
        return;
    Appearance item_app = appearance;
    if (!blk.placeable) {
        item_app.shape = 0;
        item_app.rotation = 0;
    }
    const Column col = world->generator()
                           ? world->generator()->column(int(std::floor(world_pos.x)), int(std::floor(world_pos.z)))
                           : Column();
    Ref<ArrayMesh> mesh = world->block_mesh({{{0, 0, 0}, block}}, {item_app}, col, true);
    if (mesh.is_null()) {
        collect_block(block, amount);
        return;
    }
    const float item_scale = blk.instant_break ? 0.18f : 0.24f;
    Node3D *root = memnew(Node3D);
    MeshInstance3D *mesh_node = memnew(MeshInstance3D);
    mesh_node->set_mesh(mesh);
    mesh_node->set_scale(Vector3(item_scale, item_scale, item_scale));
    mesh_node->set_position(Vector3(-0.5f, -0.5f, -0.5f) * item_scale);
    mesh_node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    root->add_child(mesh_node);
    stage->add_child(root);
    root->set_position(world_pos);
    const uint32_t seed = uint32_t(dropped_items.size() * 73856093u) ^
                          uint32_t(int(std::floor(world_pos.x * 13.0f)) * 19349663) ^
                          uint32_t(int(std::floor(world_pos.z * 17.0f)) * 83492791);
    const float angle = float(seed & 255u) / 256.0f * 6.28318530f;
    Vector3 dir_hint(std::cos(angle), 0.0f, std::sin(angle));
    if (player) {
        Vector3 to_player = player->get_global_position() - world_pos;
        to_player.y = 0.0f;
        if (to_player.length_squared() > 0.001f)
            dir_hint = (to_player.normalized() * 0.55f + dir_hint * 0.45f).normalized();
    }
    DroppedItem entry;
    entry.root = root;
    entry.block = block;
    entry.amount = amount;
    entry.start_pos = world_pos;
    entry.hop_offset = dir_hint * 0.28f;
    entry.age = 0.0f;
    entry.duration = 0.62f;
    entry.spin_speed = 4.8f;
    dropped_items.push_back(entry);
}

void AltariaGame::update_dropped_items(float dt) {
    if (dropped_items.empty() || !world || !player || !stage)
        return;
    const Vector3 target_pos = player->get_global_position() + Vector3(0.0f, 0.85f, 0.0f);
    for (size_t i = dropped_items.size(); i > 0; --i) {
        size_t idx = i - 1;
        auto &item = dropped_items[idx];
        item.age += dt;
        const float t = std::clamp(item.age / item.duration, 0.0f, 1.0f);
        Vector3 pos;
        if (t < 0.30f) {
            const float p1 = t / 0.30f;
            const float hop1 = std::sin(p1 * 3.14159265f) * 0.42f;
            pos = item.start_pos + item.hop_offset * p1 + Vector3(0.0f, hop1, 0.0f);
        } else {
            const float p2 = (t - 0.30f) / 0.70f;
            const Vector3 launch_pos = item.start_pos + item.hop_offset;
            const float pull = p2 * p2 * (3.0f - 2.0f * p2);
            const float hop2 = std::abs(std::sin(p2 * 3.14159265f * 1.5f)) * 0.48f * (1.0f - p2);
            pos = launch_pos.lerp(target_pos, pull) + Vector3(0.0f, hop2, 0.0f);
        }
        if (item.root) {
            item.root->set_position(pos);
            item.root->rotate_y(item.spin_speed * dt);
            const float scale_mul = t > 0.78f ? std::max(0.35f, 1.0f - (t - 0.78f) / 0.22f * 0.65f) : 1.0f;
            item.root->set_scale(Vector3(scale_mul, scale_mul, scale_mul));
        }
        if (t >= 1.0f || (t >= 0.35f && pos.distance_to(target_pos) < 0.38f)) {
            collect_block(item.block, item.amount);
            if (item.root) {
                if (item.root->get_parent())
                    item.root->get_parent()->remove_child(item.root);
                memdelete(item.root);
            }
            dropped_items.erase(dropped_items.begin() + ptrdiff_t(idx));
        }
    }
}

void AltariaGame::consume_held_block(int amount) {
    if (first_person_hands)
        first_person_hands->call("play_action", StringName("place"));
    if (arc_hotbar)
        arc_hotbar->notify_activity();
    if (cheat_enabled || (player && player->is_god()))
        return;
    bool emptied = false;
    if (inventory.consume_selected(amount, &emptied) && emptied) {
        selected_shape = int(BlockShape::Cube);
        close_alt_inventory();
    }
}

void AltariaGame::sync_alt_selection() {
    if (!arc_hotbar || !arc_hotbar->is_alt_open())
        return;
    const auto &slot = inventory.active_slot();
    const std::string menu_id = (current_equipped_tool() == "world_wand")
                                    ? std::string("wand_modes")
                                    : slot.alt_menu_id;
    if (const auto *def = inventory.find_alt_menu(menu_id)) {
        int idx = arc_hotbar->hovered_alt_index();
        if (idx >= 0 && idx < int(def->options.size())) {
            const auto &opt = def->options[idx];
            if (opt.shape_id >= 0) {
                if (menu_id == "wand_modes") {
                    wand_mode = opt.shape_id;
                    if (slot.alt_menu_id == "wand_modes")
                        inventory.set_active_shape(opt.shape_id);
                    refresh_wand_hud();
                } else {
                    selected_shape = opt.shape_id;
                    inventory.set_active_shape(opt.shape_id);
                }
            }
        }
    }
}

void AltariaGame::confirm_alt_selection() {
    if (!arc_hotbar || !arc_hotbar->is_alt_open())
        return;
    const bool is_wand = (current_equipped_tool() == "world_wand");
    sync_alt_selection();
    arc_hotbar->trigger_confirm();
    close_alt_inventory();
    if (is_wand) {
        if (wand_mode == int(WandMode::Fill) || wand_mode == int(WandMode::Walls) ||
            wand_mode == int(WandMode::Hollow)) {
            show_wand_block_picker(CheatPickerPurpose::WandFill);
        } else if (wand_mode == int(WandMode::Replace)) {
            show_wand_block_picker(CheatPickerPurpose::WandReplaceFrom);
        }
    }
}

void AltariaGame::close_alt_inventory() {
    if (arc_hotbar)
        arc_hotbar->close_alt_menu();
}

void AltariaGame::select_hotbar_slot(int index) {
    if (arc_hotbar)
        arc_hotbar->notify_activity();
    inventory.select_slot(index);
    const auto &s = inventory.active_slot();
    if (s.is_tool())
        console_tool_override = false;
    if (s.alt_menu_id == "wand_modes") {
        wand_mode = std::clamp(s.shape_id, 0, int(WandMode::ClearUndo));
        refresh_wand_hud();
    } else {
        selected_shape = s.has_block() ? s.shape_id : int(BlockShape::Cube);
    }
}

std::string AltariaGame::current_equipped_tool() const {
    if (first_person_hands) {
        Variant eq_var = first_person_hands->get("equipped");
        if (eq_var.get_type() == Variant::DICTIONARY) {
            Dictionary eq = eq_var;
            String r = eq.get("right", String());
            if (!r.is_empty())
                return r.utf8().get_data();
            String l = eq.get("left", String());
            if (!l.is_empty())
                return l.utf8().get_data();
        }
    }
    if (inventory.has_active_tool())
        return inventory.active_tool();
    return std::string();
}

void AltariaGame::deny_break(altaria::BlockId block, bool is_physics_debris) {
    (void)block;
    (void)is_physics_debris;
    if (arc_hotbar)
        arc_hotbar->trigger_shake();
}

bool AltariaGame::can_break_block(altaria::BlockId block, bool is_physics_debris) const {
    if (!world || block == 0)
        return false;
    const auto &def = world->registry().get(block);
    if (def.liquid || def.water || def.lava)
        return false;
    if (def.instant_break)
        return true;
    const std::string tool = current_equipped_tool();
    if (tool.empty())
        return false;
    if (is_physics_debris)
        return tool == "pickaxe" || tool == "axe";
    if (def.wood)
        return tool == "axe";
    return tool == "pickaxe";
}

static int highlight_shape_alias(const std::string &q) {
    using altaria::BlockShape;
    static const std::pair<const char *, BlockShape> table[] = {
        {"cube", BlockShape::Cube},        {"block", BlockShape::Cube},          {"bloco", BlockShape::Cube},
        {"slab", BlockShape::Slab},        {"floor", BlockShape::Slab},          {"piso", BlockShape::Slab},
        {"vertical_slab", BlockShape::VerticalSlab}, {"wall", BlockShape::VerticalSlab}, {"parede", BlockShape::VerticalSlab},
        {"pillar", BlockShape::Pillar},    {"pilar", BlockShape::Pillar},
        {"mini_pillar", BlockShape::MiniPillar}, {"minipillar", BlockShape::MiniPillar}, {"minipilar", BlockShape::MiniPillar},
        {"post", BlockShape::MiniPillar},  {"poste", BlockShape::MiniPillar},
        {"stairs", BlockShape::Stairs},    {"stair", BlockShape::Stairs},        {"escada", BlockShape::Stairs},
        {"ramp", BlockShape::Ramp},        {"rampa", BlockShape::Ramp},
        {"roof", BlockShape::Roof},        {"telhado", BlockShape::Roof},
    };
    for (const auto &e : table)
        if (q == e.first)
            return int(e.second);
    return -1;
}

std::string AltariaGame::highlight_blocks(const std::string &raw) {
    std::string q = raw;
    std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (q.empty() || q == "off" || q == "clear" || q == "none") {
        highlight_query.clear();
        if (highlight_mesh)
            highlight_mesh->set_visible(false);
        return "Highlight off.";
    }
    if (!world || !world->generator())
        return "No world active.";
    const auto &hreg = world->generator()->blocks();
    if (q != "all" && highlight_shape_alias(q) < 0 && hreg.find(q) == hreg.air && hreg.find("altaria:" + q) != hreg.air)
        q = "altaria:" + q;
    if (q != "all" && highlight_shape_alias(q) < 0 && hreg.find(q) == hreg.air)
        return "Unknown shape or block: " + raw + " (try floor, wall, stairs, ramp, roof, pillar, post, cube, all or a block id)";
    highlight_query = q;
    const int n = rebuild_highlight();
    return "Highlighting " + std::to_string(n) + " '" + raw + "' blocks. Use 'hl off' to clear.";
}

int AltariaGame::rebuild_highlight() {
    if (!world || !world->generator() || highlight_query.empty())
        return 0;
    const auto &reg = world->generator()->blocks();
    const int want_shape = highlight_shape_alias(highlight_query);
    const bool all = highlight_query == "all";
    const altaria::BlockId want_block = (want_shape < 0 && !all) ? reg.find(highlight_query) : reg.air;
    auto matches = [&](altaria::BlockId block, uint8_t packed) {
        if (all)
            return true;
        if (want_shape >= 0)
            return altaria::shape_type(packed) == want_shape;
        return block == want_block;
    };

    PackedVector3Array tri, lines;
    int count = 0;
    auto emit = [&](Vector3 origin, uint8_t packed) {
        ++count;
        // Slightly inflated around the shape centre so the overlay never z-fights the block.
        const auto bounds = altaria::shape_bounds(packed);
        const Vector3 c = origin + Vector3((bounds.first[0] + bounds.second[0]) * 0.5f, (bounds.first[1] + bounds.second[1]) * 0.5f,
                                           (bounds.first[2] + bounds.second[2]) * 0.5f);
        for (const auto &f : altaria::shape_faces(packed)) {
            Vector3 p[4];
            for (int k = 0; k < 4; ++k) {
                const Vector3 v = origin + Vector3(f.points[k][0], f.points[k][1], f.points[k][2]);
                p[k] = c + (v - c) * 1.02f + Vector3(f.normal[0], f.normal[1], f.normal[2]) * 0.004f;
            }
            tri.push_back(p[0]); tri.push_back(p[1]); tri.push_back(p[2]);
            tri.push_back(p[0]); tri.push_back(p[2]); tri.push_back(p[3]);
            for (int k = 0; k < 4; ++k) {
                if (p[k].distance_squared_to(p[(k + 1) & 3]) < 1e-8f)
                    continue;
                lines.push_back(p[k]);
                lines.push_back(p[(k + 1) & 3]);
            }
        }
    };

    if (built_world) {
        for (const auto &[id, b] : built_world->construction().blocks) {
            (void)id;
            const uint8_t packed = b.shape();
            if (matches(b.block, packed)) {
                const auto pos = b.position();
                emit(Vector3(pos[0], pos[1], pos[2]), packed);
            }
        }
    }
    // The world itself: every loaded block around the player (generated or edited),
    // using the same shape lookup the mesher uses. Only blocks touching air are
    // drawn, so whole-material queries outline surfaces instead of buried volume.
    if (player) {
        const Vector3 c = player->get_position();
        const int cx = int(std::floor(c.x)), cy = int(std::floor(c.y)), cz = int(std::floor(c.z));
        constexpr int R = kHighlightRadius, H = kHighlightHeight, kMaxEmit = 40000;
        auto solid = [&](int x, int y, int z) { return reg.get(world->block_at(x, y, z)).solid; };
        for (int x = cx - R; x <= cx + R && count < kMaxEmit; ++x)
            for (int z = cz - R; z <= cz + R && count < kMaxEmit; ++z)
                for (int y = cy - H; y <= cy + H; ++y) {
                    const altaria::BlockId b = world->block_at(x, y, z);
                    const auto &def = reg.get(b);
                    if (b == reg.air || !def.solid)
                        continue;
                    const uint8_t packed = world->shape_at(x, y, z);
                    if (!matches(b, packed))
                        continue;
                    if (altaria::shape_type(packed) == int(altaria::BlockShape::Cube) &&
                        solid(x + 1, y, z) && solid(x - 1, y, z) && solid(x, y + 1, z) && solid(x, y - 1, z) &&
                        solid(x, y, z + 1) && solid(x, y, z - 1))
                        continue;
                    emit(Vector3(x, y, z), packed);
                }
        highlight_center = c;
    }
    if (!highlight_mesh) {
        highlight_mesh = memnew(MeshInstance3D);
        highlight_mesh->set_name("BlockHighlight");
        highlight_mesh->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        add_child(highlight_mesh);
    }
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    if (!tri.is_empty()) {
        Array arr;
        arr.resize(Mesh::ARRAY_MAX);
        arr[Mesh::ARRAY_VERTEX] = tri;
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arr);
        Ref<StandardMaterial3D> fill;
        fill.instantiate();
        fill->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        fill->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        fill->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
        fill->set_albedo(Color(1.0f, 0.82f, 0.18f, 0.38f));
        mesh->surface_set_material(0, fill);

        Array la;
        la.resize(Mesh::ARRAY_MAX);
        la[Mesh::ARRAY_VERTEX] = lines;
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_LINES, la);
        Ref<StandardMaterial3D> edge;
        edge.instantiate();
        edge->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        edge->set_render_priority(10);
        edge->set_albedo(Color(1.0f, 0.95f, 0.55f, 1.0f));
        mesh->surface_set_material(1, edge);
    }
    highlight_mesh->set_mesh(mesh);
    highlight_mesh->set_visible(true);
    highlight_revision = world->world_revision() ^ (built_world ? built_world->construction().revision * 1315423911ULL : 0);
    return count;
}

Ref<ArrayMesh> AltariaGame::build_selection_outline_mesh(uint8_t shape) const {
    const auto &raw_faces = altaria::shape_faces(shape);
    struct CleanFace {
        std::vector<Vector3> pts;
        Vector3 normal;
        Vector3 min_v;
        Vector3 max_v;
        float area = 0.0f;
        bool buried = false;
    };
    auto to_vec = [](const std::array<float, 3> &p) { return Vector3(p[0], p[1], p[2]); };
    auto quant = [](const Vector3 &v) -> std::array<int, 3> {
        return {int(std::round(v.x * 1024.0f)), int(std::round(v.y * 1024.0f)), int(std::round(v.z * 1024.0f))};
    };

    std::vector<CleanFace> faces;
    faces.reserve(raw_faces.size());
    for (const auto &rf : raw_faces) {
        CleanFace cf;
        for (int k = 0; k < 4; ++k) {
            Vector3 v = to_vec(rf.points[k]);
            if (cf.pts.empty() || v.distance_squared_to(cf.pts.back()) > 1e-8f) {
                if (cf.pts.size() < 3 || v.distance_squared_to(cf.pts.front()) > 1e-8f)
                    cf.pts.push_back(v);
            }
        }
        if (cf.pts.size() < 3)
            continue;
        cf.normal = to_vec(rf.normal);
        if (cf.normal.length_squared() < 1e-6f)
            continue;
        cf.normal = cf.normal.normalized();
        cf.min_v = cf.max_v = cf.pts[0];
        for (size_t k = 1; k < cf.pts.size(); ++k) {
            cf.min_v = cf.min_v.min(cf.pts[k]);
            cf.max_v = cf.max_v.max(cf.pts[k]);
        }
        for (size_t k = 1; k + 1 < cf.pts.size(); ++k) {
            cf.area += 0.5f * (cf.pts[k] - cf.pts[0]).cross(cf.pts[k + 1] - cf.pts[0]).length();
        }
        if (cf.area < 1e-6f)
            continue;
        faces.push_back(std::move(cf));
    }

    // Filter out buried internal faces where two sub-volumes touch flush (e.g. stairs/branches).
    for (size_t i = 0; i < faces.size(); ++i) {
        for (size_t j = 0; j < faces.size(); ++j) {
            if (i == j)
                continue;
            if (faces[i].normal.dot(faces[j].normal) > -0.995f)
                continue;
            if (faces[j].area + 1e-5f < faces[i].area)
                continue;
            if (std::abs((faces[i].pts[0] - faces[j].pts[0]).dot(faces[j].normal)) > 1e-4f)
                continue;
            bool inside = true;
            for (const auto &v : faces[i].pts) {
                if (v.x < faces[j].min_v.x - 1e-4f || v.x > faces[j].max_v.x + 1e-4f ||
                    v.y < faces[j].min_v.y - 1e-4f || v.y > faces[j].max_v.y + 1e-4f ||
                    v.z < faces[j].min_v.z - 1e-4f || v.z > faces[j].max_v.z + 1e-4f) {
                    inside = false;
                    break;
                }
            }
            if (inside) {
                faces[i].buried = true;
                break;
            }
        }
    }

    using KeyPt = std::array<int, 3>;
    struct EdgeInfo {
        Vector3 p0;
        Vector3 p1;
        int touch_count = 0;
        std::vector<Vector3> normals;
    };
    std::map<std::pair<KeyPt, KeyPt>, EdgeInfo> edges;
    for (const auto &cf : faces) {
        if (cf.buried)
            continue;
        const size_t n = cf.pts.size();
        for (size_t k = 0; k < n; ++k) {
            const Vector3 a = cf.pts[k];
            const Vector3 b = cf.pts[(k + 1) % n];
            KeyPt ka = quant(a), kb = quant(b);
            if (ka == kb)
                continue;
            std::pair<KeyPt, KeyPt> key = (ka < kb) ? std::make_pair(ka, kb) : std::make_pair(kb, ka);
            auto &e = edges[key];
            if (e.touch_count == 0) {
                e.p0 = (ka < kb) ? a : b;
                e.p1 = (ka < kb) ? b : a;
            }
            ++e.touch_count;
            bool seen_normal = false;
            for (const auto &prev_n : e.normals) {
                if (prev_n.dot(cf.normal) > 0.99f) {
                    seen_normal = true;
                    break;
                }
            }
            if (!seen_normal)
                e.normals.push_back(cf.normal);
        }
    }

    PackedVector3Array line_verts;
    for (const auto &[_, e] : edges) {
        if (e.touch_count >= 2 && e.normals.size() <= 1)
            continue;
        Vector3 n_sum;
        for (const auto &n : e.normals)
            n_sum += n;
        const Vector3 nudge = n_sum.length_squared() > 1e-6f ? n_sum.normalized() * 0.0022f : Vector3();
        line_verts.push_back(e.p0 + nudge);
        line_verts.push_back(e.p1 + nudge);
    }

    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    if (!line_verts.is_empty()) {
        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = line_verts;
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_LINES, arrays);
    }
    return mesh;
}

int AltariaGame::find_damaged_target(int kind, altaria::IVec3 cell, uint64_t id) const {
    for (size_t i = 0; i < damaged_targets.size(); ++i) {
        const auto &e = damaged_targets[i];
        if (e.kind != kind)
            continue;
        if ((kind == 1 || kind == 3) ? (e.cell.x == cell.x && e.cell.y == cell.y && e.cell.z == cell.z) : (e.id == id))
            return int(i);
    }
    return -1;
}

void AltariaGame::remove_damaged_target(size_t index) {
    if (index >= damaged_targets.size())
        return;
    if (damaged_targets[index].overlay) {
        damaged_targets[index].overlay->queue_free();
        damaged_targets[index].overlay = nullptr;
    }
    damaged_targets.erase(damaged_targets.begin() + ptrdiff_t(index));
}

void AltariaGame::clear_break_damage() {
    for (auto &entry : damaged_targets) {
        if (entry.overlay) {
            entry.overlay->queue_free();
            entry.overlay = nullptr;
        }
    }
    damaged_targets.clear();
}

void AltariaGame::update_damaged_overlay(DamagedTarget &target, bool animate) {
    if (!stage || !world || target.hits < 1 || target.hits > 5) {
        if (target.overlay)
            target.overlay->hide();
        return;
    }
    if (!target.overlay) {
        target.overlay = memnew(MeshInstance3D);
        target.overlay->set_name("BlockBreakOverlay");
        target.overlay->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        stage->add_child(target.overlay, true);
        target.overlay_shape = -1;
    }
    if (target.overlay_shape != int(target.shape)) {
        Appearance a;
        unpack_shape(target.shape, a);
        target.overlay->set_mesh(
            world->block_mesh({{{0, 0, 0}, target.block > 0 ? target.block : BlockId(1)}}, {a}, Column()));
        target.overlay_shape = int(target.shape);
    }
    if (break_materials[target.hits - 1].is_valid())
        target.overlay->set_material_override(break_materials[target.hits - 1]);

    const Transform3D base_xf = target.transform * Transform3D(Basis().scaled(Vector3(1.008f, 1.008f, 1.008f)),
                                                                Vector3(-0.004f, -0.004f, -0.004f));
    target.overlay->set_visible(!is_console_open());
    if (!animate) {
        target.overlay->set_global_transform(base_xf);
        return;
    }
    const Transform3D jolt_xf = target.transform * Transform3D(Basis().scaled(Vector3(1.032f, 1.032f, 1.032f)),
                                                                Vector3(-0.016f, -0.016f, -0.016f));
    target.overlay->set_global_transform(jolt_xf);
    auto tween = target.overlay->create_tween();
    if (tween.is_valid()) {
        tween->set_trans(Tween::TRANS_QUAD)->set_ease(Tween::EASE_OUT);
        tween->tween_property(target.overlay, "global_transform", base_xf, 0.11);
    }
}

void AltariaGame::show_block_hit_effect(altaria::BlockId block, uint8_t shape, const Transform3D &transform,
                                        const Vector3 &hit_point, const Vector3 &hit_normal, uint64_t built_id,
                                        bool destroyed, int break_kind, altaria::IVec3 break_cell,
                                        uint64_t break_id) {
    if (!stage || !world)
        return;

    const Vector3 n = hit_normal.length_squared() > 0.001f ? hit_normal.normalized() : Vector3(0.0f, 1.0f, 0.0f);
    const Vector3 pos = transform.origin;

    // 1. Block hit pulse animation (squish + rebound + flash overlay, similar to show_placement)
    Appearance appearance;
    unpack_shape(shape, appearance);
    const Column col = world->generator()
                           ? world->generator()->column(int(std::floor(pos.x)), int(std::floor(pos.z)))
                           : Column();
    Ref<ArrayMesh> mesh = world->block_mesh({{{0, 0, 0}, block > 0 ? block : BlockId(1)}}, {appearance}, col);
    if (mesh.is_valid() && mesh->get_surface_count() > 0) {
        auto *effect = memnew(MeshInstance3D);
        effect->set_name("BlockHitPulse");
        effect->set_mesh(mesh);
        effect->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);

        Ref<StandardMaterial3D> flash;
        flash.instantiate();
        flash->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        flash->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        flash->set_albedo(Color(1.0f, 1.0f, 1.0f, 0.85f));
        flash->set_grow_enabled(true);
        flash->set_grow(0.009f);
        effect->set_material_overlay(flash);

        const AABB bounds = mesh->get_aabb();
        const Vector3 pivot(bounds.get_center().x, bounds.position.y, bounds.get_center().z);
        const bool is_static_voxel = (built_id == 0 && !destroyed);
        const Vector3 squish = is_static_voxel ? Vector3(1.06f, 1.07f, 1.06f) : Vector3(1.15f, 0.64f, 1.15f);
        const Vector3 rebound = is_static_voxel ? Vector3(1.005f, 1.11f, 1.005f) : Vector3(0.96f, 1.07f, 0.96f);
        const Vector3 knock = is_static_voxel
                                  ? Vector3(n.x * 0.025f, std::max(0.0f, n.y) * 0.03f, n.z * 0.025f)
                                  : (-n * 0.038f);
        flash->set_albedo(Color(1.0f, 1.0f, 1.0f, 0.90f));

        const bool is_grass = (block == world->registry().grass);
        if (is_grass || is_static_voxel) {
            const float top_h = std::max(0.25f, bounds.position.y + bounds.size.y);
            grass_hit_anim.active = true;
            grass_hit_anim.apply_grass = is_grass;
            grass_hit_anim.apply_terrain = is_static_voxel;
            grass_hit_anim.destroyed = destroyed;
            grass_hit_anim.elapsed = 0.0f;
            grass_hit_anim.top_height = top_h;
            grass_hit_anim.squish = squish;
            grass_hit_anim.rebound = rebound;
            grass_hit_anim.knock = knock;
            if (is_grass) {
                grass_hit_anim.box_min = pos + Vector3(-0.08f, top_h - 0.12f, -0.08f);
                grass_hit_anim.box_max = pos + Vector3(1.08f, top_h + 0.14f, 1.08f);
                const Vector3 s0 = destroyed ? Vector3(1.0f, 1.0f, 1.0f) : squish;
                const Vector3 shift0 = destroyed ? Vector3() : Vector3(knock.x, (s0.y - 1.0f) * top_h + knock.y, knock.z);
                world->set_grass_hit(grass_hit_anim.box_min, grass_hit_anim.box_max, 0.90f, shift0, s0);
            } else {
                world->set_grass_hit(Vector3(), Vector3(-1.0f, -1.0f, -1.0f), 0.0f, Vector3(), Vector3(1.0f, 1.0f, 1.0f));
            }
            if (is_static_voxel) {
                grass_hit_anim.terrain_min = pos - Vector3(0.0005f, 0.0005f, 0.0005f);
                grass_hit_anim.terrain_max = pos + Vector3(1.0005f, 1.0005f, 1.0005f);
                world->set_terrain_hit(grass_hit_anim.terrain_min, grass_hit_anim.terrain_max, 0.90f);
            } else {
                world->set_terrain_hit(Vector3(), Vector3(-1.0f, -1.0f, -1.0f), 0.0f);
            }
        }

        const Transform3D squish_xf(transform.basis.scaled(squish),
                                    pos + transform.basis.xform(pivot) - transform.basis.scaled(squish).xform(pivot) + knock);
        const Transform3D rebound_xf(transform.basis.scaled(rebound),
                                     pos + transform.basis.xform(pivot) - transform.basis.scaled(rebound).xform(pivot));

        stage->add_child(effect, true);
        effect->set_global_transform(squish_xf);

        if (built_id && !destroyed && built_world)
            built_world->set_placement_animated(built_id, true);

        auto tween = effect->create_tween();
        if (tween.is_valid() && destroyed) {
            // The block is already gone: its ghost puffs out and collapses into the debris.
            const Vector3 center = transform.xform(bounds.get_center());
            const Basis puff_b = transform.basis.scaled(Vector3(1.12f, 1.12f, 1.12f));
            const Basis gone_b = transform.basis.scaled(Vector3(0.08f, 0.08f, 0.08f));
            const Transform3D puff_xf(puff_b, center - puff_b.xform(bounds.get_center()));
            const Transform3D gone_xf(gone_b, center - gone_b.xform(bounds.get_center()));
            effect->set_global_transform(transform);
            flash->set_albedo(Color(1.0f, 1.0f, 1.0f, 0.9f));
            tween->set_trans(Tween::TRANS_QUAD)->set_ease(Tween::EASE_OUT);
            tween->tween_property(effect, "global_transform", puff_xf, 0.05);
            tween->set_ease(Tween::EASE_IN);
            tween->tween_property(effect, "global_transform", gone_xf, 0.11);
            tween->parallel()->tween_property(flash.ptr(), "albedo_color", Color(1.0f, 1.0f, 1.0f, 0.2f), 0.11);
            tween->tween_callback(Callable(effect, "queue_free"));
        } else if (tween.is_valid()) {
            tween->set_parallel(true)->set_trans(Tween::TRANS_QUAD)->set_ease(Tween::EASE_OUT);
            tween->tween_property(effect, "global_transform", rebound_xf, 0.10);
            tween->tween_property(flash.ptr(), "albedo_color", Color(1.0f, 1.0f, 1.0f, 0.38f), 0.10);
            tween->chain()->tween_property(effect, "global_transform", transform, 0.09);
            tween->parallel()->tween_property(flash.ptr(), "albedo_color", Color(1.0f, 1.0f, 1.0f, 0.0f), 0.09);
            if (built_id && !destroyed && built_world) {
                tween->chain()->tween_callback(
                    callable_mp(built_world, &BuiltWorld::set_placement_animated).bind(built_id, false));
                tween->chain()->tween_interval(0.03);
            } else {
                tween->chain()->tween_property(effect, "transparency", 1.0f, 0.04);
            }
            tween->chain()->tween_callback(Callable(effect, "queue_free"));
        }

        // Sync crack overlay to squish and rebound with the hit block
        if (!destroyed && break_kind != 0) {
            const int idx = find_damaged_target(break_kind, break_cell, break_id);
            MeshInstance3D *crack = (idx >= 0) ? damaged_targets[size_t(idx)].overlay : nullptr;
            if (crack && crack->is_visible()) {
                const Transform3D base_crack = transform * Transform3D(Basis().scaled(Vector3(1.008f, 1.008f, 1.008f)),
                                                                       Vector3(-0.004f, -0.004f, -0.004f));
                const Transform3D squish_crack = squish_xf * Transform3D(Basis().scaled(Vector3(1.010f, 1.010f, 1.010f)),
                                                                         Vector3(-0.005f, -0.005f, -0.005f));
                const Transform3D rebound_crack = rebound_xf * Transform3D(Basis().scaled(Vector3(1.008f, 1.008f, 1.008f)),
                                                                           Vector3(-0.004f, -0.004f, -0.004f));
                crack->set_global_transform(squish_crack);
                auto ctween = crack->create_tween();
                if (ctween.is_valid()) {
                    ctween->set_trans(Tween::TRANS_QUAD)->set_ease(Tween::EASE_OUT);
                    ctween->tween_property(crack, "global_transform", rebound_crack, 0.10);
                    ctween->chain()->tween_property(crack, "global_transform", base_crack, 0.09);
                }
            }
        }
    }

    // 2. Impact particles: block-coloured chips + a few white sparks; a full
    // burst with a dust puff when the block breaks. CPUParticles3D keeps each
    // effect in one node instead of a dozen tweened meshes.
    const Color chip = block_particle_color(block, pos);
    const Vector3 impact = hit_point + n * 0.04f;
    if (destroyed) {
        const Vector3 center = transform.xform(Vector3(0.5f, 0.5f, 0.5f));
        spawn_particles(chip, center, Vector3(0.0f, 1.0f, 0.0f) + n * 0.4f, 26, 3.6f, 75.0f, 0.105f, 0.85f,
                        Vector3(0.36f, 0.36f, 0.36f), 0);
        spawn_particles(chip.lerp(Color(1, 1, 1), 0.45f), center, Vector3(0.0f, 1.0f, 0.0f), 9, 0.9f, 90.0f, 0.22f, 0.7f,
                        Vector3(0.3f, 0.25f, 0.3f), 2);
        spawn_particles(Color(1, 1, 1), impact, n, 8, 4.6f, 55.0f, 0.032f, 0.2f, Vector3(0.05f, 0.05f, 0.05f), 1);
    } else {
        spawn_particles(chip, impact, n + Vector3(0.0f, 0.35f, 0.0f), 7, 2.9f, 48.0f, 0.07f, 0.5f,
                        Vector3(0.06f, 0.06f, 0.06f), 0);
        spawn_particles(Color(1, 1, 1), impact, n, 10, 4.8f, 58.0f, 0.03f, 0.2f, Vector3(0.04f, 0.04f, 0.04f), 1);
    }
    if (crosshair)
        crosshair->pulse(destroyed ? 1.0f : 0.6f);
    if (player)
        player->add_impact_kick(destroyed ? 0.85f : 0.55f);
}

Color AltariaGame::block_particle_color(altaria::BlockId block, const Vector3 &pos) {
    if (!world || block == 0)
        return Color(0.6f, 0.6f, 0.6f);
    const auto &reg = world->registry();
    const auto &def = reg.get(block);
    Color base;
    auto cached = particle_color_cache.find(int(block));
    if (cached != particle_color_cache.end()) {
        base = cached->second;
    } else {
        // Average the block texture once: chips read as "the same material"
        // without per-particle texturing.
        base = Color(def.color.r, def.color.g, def.color.b);
        if (def.texture_layer >= 0 && def.texture_layer < int(reg.textures.size()) &&
            !reg.textures[def.texture_layer].path.empty()) {
            Ref<Texture2D> tex = ResourceLoader::get_singleton()->load(String(reg.textures[def.texture_layer].path.c_str()));
            Ref<Image> img = tex.is_valid() ? tex->get_image() : Ref<Image>();
            if (img.is_valid() && !img->is_empty()) {
                if (img->is_compressed())
                    img->decompress();
                double r = 0, g = 0, b = 0, w = 0;
                const int iw = img->get_width(), ih = img->get_height();
                for (int y = 0; y < ih; y += std::max(1, ih / 16))
                    for (int x = 0; x < iw; x += std::max(1, iw / 16)) {
                        const Color c = img->get_pixel(x, y);
                        if (c.a < 0.5f)
                            continue;
                        r += c.r;
                        g += c.g;
                        b += c.b;
                        w += 1.0;
                    }
                if (w > 0.0)
                    base = Color(float(r / w), float(g / w), float(b / w));
            }
        }
        particle_color_cache[int(block)] = base;
    }
    if (def.tintable) {
        altaria::RGB tint = def.color;
        if (block == reg.grass && world->generator())
            tint = world->generator()->column(int(std::floor(pos.x)), int(std::floor(pos.z))).grass_tint;
        base = Color(base.r * tint.r, base.g * tint.g, base.b * tint.b);
    }
    return base;
}

void AltariaGame::spawn_particles(const Color &color, const Vector3 &origin, const Vector3 &direction, int amount,
                                  float speed, float spread_deg, float size, float lifetime, const Vector3 &emission_box,
                                  int style) {
    if (!stage || amount <= 0)
        return;
    auto *p = memnew(CPUParticles3D);
    p->set_name(style == 1 ? "HitSparks" : (style == 2 ? "BreakDust" : "BlockChips"));
    p->set_one_shot(true);
    p->set_amount(amount);
    p->set_lifetime(lifetime);
    p->set_lifetime_randomness(0.35);
    p->set_explosiveness_ratio(1.0);
    p->set_use_local_coordinates(false);
    p->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    p->set_emission_shape(CPUParticles3D::EMISSION_SHAPE_BOX);
    p->set_emission_box_extents(emission_box);
    const Vector3 dir = direction.length_squared() > 0.0001f ? direction.normalized() : Vector3(0, 1, 0);
    p->set_direction(dir);
    p->set_spread(spread_deg);
    p->set_param_min(CPUParticles3D::PARAM_INITIAL_LINEAR_VELOCITY, speed * 0.55f);
    p->set_param_max(CPUParticles3D::PARAM_INITIAL_LINEAR_VELOCITY, speed);
    p->set_param_min(CPUParticles3D::PARAM_SCALE, 0.6f);
    p->set_param_max(CPUParticles3D::PARAM_SCALE, 1.3f);

    Ref<StandardMaterial3D> mat;
    mat.instantiate();
    mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
    mat->set_specular_mode(BaseMaterial3D::SPECULAR_DISABLED);
    Ref<Curve> scale_curve;
    scale_curve.instantiate();
    Ref<Gradient> initial;
    initial.instantiate();
    if (style == 1) {
        mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        p->set_gravity(Vector3(0.0f, -7.0f, 0.0f));
        p->set_param_min(CPUParticles3D::PARAM_DAMPING, 4.0f);
        p->set_param_max(CPUParticles3D::PARAM_DAMPING, 7.0f);
        scale_curve->add_point(Vector2(0.0f, 1.0f));
        scale_curve->add_point(Vector2(1.0f, 0.0f));
        initial->set_color(0, Color(1.0f, 1.0f, 1.0f));
        initial->set_color(1, Color(0.90f, 0.94f, 1.0f));
    } else if (style == 2) {
        mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        mat->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
        p->set_gravity(Vector3(0.0f, 0.5f, 0.0f));
        p->set_param_min(CPUParticles3D::PARAM_DAMPING, 1.5f);
        p->set_param_max(CPUParticles3D::PARAM_DAMPING, 2.5f);
        scale_curve->add_point(Vector2(0.0f, 0.55f));
        scale_curve->add_point(Vector2(0.4f, 1.0f));
        scale_curve->add_point(Vector2(1.0f, 1.25f));
        Ref<Gradient> fade;
        fade.instantiate();
        fade->set_color(0, Color(1, 1, 1, 0.55f));
        fade->set_color(1, Color(1, 1, 1, 0.0f));
        p->set_color_ramp(fade);
        initial->set_color(0, Color(color.r, color.g, color.b));
        initial->set_color(1, Color(color.r, color.g, color.b).lightened(0.15f));
    } else {
        mat->set_diffuse_mode(BaseMaterial3D::DIFFUSE_TOON);
        p->set_gravity(Vector3(0.0f, -19.0f, 0.0f));
        p->set_param_min(CPUParticles3D::PARAM_DAMPING, 0.5f);
        p->set_param_max(CPUParticles3D::PARAM_DAMPING, 1.5f);
        p->set_particle_flag(CPUParticles3D::PARTICLE_FLAG_ROTATE_Y, true);
        p->set_param_min(CPUParticles3D::PARAM_ANGULAR_VELOCITY, -480.0f);
        p->set_param_max(CPUParticles3D::PARAM_ANGULAR_VELOCITY, 480.0f);
        p->set_param_min(CPUParticles3D::PARAM_ANGLE, 0.0f);
        p->set_param_max(CPUParticles3D::PARAM_ANGLE, 360.0f);
        scale_curve->add_point(Vector2(0.0f, 1.0f));
        scale_curve->add_point(Vector2(0.75f, 0.9f));
        scale_curve->add_point(Vector2(1.0f, 0.0f));
        // Natural variation around the averaged texture colour.
        initial->set_color(0, color.darkened(0.28f));
        initial->set_color(1, color.lightened(0.12f));
    }
    p->set_param_curve(CPUParticles3D::PARAM_SCALE, scale_curve);
    p->set_color_initial_ramp(initial);

    Ref<Mesh> mesh;
    if (style == 2) {
        Ref<QuadMesh> quad;
        quad.instantiate();
        quad->set_size(Vector2(size, size));
        quad->set_material(mat);
        mesh = quad;
    } else {
        Ref<BoxMesh> box;
        box.instantiate();
        box->set_size(Vector3(size, size, size));
        box->set_material(mat);
        mesh = box;
    }
    p->set_mesh(mesh);
    stage->add_child(p, true);
    p->set_global_position(origin);
    p->set_emitting(true);
    auto tween = p->create_tween();
    if (tween.is_valid()) {
        tween->tween_interval(lifetime * 1.4 + 0.1);
        tween->tween_callback(Callable(p, "queue_free"));
    }
}

bool AltariaGame::advance_break_hit(int kind, altaria::IVec3 cell, uint64_t id, altaria::BlockId block, uint8_t shape, const Transform3D &transform) {
    int idx = find_damaged_target(kind, cell, id);
    if (idx < 0) {
        DamagedTarget entry;
        entry.kind = kind;
        entry.cell = cell;
        entry.id = id;
        entry.hits = 1;
        damaged_targets.push_back(entry);
        idx = int(damaged_targets.size()) - 1;
    } else {
        ++damaged_targets[size_t(idx)].hits;
    }
    auto &entry = damaged_targets[size_t(idx)];
    entry.block = block;
    entry.shape = shape;
    entry.transform = transform;
    entry.timer = 4.5f;
    const int max_hits = (world && world->registry().get(block).instant_break) ? 1 : 6;
    if (entry.hits < max_hits) {
        update_damaged_overlay(entry, true);
        return false;
    }
    remove_damaged_target(size_t(idx));
    return true;
}

int AltariaGame::get_break_stage() const {
    if (!world || !player || !player->get_camera() || damaged_targets.empty())
        return 0;
    auto *camera = player->get_camera();
    const Vector3 origin = camera->get_global_position();
    const Vector3 direction = -camera->get_global_basis().get_column(2);
    const auto hit = target_block();
    const auto physical = physics_blocks ? physics_blocks->pick(origin, direction, reach) : PhysicsBlocks::Hit{};
    const auto constructed = built_world ? pick_built(origin, direction, reach) : BuiltWorld::Hit{};
    float res_dist = std::min({hit.hit ? hit.distance : reach, physical.distance,
                               constructed.id ? constructed.distance : reach});
    if (auto resource = world->pick_resource(origin, direction, res_dist)) {
        const Vector3 rp(resource->position[0], resource->position[1], resource->position[2]);
        const IVec3 rcell{int(std::floor(rp.x)), int(std::floor(rp.y)), int(std::floor(rp.z))};
        const int idx = find_damaged_target(3, rcell, 0);
        return idx >= 0 ? damaged_targets[size_t(idx)].hits : 0;
    }
    if (constructed.id && (!hit.hit || constructed.distance < hit.distance) &&
        constructed.distance < physical.distance) {
        const int idx = find_damaged_target(2, {}, constructed.id);
        return idx >= 0 ? damaged_targets[size_t(idx)].hits : 0;
    }
    if (physical.body >= 0 && (!hit.hit || physical.distance < hit.distance)) {
        const uint64_t phys_id = (uint64_t(uint32_t(physical.body)) << 32) | uint32_t(physical.part);
        const int idx = find_damaged_target(4, {}, phys_id);
        return idx >= 0 ? damaged_targets[size_t(idx)].hits : 0;
    }
    if (hit.hit) {
        const int idx = find_damaged_target(1, hit.cell, 0);
        return idx >= 0 ? damaged_targets[size_t(idx)].hits : 0;
    }
    return 0;
}

void AltariaGame::on_hand_break_impact() {
    if (!pending_break_hit)
        return;
    pending_break_hit = false;
    pending_break_hit_timer = 0.0f;
    update_target(true, false);
}

void AltariaGame::trigger_left_hit() {
    if (current_equipped_tool() == "world_wand") {
        handle_wand_action(false);
        return;
    }
    if (first_person_hands) {
        if (player && player->get_camera()) {
            auto *cam = player->get_camera();
            const Vector3 cam_pos = cam->get_global_position();
            const Vector3 cam_dir = -cam->get_global_basis().get_column(2);
            const auto hit = target_block();
            const auto bh = pick_built(cam_pos, cam_dir, reach);
            const auto phys = physics_blocks ? physics_blocks->pick(cam_pos, cam_dir, reach) : PhysicsBlocks::Hit{};
            float res_dist = std::min({hit.hit ? hit.distance : reach, phys.distance,
                                       bh.id ? bh.distance : reach});
            const auto res = world ? world->pick_resource(cam_pos, cam_dir, res_dist) : std::optional<altaria::ResourcePiece>{};
            Vector3 world_target = cam_pos + cam_dir * 3.0f;
            float dist = 3.0f;
            if (res.has_value()) {
                world_target = Vector3(res->position[0], res->position[1], res->position[2]);
                dist = cam_pos.distance_to(world_target);
            } else if (bh.id != 0 && (!hit.hit || bh.distance < hit.distance) && bh.distance < phys.distance) {
                dist = bh.distance;
                world_target = cam_pos + cam_dir * dist;
            } else if (phys.body >= 0 && (!hit.hit || phys.distance < hit.distance)) {
                dist = phys.distance;
                world_target = cam_pos + cam_dir * dist;
            } else if (hit.hit) {
                dist = hit.distance;
                const Vector3 cell_center(hit.cell.x + 0.5f, hit.cell.y + 0.5f, hit.cell.z + 0.5f);
                const Vector3 surf_pt = cam_pos + cam_dir * dist;
                world_target = cell_center * 0.55f + surf_pt * 0.45f;
            }
            last_action_world_pos = world_target;
            last_action_world_timer = 0.56f;
            const Vector2 screen_pt = !cam->is_position_behind(world_target)
                                          ? cam->unproject_position(world_target)
                                          : get_viewport()->get_visible_rect().get_center();
            first_person_hands->call("play_world_action", StringName("break"), screen_pt, dist);
        } else {
            first_person_hands->call("play_action", StringName("break"));
        }
    }
    pending_break_hit = true;
    // Fallback only: the hand animation fires on_hand_break_impact() at its
    // contact frame (~0.20 s); this keeps hits working if the hands are hidden.
    pending_break_hit_timer = first_person_hands && first_person_hands->is_visible_in_tree() ? 0.32f : 0.18f;
}

bool AltariaGame::collect_block_to_hotbar(int block_id, int amount) {
    if (block_id <= 0 || block_id > 4095)
        return false;
    return collect_block(altaria::BlockId(block_id), amount);
}

Array AltariaGame::get_hotbar_slots() const {
    Array out;
    for (int i = 0; i < altaria::HOTBAR_SLOT_COUNT; ++i) {
        const auto &s = inventory.slot(i);
        Dictionary entry;
        entry["slot"] = i;
        entry["block"] = int(s.block);
        entry["tool"] = String(s.tool_id.c_str());
        entry["count"] = s.count;
        entry["shape"] = s.shape_id;
        entry["empty"] = s.empty();
        entry["alt_menu"] = String(s.alt_menu_id.c_str());
        if (s.is_tool()) {
            if (s.tool_id == "world_wand") {
                entry["key"] = String("altaria:world_wand");
                entry["name"] = String(language == "pt_BR" ? "Varinha do Mundo" : "World Wand");
            } else {
                entry["key"] = String(("altaria:stone_" + s.tool_id).c_str());
                entry["name"] = s.tool_id == "pickaxe"
                    ? String(language == "pt_BR" ? "Picareta de Pedra" : "Stone Pickaxe")
                    : String(language == "pt_BR" ? "Machado de Pedra" : "Stone Axe");
            }
        } else if (s.has_block() && world) {
            const auto &b = world->registry().get(s.block);
            entry["key"] = String(b.key.c_str());
            entry["name"] = String((language == "pt_BR" ? b.name_pt : b.name).c_str());
        } else {
            entry["key"] = String();
            entry["name"] = String();
        }
        out.append(entry);
    }
    return out;
}

Dictionary AltariaGame::get_active_hotbar_slot() const {
    Array slots = get_hotbar_slots();
    int sel = inventory.selected();
    if (sel >= 0 && sel < slots.size())
        return slots[sel];
    return Dictionary();
}

int AltariaGame::placement_rotation() const {
    const auto forward = -player->get_camera()->get_global_basis().get_column(2);
    const int facing = std::abs(forward.x) >= std::abs(forward.z) ?
        (forward.x >= 0 ? 0 : 2) : (forward.z >= 0 ? 1 : 3);
    return (facing + shape_rotation) % 4;
}

bool AltariaGame::build_against(Vector3 hit, Vector3 normal, Vector3 source, uint8_t source_shape, uint64_t source_id, bool preview) {
    const auto &held = inventory.active_slot();
    if (held.empty() || (world && !world->registry().get(held.block).placeable)) {
        if (!preview && arc_hotbar)
            arc_hotbar->trigger_shake();
        return false;
    }
    const BlockId block = held.block;
    const int rotation = placement_rotation();
    auto from = shape_bounds(source_shape), to = shape_bounds(pack_shape(selected_shape, rotation));
    Vector3 preferred = source + Vector3((from.first[0] + from.second[0] - to.first[0] - to.second[0]) * .5f,
                                         (from.first[1] + from.second[1] - to.first[1] - to.second[1]) * .5f,
                                         (from.first[2] + from.second[2] - to.first[2] - to.second[2]) * .5f);
    String error;
    BuiltBlock candidate;
    auto id = built_world->attach(hit, normal, preferred, source, source_shape, block,
                                  selected_shape, rotation, error, preview ? &candidate : nullptr);
    if (!id) {
        if (!preview && !error.is_empty())
            print_console(error);
    } else {
        show_placement(preview ? candidate : *built_world->get(id), !preview);
        if (!preview)
            consume_held_block(1);
    }
    return id != 0;
}
bool AltariaGame::release_built(uint64_t id) {
    auto b = built_world ? built_world->get(id) : nullptr;
    if (!b)
        return false;
    auto copy = *b;
    auto p = copy.position();
    unpack_shape(copy.shape(),copy.appearance);
    String error;
    if (!physics_blocks->release_piece(copy.block, copy.appearance, Vector3(p[0], p[1], p[2]), error)) {
        print_console(error);
        return false;
    }
    if (!built_world->remove(id, error)) {
        physics_blocks->discard_last();
        print_console(error);
        return false;
    }
    return true;
}
bool AltariaGame::set_voxel(int x, int y, int z, int block) {
    if (!world || block < 0 || block > 4095)
        return false;
    String error;
    EditCommand cmd{1, ++sequence, {x, y, z}, BlockId(block)};
    if (!world->edit(cmd, error)) {
        print_console(error);
        return false;
    }
    auto appearance=world->appearance_at({x,y,z});
    if (block != world->registry().air && appearance.shape != 255)
        return shape_voxel(x, y, z, "cube", 0);
    return true;
}
int AltariaGame::get_voxel(int x, int y, int z) const {
    return world ? world->block_at(x, y, z) : 0;
}
bool AltariaGame::shape_voxel(int x, int y, int z, const String &name, int rotation) {
    int shape = shape_id(name.utf8().get_data());
    if (!world || shape < 0 || rotation < 0 || rotation > 3)
        return false;
    auto a = world->appearance_at({x, y, z});
    a.shape = shape;
    a.rotation = rotation;
    String error;
    if (!world->edit_surface({1, ++sequence, {x, y, z}, a}, error)) {
        print_console(error);
        return false;
    }
    return true;
}
Dictionary AltariaGame::get_temperature(Vector3 p) const {
    if (!atmosphere || !p.is_finite() || std::abs(p.x) > 1000000 || std::abs(p.z) > 1000000 ||
        std::abs(p.y) > 10000)
        return {};
    auto out = atmosphere->sample_temperature(p);
    float cold=out["cold_delta_c"],warm=out["warm_delta_c"];
    if (physics_blocks) {
        auto influence = physics_blocks->thermal_influence(p);
        cold=std::min(cold,influence.first);
        warm=std::max(warm,influence.second);
    }
    if (built_world) {
        auto influence = built_world->thermal(p);
        cold=std::min(cold,influence.first);
        warm=std::max(warm,influence.second);
    }
    out["feels_like_c"]=float(out["feels_like_c"])+cold+warm-float(out["block_delta_c"]);
    out["block_delta_c"]=cold+warm;
    out["cold_delta_c"]=cold;
    out["warm_delta_c"]=warm;
    return out;
}
bool AltariaGame::release_voxel(int x, int y, int z, bool tree) {
    if (!physics_blocks)
        return false;
    String error;
    if (!physics_blocks->release({x, y, z}, tree, error)) {
        print_console(error);
        return false;
    }
    return true;
}
static bool parse_hex_color(const String &value, uint32_t &out) {
    String hex = value.begins_with("#") ? value.substr(1) : value;
    if (hex.length() != 6)
        return false;
    out = 0;
    for (int i = 0; i < 6; i++) {
        char32_t c = hex[i];
        int v = c >= '0' && c <= '9'   ? int(c - '0')
                : c >= 'a' && c <= 'f' ? int(c - 'a' + 10)
                : c >= 'A' && c <= 'F' ? int(c - 'A' + 10)
                                       : -1;
        if (v < 0)
            return false;
        out = (out << 4) | uint32_t(v);
    }
    return true;
}
bool AltariaGame::paint_voxel(int x, int y, int z, const String &color, double preserve, double brightness) {
    if (!world || !std::isfinite(preserve) || !std::isfinite(brightness) || preserve < 0 || preserve > 1 ||
        brightness < 0 || brightness > 2)
        return false;
    auto a = world->appearance_at({x, y, z});
    if (color == "reset")
        a.painted = false;
    else {
        uint32_t rgb;
        if (!parse_hex_color(color, rgb))
            return false;
        a.tint = rgb;
        a.painted = true;
        a.preserve = uint16_t(std::round(preserve * 1000));
        a.brightness = uint16_t(std::round(brightness * 1000));
    }
    String error;
    if (!world->edit_surface({1, ++sequence, {x, y, z}, a}, error)) {
        print_console(error);
        return false;
    }
    return true;
}
bool AltariaGame::overlay_voxel(int x, int y, int z, const String &key) {
    if (!world)
        return false;
    auto a = world->appearance_at({x, y, z});
    if (key == "clear") {
        a.overlay = 0;
        a.decoration = 0;
    } else if (key == "reset") {
        a.overlay = 65535;
        a.decoration = 65535;
    } else {
        String full = key.contains(":") ? key : String("altaria:") + key;
        auto it = world->registry().overlay_keys.find(full.utf8().get_data());
        if (it == world->registry().overlay_keys.end())
            return false;
        auto &o = world->registry().overlays[it->second];
        if (o.decal)
            a.decoration = o.id;
        else
            a.overlay = o.id;
    }
    String error;
    if (!world->edit_surface({1, ++sequence, {x, y, z}, a}, error)) {
        print_console(error);
        return false;
    }
    return true;
}
Dictionary AltariaGame::get_voxel_appearance(int x, int y, int z) const {
    Dictionary d;
    if (!world)
        return d;
    auto a = world->appearance_at({x, y, z});
    auto rgb = unpack_rgb(a.tint);
    d["painted"] = a.painted;
    d["color"] = String("#") + Color(rgb.r, rgb.g, rgb.b).to_html(false);
    d["preserve"] = a.preserve / 1000.;
    d["brightness"] = a.brightness / 1000.;
    d["overlay_id"] = a.overlay;
    d["decoration_id"] = a.decoration;
    d["original_shape"] = String(a.shape==255 ? "generated" : shape_name(a.shape));
    auto shape = world->shape_at(x, y, z);
    d["shape"] = shape_name(shape_type(shape));
    d["rotation"] = altaria::shape_rotation(shape);
    d["shape_code"] = int(shape);
    auto ramp=ramp_spec(shape);
    if(ramp.valid) {d["ramp_corner"]=ramp_corner_name(ramp.corner);d["ramp_rise"]=ramp.rise();d["ramp_low"]=ramp.low();}
    return d;
}
RayHit AltariaGame::target_block() const {
    if (!world || !player)
        return {};
    auto camera = player->get_camera();
    auto origin = camera->get_global_position();
    auto dir = -camera->get_global_basis().get_column(2);
    bool unknown=false;
    auto hit=raycast_shapes(
        {origin.x, origin.y, origin.z}, {dir.x, dir.y, dir.z}, reach,
        [this,&unknown](int x, int y, int z) {
            if((world->settings().caves.enabled || world->settings().adaptive_streaming) && y>=world->settings().min_y && y<world->settings().max_y && !world->voxel_ready(x,y,z))unknown=true;
            return unknown ? world->registry().air : world->block_at(x,y,z);
        },
        [this](int x, int y, int z) { return world->shape_at(x, y, z); }, world->registry());
    return unknown ? RayHit() : hit;
}
BuiltWorld::Hit AltariaGame::pick_built(Vector3 origin,Vector3 direction,float distance) const {
    auto hit=built_world->pick(origin,direction,distance);
    return hit;
}
void AltariaGame::show_selection(BlockId block, uint8_t shape, const Transform3D &transform) {
    (void)block;
    if(selection_shape!=shape) {
        selection->set_mesh(build_selection_outline_mesh(shape));
        selection_shape = shape;
    }
    // Expand in the selected cell's local space so rotated physical forms fit exactly.
    selection->set_global_transform(transform * Transform3D(Basis().scaled(Vector3(1.006f, 1.006f, 1.006f)),
                                                            Vector3(-.003f, -.003f, -.003f)));
    selection->set_visible(!is_console_open());
}
void AltariaGame::update_target(bool edit, bool place) {
    if (placement_preview && !edit)
        placement_preview->hide();
    if (structure_paste_preview && !edit)
        structure_paste_preview->hide();
    if (!world || !player || !selection || loading_world || is_console_open() || cheat_inventory_open() || (arc_hotbar && arc_hotbar->is_alt_open()))
        return;
    selection_is_physical = false;
    selected_built_id = 0;
    if (!edit && !place && current_equipped_tool() == "world_wand") {
        Vector3i aim_cell;
        altaria::BlockId aim_block = 0;
        bool has_aim_block = false;
        const bool copy_paste_preview = (wand_mode == int(altaria::WandMode::CopyPaste) && structure_clipboard.valid());
        wand_aim_cell(copy_paste_preview, aim_cell, &aim_block, &has_aim_block, nullptr);
        const altaria::BlockId preview_id = (has_aim_block && aim_block != 0) ? aim_block : world->registry().stone;
        show_selection(preview_id, 0, Transform3D(Basis(), Vector3(aim_cell)));
        if (copy_paste_preview && structure_paste_preview) {
            Ref<ImmediateMesh> plines = structure_paste_preview->get_mesh();
            if (plines.is_valid()) {
                plines->clear_surfaces();
                const Vector3i rsize = (wand_paste_rot & 1)
                                           ? Vector3i(structure_clipboard.size.z, structure_clipboard.size.y,
                                                      structure_clipboard.size.x)
                                           : structure_clipboard.size;
                const AABB pbox{Vector3(), Vector3(rsize)};
                structure_paste_preview->set_position(Vector3(aim_cell));
                structure_paste_preview->set_custom_aabb(pbox);
                plines->surface_begin(Mesh::PRIMITIVE_LINES);
                plines->surface_set_color(Color(1.0f, 0.78f, 0.22f, 0.92f));
                for (int i = 0; i < 12; ++i) {
                    Vector3 a, b;
                    pbox.get_edge(i, a, b);
                    plines->surface_add_vertex(a);
                    plines->surface_add_vertex(b);
                }
                plines->surface_end();
                structure_paste_preview->show();
            }
        }
        return;
    }
    const bool holding_placeable = inventory.has_active_block() &&
                                   world->registry().get(inventory.active_slot().block).placeable;
    auto hit = target_block();
    auto camera = player->get_camera();
    auto direction = -camera->get_global_basis().get_column(2);
    auto physical = physics_blocks->pick(camera->get_global_position(), direction, reach);
    auto constructed = pick_built(camera->get_global_position(), direction, reach);
    float resource_distance = std::min({hit.hit ? hit.distance : reach, physical.distance,
                                         constructed.id ? constructed.distance : reach});
    if (!place) {
        if (auto resource = world->pick_resource(camera->get_global_position(), direction, resource_distance)) {
            const Vector3 position(resource->position[0], resource->position[1], resource->position[2]);
            const Vector3 scale(resource->dimensions[0], resource->dimensions[1], resource->dimensions[2]);
            const auto axis = [&](int i) {
                return Vector3(resource->axes[i][0], resource->axes[i][1], resource->axes[i][2]);
            };
            const Basis basis = Basis(axis(0) * scale.x, axis(1) * scale.y, axis(2) * scale.z);
            const Transform3D res_xf(basis, position - basis.xform(Vector3(.5f, .5f, .5f)));
            if (can_break_block(resource->block, false))
                show_selection(resource->block, 0, res_xf);
            else
                selection->hide();
            if (edit) {
                BlockId mined_block = resource->block;
                if (!can_break_block(mined_block, false)) {
                    deny_break(mined_block, false);
                    return;
                }
                const IVec3 res_cell{int(std::floor(position.x)), int(std::floor(position.y)), int(std::floor(position.z))};
                const bool broken = advance_break_hit(3, res_cell, 0, mined_block, 0, res_xf);
                show_block_hit_effect(mined_block, 0, res_xf, position, -direction, 0, broken, 3, res_cell, 0);
                if (!broken)
                    return;
                world->mine_resource(*resource);
                spawn_item_drop(mined_block, position, Appearance(), 1);
                selection->hide();
            }
            return;
        }
    }
    if (constructed.id && (!hit.hit || constructed.distance < hit.distance) &&
        constructed.distance < physical.distance) {
        const auto *piece = built_world->get(constructed.id);
        auto p = piece->position();
        selected_built_id = piece->id;
        Vector3 origin(p[0], p[1], p[2]);
        uint8_t form = piece->shape();
        BlockId piece_block = piece->block;
        const Transform3D piece_xf(Basis(), origin);
        if (can_break_block(piece_block, false) || holding_placeable)
            show_selection(piece_block, form, piece_xf);
        else
            selection->hide();
        if (!edit || place) {
            build_against(camera->get_global_position() + direction * constructed.distance,
                          Vector3(constructed.normal[0], constructed.normal[1], constructed.normal[2]),
                          origin, form, piece->id, !edit);
        } else {
            if (!can_break_block(piece_block, false)) {
                deny_break(piece_block, false);
                return;
            }
            const Vector3 hit_pt = camera->get_global_position() + direction * constructed.distance;
            const Vector3 hit_norm(constructed.normal[0], constructed.normal[1], constructed.normal[2]);
            const bool broken = advance_break_hit(2, {}, constructed.id, piece_block, form, piece_xf);
            show_block_hit_effect(piece_block, form, piece_xf, hit_pt, hit_norm, constructed.id, broken, 2, {}, constructed.id);
            if (!broken)
                return;
            const auto bounds = altaria::shape_bounds(form);
            const Vector3 drop_pos = origin + Vector3((bounds.first[0] + bounds.second[0]) * 0.5f,
                                                      (bounds.first[1] + bounds.second[1]) * 0.5f,
                                                      (bounds.first[2] + bounds.second[2]) * 0.5f);
            const Appearance piece_app = piece->appearance;
            String error;
            if (!built_world->remove(constructed.id, error))
                print_console(error);
            else
                spawn_item_drop(piece_block, drop_pos, piece_app, 1);
        }
        return;
    }
    if (physical.body >= 0 && (!hit.hit || physical.distance < hit.distance)) {
        Transform3D transform;
        BlockId block = 0;
        uint8_t shape;
        if (physics_blocks->selection_for(physical, transform, block, shape) && can_break_block(block, true)) {
            selection_is_physical = true;
            show_selection(block, shape, transform);
        } else
            selection->hide();
        if (edit && !place) {
            if (!can_break_block(block, true)) {
                deny_break(block, true);
                return;
            }
            const uint64_t phys_id = (uint64_t(uint32_t(physical.body)) << 32) | uint32_t(physical.part);
            const Vector3 hit_pt = camera->get_global_position() + direction * physical.distance;
            const bool broken = advance_break_hit(4, {}, phys_id, block, shape, transform);
            show_block_hit_effect(block, shape, transform, hit_pt, -direction, 0, broken, 4, {}, phys_id);
            if (!broken)
                return;
            int bodies_before = int(physics_blocks->stats().get("physics_bodies", 0));
            String error;
            if (!physics_blocks->detach(physical, direction * 2.0f, error))
                print_console(error);
            else if (int(physics_blocks->stats().get("physics_bodies", 0)) < bodies_before)
                spawn_item_drop(block, hit_pt, Appearance(), 1);
        }
        return;
    }
    if (!hit.hit) {
        selection->hide();
        if ((!edit || place) && world->settings().preset == WorldPreset::Structure && inventory.has_active_block() &&
            world->registry().get(inventory.active_slot().block).placeable) {
            const Vector3 cell = (camera->get_global_position() + direction * 4.f).floor();
            BuiltBlock candidate;
            String error;
            const auto id = built_world->place(cell, inventory.active_slot().block, selected_shape, placement_rotation(),
                                               error, 0, edit ? nullptr : &candidate);
            if (id) {
                show_placement(edit ? *built_world->get(id) : candidate, edit);
                if (edit)
                    consume_held_block(1);
            } else if (edit) {
                print_console(error);
            }
        }
        return;
    }
    auto shape = world->shape_at(hit.cell.x, hit.cell.y, hit.cell.z);
    const Transform3D vox_xf(Basis(), Vector3(hit.cell.x, hit.cell.y, hit.cell.z));
    if (can_break_block(hit.block, false) || holding_placeable)
        show_selection(hit.block, shape, vox_xf);
    else
        selection->hide();
    if (!edit || place) {
        if (!inventory.has_active_block() || !world->registry().get(inventory.active_slot().block).placeable) {
            if (edit && arc_hotbar)
                arc_hotbar->trigger_shake();
            return;
        }
        auto position = camera->get_global_position() + direction * hit.distance;
        auto source = Vector3(hit.cell.x, hit.cell.y, hit.cell.z);
        std::array<float, 3> normal{};
        auto local = camera->get_global_position() - source;
        if (shape_ray(shape, {local.x, local.y, local.z}, {direction.x, direction.y, direction.z}, reach, &normal) >= 0)
            build_against(position, Vector3(normal[0], normal[1], normal[2]), source, shape, 0, !edit);
    } else {
        if (!can_break_block(hit.block, false)) {
            deny_break(hit.block, false);
            return;
        }
        const Vector3 hit_pt = camera->get_global_position() + direction * hit.distance;
        const Vector3 source(hit.cell.x, hit.cell.y, hit.cell.z);
        std::array<float, 3> normal{0.f, 1.f, 0.f};
        const Vector3 local = camera->get_global_position() - source;
        shape_ray(shape, {local.x, local.y, local.z}, {direction.x, direction.y, direction.z}, reach, &normal);
        Vector3 hit_norm(normal[0], normal[1], normal[2]);
        if (hit_norm.length_squared() < 0.01f)
            hit_norm = -direction;
        const bool broken = advance_break_hit(1, hit.cell, 0, hit.block, shape, vox_xf);
        show_block_hit_effect(hit.block, shape, vox_xf, hit_pt, hit_norm, 0, broken, 1, hit.cell, 0);
        if (!broken)
            return;
        if (world->registry().get(hit.block).wood)
            release_voxel(hit.cell.x, hit.cell.y, hit.cell.z, true);
        else {
            const Appearance drop_app = world->appearance_at(hit.cell);
            if (set_voxel(hit.cell.x, hit.cell.y, hit.cell.z, world->registry().air))
                spawn_item_drop(hit.block, Vector3(hit.cell.x + 0.5f, hit.cell.y + 0.5f, hit.cell.z + 0.5f), drop_app, 1);
        }
    }
}
void AltariaGame::set_debug_overlay(bool v) {
    debug_overlay_visible = v;
    if (telemetry)
        telemetry->set_visible(v);
    if (hotbar_label)
        hotbar_label->set_visible(v);
}
void AltariaGame::update_hud() {
    if (!world || !player)
        return;
    auto s = world->stats();
    if (loading_world) {
        if (!world->initial_stream_ready()) {
            const int wanted = s["wanted"];
            if (wanted > 0)
                loading_status->set_text(String::num_int64(int(s["tiles"])) + " / " +
                                         String::num_int64(wanted) + " chunks");
            return;
        }
        loading_world = false;
        loading_panel->hide();
        atmosphere->set_loading_world(false);
        set_game_input(!is_console_open());
    }
    if (!generation_reported && double(s["world_ready_ms"]) >= 0) {
        generation_reported = true;
        String report = structure_outline
            ? "Structure ready in " + String::num(double(s["world_ready_ms"]), 1) + " ms"
            : "Generation complete: first tile " + String::num(double(s["first_tile_ms"]), 1) +
                " ms | all LODs " + String::num(double(s["world_ready_ms"]), 1) + " ms";
        print_console(report);
        UtilityFunctions::print("WORLD_STREAM_READY ", report);
    }
    if (arc_hotbar)
        arc_hotbar->set_state(selected_shape, shape_rotation);
    // F3 hides every debug layer, including the controls cheat-sheet that used
    // to stay on screen (and overlap the hands) after the overlay was turned off.
    if (hotbar_label)
        hotbar_label->set_visible(debug_overlay_visible);
    if (!debug_overlay_visible) {
        if (telemetry) telemetry->set_visible(false);
        return;
    }
    if (telemetry) telemetry->set_visible(true);

    Vector3 p = player->get_position();
    String hud_text = "";

    // [PERFORMANCE]
    if (debug_perf_panel) {
        int fps = Engine::get_singleton()->get_frames_per_second();
        double frame_time = fps > 0 ? (1000.0 / fps) : 0.0;
        int tris = int(s.get("triangles", 0));
        hud_text += "[PERFORMANCE] " + String::num_int64(fps) + " FPS (" + String::num(frame_time, 2) + " ms) | " +
                    "Worst: " + String::num(double(s.get("frame_worst_ms", 0.0)), 1) + " ms, p99 " +
                    String::num(double(s.get("frame_p99_ms", 0.0)), 1) + " ms | " +
                    "Draw Tris: " + String::num_int64(tris) + " | Mesh: " + String::num(double(s["mesh_ms"]), 1) + " ms\n";
    }

    // [WORLD]
    if (debug_world_panel) {
        auto column = world->generator()->column(int(std::floor(p.x)), int(std::floor(p.z)));
        auto temperature = get_temperature(p);
        auto climate = atmosphere->state();
        hud_text += "[WORLD] " + String(metadata.get("name", "World")) + " (Seed: " +
                    String::num_int64(int64_t(metadata.get("seed", 0))) + ") | Pos: " +
                    String::num(p.x, 1) + ", " + String::num(p.y, 1) + ", " + String::num(p.z, 1) +
                    " | Biome: " + String(world->settings().biomes[column.biome].name.c_str()) +
                    " | Temp: " + String::num(float(temperature["feels_like_c"]), 1) + " °C | Time: " +
                    String::num(float(climate["hour"]), 1) + " h (" + String(climate["weather"]) + ")\n";
    }

    // [CHUNKS]
    if (debug_chunks_panel) {
        hud_text += "[CHUNKS] Active: " + String::num_int64(int(s["tiles"])) + " | LOD [0..5]: [" +
                    String::num_int64(int(s["lod0"])) + ", " + String::num_int64(int(s["lod1"])) + ", " +
                    String::num_int64(int(s["lod2"])) + ", " + String::num_int64(int(s["lod3"])) + ", " +
                    String::num_int64(int(s["lod4"])) + ", " + String::num_int64(int(s["lod5"])) + "] | Queue: " +
                    String::num_int64(int(s["pending"])) + " | Ready: " +
                    (double(s["world_ready_ms"]) >= 0 ? String::num(double(s["world_ready_ms"]), 1) + " ms" : String("loading...")) + "\n";
    }

    // [PHYSICS]
    if (debug_physics_panel) {
        int bodies = int(physics_blocks->stats()["physics_bodies"]);
        int colliders = int(s.get("terrain_colliders", 0));
        hud_text += "[PHYSICS] Mode: " + player->get_movement_state_name() +
                    " (" + String::num(player->get_horizontal_speed(), 1) + " m/s)" +
                    (player->is_noclip() ? " [NOCLIP]" : "") + (player->is_god() ? " [GOD]" : "") +
                    " | Jolt Bodies: " + String::num_int64(bodies) +
                    " | Chunk Colliders: " + String::num_int64(colliders) +
                    " | Fullbright: " + (world->is_fullbright() ? String("ON") : String("OFF"));
    }

    telemetry->set_text(hud_text);

    if (arc_hotbar)
        arc_hotbar->set_state(selected_shape, shape_rotation);

    const char *move_hint = player->is_flying()
        ? (language == "pt_BR"
               ? "WASD mover | Shift acelerar | Espaço / Ctrl subir/descer | Esc/F1 console"
               : "WASD move | Shift boost | Space / Ctrl up/down | Esc/F1 console")
        : (language == "pt_BR"
               ? "WASD mover | Shift correr | Espaço pular | C/Ctrl agachar/deslizar | Esc/F1 console"
               : "WASD move | Shift sprint | Space jump | C/Ctrl crouch/slide | Esc/F1 console");
    String build_hint = language == "pt_BR"
        ? "1-9 / Roda: slot | Segure Alt: formas | Q: girar | G: física | M: mapa | F3: HUD"
        : "1-9 / Wheel: slot | Hold Alt: shapes | Q: rotate | G: physics | M: map | F3: HUD";
    hotbar_label->set_text(
        build_hint + "\n" + String(move_hint) +
        (cheat_enabled ? "\nT: Build Mode" : "") + structure_selection_status());
}
void AltariaGame::_process(double delta) {
    if (!world || !player)
        return;
    {
        const Vector3 fcenter = player->get_camera()
                                    ? player->get_camera()->get_global_position()
                                    : (player->get_position() + Vector3(0.0f, 1.3f, 0.0f));
        world->set_player_foliage_fade(fcenter, 1.0f - 0.72f * player->get_foliage_factor());
    }
    if (!highlight_query.empty()) {
        // Keep the highlight in sync while building/breaking.
        const uint64_t rev = world->world_revision() ^ (built_world ? built_world->construction().revision * 1315423911ULL : 0);
        if (rev != highlight_revision || player->get_position().distance_to(highlight_center) > kHighlightRadius * 0.5f)
            rebuild_highlight();
    }
    if (first_person_hands) {
        const auto &active_slot = inventory.active_slot();
        if (!console_tool_override) {
            const std::string wanted_tool = active_slot.is_tool() ? active_slot.tool_id : std::string();
            if (wanted_tool != hand_equipped_tool) {
                hand_equipped_tool = wanted_tool;
                first_person_hands->call("set_tool", String("right"), String(wanted_tool.c_str()));
            }
        }
        if (first_person_hands->is_visible_in_tree()) {
            first_person_hands->call("set_running", player->is_sprinting());
            const int cur_block = active_slot.has_block() ? int(active_slot.block) : 0;
            const int cur_shape = selected_shape;
            const int cur_rot = shape_rotation;
            if (cur_block != hand_held_block || cur_shape != hand_held_shape || cur_rot != hand_held_rot) {
                hand_held_block = cur_block;
                hand_held_shape = cur_shape;
                hand_held_rot = cur_rot;
                Ref<ArrayMesh> held_mesh;
                if (cur_block > 0 && world) {
                    Appearance a;
                    const uint8_t packed = cur_shape < 8 ? pack_shape(cur_shape, cur_rot) : uint8_t(cur_shape);
                    unpack_shape(packed, a);
                    const Vector3 ppos = player->get_position();
                    const Column col = world->generator()
                                           ? world->generator()->column(int(std::floor(ppos.x)), int(std::floor(ppos.z)))
                                           : Column();
                    // Physical material samples textures in local space: the world-space
                    // material smeared one stretched texel across the tiny held cube.
                    held_mesh = world->block_mesh({{{0, 0, 0}, BlockId(cur_block)}}, {a}, col, true);
                }
                first_person_hands->call("set_held_block", held_mesh, cur_block, cur_shape, cur_rot);
            }
            const Vector3 cur_pos = player->get_position();
            const float dt_safe = std::max(float(delta), 0.0005f);
            const Vector3 vel = prev_player_pos_valid ? (cur_pos - prev_player_pos) / dt_safe : Vector3();
            prev_player_pos = cur_pos;
            prev_player_pos_valid = true;
            if (auto *cam = player->get_camera()) {
                if (atmosphere) {
                    first_person_hands->call("set_environment_light",
                                             atmosphere->sample_hand_light(cam->get_global_position()));
                }
                const Vector3 cam_local_pos = cam->get_position();
                const Vector3 cam_rot(cam->get_rotation().x, player->get_rotation().y, cam->get_rotation().z);
                first_person_hands->call("update_motion_state",
                                         player->get_movement_state_name(),
                                         player->get_horizontal_speed(),
                                         vel.y,
                                         cam_local_pos,
                                         cam_rot,
                                         player->is_climbing());
                if (player->is_climbing() || player->is_mantling()) {
                    Vector3 wnorm = player->get_wall_normal();
                    if (wnorm.length_squared() < 0.01f) {
                        wnorm = cam->get_global_basis().get_column(2);
                        wnorm.y = 0.0f;
                        if (wnorm.length_squared() > 0.001f)
                            wnorm = wnorm.normalized();
                        else
                            wnorm = Vector3(0, 0, 1);
                    }
                    Vector3 wall_right = Vector3(0.0f, 1.0f, 0.0f).cross(wnorm);
                    if (wall_right.length_squared() > 0.001f)
                        wall_right = wall_right.normalized();
                    else
                        wall_right = Vector3(1, 0, 0);

                    Vector3 world_hold_L, world_hold_R;
                    bool reaching_L = false, reaching_R = false;
                    if (player->is_mantling()) {
                        climb_active = false;
                        const Vector3 ledge = player->get_mantle_end();
                        world_hold_L = Vector3(ledge.x, ledge.y + 0.05f, ledge.z) - wall_right * 0.24f;
                        world_hold_R = Vector3(ledge.x, ledge.y + 0.05f, ledge.z) + wall_right * 0.24f;
                    } else {
                        Vector3 grips[2];
                        bool reach[2];
                        update_climb_hands(float(delta), cam->get_global_position(), wnorm, wall_right,
                                           cur_pos - wnorm * 0.46f, vel.y, grips, reach);
                        world_hold_L = grips[0];
                        world_hold_R = grips[1];
                        reaching_L = reach[0];
                        reaching_R = reach[1];
                    }
                    const Rect2 vrect = get_viewport()->get_visible_rect();
                    const Vector2 fallback_L(vrect.size.x * 0.36f, vrect.size.y * 0.54f);
                    const Vector2 fallback_R(vrect.size.x * 0.64f, vrect.size.y * 0.54f);
                    const Vector2 scr_L = !cam->is_position_behind(world_hold_L)
                        ? cam->unproject_position(world_hold_L)
                        : fallback_L;
                    const Vector2 scr_R = !cam->is_position_behind(world_hold_R)
                        ? cam->unproject_position(world_hold_R)
                        : fallback_R;
                    first_person_hands->call("update_climb_world_holds", scr_L, scr_R, reaching_L, reaching_R, true);
                } else {
                    climb_active = false;
                    first_person_hands->call("update_climb_world_holds", Vector2(), Vector2(), false, false, false);
                }
                if (last_action_world_timer > 0.0f) {
                    last_action_world_timer = std::max(0.0f, last_action_world_timer - float(delta));
                    if (!cam->is_position_behind(last_action_world_pos)) {
                        first_person_hands->call("update_world_target",
                                                 cam->unproject_position(last_action_world_pos),
                                                 true);
                    }
                }
            }
        }
    }
    if (arc_hotbar) {
        arc_hotbar->set_state(selected_shape, shape_rotation);
        refresh_wand_hud();
        if (arc_hotbar->is_alt_open() &&
            !Input::get_singleton()->is_physical_key_pressed(Key::KEY_ALT) &&
            !Input::get_singleton()->is_key_pressed(Key::KEY_ALT)) {
            confirm_alt_selection();
        }
        if (diorama_minimap) {
            Vector2 mc;
            float mr = 104.0f;
            arc_hotbar->minimap_rect(mc, mr);
            float map_alpha = arc_hotbar->is_alt_open() ? 0.0f : arc_hotbar->get_idle_alpha();
            diorama_minimap->set_placement(mc, mr, map_alpha);
        }
    }
    if (crosshair) {
        const bool map_open = diorama_minimap && diorama_minimap->is_full_map_open();
        crosshair->set_visible(!map_open);
        crosshair_sample_timer -= float(delta);
        // The contrast probe repeats the target raycasts; 20 Hz is plenty for
        // a value that is smoothed over ~60 ms anyway.
        if (!map_open && player && player->get_camera() && crosshair_sample_timer <= 0.0f) {
            crosshair_sample_timer = 0.05f;
            const float sun_energy = sun ? sun->get_param(Light3D::PARAM_ENERGY) : 1.0f;
            const float sun_factor = std::clamp(0.35f + 0.75f * sun_energy, 0.25f, 1.15f);
            const auto camera = player->get_camera();
            const Vector3 cam_pos = camera->get_global_position();
            const Vector3 cam_dir = -camera->get_global_basis().get_column(2);
            const auto hit = target_block();
            const auto bh = pick_built(cam_pos, cam_dir, reach);
            float luma = 0.0f;
            if (bh.id != 0 && (!hit.hit || bh.distance < hit.distance)) {
                if (const BuiltBlock *bb = built_world ? built_world->get(bh.id) : nullptr) {
                    RGB rgb = bb->appearance.painted ? unpack_rgb(bb->appearance.tint)
                                                     : world->registry().get(bb->block).color;
                    if (!bb->appearance.painted && bb->block == world->registry().grass && world->generator()) {
                        rgb = world->generator()->column(int(std::floor(bb->position()[0])),
                                                         int(std::floor(bb->position()[2]))).grass_tint;
                    }
                    luma = (0.299f * rgb.r + 0.587f * rgb.g + 0.114f * rgb.b) * sun_factor;
                }
            } else if (hit.hit) {
                const auto app = world->appearance_at(hit.cell);
                RGB rgb = app.painted ? unpack_rgb(app.tint) : world->registry().get(hit.block).color;
                if (!app.painted && hit.block == world->registry().grass && world->generator()) {
                    rgb = world->generator()->column(hit.cell.x, hit.cell.z).grass_tint;
                }
                luma = (0.299f * rgb.r + 0.587f * rgb.g + 0.114f * rgb.b) * sun_factor;
            } else {
                // Aiming at sky or clouds: bright daytime sky/clouds darken the dot crosshair.
                const float sky_lift = std::clamp(0.55f + 0.45f * std::max(0.0f, cam_dir.y + 0.15f), 0.0f, 1.0f);
                luma = 0.78f * sun_factor * sky_lift;
            }
            const float target_dark = std::clamp((luma - 0.56f) / 0.22f, 0.0f, 1.0f);
            crosshair->set_target_darkness(target_dark);
        }
    }
    if (game_log.is_valid())
        game_log->sample(delta, *world);
    ALTARIA_PROFILE("game.process");
    world->update_viewer(player->get_position());
    built_world->update_viewer(player->get_position());
    physics_blocks->update_viewer(player->get_position());
    hud_timer += delta;
    if (hud_timer >= hud_interval || (loading_world && world->initial_stream_ready())) {
        hud_timer = 0;
        ALTARIA_PROFILE("game.hud");
        update_hud();
        if(cave_map_enabled)update_cave_map();
    }

    if (lmb_cooldown_timer > 0.0f)
        lmb_cooldown_timer = std::max(0.0f, lmb_cooldown_timer - float(delta));
    if (grass_hit_anim.active) {
        grass_hit_anim.elapsed += float(delta);
        const float t = grass_hit_anim.elapsed;
        const float total = grass_hit_anim.destroyed ? 0.16f : 0.19f;
        if (t >= total) {
            grass_hit_anim.active = false;
            grass_hit_anim.apply_grass = false;
            grass_hit_anim.apply_terrain = false;
            world->set_grass_hit(Vector3(), Vector3(-1.0f, -1.0f, -1.0f), 0.0f, Vector3(), Vector3(1.0f, 1.0f, 1.0f));
            world->set_terrain_hit(Vector3(), Vector3(-1.0f, -1.0f, -1.0f), 0.0f);
        } else if (grass_hit_anim.destroyed) {
            Vector3 scale;
            float flash = 0.9f;
            if (t < 0.05f) {
                float u = t / 0.05f;
                float q = 1.0f - (1.0f - u) * (1.0f - u);
                scale = Vector3(1, 1, 1).lerp(Vector3(1.12f, 1.12f, 1.12f), q);
            } else {
                float u = (t - 0.05f) / 0.11f;
                float q = u * u;
                scale = Vector3(1.12f, 1.12f, 1.12f).lerp(Vector3(0.08f, 0.08f, 0.08f), q);
                flash = Math::lerp(0.9f, 0.2f, q);
            }
            if (grass_hit_anim.apply_grass) {
                Vector3 shift(0.0f, (scale.y - 1.0f) * grass_hit_anim.top_height * 0.5f, 0.0f);
                world->set_grass_hit(grass_hit_anim.box_min, grass_hit_anim.box_max, flash, shift, scale);
            }
        } else {
            Vector3 scale;
            Vector3 k;
            float flash;
            if (t < 0.10f) {
                float u = t / 0.10f;
                float q = 1.0f - (1.0f - u) * (1.0f - u);
                scale = grass_hit_anim.squish.lerp(grass_hit_anim.rebound, q);
                k = grass_hit_anim.knock.lerp(Vector3(), q);
                flash = Math::lerp(0.90f, 0.38f, q);
            } else {
                float u = (t - 0.10f) / 0.09f;
                float q = 1.0f - (1.0f - u) * (1.0f - u);
                scale = grass_hit_anim.rebound.lerp(Vector3(1.0f, 1.0f, 1.0f), q);
                flash = Math::lerp(0.38f, 0.0f, q);
            }
            if (grass_hit_anim.apply_grass) {
                Vector3 shift(k.x, (scale.y - 1.0f) * grass_hit_anim.top_height + k.y, k.z);
                world->set_grass_hit(grass_hit_anim.box_min, grass_hit_anim.box_max, flash, shift, scale);
            }
            if (grass_hit_anim.apply_terrain)
                world->set_terrain_hit(grass_hit_anim.terrain_min, grass_hit_anim.terrain_max, flash);
        }
    }
    update_dropped_items(float(delta));
    update_mount_input(float(delta));
    creatures.update(float(delta), player->get_position());
    sync_rider();
    if (pending_break_hit) {
        pending_break_hit_timer -= float(delta);
        if (pending_break_hit_timer <= 0.0f) {
            pending_break_hit = false;
            pending_break_hit_timer = 0.0f;
            update_target(true, false);
        }
    }
    for (size_t i = damaged_targets.size(); i > 0; --i) {
        size_t idx = i - 1;
        auto &entry = damaged_targets[idx];
        const bool target_gone =
            (entry.kind == 1 && world &&
             world->block_at(entry.cell.x, entry.cell.y, entry.cell.z) == world->registry().air) ||
            (entry.kind == 2 && (!built_world || !built_world->get(entry.id)));
        if (target_gone) {
            remove_damaged_target(idx);
            continue;
        }
        entry.timer -= float(delta);
        if (entry.timer <= 0.0f) {
            --entry.hits;
            if (entry.hits > 0) {
                entry.timer = 2.0f;
                update_damaged_overlay(entry, false);
            } else {
                remove_damaged_target(idx);
            }
        }
    }
    if (!loading_world && !is_console_open() && !cheat_inventory_open() && !is_alt_inventory_open() &&
        !is_full_map_open() && player && player->is_physics_processing()) {
        auto *inp = Input::get_singleton();
        const bool left_down = lmb_held && inp && inp->is_mouse_button_pressed(MouseButton::MOUSE_BUTTON_LEFT);
        const bool right_down = rmb_held && inp && inp->is_mouse_button_pressed(MouseButton::MOUSE_BUTTON_RIGHT);
        if (!left_down)
            lmb_held = false;
        if (!right_down)
            rmb_held = false;
        if (left_down) {
            lmb_repeat_timer -= float(delta);
            if (lmb_repeat_timer <= 0.0f && lmb_cooldown_timer <= 0.0f) {
                lmb_cooldown_timer = 0.56f;
                lmb_repeat_timer = 0.56f;
                trigger_left_hit();
            }
        } else {
            lmb_repeat_timer = 0.0f;
        }
        if (right_down) {
            rmb_repeat_timer -= float(delta);
            if (rmb_repeat_timer <= 0.0f) {
                rmb_repeat_timer = 0.20f;
                update_target(true, true);
            }
        } else {
            rmb_repeat_timer = 0.0f;
        }
    } else {
        lmb_held = false;
        rmb_held = false;
        lmb_repeat_timer = 0.0f;
        rmb_repeat_timer = 0.0f;
    }

    selection_timer+=delta;
    const auto pose=player->get_camera()->get_global_transform();
    if(pose!=selection_camera || selection_is_physical || selection_timer>=1.0/30.0) {
        selection_camera=pose;
        selection_timer=0.0;
        ALTARIA_PROFILE("game.target");
        update_target();
    }
}
void AltariaGame::update_climb_hands(float dt, const Vector3 &eye, const Vector3 &wall_normal, const Vector3 &wall_right,
                                     const Vector3 &wall_point, float vertical_speed, Vector3 out_world[2],
                                     bool out_reaching[2]) {
    // Grips snap to the top edge of the block row they land on, slightly off the wall face.
    const auto snap_hold = [&](float y, float side) {
        const float edge = std::floor(y) + 0.9f;
        Vector3 p = wall_point + wall_right * (0.27f * side) + wall_normal * 0.03f;
        p.y = edge;
        return p;
    };
    const float sides[2] = {-1.0f, 1.0f};
    if (!climb_active) {
        climb_active = true;
        climb_hands[0] = {snap_hold(eye.y + 0.35f, -1.0f), Vector3(), 1.0f, false};
        climb_hands[1] = {snap_hold(eye.y - 0.45f, 1.0f), Vector3(), 1.0f, false};
        if (std::abs(climb_hands[0].hold.y - climb_hands[1].hold.y) < 0.5f)
            climb_hands[1].hold.y -= 1.0f;
    }
    const bool any_reaching = climb_hands[0].reaching || climb_hands[1].reaching;
    if (!any_reaching) {
        // Pick the hand that has fallen out of comfortable reach; the other keeps holding.
        int mover = -1;
        Vector3 target;
        const float rel0 = climb_hands[0].hold.y - eye.y, rel1 = climb_hands[1].hold.y - eye.y;
        const int low = rel0 < rel1 ? 0 : 1, high = 1 - low;
        if (vertical_speed > -0.2f && std::min(rel0, rel1) < -0.70f) {
            mover = low;
            const float other = climb_hands[high].hold.y;
            target = snap_hold(std::max(eye.y + 0.45f, other + 0.55f), sides[mover]);
        } else if (vertical_speed < 0.2f && std::max(rel0, rel1) > 0.85f) {
            mover = high;
            const float other = climb_hands[low].hold.y;
            target = snap_hold(std::min(eye.y - 0.55f, other - 0.45f), sides[mover]);
        } else {
            for (int i = 0; i < 2; ++i) {
                const float lateral = (climb_hands[i].hold - wall_point).dot(wall_right);
                const float depth = std::abs((climb_hands[i].hold - wall_point).dot(wall_normal));
                if (std::abs(lateral - 0.27f * sides[i]) > 0.65f || depth > 0.6f) {
                    mover = i;
                    target = snap_hold(climb_hands[i].hold.y, sides[i]);
                    break;
                }
            }
        }
        if (mover >= 0) {
            auto &h = climb_hands[mover];
            h.from = h.hold;
            h.hold = target;
            h.t = 0.0f;
            h.reaching = true;
        }
    }
    for (int i = 0; i < 2; ++i) {
        auto &h = climb_hands[i];
        if (h.reaching) {
            h.t = std::min(1.0f, h.t + dt / 0.20f);
            if (h.t >= 1.0f)
                h.reaching = false;
        }
        if (h.reaching) {
            const float e = 1.0f - (1.0f - h.t) * (1.0f - h.t) * (1.0f - h.t);
            const float arc = std::sin(h.t * 3.14159265f);
            // Lift off the wall and swing up in an arc before latching onto the next edge.
            out_world[i] = h.from.lerp(h.hold, e) + wall_normal * (0.20f * arc) + Vector3(0.0f, 0.10f * arc, 0.0f);
        } else {
            out_world[i] = h.hold;
        }
        out_reaching[i] = h.reaching;
    }
}

void AltariaGame::_input(const Ref<InputEvent> &event) {
    Ref<InputEventKey> key = event;
    if (key.is_valid()) {
        Key k = key->get_keycode() == Key::KEY_NONE ? key->get_physical_keycode() : key->get_keycode();
        if (!key->is_pressed()) {
            if (k == Key::KEY_ALT && is_alt_inventory_open()) {
                confirm_alt_selection();
                get_viewport()->set_input_as_handled();
            }
            return;
        }
        if (key->is_echo())
            return;
        if (cheat_inventory_open()) {
            if (k == Key::KEY_ESCAPE) {
                close_cheat_inventory();
                get_viewport()->set_input_as_handled();
            }
            return;
        }
        if (is_full_map_open()) {
            if (k == Key::KEY_M || k == Key::KEY_ESCAPE) {
                diorama_minimap->set_full_map_open(false);
            } else if (k == Key::KEY_R || k == Key::KEY_HOME) {
                diorama_minimap->recenter_full_map();
            } else if (k == Key::KEY_EQUAL || k == Key::KEY_KP_ADD) {
                diorama_minimap->zoom_full_map(0.82f);
            } else if (k == Key::KEY_MINUS || k == Key::KEY_KP_SUBTRACT) {
                diorama_minimap->zoom_full_map(1.0f / 0.82f);
            }
            get_viewport()->set_input_as_handled();
            return;
        }
        if (k == Key::KEY_F3 && world) {
            set_debug_overlay(!is_debug_overlay());
            get_viewport()->set_input_as_handled();
            return;
        }
        if (!world || loading_world || is_console_open())
            return;
        if (k == Key::KEY_M) {
            toggle_full_map();
            get_viewport()->set_input_as_handled();
            return;
        }
        if (k == Key::KEY_ALT) {
            if (world && arc_hotbar) {
                const auto &s = inventory.active_slot();
                if (current_equipped_tool() == "world_wand") {
                    arc_hotbar->open_alt_menu("wand_modes", wand_mode);
                } else if (inventory.has_active_block() && world->registry().get(s.block).placeable) {
                    arc_hotbar->open_alt_menu(s.alt_menu_id, selected_shape);
                }
                if (arc_hotbar->is_alt_open()) {
                    lmb_held = false;
                    rmb_held = false;
                }
            }
            get_viewport()->set_input_as_handled();
            return;
        }
        if (k == Key::KEY_Z && key->is_ctrl_pressed() && !is_alt_inventory_open()) {
            const String msg = undo_structure();
            if (arc_hotbar)
                arc_hotbar->show_wand_feedback(msg);
            print_console(msg);
            get_viewport()->set_input_as_handled();
            return;
        }
        auto key_digit_index = [](Key code) -> int {
            if (code >= Key::KEY_1 && code <= Key::KEY_9)
                return int(code) - int(Key::KEY_1);
            if (code >= Key::KEY_KP_1 && code <= Key::KEY_KP_9)
                return int(code) - int(Key::KEY_KP_1);
            return -1;
        };
        if (is_alt_inventory_open()) {
            if (k == Key::KEY_ESCAPE) {
                close_alt_inventory();
                get_viewport()->set_input_as_handled();
                return;
            }
            int alt_idx = key_digit_index(k);
            if (alt_idx < 0)
                alt_idx = key_digit_index(key->get_physical_keycode());
            if (alt_idx >= 0) {
                if (arc_hotbar->set_alt_option(alt_idx))
                    sync_alt_selection();
                get_viewport()->set_input_as_handled();
                return;
            }
        }
        if (k == Key::KEY_T) {
            if (current_equipped_tool() == "world_wand") {
                if (wand_mode == int(WandMode::Fill) || wand_mode == int(WandMode::Walls) ||
                    wand_mode == int(WandMode::Hollow)) {
                    show_wand_block_picker(CheatPickerPurpose::WandFill);
                    get_viewport()->set_input_as_handled();
                    return;
                }
                if (wand_mode == int(WandMode::Replace)) {
                    show_wand_block_picker(CheatPickerPurpose::WandReplaceFrom);
                    get_viewport()->set_input_as_handled();
                    return;
                }
            }
            if (cheat_enabled) {
                show_cheat_inventory();
                get_viewport()->set_input_as_handled();
                return;
            }
        }
        if (k == Key::KEY_F && fly_hotkey_enabled && player) {
            player->set_flying(!player->is_flying());
            get_viewport()->set_input_as_handled();
            return;
        }
        if (k == Key::KEY_N && noclip_hotkey_enabled && player) {
            const bool next = !player->is_noclip();
            player->set_noclip(next);
            if (next && !player->is_flying())
                player->set_flying(true);
            get_viewport()->set_input_as_handled();
            return;
        }
        if (structure_outline && (k == Key::KEY_V || k == Key::KEY_B)) {
            print_console(mark_structure_corner(k == Key::KEY_V ? 0 : 1, {}));
            get_viewport()->set_input_as_handled();
            return;
        }
        if (k == Key::KEY_E && player && world) {
            // E: mount the creature in front of you, or get off the current one.
            bool handled = false;
            if (creatures.is_riding()) {
                dismount_player();
                handled = true;
            } else if (player->get_camera() &&
                       creatures.mount_nearest(player->get_camera()->get_global_position(), 4.5f)) {
                player->set_riding(true);
                handled = true;
            }
            if (handled) {
                get_viewport()->set_input_as_handled();
                return;
            }
        }
        if (k == Key::KEY_Q) {
            if (current_equipped_tool() == "world_wand") {
                wand_paste_rot = (wand_paste_rot + 1) % 4;
                refresh_wand_hud();
                if (arc_hotbar)
                    arc_hotbar->show_wand_feedback(
                        String::utf8(language == "pt_BR" ? "Rotação: " : "Rotation: ") +
                        String::num_int64(wand_paste_rot * 90) + String::utf8("°"));
            } else {
                shape_rotation = (shape_rotation + 1) % 4;
            }
        }
        if (k == Key::KEY_G) {
            auto id = target_built();
            if (id)
                release_built(id);
            else {
                auto hit = target_block();
                if (hit.hit)
                    release_voxel(hit.cell.x, hit.cell.y, hit.cell.z, false);
            }
        }
        if (k >= Key::KEY_1 && k <= Key::KEY_9 && !key->is_alt_pressed()) {
            select_hotbar_slot(int(k) - int(Key::KEY_1));
        }
    }
    if (!world || loading_world || is_console_open() || cheat_inventory_open())
        return;
    Ref<InputEventMouseButton> mouse = event;
    if (mouse.is_valid()) {
        auto b = mouse->get_button_index();
        if (!mouse->is_pressed()) {
            if (b == MouseButton::MOUSE_BUTTON_LEFT) {
                lmb_held = false;
                lmb_repeat_timer = 0.0f;
            } else if (b == MouseButton::MOUSE_BUTTON_RIGHT) {
                rmb_held = false;
                rmb_repeat_timer = 0.0f;
            }
            return;
        }
        if (is_alt_inventory_open()) {
            if (b == MouseButton::MOUSE_BUTTON_WHEEL_UP) {
                arc_hotbar->cycle_alt_option(-1);
                sync_alt_selection();
                get_viewport()->set_input_as_handled();
            } else if (b == MouseButton::MOUSE_BUTTON_WHEEL_DOWN) {
                arc_hotbar->cycle_alt_option(1);
                sync_alt_selection();
                get_viewport()->set_input_as_handled();
            }
            return;
        }
        if (b == MouseButton::MOUSE_BUTTON_LEFT) {
            if (current_equipped_tool() == "world_wand") {
                trigger_left_hit();
                return;
            }
            lmb_held = true;
            if (lmb_cooldown_timer <= 0.0f) {
                lmb_cooldown_timer = 0.56f;
                lmb_repeat_timer = 0.56f;
                trigger_left_hit();
            }
        }
        if (b == MouseButton::MOUSE_BUTTON_RIGHT) {
            if (current_equipped_tool() == "world_wand") {
                handle_wand_action(true);
                return;
            }
            rmb_held = true;
            rmb_repeat_timer = 0.20f;
            update_target(true, true);
        }
        if (Input::get_singleton()->is_physical_key_pressed(Key::KEY_R)) {
            if (b == MouseButton::MOUSE_BUTTON_WHEEL_UP)
                set_selected_shape((selected_shape + SHAPE_COUNT - 1) % SHAPE_COUNT);
            if (b == MouseButton::MOUSE_BUTTON_WHEEL_DOWN)
                set_selected_shape((selected_shape + 1) % SHAPE_COUNT);
        } else {
            if (b == MouseButton::MOUSE_BUTTON_WHEEL_UP)
                select_hotbar_slot((inventory.selected() + altaria::HOTBAR_SLOT_COUNT - 1) %
                                   altaria::HOTBAR_SLOT_COUNT);
            if (b == MouseButton::MOUSE_BUTTON_WHEEL_DOWN)
                select_hotbar_slot((inventory.selected() + 1) % altaria::HOTBAR_SLOT_COUNT);
        }
    }
}
static bool number(const String &s, float &value) {
    if (!s.is_valid_float())
        return false;
    value = s.to_float();
    return std::isfinite(value);
}
String AltariaGame::execute_command(const String &text) {
    std::string line = text.utf8().get_data();
    std::string res = altaria::CommandRegistry::get_singleton().execute(this, line);
    return String(res.c_str());
}

String AltariaGame::item_command(const std::vector<std::string> &args) {
    const String usage = "Usage: item pickaxe|axe|world_wand|clear [left|right]";
    if (args.size() < 2 || args.size() > 3)
        return usage;
    String item = String(args[1].c_str()).to_lower();
    if (item == "wand")
        item = "world_wand";
    const String hand = args.size() == 3 ? String(args[2].c_str()).to_lower() : String("right");
    if ((item != "pickaxe" && item != "axe" && item != "world_wand" && item != "clear") ||
        (hand != "left" && hand != "right"))
        return usage;
    if (!world || !first_person_hands)
        return "Open a world to equip an item.";
    if (item == "world_wand" && hand == "right") {
        int slot_idx = -1;
        if (inventory.give_tool("world_wand", &slot_idx) && slot_idx >= 0) {
            select_hotbar_slot(slot_idx);
            console_tool_override = false;
            hand_equipped_tool = "world_wand";
            first_person_hands->call("set_tool", hand, item);
            if (arc_hotbar)
                arc_hotbar->notify_activity();
            return "Equipped world_wand in the right hand.";
        }
    }
    console_tool_override = true;
    first_person_hands->call("set_tool", hand, item == "clear" ? String() : item);
    return item == "clear" ? "Cleared " + hand + String(" hand.")
                           : "Equipped " + item + String(" in the ") + hand + String(" hand.");
}

String AltariaGame::export_game_log(const String &duration) {
    return game_log.is_valid() ? game_log->export_recent(duration) : String("Game log is unavailable.");
}

String AltariaGame::resolution_command(const std::vector<std::string> &args) {
    auto *window = get_window();
    if (!window)
        return "No game window.";
    if (args.size() == 1) {
        const auto size = window->get_size();
        return "Window resolution: " + String::num_int64(size.x) + String("x") + String::num_int64(size.y);
    }
    if (args.size() == 2 && String(args[1].c_str()).to_lower() == "list")
        return "1920x1080 (default), 1600x900, 1280x720, 2560x1440, 3840x2160. Custom sizes accepted.";
    if (args.size() != 2)
        return "Usage: resolution [list|WIDTHxHEIGHT]";
    const auto size = parse_resolution(String(args[1].c_str()));
    if (!size)
        return "Invalid resolution. Use WIDTHxHEIGHT from 640x360 to 7680x4320.";
    if (window->get_mode() == Window::MODE_FULLSCREEN || window->get_mode() == Window::MODE_EXCLUSIVE_FULLSCREEN)
        return "Switch to windowed mode before setting a resolution.";

    const String value = String::num_int64(size->x) + String("x") + String::num_int64(size->y);
    const String path = "user://preferences.json";
    String error;
    Dictionary preferences;
    if (FileAccess::file_exists(path)) {
        preferences = read_json(path, error);
        if (!error.is_empty())
            return "Could not load resolution preferences. " + error;
    }
    preferences["resolution"] = value;
    if (!write_json_atomic(path, preferences, error))
        return "Could not save resolution preferences. " + error;
    if (window->get_mode() == Window::MODE_MAXIMIZED)
        window->set_mode(Window::MODE_WINDOWED);
    window->set_size(*size);
    return "Requested window resolution: " + value;
}

String AltariaGame::execute_legacy_command(const String &text) {
    auto args = text.strip_edges().split(" ", false);
    if (args.is_empty())
        return "";
    String command = args[0].to_lower();
    if (command.begins_with("/"))
        command = command.substr(1);
    if (command == "help")
        return "world new [default|plain|structure] [seed] | fullbright [on|off] | fly [on|off] | speed <1..150> | tp <x> <y> "
               "<z>\nseed | stats | "
               "paint <#RRGGBB|reset> [preserve brightness] | overlay <key|clear|reset> | biome [key]\nblock "
               "<namespace:key> | spawn <namespace:key> | fog <#RRGGBB> [start end] | fog biome\noutline "
               "<on|off> | pixelation <on|off> | godrays <on|off> | help\nshape <name> | shape at x y z <name> "
               "[rotation] | cut | physics [at x y z|clear]\ntime "
               "<hour|pause|resume> | weather <clear|rain|storm> | wind <0..3> | temperature | light [color] | light follow";
    if (command == "world")
        return execute_command(text);
    if (!world)
        return "No world is open.";
    if (command == "cave") {
        auto caves=world->generator()->underground();
        if(!caves)return "Caves are disabled in caves.json.";
        if(args.size()==2 && args[1]=="environment")return Variant(get_cave_environment(player->get_position())).stringify();
        if(args.size()==2 && args[1]=="map")return toggle_cave_map();
        if(args.size()==2 && args[1]=="formations") {
            auto features=get_cave_formations();String out;
            for(int i=0;i<features.size();i++) {
                Dictionary feature=features[i];
                out+=String::num_int64(i)+": "+String(feature["rule"]);
                if(bool(feature["support_ready"]))out+=" ("+String::num_int64(feature.get("supported_voxels",0))+" voxels)";
                out+="\n";
            }
            return out.is_empty()?String("No nearby formation plans are loaded."):out+"cave feature <index> to inspect.";
        }
        auto pos=player->get_position();int span=world->settings().caves.plan.region_size;
        auto net=world->cave_plan_at({int(std::floor(pos.x/span)),int(std::floor(pos.z/span))});
        if(!net)return "Cave plan is not loaded yet. Wait for nearby chunks.";
        if(args.size()==2 && args[1]=="entrances") {
            String entries;
            for(const auto &entrance:net->network.entrances) {
                const auto &p=entrance.position;
                entries+=String(altaria::caves::entrance_kind_name(entrance.kind))+" | tp "+
                    String::num(p[0],1)+" "+String::num(p[1],1)+" "+String::num(p[2],1)+"\n";
            }
            return entries.is_empty()?String("No cave entrance in this region."):entries.strip_edges();
        }
        if(args.size()==3 && args[1]=="feature" && args[2].is_valid_int()) {
            int index=args[2].to_int();
            if(index<0 || index>=int(net->formations.size()))return "Invalid formation index. Use cave formations.";
            const auto &f=net->formations[index];
            for(const auto &room:net->network.chambers)if(room.id==f.chamber) {
                Vector3 center(room.center[0],room.center[1],room.center[2]);
                player->set_position(center);player->set_flying(true);
                Vector3 target(f.base[0],f.base[1]+f.vertical*f.height*.45f,f.base[2]);
                auto delta=target-center;
                player->set_view(std::atan2(-delta.x,-delta.z),std::atan2(delta.y,std::sqrt(delta.x*delta.x+delta.z*delta.z)));
                return "Inspecting "+String(world->generator()->underground_biomes()->formation_definition(f.rule).id.c_str())+
                       ". Wait for nearby chunks.";
            }
            return "Formation chamber is unavailable.";
        }
        if(args.size()==3 && args[1]=="tp" && args[2].is_valid_int()) {
            int depth=args[2].to_int();
            for(const auto &room:net->network.chambers)if(room.depth==depth && !room.isolated) {
                player->set_position(Vector3(room.center[0],room.center[1],room.center[2]));
                player->set_flying(true);return "Teleported to depth "+String::num_int64(depth)+String(" cave.");
            }
            return "No chamber at that depth in this region.";
        }
        if(args.size()!=1)return "Usage: cave | cave environment | cave map | cave entrances | cave tp <0..3> | cave formations | cave feature <index>";
        return "Resident chambers: "+String::num_int64(net->network.chambers.size())+String(" | passages: ")+
            String::num_int64(net->network.passages.size())+String(" | cave map | cave entrances | cave tp <0..3>");
    }
    if (command == "fog") {
        if (args.size() == 1)
            return "Fog " + String(atmosphere->state()["fog_mode"]) + " | " +
                   String(atmosphere->state()["fog_color"].stringify());
        if (args.size() == 2 && args[1] == "biome") {
            atmosphere->reset_fog();
            return "Biome fog restored.";
        }
        float start = atmosphere->state()["fog_start"], end = atmosphere->state()["fog_end"];
        if ((args.size() != 2 && args.size() != 4) || !Color::html_is_valid(args[1]) ||
            (args.size() == 4 && (!number(args[2], start) || !number(args[3], end))) ||
            !atmosphere->set_fog(Color::html(args[1]), start, end))
            return "Usage: fog <#RRGGBB> [start end] | fog biome";
        return "Fog updated.";
    }
    if (command == "outline" || command == "pixelation" || command == "godrays") {
        if (args.size() != 2 || (args[1] != "on" && args[1] != "off"))
            return "Usage: " + command + " <on|off>";
        if (command == "outline")
            atmosphere->set_outline(args[1] == "on");
        else if(command=="pixelation")
            atmosphere->set_pixelation(args[1] == "on");
        else atmosphere->set_godrays(args[1]=="on");
        return command + ": " + (args[1] == "on" ? String("ON") : String("OFF"));
    }
    if (command == "time") {
        float h;
        if (args.size() == 1)
            return "World hour: " + String::num(float(atmosphere->state()["hour"]), 2);
        if (args.size() != 2)
            return "Usage: time <0..24|pause|resume>";
        if (args[1] == "pause" || args[1] == "resume") {
            atmosphere->set_running(args[1] == "resume");
            return "Time " + args[1] + "d.";
        }
        if (!number(args[1], h) || !atmosphere->set_hour(h))
            return "Usage: time <0..24|pause|resume>";
        return "Time set.";
    }
    if (command == "weather") {
        if (args.size() != 2 || !atmosphere->set_weather(args[1]))
            return "Usage: weather <clear|rain|storm>";
        return "Weather set.";
    }
    if (command == "wind") {
        float w;
        if (args.size() != 2 || !number(args[1], w) || !atmosphere->set_wind(w))
            return "Usage: wind <0..3>";
        return "Wind set.";
    }
    if (command == "temperature")
        return Variant(get_temperature(player->get_position())).stringify();
    if (command == "light") {
        const String mode = args.size() > 1 ? args[1].to_lower() : String();
        const bool follow = mode == "follow";
        const int color_index = (follow || mode == "place") ? 2 : 1;
        const String option = args.size() > color_index ? args[color_index].to_lower() : String();
        if (follow && (option.is_empty() || option == "on" || option == "off") &&
            args.size() <= color_index + 1) {
            const bool enabled = option.is_empty() ? !atmosphere->is_following_light_on() : option == "on";
            atmosphere->set_following_light(enabled);
            return enabled ? "Following light on." : "Following light off.";
        }
        const Color invalid(-1, -1, -1);
        const Color color = option.is_empty() ? Color(1.f, .88f, .70f) : Color::from_string(option, invalid);
        float energy = 3.f, radius = 12.f;
        const bool has_energy = args.size() > color_index + 1;
        const bool has_radius = args.size() > color_index + 2;
        if (args.size() > color_index + 3 || color == invalid ||
            (has_energy && (!number(args[color_index + 1], energy) || energy < .1f || energy > 20.f)) ||
            (has_radius && (!number(args[color_index + 2], radius) || radius < 1.f || radius > 192.f)))
            return "Try light [color], light follow [color], or light follow on|off. "
                   "Optional energy: 0.1–20. Optional radius: 1–192.";
        if (follow) {
            atmosphere->set_following_light(true, color,
                has_energy ? std::optional<float>(energy) : std::nullopt,
                has_radius ? std::optional<float>(radius) : std::nullopt);
            return "Following light updated.";
        }
        if (int(atmosphere->state()["colored_lights"]) >= 16)
            return "Colored light budget reached.";
        auto camera = player->get_camera();
        atmosphere->add_light(camera->get_global_position(), color, energy, radius);
        return "Colored light placed.";
    }
    if (command == "shape") {
        if (args.size() >= 2 && args[1] == "at") {
            if (args.size() < 6 || args.size() > 7 || !args[2].is_valid_int() || !args[3].is_valid_int() ||
                !args[4].is_valid_int() || (args.size() == 7 && !args[6].is_valid_int()))
                return "Usage: shape at x y z <name> [rotation 0..3]";
            for (int index : {2, 4})
                if (std::abs(args[index].to_int()) > 1000000)
                    return "Coordinates outside world bounds.";
            if (args[3].to_int() <= world->settings().min_y || args[3].to_int() >= world->settings().max_y)
                return "Coordinates outside world bounds.";
            return shape_voxel(args[2].to_int(), args[3].to_int(), args[4].to_int(), args[5],
                               args.size() == 7 ? args[6].to_int() : 0)
                       ? "Block shape applied."
                       : "Invalid shape or supporting block.";
        }
        if (args.size() != 2)
            return "Usage: shape <cube|slab|vertical_slab|ramp|stairs|roof|pillar|mini_pillar>";
        int value = shape_id(args[1].utf8().get_data());
        if (value < 0)
            return "Unknown shape.";
        set_selected_shape(value);
        return "Selected shape: " + args[1];
    }
    if (command == "cut" || command == "physics") {
        if (command == "physics" && args.size() == 2 && args[1] == "clear") {
            physics_blocks->clear();
                return "Physics debris cleared.";
        }
        IVec3 p;
        if (args.size() == 5 && args[1] == "at" && args[2].is_valid_int() && args[3].is_valid_int() &&
            args[4].is_valid_int()) {
            int64_t x = args[2].to_int(), y = args[3].to_int(), z = args[4].to_int();
            if (std::abs(x) > 1000000 || std::abs(z) > 1000000 || y <= world->settings().min_y ||
                y >= world->settings().max_y)
                return "Coordinates outside world bounds.";
            p = {int(x), int(y), int(z)};
        } else if (args.size() == 1) {
            if (auto id = target_built())
                return release_built(id) ? "Released into Jolt physics."
                                         : "Could not release building piece.";
            auto h = target_block();
            if (!h.hit)
                return "Look at a block within reach.";
            p = h.cell;
        } else
            return "Usage: " + command + " [at x y z] | physics clear";
        return release_voxel(p.x, p.y, p.z, command == "cut")
                   ? "Released into Jolt physics."
                   : "Could not release block. Check console output.";
    }
    if (command == "paint" || command == "overlay") {
        String usage = command == "paint" ? String("paint <#RRGGBB|reset> [preserve 0..1] [brightness 0..2]")
                                          : String("overlay <key|clear|reset>");
        int argument = 1;
        IVec3 p;
        uint64_t building = 0;
        if (args.size() > 1 && args[1] == "at") {
            if (args.size() < 6 || !args[2].is_valid_int() || !args[3].is_valid_int() ||
                !args[4].is_valid_int())
                return "Usage: " + command + " at <x> <y> <z> <value>";
            int64_t x = args[2].to_int(), y = args[3].to_int(), z = args[4].to_int();
            if (std::abs(x) > 1000000 || std::abs(z) > 1000000 ||
                y < world->settings().min_y || y >= world->settings().max_y)
                return "Coordinates outside world bounds.";
            p = {int(x), int(y), int(z)};
            argument = 5;
        } else {
            building = target_built();
            auto target = target_block();
            if (!building && !target.hit)
                return "Look at a block within reach, or use " + command + " at x y z <value>.";
            p = target.cell;
        }
        if (args.size() <= argument)
            return "Usage: " + usage;
        if (command == "overlay") {
            if (args.size() != argument + 1)
                return "Usage: " + usage;
            if (building) {
                auto a = built_world->get(building)->appearance;
                String key = args[argument];
                if (key == "clear") {
                    a.overlay = 0;
                    a.decoration = 0;
                } else if (key == "reset") {
                    a.overlay = 65535;
                    a.decoration = 65535;
                } else {
                    String full = key.contains(":") ? key : String("altaria:") + key;
                    auto it = world->registry().overlay_keys.find(full.utf8().get_data());
                    if (it == world->registry().overlay_keys.end())
                        return "Unknown overlay.";
                    if (world->registry().overlays[it->second].decal)
                        a.decoration = it->second;
                    else
                        a.overlay = it->second;
                }
                String e;
                return built_world->appearance(building, a, e) ? String("Overlay applied.") : e;
            }
            return overlay_voxel(p.x, p.y, p.z, args[argument])
                       ? String("Overlay applied.")
                       : String("Unknown overlay or invalid supporting block.");
        }
        float preserve = .35f, brightness = 1.f;
        if (args.size() > argument + 3 ||
            (args.size() > argument + 1 && !number(args[argument + 1], preserve)) ||
            (args.size() > argument + 2 && !number(args[argument + 2], brightness)))
            return "Usage: " + usage;
        if (building) {
            auto a = built_world->get(building)->appearance;
            uint32_t rgb;
            if (preserve < 0 || preserve > 1 || brightness < 0 || brightness > 2)
                return "Invalid paint parameters.";
            if (args[argument] == "reset")
                a.painted = false;
            else {
                if (!parse_hex_color(args[argument], rgb))
                    return "Invalid color.";
                a.painted = true;
                a.tint = rgb;
                a.preserve = uint16_t(std::round(preserve * 1000));
                a.brightness = uint16_t(std::round(brightness * 1000));
            }
            String e;
            return built_world->appearance(building, a, e) ? String("Paint applied.") : e;
        }
        return paint_voxel(p.x, p.y, p.z, args[argument], preserve, brightness)
                   ? String("Paint applied.")
                   : String("Invalid color, tint parameters or supporting block.");
    }
    if (command == "biome") {
        auto origin = player->get_position();
        auto current = world->generator()->column(int(std::floor(origin.x)), int(std::floor(origin.z)));
        const auto &biomes = world->settings().biomes;
        if (args.size() == 1)
            return "Biome: " + String(biomes[current.biome].key.c_str()) + " | temperature " +
                   String::num(current.temperature, 2) + " | mountain mask " +
                   String::num(current.mountain, 2);
        if (args.size() != 2)
            return "Usage: biome [normal|snow|desert|jungle|enchanted]";
        int wanted = -1;
        for (size_t i = 0; i < biomes.size(); i++)
            if (String(biomes[i].key.c_str()) == args[1])
                wanted = int(i);
        if (wanted < 0)
            return "Unknown biome key.";
        float step = std::max(32.f, world->settings().biome_scale / 8),
              radius = std::min(16000.f, std::max(1024.f, world->settings().biome_scale * 6));
        std::vector<float> x, z;
        for (float dz = -radius; dz <= radius; dz += step)
            for (float dx = -radius; dx <= radius; dx += step) {
                x.push_back(std::floor(std::clamp(origin.x + dx, -999000.f, 999000.f)));
                z.push_back(std::floor(std::clamp(origin.z + dz, -999000.f, 999000.f)));
            }
        auto columns = world->generator()->columns(x, z);
        float best = 1e30f;
        int found = -1;
        // Prefer an interior sample so a debug teleport showcases the biome itself,
        // rather than landing one voxel inside a transition to a different biome.
        for (int pass = 0; pass < 2 && found < 0; pass++)
            for (size_t i = 0; i < columns.size(); i++) {
                const auto &c = columns[i];
                if (c.biome != wanted || c.surface != (biomes[wanted].snow     ? world->registry().snow
                                                       : biomes[wanted].desert ? world->registry().sand
                                                                               : world->registry().grass))
                    continue;
                RGB desired = biomes[wanted].grass;
                float blend = std::abs(c.grass_tint.r - desired.r) + std::abs(c.grass_tint.g - desired.g) +
                              std::abs(c.grass_tint.b - desired.b);
                if (pass == 0 && blend > .03f)
                    continue;
                float dx = x[i] - origin.x, dz = z[i] - origin.z, distance = dx * dx + dz * dz;
                if (distance < best) {
                    best = distance;
                    found = int(i);
                }
            }
        if (found < 0)
            return "No matching biome nearby. Increase biome scale/search range or try another seed.";
        player->set_position(Vector3(x[found], columns[found].height + 24, z[found]));
        return "Teleported to biome " + args[1] + ".";
    }
    if (command == "profile") {
        // Frame profiler: "profile" lists the main-thread scopes, "profile reset" clears them.
        if (args.size() > 1 && String(args[1]).to_lower() == "reset") {
            altaria::profile::reset();
            return "Profiler reset.";
        }
        String out = "scope: average ms / worst ms / calls";
        for (const auto *slot : altaria::profile::slots())
            if (slot->calls)
                out += "\n" + String(slot->name) + ": " + String::num(slot->total_ms / double(slot->calls), 3) + " / " +
                       String::num(slot->max_ms, 2) + " / " + String::num_int64(int64_t(slot->calls));
        return out;
    }
    if (command == "fullbright" || command == "fly") {
        if (args.size() > 2 || (args.size() == 2 && args[1] != "on" && args[1] != "off"))
            return "Usage: " + command + " [on|off]";
        bool value = args.size() == 2
                         ? args[1] == "on"
                         : !(command == "fullbright" ? world->is_fullbright() : player->is_flying());
        if (command == "fullbright") {
            world->set_fullbright(value);

            if (sun)
                sun->set_shadow(!value);
        } else
            player->set_flying(value);
        return command + ": " + (value ? String("ON") : String("OFF"));
    }
    if (command == "seed")
        return "Seed: " + String::num_int64(int64_t(metadata["seed"]));
    if (command == "stats")
        return Variant(get_debug_state()).stringify();

    if (command == "speed") {
        float speed = 0;
        if (args.size() != 2 || !number(args[1], speed) || speed < 1 || speed > 150)
            return "Usage: speed <1..150>";
        player->set_speed(speed);
        return "Flight speed: " + String::num(speed);
    }
    if (command == "tp") {
        float x, y, z;
        if (args.size() != 4 || !number(args[1], x) || !number(args[2], y) || !number(args[3], z) ||
            std::abs(x) > 999990 || std::abs(z) > 999990 || y < world->settings().min_y + 1 || y > 3000)
            return "Usage: tp <x> <y> <z> (within world bounds)";
        player->set_position(Vector3(x, y, z));
        return "Teleported.";
    }
    if (command == "block") {
        if (args.size() != 2)
            return "Usage: block <namespace:key>";
        auto &registry = world->registry();
        auto it = registry.keys.find(args[1].utf8().get_data());
        if (it == registry.keys.end())
            return "Unknown block key.";
        if (!registry.get(it->second).placeable || it->second == registry.air)
            return "Block is not placeable.";
        int slot_idx = inventory.give_and_select(it->second, 64);
        selected_shape = inventory.active_shape();
        if (arc_hotbar)
            arc_hotbar->trigger_pickup_anim(slot_idx);
        return "Selected " + args[1];
    }
    if (command == "mount" || command == "dismount")
        return mount_command(args);
    if (command == "spawn") {
        if (args.size() >= 2) {
            const String creature = creature_command(args);
            if (!creature.is_empty())
                return creature;
        }
        if (args.size() != 2)
            return "Usage: spawn <namespace:key> | spawn <creature> [count] | spawn clear";
        return spawn_entity(args[1]) ? String("Entity spawned.")
                                     : String("Unknown entity or unsupported controller.");
    }
    return "Unknown command. Type help.";
}
void AltariaGame::update_mount_input(float dt) {
    if (!player)
        return;
    if (!creatures.is_riding()) {
        if (player->is_riding())
            player->set_riding(false);
        return;
    }
    auto *in = Input::get_singleton();
    const bool controls = in->get_mouse_mode() == Input::MOUSE_MODE_CAPTURED && !is_console_open();
    const bool crouch = controls && (in->is_physical_key_pressed(Key::KEY_CTRL) || in->is_physical_key_pressed(Key::KEY_C));
    mount_crouch_timer = std::max(0.0f, mount_crouch_timer - dt);
    if (crouch && !mount_crouch_prev) {
        if (mount_crouch_timer > 0.0f) {
            mount_crouch_prev = true;
            dismount_player();
            return;
        }
        mount_crouch_timer = 0.32f;
    }
    mount_crouch_prev = crouch;
    Vector3 wish;
    float vertical = 0.0f;
    bool sprint = false;
    if (controls) {
        const float strafe = float(in->is_physical_key_pressed(Key::KEY_D)) - float(in->is_physical_key_pressed(Key::KEY_A));
        const float fwd = float(in->is_physical_key_pressed(Key::KEY_S)) - float(in->is_physical_key_pressed(Key::KEY_W));
        wish = player->get_basis().xform(Vector3(strafe, 0.0f, fwd));
        wish.y = 0.0f;
        if (wish.length_squared() > 0.0f)
            wish = wish.normalized();
        vertical = float(in->is_physical_key_pressed(Key::KEY_SPACE)) - float(crouch);
        sprint = in->is_physical_key_pressed(Key::KEY_SHIFT);
    }
    creatures.set_rider_input(wish, vertical, sprint, player->get_rotation().y);
}
void AltariaGame::sync_rider() {
    Vector3 seat;
    if (player && player->is_riding() && creatures.rider_seat(seat))
        player->set_position(seat);
}
void AltariaGame::dismount_player() {
    creatures.dismount();
    if (player)
        player->set_riding(false); // stays where the saddle was and falls unless flying
}
String AltariaGame::mount_command(const PackedStringArray &args) {
    if (!player)
        return "No world open.";
    if (args[0].to_lower().trim_prefix("/") == "dismount" || (args.size() == 2 && args[1].to_lower() == "off")) {
        if (!creatures.is_riding())
            return "Not riding anything.";
        dismount_player();
        return "Dismounted.";
    }
    if (args.size() == 1) {
        if (creatures.is_riding())
            return "Already riding " + String(creatures.ridden_species().c_str()) + ".";
        if (player->get_camera() && creatures.mount_nearest(player->get_camera()->get_global_position(), 4.5f)) {
            player->set_riding(true);
            return "Mounted " + String(creatures.ridden_species().c_str()) + ".";
        }
        return "Nothing to mount nearby. Usage: mount <name> | mount | dismount";
    }
    String name;
    for (int i = 1; i < args.size(); i++)
        name += (i > 1 ? " " : "") + args[i].to_lower();
    const std::string key = creatures.resolve_species(name.utf8().get_data());
    if (key.empty())
        return "Unknown mount: " + name;
    if (!creatures.is_mountable(key))
        return name + " cannot be ridden.";
    Vector3 feet = player->get_position();
    if (!creatures.summon_mount(key, feet, player->get_rotation().y))
        return "No room to summon " + name + " here.";
    player->set_position(feet);
    player->set_riding(true);
    return "Riding " + String(key.c_str()) + ". Shift sprint, Space up, Ctrl down, double-tap Ctrl to dismount.";
}
String AltariaGame::creature_command(const PackedStringArray &args) {
    // Returns an empty string when args[1] is not a creature, so `spawn` falls back to templates.
    const String name = args[1].to_lower();
    if (name == "clear") {
        const int removed = creatures.count();
        creatures.clear();
        return "Removed " + String::num_int64(removed) + " creature(s).";
    }
    const std::string key = creatures.resolve_species(name.utf8().get_data());
    if (key.empty())
        return String();
    if (args.size() > 3 || (args.size() == 3 && !args[2].is_valid_int()))
        return "Usage: spawn <creature> [count]";
    const int wanted = args.size() == 3 ? std::clamp(int(args[2].to_int()), 1, 64) : 1;
    // In front of the player, a few blocks away, on any dry ground.
    const Vector3 ahead = player->get_position() - player->get_basis().get_column(2) * 4.0f;
    const int spawned = creatures.spawn_near(key, ahead, wanted, 0.0f, wanted > 1 ? 3.0f : 1.0f);
    if (spawned == 0)
        return "No room to spawn " + name + " here.";
    return "Spawned " + String::num_int64(spawned) + " " + String(key.c_str()) + ".";
}
bool AltariaGame::spawn_entity(const String &key, const Dictionary &saved) {
    if (!stage || !world)
        return false;
    Array definitions = entity_data.get("entities", Array());
    Dictionary definition;
    for (int i = 0; i < definitions.size(); i++) {
        Dictionary d = definitions[i];
        if (String(d.get("key", String())) == key) {
            definition = d;
            break;
        }
    }
    if (definition.is_empty() || String(definition.get("controller", String())) != "static_box")
        return false;
    auto node = memnew(MeshInstance3D);
    Ref<BoxMesh> mesh;
    mesh.instantiate();
    Array size = definition.get("size", Array());
    Array color = definition.get("color", Array());
    if (size.size() != 3 || color.size() != 3) {
        memdelete(node);
        return false;
    }
    mesh->set_size(Vector3(size[0], size[1], size[2]));
    node->set_mesh(mesh);
    Ref<StandardMaterial3D> mat;
    mat.instantiate();
    mat->set_albedo(Color(color[0], color[1], color[2]));
    node->set_material_override(mat);
    stage->add_child(node);
    Dictionary entity = saved.duplicate();
    Array xyz = entity.get("position", Array());
    if (xyz.size() == 3)
        node->set_position(Vector3(xyz[0], xyz[1], xyz[2]));
    else {
        auto pos = player->get_global_position() - player->get_basis().get_column(2) * 4 + Vector3(0, 1, 0);
        node->set_position(pos);
        xyz.push_back(pos.x);
        xyz.push_back(pos.y);
        xyz.push_back(pos.z);
        entity["position"] = xyz;
        entity["key"] = key;
        Array entities = metadata.get("entities", Array());
        entity["entity_id"] = entities.size() + 1;
        entities.push_back(entity);
        metadata["entities"] = entities;
    }
    return true;
}
Dictionary AltariaGame::get_debug_state() const {
    Dictionary d = world ? world->stats() : Dictionary();
    d["world_open"] = world != nullptr;
    d["creatures"] = creatures.debug_state();
    d["riding"] = String(creatures.ridden_species().c_str());
    d["language"] = language;
    d["world_path"] = world_path;
    if (world)
        d["seed"] = int64_t(metadata.get("seed", 0));
    if (atmosphere)
        d.merge(atmosphere->state(), true);
    if (physics_blocks)
        d.merge(physics_blocks->stats(), true);
    if (built_world) {
        d.merge(built_world->stats(), true);
        d["pending"] = int(d["pending"]) + int(d["building_pending"]);
    }
    d["selection_built_id"] = int64_t(selected_built_id);
    d["selection_visible"] = selection && selection->is_visible_in_tree();
    d["selection_physical"] = selection_is_physical;
    if (selection)
        d["selection_transform"] = selection->get_global_transform();
    d["selected_shape"] = shape_name(selected_shape);
    d["shape_rotation"] = shape_rotation;
    d["selected_slot"] = inventory.selected();
    d["selected_block_count"] = inventory.active_slot().count;
    d["alt_inventory_open"] = is_alt_inventory_open();
    if (player) {
        d["player_position"] = player->get_position();
        if(auto caves=world->generator()->underground()) {
            d["cave_map_visible"]=cave_map_enabled;
            d["cave_map_surfaces"]=cave_map && cave_map->get_mesh().is_valid()?cave_map->get_mesh()->get_surface_count():0;
            auto p=player->get_position();int span=world->settings().caves.plan.region_size;
            auto plan=world->cave_plan_at({int(std::floor(p.x/span)),int(std::floor(p.z/span))});
            Array chambers;
            if(plan)for(const auto &room:plan->network.chambers) {
                Dictionary c;c["id"]=String::num_uint64(room.id);c["depth"]=room.depth;
                c["center"]=Vector3(room.center[0],room.center[1],room.center[2]);
                c["radius"]=Vector3(room.radius[0],room.radius[1],room.radius[2]);
                c["archetype"]=String(room.archetype.c_str());c["isolated"]=room.isolated;chambers.append(c);
            }
            d["cave_chambers"]=chambers;
            Array entrances;
            if(plan)for(const auto &entry:plan->network.entrances) {
                Dictionary e;e["kind"]=String(altaria::caves::entrance_kind_name(entry.kind));
                e["position"]=Vector3(entry.position[0],entry.position[1],entry.position[2]);entrances.append(e);
            }
            d["cave_entrances"]=entrances;
        }
        d["flying"] = player->is_flying();
        d["noclip"] = player->is_noclip();
        d["fly_hotkey_enabled"] = fly_hotkey_enabled;
        d["noclip_hotkey_enabled"] = noclip_hotkey_enabled;
        d["single_biome"] = String(world->settings().single_biome.c_str());
        d["jump_count"] = player->get_jump_count();
        d["grounded"] = player->is_grounded();
        d["player_collision_ready"]=world->collision_ready(player->get_position());
        auto p = player->get_position();
        auto c = world->generator()->column(int(std::floor(p.x)), int(std::floor(p.z)));
        d["biome"] = String(world->settings().biomes[c.biome].key.c_str());
        d["transition_biome"] = c.transition_biome >= 0
                                    ? String(world->settings().biomes[c.transition_biome].key.c_str())
                                    : String("");
        d["temperature"] = c.temperature;
        d.merge(get_temperature(p), true);
        d["mountain_mask"] = c.mountain;
        d["plateau_mask"] = c.plateau;
        d["valley_mask"] = c.valley;
        d["river_mask"] = c.river;
        d["continentalness"] = c.continentalness;
        d["buildable_mask"] = c.buildable;
        d["adventure_mask"] = c.adventure;
        d["terrace_mask"] = c.terrace_step;
        d["arch_mask"] = c.arch_mask;
        d["lake_mask"] = c.lake_mask;
        d["beach_mask"] = c.beach_mask;
        d["river_bank_step"] = c.river_bank_step;
        d["waterfall_mask"] = c.waterfall_mask;
        d["water_top"] = c.water_top;
        d["slope"] = c.slope;
        d["chain_axis"] = c.chain_axis;
        d["region"] = c.region;
        d["surface_block_id"] = c.surface;
    }
    return d;
}
void AltariaGame::_exit_tree() {
    if (game_log.is_valid()) {
        OS::get_singleton()->remove_logger(game_log);
        game_log.unref();
    }
    if (active_game == this)
        active_game = nullptr;
    if (player)
        player->set_active(false);
    if (world) {
        world->set_process(false);
    }
    Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
}
} // namespace godot
