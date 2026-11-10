#include "altaria_game.h"
#include "game/world_generation/block_shapes.h"
#include "game/world_generation/world_data.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/center_container.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/procedural_sky_material.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/sky.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <sstream>
using namespace altaria;
namespace godot {
void AltariaGame::_bind_methods() {
    ClassDB::bind_method(D_METHOD("place_block", "position", "block", "shape", "rotation"),
                         &AltariaGame::place_block, DEFVAL("cube"), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("terrain_hit", "origin", "direction", "distance"), &AltariaGame::terrain_hit, DEFVAL(100));
    ClassDB::bind_method(D_METHOD("terrain_built", "id", "mode"), &AltariaGame::terrain_built, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("get_built_blocks"), &AltariaGame::get_built_blocks);
    ClassDB::bind_method(D_METHOD("remove_built_block", "id"), &AltariaGame::remove_built_block);
    ClassDB::bind_method(D_METHOD("start_world", "name", "seed"), &AltariaGame::start_world);
    ClassDB::bind_method(D_METHOD("start_random_world"), &AltariaGame::start_random_world);
    ClassDB::bind_method(D_METHOD("open_world", "path"), &AltariaGame::open_world);
    ClassDB::bind_method(D_METHOD("save_world"), &AltariaGame::save_world);
    ClassDB::bind_method(D_METHOD("shape_voxel", "x", "y", "z", "shape", "rotation"),
                         &AltariaGame::shape_voxel, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("get_temperature", "position"), &AltariaGame::get_temperature);
    ClassDB::bind_method(D_METHOD("release_voxel", "x", "y", "z", "tree"), &AltariaGame::release_voxel,
                         DEFVAL(true));
    ClassDB::bind_method(D_METHOD("set_voxel", "x", "y", "z", "block"), &AltariaGame::set_voxel);
    ClassDB::bind_method(D_METHOD("get_voxel", "x", "y", "z"), &AltariaGame::get_voxel);
    ClassDB::bind_method(D_METHOD("paint_voxel", "x", "y", "z", "color", "preserve", "brightness"),
                         &AltariaGame::paint_voxel, DEFVAL(.35), DEFVAL(1.0));
    ClassDB::bind_method(D_METHOD("overlay_voxel", "x", "y", "z", "key"), &AltariaGame::overlay_voxel);
    ClassDB::bind_method(D_METHOD("get_voxel_appearance", "x", "y", "z"), &AltariaGame::get_voxel_appearance);
    ClassDB::bind_method(D_METHOD("execute_command", "text"), &AltariaGame::execute_command);
    ClassDB::bind_method(D_METHOD("get_debug_state"), &AltariaGame::get_debug_state);
    ClassDB::bind_method(D_METHOD("get_world_path"), &AltariaGame::get_world_path);
}
String AltariaGame::local(const String &key) const {
    // English is always the default. Portuguese is an explicit menu option.
    static const std::unordered_map<std::string, std::pair<const char *, const char *>> texts = {
        {"worlds", {"Worlds", "Mundos"}},
        {"new", {"Create new world", "Criar novo mundo"}},
        {"load", {"Play selected world", "Jogar mundo selecionado"}},
        {"resume", {"Resume world", "Continuar mundo"}},
        {"name", {"World name", "Nome do mundo"}},
        {"seed", {"Seed (integer or random)", "Seed (inteiro ou random)"}},
        {"quit", {"Quit", "Sair"}},
        {"back", {"Save & return to worlds", "Salvar e voltar aos mundos"}},
        {"intro", {"Voxel world generation / FastNoise2", "Geração de mundo voxel / FastNoise2"}},
        {"controls",
         {"WASD move | Mouse look | Shift sprint | Space / Ctrl up / down\nF fly / walk | Space double jump "
          "on foot | LMB break | RMB place\n1–8 / Wheel select block | F1 or ` console | Esc menu",
          "WASD mover | Mouse olhar | Shift acelerar | Espaço / Ctrl subir / descer\nF voar / andar | Espaço "
          "pulo duplo a pé | Esq. remover | Dir. colocar\n1–8 / Roda selecionar bloco | F1 ou ` console | "
          "Esc menu"}},
        {"empty", {"No saved worlds. Create one below.", "Nenhum mundo salvo. Crie um abaixo."}},
        {"ready", {"Choose a world or create a new one.", "Selecione um mundo ou crie um novo."}}};
    auto it = texts.find(key.utf8().get_data());
    if (it == texts.end())
        return key;
    return String(language == "pt_BR" ? it->second.second : it->second.first);
}
static Label *label(VBoxContainer *parent, const String &text, int size = 16) {
    auto l = memnew(Label);
    l->set_text(text);
    l->add_theme_font_size_override("font_size", size);
    parent->add_child(l);
    return l;
}
void AltariaGame::_ready() {
    if (Engine::get_singleton()->is_editor_hint())
        return;
    get_tree()->set_auto_accept_quit(false);
    String error;
    generation_data = load_generation_data(error);
    if (error.is_empty())
        blocks_data = load_block_data(error);
    if (error.is_empty())
        entity_data = read_json("res://game/player/data/entities.json", error);
    data_ready = error.is_empty();
    auto settings = read_json("user://preferences.json", error);
    if (settings.get("language", "en") == String("pt_BR"))
        language = "pt_BR";
    overlay = memnew(CanvasLayer);
    overlay->set_layer(20);
    add_child(overlay);
    build_menu();
    build_hud();
    console.create(overlay, callable_mp(this, &AltariaGame::submit_console));
    console.input->connect("gui_input", callable_mp(this, &AltariaGame::on_console_gui_input));
    if (!data_ready)
        status->set_text("Could not load game data. Check JSON files.");
    UtilityFunctions::print("Altaria: C++ voxel runtime ready (FastNoise2 / English default).");
    if (data_ready)
        start_random_world();
}
void AltariaGame::build_menu() {
    if (menu) {
        menu->queue_free();
        menu = nullptr;
    }
    menu = memnew(Control);
    menu->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    overlay->add_child(menu);
    auto bg = memnew(ColorRect);
    bg->set_color(Color(.075, .085, .1, .96));
    bg->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    menu->add_child(bg);
    auto center = memnew(CenterContainer);
    center->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    menu->add_child(center);
    auto panel = memnew(PanelContainer);
    panel->set_custom_minimum_size(Vector2(550, 0));
    center->add_child(panel);
    auto box = memnew(VBoxContainer);
    box->add_theme_constant_override("separation", 8);
    panel->add_child(box);
    label(box, "ALTARIA", 36);
    label(box, local("intro"), 14);
    auto button = [&](const String &text, const Callable &call) {
        auto b = memnew(Button);
        b->set_text(text);
        b->set_custom_minimum_size(Vector2(0, 32));
        box->add_child(b);
        b->connect("pressed", call);
    };
    if (world) {
        button(local("resume"), callable_mp(this, &AltariaGame::on_resume));
        button(local("back"), callable_mp(this, &AltariaGame::on_worlds));
    }
    label(box, local("worlds"));
    world_list = memnew(ItemList);
    world_list->set_custom_minimum_size(Vector2(510, 115));
    box->add_child(world_list);
    button(local("load"), callable_mp(this, &AltariaGame::on_load));
    world_name = memnew(LineEdit);
    world_name->set_placeholder(local("name"));
    world_name->set_text("New World");
    box->add_child(world_name);
    seed_input = memnew(LineEdit);
    seed_input->set_placeholder(local("seed"));
    seed_input->set_text("random");
    box->add_child(seed_input);
    button(local("new"), callable_mp(this, &AltariaGame::on_create));
    auto languages = memnew(OptionButton);
    languages->add_item("English");
    languages->add_item("Português (Brasil)");
    languages->select(language == "en" ? 0 : 1);
    languages->connect("item_selected", callable_mp(this, &AltariaGame::on_language));
    box->add_child(languages);
    status = label(box, local("ready"), 13);
    status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    status->set_custom_minimum_size(Vector2(510, 30));
    button(local("quit"), callable_mp(this, &AltariaGame::on_quit));
    refresh_worlds();
}
void AltariaGame::build_hud() {
    if (hud)
        hud->queue_free();
    hud = memnew(Control);
    hud->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    hud->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    overlay->add_child(hud);
    telemetry = memnew(Label);
    telemetry->set_position(Vector2(14, 12));
    telemetry->add_theme_font_size_override("font_size", 15);
    telemetry->add_theme_color_override("font_outline_color", Color(0, 0, 0, .8));
    telemetry->add_theme_constant_override("outline_size", 4);
    hud->add_child(telemetry);
    crosshair = memnew(Label);
    crosshair->set_text("+");
    crosshair->add_theme_font_size_override("font_size", 24);
    crosshair->set_anchors_and_offsets_preset(Control::PRESET_CENTER);
    crosshair->set_position(Vector2(-7, -16));
    hud->add_child(crosshair);
    hotbar_label = memnew(Label);
    hotbar_label->set_anchors_and_offsets_preset(Control::PRESET_BOTTOM_WIDE);
    hotbar_label->set_offset(SIDE_LEFT, 14);
    hotbar_label->set_offset(SIDE_TOP, -100);
    hotbar_label->add_theme_font_size_override("font_size", 15);
    hotbar_label->add_theme_color_override("font_outline_color", Color(0, 0, 0, .8));
    hotbar_label->add_theme_constant_override("outline_size", 4);
    hud->add_child(hotbar_label);
    hud->hide();
}
void AltariaGame::refresh_worlds() {
    world_list->clear();
    saved_worlds.clear();
    DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path("user://worlds"));
    auto dirs = DirAccess::get_directories_at("user://worlds");
    dirs.sort();
    for (int i = 0; i < dirs.size(); i++) {
        String path = "user://worlds/" + dirs[i];
        if (!FileAccess::file_exists(path + String("/world.json")))
            continue;
        String error;
        auto d = read_json(path + String("/world.json"), error);
        if (!error.is_empty())
            continue;
        d["path"] = path;
        saved_worlds.push_back(d);
        world_list->add_item(String(d.get("name", dirs[i])) + "   [" +
                             String::num_int64(int64_t(d.get("seed", 0))) + "]");
    }
    if (!saved_worlds.is_empty())
        world_list->select(0);
    else
        status->set_text(local("empty"));
}
void AltariaGame::on_language(int64_t index) {
    language = index == 1 ? "pt_BR" : "en";
    Dictionary p;
    p["language"] = language;
    String e;
    write_json_atomic("user://preferences.json", p, e);
    build_menu();
}
void AltariaGame::on_create() {
    String seed_text = seed_input->get_text().strip_edges().to_lower();
    bool random = seed_text.is_empty() || seed_text == "random";
    if (!random && !seed_text.is_valid_int()) {
        status->set_text("Seed must be an integer or random.");
        return;
    }
    int64_t seed = random ? UtilityFunctions::randi_range(INT32_MIN, INT32_MAX) : seed_text.to_int();
    if (seed < INT32_MIN || seed > INT32_MAX) {
        status->set_text("Seed must fit a signed 32-bit integer.");
        return;
    }
    start_world(world_name->get_text().strip_edges(), seed);
}
void AltariaGame::on_load() {
    auto indices = world_list->get_selected_items();
    if (indices.is_empty()) {
        status->set_text("Select a world first.");
        return;
    }
    Dictionary entry = saved_worlds[indices[0]];
    load_world(entry["path"]);
}
void AltariaGame::set_game_input(bool active) {
    if (player)
        player->set_active(active);
    bool capture = active && get_window()->has_focus();
    Input::get_singleton()->set_mouse_mode(capture ? Input::MOUSE_MODE_CAPTURED : Input::MOUSE_MODE_VISIBLE);
    if (hud)
        hud->set_visible(world && !menu->is_visible() && !console.is_open());
}
void AltariaGame::on_resume() {
    if (!world)
        return;
    menu->hide();
    console.set_open(false);
    set_game_input(true);
}
void AltariaGame::on_worlds() {
    if (world && !save_world()) {
        status->set_text("Save failed. World kept open.");
        return;
    }
    stop_world();
    build_menu();
    set_game_input(false);
}
void AltariaGame::on_quit() {
    if (world && !save_world()) {
        status->set_text("Save failed. Check disk permissions before quitting.");
        return;
    }
    exiting = true;
    get_tree()->quit();
}
void AltariaGame::_notification(int what) {
    if (what == NOTIFICATION_WM_CLOSE_REQUEST && is_inside_tree())
        on_quit();
    if (world && player && what == NOTIFICATION_APPLICATION_FOCUS_OUT) {
        player->set_active(false);
        Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
    }
    if (world && player && what == NOTIFICATION_APPLICATION_FOCUS_IN && !menu->is_visible() &&
        !console.is_open())
        set_game_input(true);
}
void AltariaGame::stop_world() {
    if(terrain_surface){stage->remove_child(terrain_surface);memdelete(terrain_surface);terrain_surface=nullptr;}
    if (player)
        player->set_active(false);
    world = nullptr;
    player = nullptr;
    selection_is_physical = false;
    atmosphere = nullptr;
    physics_blocks = nullptr;
    built_world = nullptr;
    selected_built_id = 0;
    selection_shape = -1;
    selection_preview_hash = 0;
    selection_preview_shape = -1;
    selection_preview_cells = 0;
    selection = nullptr;
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
    hotbar.clear();
    set_game_input(false);
    if (console.input)
        console.set_open(false);
}
bool AltariaGame::start_random_world() {
    int64_t seed = UtilityFunctions::randi_range(INT32_MIN, INT32_MAX);
    return start_world("World " + String::num_int64(seed), seed);
}
bool AltariaGame::start_world(const String &name, int64_t seed) {
    if (!data_ready || name.is_empty() || name.length() > 80 || seed < INT32_MIN || seed > INT32_MAX) {
        if (status)
            status->set_text("Invalid world name or seed.");
        return false;
    }
    String error;
    auto config = load_generation_data(error);
    if (error.is_empty())
        blocks_data = load_block_data(error);
    if (error.is_empty())
        entity_data = read_json("res://game/player/data/entities.json", error);
    Settings s;
    Registry r;
    if (!error.is_empty() || !parse_settings(config, s, error) || !parse_registry(blocks_data, r, error)) {
        status->set_text(error);
        return false;
    }
    if (world && !save_world())
        return false;
    String id = String::num_uint64(Time::get_singleton()->get_unix_time_from_system() * 1000000) + "_" +
                String::num_int64(seed);
    String path = "user://worlds/" + id;
    if (DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(path)) !=
        OK) {
        status->set_text("Cannot create world directory.");
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
    if (!write_json_atomic(path + String("/world.json"), meta, error)) {
        status->set_text(error);
        return false;
    }
    return load_world(path);
}
bool AltariaGame::load_world(const String &path) {
    if (!data_ready)
        return false;
    if (world && !save_world())
        return false;
    String error;
    Dictionary data = read_json(path + String("/world.json"), error);
    Settings settings;
    Registry registry;
    auto current_blocks = load_block_data(error);
    if (!error.is_empty() || int(data.get("format_version", 0)) != 1 ||
        data.get("generation", Variant()).get_type() != Variant::DICTIONARY ||
        !parse_settings(data.get("generation", Dictionary()), settings, error) ||
        !parse_registry(current_blocks, registry, error)) {
        status->set_text(error.is_empty() ? String("Unsupported or corrupt world file.") : error);
        return false;
    }
    Dictionary ids = data.get("block_ids", Dictionary());
    Array keys = ids.keys();
    for (int i = 0; i < keys.size(); i++) {
        String key = keys[i];
        auto it = registry.keys.find(key.utf8().get_data());
        if (it == registry.keys.end() || it->second != int(ids[key])) {
            status->set_text("Saved block IDs changed. Restore blocks.json before opening this world.");
            return false;
        }
    }
    Dictionary overlay_ids = data.get("overlay_ids", Dictionary());
    Array overlay_keys = overlay_ids.keys();
    for (int i = 0; i < overlay_keys.size(); i++) {
        String key = overlay_keys[i];
        auto it = registry.overlay_keys.find(key.utf8().get_data());
        if (it == registry.overlay_keys.end() || it->second != int(overlay_ids[key])) {
            status->set_text("Saved overlay IDs changed. Restore overlays.json first.");
            return false;
        }
    }
    bool new_ids = false;
    for (const auto &o : registry.overlays)
        if (!o.key.empty() && !overlay_ids.has(String(o.key.c_str()))) {
            overlay_ids[String(o.key.c_str())] = o.id;
            new_ids = true;
        }
    data["overlay_ids"] = overlay_ids;
    for (const auto &block : registry.blocks)
        if (!block.key.empty() && !ids.has(String(block.key.c_str()))) {
            ids[String(block.key.c_str())] = block.id;
            new_ids = true;
        }
    if (new_ids) {
        data["block_ids"] = ids;
        if (!write_json_atomic(path + String("/world.json"), data, error)) {
            status->set_text(error);
            return false;
        }
    }
    auto candidate = memnew(VoxelWorld);
    Dictionary saved_generation=data.get("generation",Dictionary());
    bool migrate_surface=saved_generation.has("terrain_smoothing") || saved_generation.has("surface_ramps");
    if(migrate_surface) {
        saved_generation.erase("terrain_smoothing");saved_generation.erase("terrain_lod_max");
        saved_generation.erase("surface_ramps");saved_generation.erase("ramp_lod_max");
        saved_generation["surface_shapes"]=settings.surface_shapes;
        data["generation"]=saved_generation;
    }
    if (!candidate->configure(settings, registry, int(data.get("seed", 0)), path + String("/edits.log"),
                              error)) {
        memdelete(candidate);
        status->set_text(error);
        return false;
    }
    auto candidate_building = memnew(BuiltWorld);
    if (!candidate_building->configure(candidate, path + String("/construction.log"), error)) {
        memdelete(candidate_building);
        memdelete(candidate);
        status->set_text(error);
        return false;
    }
    stop_world();
    built_world = candidate_building;
    world_path = path;
    metadata = data;
    world = candidate;
    sequence = 0;
    generation_reported = false;
    save_timer = 0;
    stage = memnew(Node3D);
    stage->set_name("WorldSession");
    add_child(stage);
    stage->add_child(world);
    stage->add_child(built_world);
    create_stage();
    terrain_surface=memnew(TerrainSurface);terrain_surface->configure(world,built_world);stage->add_child(terrain_surface);
    for (auto &b : registry.blocks)
        if (!b.key.empty() && b.placeable && b.id != registry.air)
            hotbar.push_back(b.id);
    selected_block = 0;
    player = memnew(FlyingPlayer);
    Dictionary pd = entity_data.get("player", Dictionary());
    reach = std::clamp(float(pd.get("reach", 9)), 1.f, 16.f);
    player->configure(pd, [this](Vector3 minimum, Vector3 maximum) {
        return world && (world->collides_box(minimum, maximum) ||
                         (physics_blocks && physics_blocks->collides_box(minimum, maximum)) ||
                         (built_world && built_world->collides_box(minimum, maximum)));
    });
    stage->add_child(player);
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
    } else {
        // Prefer a dry scenic spawn, with enough clearance for creative flight.
        float best = -1e9;
        for (int z = -256; z <= 256; z += 64)
            for (int x = -256; x <= 256; x += 64) {
                auto c = world->generator()->column(x, z);
                float score = -std::abs(c.height - 120);
                if (c.height > settings.sea_level + 5 && score > best) {
                    best = score;
                    spawn = Vector3(x, c.height + 28, z);
                }
            }
    }
    player->set_position(spawn);
    player->set_view(float(saved.get("yaw", -.65)), float(saved.get("pitch", -.28)));
    player->set_flying(saved.get("flying", true));
    // A legacy generated-surface layout may have a different cap height after
    // migration back to slabs. Preserve the save while restoring safe headroom.
    for(int attempt=0;migrate_surface && attempt<32;attempt++) {
        auto bounds=player->body_bounds();
        if(!world->collides_box(bounds.position,bounds.position+bounds.size) &&
           !built_world->collides_box(bounds.position,bounds.position+bounds.size))break;
        spawn.y+=.125f;player->set_position(spawn);
    }
    world->update_viewer(spawn);
    built_world->update_viewer(spawn);
    physics_blocks = memnew(PhysicsBlocks);
    physics_blocks->configure(world, atmosphere->get_settings());
    stage->add_child(physics_blocks);
    physics_blocks->update_viewer(spawn);
    if (!physics_blocks->restore(metadata.get("debris", Array()), error))
        console.print("Debris restore failed: " + error);
    Array entities = metadata.get("entities", Array());
    for (int i = 0; i < entities.size(); i++)
        if (entities[i].get_type() == Variant::DICTIONARY) {
            Dictionary entity = entities[i];
            spawn_entity(entity.get("key", String()), entity);
        }
    build_menu();
    menu->hide();
    hud->show();
    set_game_input(true);
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
        console.print(error);
        UtilityFunctions::push_error(error);
    }
    selection = memnew(MeshInstance3D);
    selection->set_name("BlockSelection");
    Ref<BoxMesh> box;
    box.instantiate();
    box->set_size(Vector3(1.006, 1.006, 1.006));
    selection->set_mesh(box);
    Ref<StandardMaterial3D> mat;
    mat.instantiate();
    mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    mat->set_albedo(Color(1, 1, 1, .13));
    // Normal extrusion also separates ramp/slab faces that uniform cell scaling
    // leaves coplanar with the selected block.
    mat->set_grow_enabled(true);
    mat->set_grow(.004);
    selection->set_material_override(mat);
    selection->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    stage->add_child(selection);
    selection->hide();
}
bool AltariaGame::save_world() {
    if (!world || !player)
        return true;
    Dictionary p;
    auto position = player->get_position();
    Array xyz;
    xyz.push_back(position.x);
    xyz.push_back(position.y);
    xyz.push_back(position.z);
    p["position"] = xyz;
    p["yaw"] = player->get_rotation().y;
    p["pitch"] = player->get_pitch();
    p["flying"] = player->is_flying();
    metadata["player"] = p;
    if (atmosphere)
        metadata["atmosphere"] = atmosphere->save();
    if (physics_blocks)
        metadata["debris"] = physics_blocks->save();
    metadata["last_played"] = Time::get_singleton()->get_datetime_string_from_system(true);
    String error;
    if (!write_json_atomic(world_path + String("/world.json"), metadata, error)) {
        console.print(error);
        return false;
    }
    return true;
}
int64_t AltariaGame::place_block(Vector3 position, int block, const String &name, int rotation) {
    if (!built_world)
        return 0;
    int shape = shape_id(name.utf8().get_data());
    String error;
    auto id = built_world->place(position, block, shape==8 ? 0 : shape, rotation, error);
    if(id && shape==8 && !terrain_built(id,1)){built_world->remove(id,error);return 0;}
    if (!id)
        console.print(error);
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
        console.print(error);
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
bool AltariaGame::build_against(Vector3 hit, Vector3 normal, Vector3 source, uint8_t source_shape) {
    if (hotbar.empty())
        return false;
    auto from = shape_bounds(source_shape), to = shape_bounds(pack_shape(selected_shape==int(BlockShape::Terrain)?0:selected_shape, shape_rotation));
    Vector3 preferred = source + Vector3((from.first[0] + from.second[0] - to.first[0] - to.second[0]) * .5f,
                                         (from.first[1] + from.second[1] - to.first[1] - to.second[1]) * .5f,
                                         (from.first[2] + from.second[2] - to.first[2] - to.second[2]) * .5f);
    String error;
    auto id = built_world->attach(hit, normal, preferred, source, source_shape, hotbar[selected_block],
                                  selected_shape, shape_rotation, error);
    if(id && selected_shape==8 && !terrain_surface->can_apply(id,{})){built_world->remove(id,error);id=0;error="Terrain needs an exposed block with a normal base.";}
    if (!id)
        console.print(error);
    return id != 0;
}
bool AltariaGame::build_over_terrain(uint64_t id,IVec3 cell) {
    if(hotbar.empty() || selected_shape==8 || !terrain_surface)return false;
    auto base=terrain_surface->base_for(id,cell);if(!base.valid)return false;
    // Fill the cap's cell from the ordinary support plane, never from the
    // curved hit height. This punches through/replaces the cosmetic surface.
    String error;
    if(id) {
        bool ok=built_world->replace_cap(id,hotbar[selected_block],selected_shape,shape_rotation,error);
        if(!ok)console.print(error);return ok;
    }
    // Use the same player occupancy guard as ordinary construction.
    auto body=player->body_bounds();auto player_lo=body.position,player_hi=body.position+body.size;
    Vector3 p(cell.x,cell.y,cell.z);
    if(shape_intersects(pack_shape(selected_shape,shape_rotation),{player_lo.x-p.x,player_lo.y-p.y,player_lo.z-p.z},{player_hi.x-p.x,player_hi.y-p.y,player_hi.z-p.z}))return false;
    if(!set_voxel(cell.x,cell.y,cell.z,hotbar[selected_block]))return false;
    return selected_shape==0 || shape_voxel(cell.x,cell.y,cell.z,String(shape_name(selected_shape)),shape_rotation);
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
        console.print(error);
        return false;
    }
    if (!built_world->remove(id, error)) {
        physics_blocks->discard_last();
        console.print(error);
        return false;
    }
    return save_world();
}
bool AltariaGame::set_voxel(int x, int y, int z, int block) {
    if (!world || block < 0 || block > 4095)
        return false;
    String error;
    EditCommand cmd{1, ++sequence, {x, y, z}, BlockId(block)};
    if (!world->edit(cmd, error)) {
        console.print(error);
        return false;
    }
    auto appearance=world->appearance_at({x,y,z});
    if(world->terrain_enabled({x,y,z})) {
        appearance.terrain=0;
        if(!world->edit_surface({1,++sequence,{x,y,z},appearance},error)){console.print(error);return false;}
    }
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
    if (shape == int(BlockShape::Terrain)) return toggle_terrain_at({x,y,z},1);
    auto a = world->appearance_at({x, y, z});
    a.shape = shape;
    a.rotation = rotation;
    String error;
    if (!world->edit_surface({1, ++sequence, {x, y, z}, a}, error)) {
        console.print(error);
        return false;
    }
    return true;
}
Dictionary AltariaGame::terrain_hit(Vector3 origin,Vector3 direction,double distance) const {
    Dictionary d;if(!terrain_surface || !origin.is_finite() || !direction.is_finite() || direction.length_squared()<.0001 || distance<0 || distance>10000)return d;
    auto hit=terrain_surface->pick(origin,direction.normalized(),distance);if(!hit.hit)return d;
    d["position"]=origin+direction.normalized()*hit.distance;d["normal"]=hit.normal;d["built_id"]=int64_t(hit.built);
    d["cell"]=Vector3i(hit.cell.x,hit.cell.y,hit.cell.z);d["block"]=int(hit.block);
    auto base=terrain_surface->base_for(hit.built,hit.cell);d["has_base"]=base.valid;d["base_position"]=base.position;d["cap_origin"]=base.cap_position;return d;
}
bool AltariaGame::terrain_built(int64_t id,int mode) {
    auto b=built_world ? built_world->get(id) : nullptr;
    if(!b || mode < -1 || mode > 1)return false;
    auto a=b->appearance;a.terrain=mode<0 ? (a.terrain>0 ? 0:1) : mode;
    if(a.terrain>0 && !terrain_surface->can_apply(id,{})){console.print("Terrain needs an exposed block with a normal base.");return false;}
    String error;
    if(!built_world->appearance(id,a,error)){console.print(error);return false;}
    return true;
}
bool AltariaGame::toggle_terrain_at(IVec3 cell, int mode) {
    if (!world)
        return false;
    auto a = world->appearance_at(cell);
    if (!world->registry().get(world->block_at(cell.x,cell.y,cell.z)).solid) return false;
    a.terrain = mode < 0 ? (world->terrain_enabled(cell) ? 0 : 1) : (mode != 0 ? 1 : 0);
    if(a.terrain>0 && !terrain_surface->can_apply(0,cell)){console.print("Terrain needs an exposed block with a normal base.");return false;}
    String error;
    if (!world->edit_surface({1,++sequence,cell,a},error)) { console.print(error); return false; }
    return true;
}
Dictionary AltariaGame::get_temperature(Vector3 p) const {
    if (!atmosphere || !p.is_finite() || std::abs(p.x) > 1000000 || std::abs(p.z) > 1000000 ||
        std::abs(p.y) > 10000)
        return {};
    auto out = atmosphere->sample_temperature(p);
    if (physics_blocks) {
        auto influence = physics_blocks->thermal_influence(p);
        float total = std::min(float(out["cold_delta_c"]), influence.first) +
                      std::max(float(out["warm_delta_c"]), influence.second);
        out["feels_like_c"] = float(out["feels_like_c"]) + total - float(out["block_delta_c"]);
        out["block_delta_c"] = total;
    }
    if (built_world) {
        auto influence = built_world->thermal(p);
        float cold = std::min(float(out["cold_delta_c"]), influence.first),
              warm = std::max(float(out["warm_delta_c"]), influence.second);
        if (physics_blocks) {
            auto d = physics_blocks->thermal_influence(p);
            cold = std::min(cold, d.first);
            warm = std::max(warm, d.second);
        }
        float value = cold + warm;
        out["feels_like_c"] = float(out["feels_like_c"]) + value - float(out["block_delta_c"]);
        out["block_delta_c"] = value;
    }
    return out;
}
bool AltariaGame::release_voxel(int x, int y, int z, bool tree) {
    if (!physics_blocks)
        return false;
    String error;
    if (!physics_blocks->release({x, y, z}, tree, error)) {
        console.print(error);
        return false;
    }
    return save_world();
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
        console.print(error);
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
        console.print(error);
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
    d["terrain"] = world->terrain_enabled({x,y,z});
    d["original_shape"] = String(a.shape==255 ? "generated" : shape_name(a.shape));
    auto shape = world->shape_at(x, y, z);
    d["shape"] = shape_name(shape_type(shape));
    d["rotation"] = altaria::shape_rotation(shape);
    d["shape_code"] = int(shape);
    auto ramp=ramp_spec(shape);
    if(ramp.valid) {d["ramp_corner"]=ramp_corner_name(ramp.corner);d["ramp_rise"]=ramp.rise();d["ramp_low"]=ramp.low();}
    auto terrain = terrain_spec(shape);
    if (terrain.valid) {
        Array h;
        for (int v : terrain.h)
            h.push_back(v);
        d["terrain_heights"] = h;
    }
    return d;
}
RayHit AltariaGame::target_block() const {
    if (!world || !player)
        return {};
    auto camera = player->get_camera();
    auto origin = camera->get_global_position();
    auto dir = -camera->get_global_basis().get_column(2);
    auto hit=raycast_shapes(
        {origin.x, origin.y, origin.z}, {dir.x, dir.y, dir.z}, reach,
        [this](int x, int y, int z) { return world->terrain_meshed({x,y,z}) ? world->registry().air : world->block_at(x, y, z); },
        [this](int x, int y, int z) { return world->shape_at(x, y, z); }, world->registry());
    if(terrain_surface) {auto smooth=terrain_surface->pick(origin,dir,reach);
        if(smooth.hit && !smooth.built && (!hit.hit || smooth.distance<hit.distance))
            hit={true,smooth.cell,smooth.cell,smooth.distance,smooth.block};
    }
    return hit;
}
BuiltWorld::Hit AltariaGame::pick_built(Vector3 origin,Vector3 direction,float distance) const {
    auto hit=built_world->pick(origin,direction,distance);
    if(terrain_surface){auto smooth=terrain_surface->pick(origin,direction,distance);
        if(smooth.hit && smooth.built && smooth.distance<hit.distance)
            hit={smooth.built,smooth.distance,{smooth.normal.x,smooth.normal.y,smooth.normal.z}};
    }
    return hit;
}
void AltariaGame::show_smooth_selection(uint64_t built,IVec3 cell) {
    if(!terrain_surface)return;
    selection->set_mesh(terrain_surface->selection_mesh(built,cell));
    selection->set_global_transform(Transform3D());selection_shape=-2;selection_preview_hash=0;
    selection->set_visible(!menu->is_visible() && !console.is_open());
}
void AltariaGame::show_selection(BlockId block, uint8_t shape, const Transform3D &transform) {
    if (selection_shape != shape || selection_preview_hash != 0) {
        Appearance a;
        unpack_shape(shape,a);
        selection->set_mesh(world->block_mesh({{{0, 0, 0}, block}}, {a}, Column()));
        selection_shape = shape;
        selection_preview_hash = 0;
        selection_preview_shape = int(shape);
        selection_preview_cells = 1;
    }
    // Expand in the selected cell's local space so rotated physical forms fit exactly.
    selection->set_global_transform(transform * Transform3D(Basis().scaled(Vector3(1.006f, 1.006f, 1.006f)),
                                                            Vector3(-.003f, -.003f, -.003f)));
    selection->set_visible(!menu->is_visible() && !console.is_open());
}
void AltariaGame::show_smooth_preview(uint64_t built,IVec3 cell) {
    auto mesh=terrain_surface ? terrain_surface->preview(built,cell) : Ref<ArrayMesh>();
    if(mesh.is_valid()) {
        selection->set_mesh(mesh);selection->set_global_transform(Transform3D());selection_shape=-3;
        auto state=terrain_surface->stats();selection_preview_hash=int64_t(state["terrain_preview_version"]);
        selection_preview_cells=state["terrain_preview_cells"];selection_preview_shape=8;
        selection->set_visible(!menu->is_visible() && !console.is_open());
    } else if(built) {
        auto source=built_world->get(built);if(!source)return;
        if(built_world->terrain_meshed(built))show_smooth_selection(built,{});
        else {auto p=source->position();show_selection(source->block,source->shape(),Transform3D(Basis(),Vector3(p[0],p[1],p[2])));}
    } else if(world->terrain_meshed(cell))show_smooth_selection(0,cell);
    else show_selection(world->block_at(cell.x,cell.y,cell.z),world->shape_at(cell.x,cell.y,cell.z),Transform3D(Basis(),Vector3(cell.x,cell.y,cell.z)));
}
void AltariaGame::show_terrain_preview(IVec3 center,BlockId) {show_smooth_preview(0,center);}
void AltariaGame::update_target(bool edit, bool place) {
    if (!world || !player || !selection)
        return;
    selection_is_physical = false;
    selected_built_id = 0;
    auto hit = target_block();
    auto camera = player->get_camera();
    auto direction = -camera->get_global_basis().get_column(2);
    auto physical = physics_blocks->pick(camera->get_global_position(), direction, reach);
    auto constructed = pick_built(camera->get_global_position(), direction, reach);
    if (constructed.id && (!hit.hit || constructed.distance < hit.distance) &&
        constructed.distance < physical.distance) {
        const auto *piece = built_world->get(constructed.id);
        auto p = piece->position();
        selected_built_id = piece->id;
        Vector3 origin(p[0], p[1], p[2]);
        uint8_t form = piece->shape();
        if(selected_shape==int(BlockShape::Terrain))show_smooth_preview(constructed.id,{});
        else if(built_world->terrain_meshed(constructed.id))show_smooth_selection(constructed.id,{});
        else show_selection(piece->block,form,Transform3D(Basis(),origin));
        if (edit) {
            if (place) {
                if (selected_shape == int(BlockShape::Terrain) &&
                    !Input::get_singleton()->is_physical_key_pressed(Key::KEY_SHIFT)) {
                    terrain_built(constructed.id);
                } else if(piece->appearance.terrain>0 && selected_shape!=8 && terrain_surface->can_apply(constructed.id,{}))
                    build_over_terrain(constructed.id,{});
                else
                    build_against(camera->get_global_position() + direction * constructed.distance,
                                  Vector3(constructed.normal[0], constructed.normal[1], constructed.normal[2]),
                                  origin, form);
            } else {
                String error;
                if (!built_world->remove(constructed.id, error))
                    console.print(error);
            }
        }
        return;
    }
    if (physical.body >= 0 && (!hit.hit || physical.distance < hit.distance)) {
        Transform3D transform;
        BlockId block;
        uint8_t shape;
        if (physics_blocks->selection_for(physical, transform, block, shape)) {
            selection_is_physical = true;
            show_selection(block, shape, transform);
        } else
            selection->hide();
        if (edit && !place) {
            String error;
            if (physics_blocks->detach(physical, direction * 2.0f, error))
                save_world();
            else
                console.print(error);
        }
        return;
    }
    selection->set_visible(hit.hit && !menu->is_visible() && !console.is_open());
    if (!hit.hit)
        return;
    auto shape = world->shape_at(hit.cell.x, hit.cell.y, hit.cell.z);
    if (selected_shape == int(BlockShape::Terrain)) show_terrain_preview(hit.cell,hit.block);
    else if (world->terrain_meshed(hit.cell)) show_smooth_selection(0,hit.cell);
    else
        show_selection(hit.block, shape, Transform3D(Basis(), Vector3(hit.cell.x, hit.cell.y, hit.cell.z)));
    if (edit) {
        if (place) {
            if (selected_shape == int(BlockShape::Terrain) &&
                !Input::get_singleton()->is_physical_key_pressed(Key::KEY_SHIFT)) {
                toggle_terrain_at(hit.cell);
                return;
            }
            if (hotbar.empty())
                return;
            if(world->terrain_enabled(hit.cell) && selected_shape!=8 && terrain_surface->can_apply(0,hit.cell)) {
                build_over_terrain(0,hit.cell);return;
            }
            auto position = camera->get_global_position() + direction * hit.distance;
            auto source = Vector3(hit.cell.x, hit.cell.y, hit.cell.z);
            std::array<float, 3> normal;
            auto local = camera->get_global_position() - source;
            if(world->terrain_meshed(hit.cell) && terrain_surface) {
                auto smooth=terrain_surface->pick(camera->get_global_position(),direction,reach);
                normal={smooth.normal.x,smooth.normal.y,smooth.normal.z};
            } else shape_ray(shape, {local.x, local.y, local.z}, {direction.x, direction.y, direction.z}, reach,&normal);
            build_against(position, Vector3(normal[0], normal[1], normal[2]), source, shape);
        } else if (world->registry().get(hit.block).wood)
            release_voxel(hit.cell.x, hit.cell.y, hit.cell.z, true);
        else
            set_voxel(hit.cell.x, hit.cell.y, hit.cell.z, world->registry().air);
    }
}
void AltariaGame::update_hud() {
    if (!world || !player)
        return;
    auto s = world->stats();
    if (!generation_reported && double(s["world_ready_ms"]) >= 0) {
        generation_reported = true;
        String report = "Generation complete: first tile " + String::num(double(s["first_tile_ms"]), 1) +
                        " ms | all LODs " + String::num(double(s["world_ready_ms"]), 1) + " ms";
        console.print(report);
        UtilityFunctions::print("WORLD_STREAM_READY ", report);
    }
    Vector3 p = player->get_position();
    telemetry->set_text(
        String(metadata.get("name", "World")) + "  |  Seed " +
        String::num_int64(int64_t(metadata.get("seed", 0))) + "  |  " +
        String::num_int64(Engine::get_singleton()->get_frames_per_second()) + " FPS\nXYZ " +
        String::num(p.x, 1) + " " + String::num(p.y, 1) + " " + String::num(p.z, 1) + "  |  " +
        (player->is_flying() ? String("FLY") : String("WALK")) + "  |  Fullbright " +
        (world->is_fullbright() ? String("ON") : String("OFF")) + "\nLOD [" +
        String::num_int64(int(s["lod0"])) + ", " + String::num_int64(int(s["lod1"])) + ", " +
        String::num_int64(int(s["lod2"])) + ", " + String::num_int64(int(s["lod3"])) + ", " +
        String::num_int64(int(s["lod4"])) + ", " + String::num_int64(int(s["lod5"])) + "]  |  Queue " +
        String::num_int64(int(s["pending"])) + "  |  Edits " + String::num_int64(int(s["edits"])));
    String bar;
    for (int i = std::max(0, selected_block - 3); i < std::min(int(hotbar.size()), selected_block + 4); i++) {
        const auto &b = world->registry().get(hotbar[i]);
        bar += (i == selected_block ? "[" : " ") + String::num_int64(i + 1) + " " +
               String((language == "pt_BR" ? b.name_pt : b.name).c_str()) +
               (i == selected_block ? "] " : "  ");
    }
    telemetry->set_text(telemetry->get_text() + "\nGeneration: first tile " +
                        String::num(double(s["first_tile_ms"]), 1) + " ms | ready " +
                        (double(s["world_ready_ms"]) >= 0
                             ? String::num(double(s["world_ready_ms"]), 1) + " ms"
                             : String("loading...")));
    auto column = world->generator()->column(int(std::floor(p.x)), int(std::floor(p.z)));
    telemetry->set_text(telemetry->get_text() +
                        "\nBiome: " + String(world->settings().biomes[column.biome].name.c_str()) +
                        " | view " + String::num(world->settings().view_distance / 1000.f, 1) + " km");
    auto temperature = get_temperature(p);
    auto climate = atmosphere->state();
    telemetry->set_text(telemetry->get_text() + " | " + String::num(float(climate["hour"]), 1) + " h | " +
                        String(climate["weather"]) + "\nTemperature " +
                        String::num(float(temperature["feels_like_c"]), 1) + " °C | Jolt bodies " +
                        String::num_int64(int(physics_blocks->stats()["physics_bodies"])));
    hotbar_label->set_text(
        bar + "\nShape: " + String(shape_name(selected_shape)) + " | rotation " +
        String::num_int64(shape_rotation * 90) +
        "° | Hold R + wheel: shape | T: terrain | Q: rotate | G: physics | LMB: cut tree / detach part\n" +
        local("controls"));
}
void AltariaGame::_process(double delta) {
    if (!world || !player)
        return;
    world->update_viewer(player->get_position());
    built_world->update_viewer(player->get_position());
    physics_blocks->update_viewer(player->get_position());
    hud_timer += delta;
    save_timer += delta;
    if (hud_timer > .2) {
        hud_timer = 0;
        update_hud();
    }
    if (save_timer > world->settings().autosave_seconds) {
        save_timer = 0;
        save_world();
    }
    update_target();
}
void AltariaGame::on_console_gui_input(const Ref<InputEvent> &event) {
    Ref<InputEventKey> key = event;
    if (key.is_valid() && key->is_pressed()) {
        if (key->get_keycode() == Key::KEY_UP) {
            console.navigate(-1);
            console.input->accept_event();
        }
        if (key->get_keycode() == Key::KEY_DOWN) {
            console.navigate(1);
            console.input->accept_event();
        }
    }
}
void AltariaGame::_input(const Ref<InputEvent> &event) {
    Ref<InputEventKey> key = event;
    if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
        Key k = key->get_keycode();
        if ((k == Key::KEY_F1 || k == Key::KEY_QUOTELEFT) && world && !menu->is_visible()) {
            bool open = !console.is_open();
            console.set_open(open);
            set_game_input(!open);
            get_viewport()->set_input_as_handled();
            return;
        }
        if (k == Key::KEY_ESCAPE && world) {
            if (console.is_open()) {
                console.set_open(false);
                set_game_input(true);
            } else {
                menu->set_visible(!menu->is_visible());
                set_game_input(!menu->is_visible());
            }
            get_viewport()->set_input_as_handled();
            return;
        }
        if (!world || menu->is_visible() || console.is_open())
            return;
        if (k == Key::KEY_Q)
            shape_rotation = (shape_rotation + 1) % 4;
        if (k == Key::KEY_T) {
            if (auto id = target_built()) {
                terrain_built(id);
            } else {
                auto hit = target_block();
                if (hit.hit)
                    toggle_terrain_at(hit.cell);
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
        if (k == Key::KEY_F)
            player->set_flying(!player->is_flying());
        if (k >= Key::KEY_1 && k <= Key::KEY_8) {
            int i = int(k) - int(Key::KEY_1);
            if (i < int(hotbar.size()))
                selected_block = i;
        }
    }
    if (!world || menu->is_visible() || console.is_open())
        return;
    Ref<InputEventMouseButton> mouse = event;
    if (mouse.is_valid() && mouse->is_pressed()) {
        auto b = mouse->get_button_index();
        if (b == MouseButton::MOUSE_BUTTON_LEFT)
            update_target(true, false);
        if (b == MouseButton::MOUSE_BUTTON_RIGHT)
            update_target(true, true);
        if (Input::get_singleton()->is_physical_key_pressed(Key::KEY_R)) {
            if (b == MouseButton::MOUSE_BUTTON_WHEEL_UP)
                selected_shape = (selected_shape + SHAPE_COUNT - 1) % SHAPE_COUNT;
            if (b == MouseButton::MOUSE_BUTTON_WHEEL_DOWN)
                selected_shape = (selected_shape + 1) % SHAPE_COUNT;
        } else if (!hotbar.empty()) {
            if (b == MouseButton::MOUSE_BUTTON_WHEEL_UP)
                selected_block = (selected_block + int(hotbar.size()) - 1) % int(hotbar.size());
            if (b == MouseButton::MOUSE_BUTTON_WHEEL_DOWN)
                selected_block = (selected_block + 1) % int(hotbar.size());
        }
    }
}
void AltariaGame::submit_console(const String &text) {
    console.remember(text);
    console.print("> " + text);
    console.print(execute_command(text));
}
static bool number(const String &s, float &value) {
    if (!s.is_valid_float())
        return false;
    value = s.to_float();
    return std::isfinite(value);
}
String AltariaGame::execute_command(const String &text) {
    auto args = text.strip_edges().split(" ", false);
    if (args.is_empty())
        return "";
    String command = args[0].to_lower();
    if (command.begins_with("/"))
        command = command.substr(1);
    if (command == "help")
        return "world new [seed] | fullbright [on|off] | fly [on|off] | speed <1..150> | tp <x> <y> "
               "<z>\nseed | stats | save | "
               "paint <#RRGGBB|reset> [preserve brightness] | overlay <key|clear|reset> | biome [key]\nblock "
               "<namespace:key> | spawn <namespace:key> | fog <#RRGGBB> [start end] | fog biome\noutline "
               "<on|off> | pixelation <on|off> | godrays <on|off> | help\nshape <name> | shape at x y z <name> "
               "[rotation] | terrain [at x y z] [on|off|toggle] | cut | physics [at x y z|clear]\ntime "
               "<hour|pause|resume> | weather <clear|rain|storm> | wind <0..3> | temperature | light <#RRGGBB> "
               "[energy radius]";
    if (command == "world") {
        if (args.size() < 2 || args.size() > 3 || args[1] != "new")
            return "Usage: world new [seed] (omit seed for a random world)";
        int64_t seed = UtilityFunctions::randi_range(INT32_MIN, INT32_MAX);
        if (args.size() == 3) {
            if (!args[2].is_valid_int())
                return "Seed must be a signed 32-bit integer.";
            seed = args[2].to_int();
            if (seed < INT32_MIN || seed > INT32_MAX)
                return "Seed must be a signed 32-bit integer.";
        }
        bool keep_console = console.is_open();
        bool keep_fullbright = world && world->is_fullbright();
        if (!start_world("World " + String::num_int64(seed), seed))
            return "Could not generate world. Check the world menu for the error.";
        if (keep_fullbright)
            execute_command("fullbright on");
        if (keep_console) {
            console.set_open(true);
            set_game_input(false);
        }
        return "Generating new world. Seed: " + String::num_int64(seed) +
               ". Previous world saved; timing appears on completion.";
    }
    if (!world)
        return "No world is open.";
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
        float energy = 3, radius = 12;
        if (args.size() < 2 || args.size() > 4 || !Color::html_is_valid(args[1]) ||
            (args.size() > 2 && !number(args[2], energy)) || (args.size() > 3 && !number(args[3], radius)) ||
            energy < .1f || energy > 20 || radius < 1 || radius > 64)
            return "Usage: light <#RRGGBB> [energy .1..20] [radius 1..64] (max 16)";
        if (int(atmosphere->state()["colored_lights"]) >= 16)
            return "Colored light budget reached.";
        auto camera = player->get_camera();
        atmosphere->add_light(camera->get_global_position() - camera->get_global_basis().get_column(2) * 3,
                              Color::html(args[1]), energy, radius);
        save_world();
        return "Colored light placed.";
    }
    if (command == "terrain") {
        IVec3 p;
        int mode = -1;
        if (args.size() >= 5 && args[1] == "at" && args[2].is_valid_int() && args[3].is_valid_int() &&
            args[4].is_valid_int()) {
            int64_t x = args[2].to_int(), y = args[3].to_int(), z = args[4].to_int();
            if (std::abs(x) > 1000000 || std::abs(z) > 1000000 || y <= world->settings().min_y ||
                y >= world->settings().max_y)
                return "Coordinates outside world bounds.";
            p = {int(x), int(y), int(z)};
            if (args.size() == 6) {
                if (args[5] == "on")
                    mode = 1;
                else if (args[5] == "off")
                    mode = 0;
                else if (args[5] != "toggle")
                    return "Usage: terrain [at x y z] [on|off|toggle]";
            } else if (args.size() > 6)
                return "Usage: terrain [at x y z] [on|off|toggle]";
        } else if (args.size() <= 2) {
            if (args.size() == 2) {
                if (args[1] == "on")
                    mode = 1;
                else if (args[1] == "off")
                    mode = 0;
                else if (args[1] != "toggle")
                    return "Usage: terrain [at x y z] [on|off|toggle]";
            }
            auto hit = target_block();
            if (!hit.hit)
                return "Look at a block within reach, or use terrain at x y z [on|off|toggle].";
            p = hit.cell;
        } else
            return "Usage: terrain [at x y z] [on|off|toggle]";
        return toggle_terrain_at(p, mode) ? "Terrain modifier updated." : "Invalid supporting block.";
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
            return "Usage: shape <cube|slab|vertical_slab|ramp|stairs|roof|pillar|mini_pillar|terrain>";
        int value = shape_id(args[1].utf8().get_data());
        if (value < 0)
            return "Unknown shape.";
        selected_shape = value;
        return "Selected shape: " + args[1];
    }
    if (command == "cut" || command == "physics") {
        if (command == "physics" && args.size() == 2 && args[1] == "clear") {
            physics_blocks->clear();
            save_world();
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
            if (std::abs(x) > 1000000 || std::abs(z) > 1000000 || y < -512 || y > 1024)
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
    if (command == "save")
        return save_world() ? String("World saved.") : String("Save failed.");
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
            std::abs(x) > 999990 || std::abs(z) > 999990 || y < -510 || y > 3000)
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
        auto found = std::find(hotbar.begin(), hotbar.end(), it->second);
        if (found == hotbar.end())
            return "Block is not placeable.";
        selected_block = int(found - hotbar.begin());
        return "Selected " + args[1];
    }
    if (command == "spawn") {
        if (args.size() != 2)
            return "Usage: spawn <namespace:key>";
        return spawn_entity(args[1]) ? String("Entity spawned.")
                                     : String("Unknown entity or unsupported controller.");
    }
    return "Unknown command. Type help.";
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
    if(terrain_surface) {d.merge(terrain_surface->stats(),true);d["pending"]=int(d["pending"])+int(d["terrain_pending"]);}
    d["selection_built_id"] = int64_t(selected_built_id);
    d["selection_visible"] = selection && selection->is_visible_in_tree();
    d["selection_physical"] = selection_is_physical;
    if (selection)
        d["selection_transform"] = selection->get_global_transform();
    d["selected_shape"] = shape_name(selected_shape);
    d["shape_rotation"] = shape_rotation;
    d["selection_preview_shape"] = selection_preview_shape;
    d["selection_preview_cells"] = selection_preview_cells;
    if (player) {
        d["player_position"] = player->get_position();
        d["flying"] = player->is_flying();
        d["jump_count"] = player->get_jump_count();
        d["grounded"] = player->is_grounded();
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
    if (world && !exiting)
        save_world();
    if (player)
        player->set_active(false);
    if (world) {
        world->set_process(false);
    }
    Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
}
} // namespace godot
