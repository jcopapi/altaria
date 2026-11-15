#pragma once
#include "arc_hotbar.h"
#include "diorama_minimap.h"
#include "game_log.h"
#include "stamina_ring.h"
#include "game/entities/entity_world.h"
#include "game/player/flying_player.h"
#include "game/player/sandbox_inventory.h"
#include "game/world_generation/built_world.h"
#include "game/world_generation/physics_blocks.h"
#include "game/world_generation/voxel_world.h"
#include "game/world_generation/block_shapes.h"
#include "game/world_generation/world_atmosphere.h"
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <unordered_map>
namespace godot {
class CrabConsole;
class ItemList;
class LineEdit;
class AltariaGame : public Node {
    GDCLASS(AltariaGame, Node)
    static AltariaGame *active_game;
    CanvasLayer *overlay = nullptr;
    Control *hud = nullptr;
    Control *first_person_hands = nullptr;
    Control *loading_panel = nullptr;
    Label *loading_status = nullptr;
    bool loading_world = false;
    Label *telemetry = nullptr, *hotbar_label = nullptr;
    CrosshairDot *crosshair = nullptr;
    ArcHotbar *arc_hotbar = nullptr;
    StaminaRing *stamina_ring = nullptr;
    DioramaMinimap *diorama_minimap = nullptr;
    Control *cheat_inventory = nullptr;
    Label *cheat_title = nullptr;
    LineEdit *cheat_search = nullptr;
    ItemList *cheat_blocks = nullptr;
    Label *cheat_empty = nullptr;
    enum class CheatPickerPurpose : int {
        Normal = 0,
        WandFill = 1,
        WandReplaceFrom = 2,
        WandReplaceTo = 3,
    };
    CheatPickerPurpose cheat_picker_purpose = CheatPickerPurpose::Normal;
    bool cheat_enabled = false;
    bool fly_hotkey_enabled = false;
    bool noclip_hotkey_enabled = false;
    struct GrassHitAnim {
        bool active = false;
        bool apply_grass = false;
        bool apply_terrain = false;
        bool destroyed = false;
        float elapsed = 0.0f;
        float top_height = 1.0f;
        Vector3 box_min;
        Vector3 box_max = Vector3(-1.0f, -1.0f, -1.0f);
        Vector3 terrain_min;
        Vector3 terrain_max = Vector3(-1.0f, -1.0f, -1.0f);
        Vector3 squish = Vector3(1.15f, 0.64f, 1.15f);
        Vector3 rebound = Vector3(0.96f, 1.07f, 0.96f);
        Vector3 knock;
    } grass_hit_anim;
    Node3D *stage = nullptr;
    VoxelWorld *world = nullptr;
    FlyingPlayer *player = nullptr;
    WorldAtmosphere *atmosphere = nullptr;
    PhysicsBlocks *physics_blocks = nullptr;
    BuiltWorld *built_world = nullptr;
    uint64_t selected_built_id = 0;
    int selected_shape = 0, shape_rotation = 0, selection_shape = -1;
    struct DamagedTarget {
        int kind = 0; // 1=voxel, 2=built, 3=resource, 4=physics
        altaria::IVec3 cell{};
        uint64_t id = 0;
        altaria::BlockId block = 0;
        uint8_t shape = 0;
        Transform3D transform;
        int hits = 0;
        float timer = 0.0f;
        MeshInstance3D *overlay = nullptr;
        int overlay_shape = -1;
    };
    struct DroppedItem {
        Node3D *root = nullptr;
        altaria::BlockId block = 0;
        int amount = 1;
        Vector3 start_pos;
        Vector3 hop_offset;
        float age = 0.0f;
        float duration = 0.62f;
        float spin_speed = 4.5f;
    };
    std::vector<DroppedItem> dropped_items;
    // EnTT-backed creatures (chickens, ...); see game/entities/entity_world.h.
    altaria::EntityWorld creatures;
    String creature_command(const PackedStringArray &args);
    // Riding: double-tapping crouch dismounts.
    bool mount_crouch_prev = false;
    float mount_crouch_timer = 0.0f;
    void update_mount_input(float dt);
    void sync_rider();
    void dismount_player();
    String mount_command(const PackedStringArray &args);
    void spawn_item_drop(altaria::BlockId block, const Vector3 &world_pos,
                         const altaria::Appearance &appearance = altaria::Appearance(), int amount = 1);
    void update_dropped_items(float dt);
    void clear_dropped_items();
    MeshInstance3D *selection = nullptr;
    Ref<StandardMaterial3D> break_materials[5];
    Ref<Texture2D> break_textures[5];
    std::vector<DamagedTarget> damaged_targets;
    float crosshair_sample_timer = 0.0f;
    float lmb_cooldown_timer = 0.0f;
    float lmb_repeat_timer = 0.0f;
    float rmb_repeat_timer = 0.0f;
    bool pending_break_hit = false;
    float pending_break_hit_timer = 0.0f;
    bool lmb_held = false;
    bool rmb_held = false;
    MeshInstance3D *placement_preview = nullptr;
    int preview_block = -1, preview_shape = -1;
    altaria::Appearance preview_appearance;
    int hand_held_block = -1, hand_held_shape = -1, hand_held_rot = -1;
    std::string hand_equipped_tool;
    bool console_tool_override = false;
    // World-anchored climbing grips: one hand holds a block edge while the other
    // reaches for the next one, so holds stay fixed in the world as you climb.
    struct ClimbHand {
        Vector3 hold, from;
        float t = 1.0f;
        bool reaching = false;
    };
    ClimbHand climb_hands[2];
    bool climb_active = false;
    void update_climb_hands(float dt, const Vector3 &eye, const Vector3 &wall_normal, const Vector3 &wall_right,
                            const Vector3 &wall_point, float vertical_speed, Vector3 out_world[2], bool out_reaching[2]);
    Vector3 prev_player_pos = Vector3();
    bool prev_player_pos_valid = false;
    Vector3 last_action_world_pos = Vector3();
    float last_action_world_timer = 0.0f;
    std::string current_equipped_tool() const;
    bool can_break_block(altaria::BlockId block, bool is_physics_debris = false) const;
    void deny_break(altaria::BlockId block, bool is_physics_debris);
    std::unordered_map<int, Color> particle_color_cache;
    Color block_particle_color(altaria::BlockId block, const Vector3 &pos);
    // style: 0 = lit block chips, 1 = unshaded sparks, 2 = translucent dust puff
    void spawn_particles(const Color &color, const Vector3 &origin, const Vector3 &direction, int amount, float speed,
                         float spread_deg, float size, float lifetime, const Vector3 &emission_box, int style);
    Ref<ArrayMesh> build_selection_outline_mesh(uint8_t shape) const;
    // `hl` command: glowing overlay on every placed block matching a shape/material query.
    MeshInstance3D *highlight_mesh = nullptr;
    std::string highlight_query;
    uint64_t highlight_revision = UINT64_MAX;
    Vector3 highlight_center;
    static constexpr int kHighlightRadius = 48, kHighlightHeight = 24;
    int rebuild_highlight();
    int find_damaged_target(int kind, altaria::IVec3 cell, uint64_t id) const;
    void remove_damaged_target(size_t index);
    void clear_break_damage();
    void update_damaged_overlay(DamagedTarget &target, bool animate = true);
    void show_block_hit_effect(altaria::BlockId block, uint8_t shape, const Transform3D &transform,
                               const Vector3 &hit_point, const Vector3 &hit_normal, uint64_t built_id = 0,
                               bool destroyed = false, int break_kind = 0, altaria::IVec3 break_cell = {},
                               uint64_t break_id = 0);
    bool advance_break_hit(int kind, altaria::IVec3 cell, uint64_t id, altaria::BlockId block, uint8_t shape, const Transform3D &transform);
    void trigger_left_hit();
    void on_hand_break_impact();
    void show_placement(const altaria::BuiltBlock &piece, bool animate);
    MeshInstance3D *structure_outline = nullptr;
    MeshInstance3D *structure_volume = nullptr;
    MeshInstance3D *structure_corner_markers[2] = {nullptr, nullptr};
    MeshInstance3D *structure_paste_preview = nullptr;
    std::optional<Vector3i> structure_corners[2];
    int wand_mode = 0;
    altaria::BlockId wand_material = 0;
    bool wand_material_is_air = false;
    altaria::BlockId wand_replace_target = 0;
    bool wand_replace_target_is_air = false;
    int wand_paste_rot = 0;
    struct StructureVoxelRecord {
        Vector3i pos;
        altaria::BlockId block = 0;
        altaria::Appearance appearance{};
    };
    struct StructureBuiltRecord {
        Vector3 pos;
        altaria::BlockId block = 0;
        uint8_t shape = 0;
        altaria::Appearance appearance{};
    };
    struct StructureUndoEntry {
        Vector3i low, size;
        std::vector<altaria::BlockId> blocks;
        std::vector<altaria::Appearance> appearances;
        std::vector<StructureBuiltRecord> removed_built;
    };
    struct StructureClipboard {
        Vector3i size;
        std::vector<StructureVoxelRecord> voxels;
        std::vector<StructureBuiltRecord> built;
        bool valid() const { return size.x > 0 && size.y > 0 && size.z > 0; }
    };
    StructureClipboard structure_clipboard;
    std::vector<StructureUndoEntry> structure_undo_stack;
    void record_structure_undo(Vector3i low, Vector3i size, const std::vector<StructureBuiltRecord> &removed_built = {});
    String undo_structure();
    String replace_structure(const String &from_mat, const String &to_mat, Vector3i low, Vector3i size);
    String walls_structure(const String &material, Vector3i low, Vector3i size);
    String hollow_structure(const String &material, Vector3i low, Vector3i size);
    String copy_structure(Vector3i low, Vector3i size);
    String paste_structure(Vector3i anchor, int rot);
    String stack_structure(int count);
    altaria::BlockId resolve_wand_material() const;
    bool wand_aim_cell(bool adjacent_for_place, Vector3i &out_cell, altaria::BlockId *out_block = nullptr,
                       bool *out_has_block = nullptr, Vector3 *out_world_pt = nullptr) const;
    void handle_wand_action(bool secondary);
    void refresh_wand_hud();
    MeshInstance3D *cave_map = nullptr;
    std::weak_ptr<const altaria::caves::IndexedNetwork> cave_map_plan;
    altaria::TileKey cave_map_tile{};
    bool cave_map_enabled=false, cave_map_loaded=false;
    bool selection_is_physical = false;
    Transform3D selection_camera;
    double selection_timer=1.0;
    WorldEnvironment *world_environment = nullptr;
    DirectionalLight3D *sun = nullptr;
    CrabConsole *cached_console = nullptr;
    Ref<GameLog> game_log;
    Dictionary generation_data, blocks_data, entity_data, metadata;
    altaria::SandboxInventory inventory;
    uint64_t sequence = 0;
    String world_path, language = "en";
    bool data_ready = false, generation_reported = false;
    bool debug_overlay_visible = true;
    bool debug_perf_panel = true;
    bool debug_world_panel = true;
    bool debug_chunks_panel = true;
    bool debug_physics_panel = true;
    float hud_timer = 0, hud_interval = 0.1f, reach = 9;
    void build_hud();
    void build_cheat_inventory();
    void refresh_cheat_blocks(const String &query);
    void select_cheat_block(int index);
    void show_cheat_inventory();
    void show_wand_block_picker(CheatPickerPurpose purpose);
    void close_cheat_inventory();
    bool cheat_inventory_open() const;
    bool collect_block(altaria::BlockId block, int amount = 1);
    void consume_held_block(int amount = 1);
    void sync_alt_selection();
    void confirm_alt_selection();
    void close_alt_inventory();
    void create_structure_outline();
    String mark_structure_corner(int corner, const std::vector<std::string> &coordinates);
    void refresh_structure_outline();
    String structure_selection_status() const;
    String export_structure(const String &name, Vector3i low, Vector3i size) const;
    String fill_structure(const String &material, Vector3i low, Vector3i size);
    bool load_world(const Dictionary &input);
    void create_stage();
    void stop_world();
    void set_game_input(bool active);
    void update_target(bool edit = false, bool place = false);
    uint64_t target_built() const;
    bool build_against(Vector3 hit, Vector3 normal, Vector3 source, uint8_t source_shape, uint64_t source_id = 0, bool preview = false);
    int placement_rotation() const;
    bool release_built(uint64_t id);
    void show_selection(altaria::BlockId block, uint8_t shape, const Transform3D &transform);
    altaria::RayHit target_block() const;
    BuiltWorld::Hit pick_built(Vector3 origin,Vector3 direction,float reach) const;
    void update_hud();
    void update_cave_map();
    String toggle_cave_map();
    bool spawn_entity(const String &key, const Dictionary &saved = Dictionary());

  protected:
    static void _bind_methods();
    void _notification(int what);

  public:
    void _ready() override;
    void _process(double delta) override;
    void _input(const Ref<InputEvent> &event) override;
    void _exit_tree() override;
    bool start_world(const String &name, int64_t seed, const String &preset = "default", const String &biome = "");
    bool start_random_world();
    void quit_game();

    int64_t place_block(Vector3 position, int block, const String &shape = "cube", int rotation = 0);
    Array get_built_blocks() const;
    bool remove_built_block(int64_t id);
    bool set_voxel(int x, int y, int z, int block);
    int get_voxel(int x, int y, int z) const;
    bool paint_voxel(int x, int y, int z, const String &color, double preserve = .35, double brightness = 1);
    bool shape_voxel(int x, int y, int z, const String &shape, int rotation = 0);
    Dictionary get_temperature(Vector3 position) const;
    bool release_voxel(int x, int y, int z, bool tree = true);
    bool overlay_voxel(int x, int y, int z, const String &key);
    Dictionary get_voxel_appearance(int x, int y, int z) const;
    String execute_command(const String &text);
    String item_command(const std::vector<std::string> &args);
    String resolution_command(const std::vector<std::string> &args);
    String export_game_log(const String &duration);
    String cheat_inventory_command(const std::vector<std::string> &args);
    String structure_command(const std::vector<std::string> &args);
    String execute_legacy_command(const String &text);
    Dictionary get_debug_state() const;
    Dictionary get_cave_environment(Vector3 position) const;
    Array get_cave_formations() const;
    String get_world_path() const {
        return world_path;
    }
    static AltariaGame *get_active_game() {
        return active_game;
    }
    CrabConsole *get_console();
    void print_console(const String &text);
    bool is_console_open();
    void on_console_visibility_changed(bool open);

    VoxelWorld *get_world() const { return world; }
    FlyingPlayer *get_player() const { return player; }
    WorldAtmosphere *get_atmosphere() const { return atmosphere; }
    PhysicsBlocks *get_physics_blocks() const { return physics_blocks; }
    BuiltWorld *get_built_world() const { return built_world; }
    std::string highlight_blocks(const std::string &query);
    int get_shape_type(int x, int y, int z) const {
        return world ? altaria::shape_type(world->shape_at(x, y, z)) : 0;
    }
    DirectionalLight3D *get_sun() const { return sun; }
    const Dictionary &get_generation_data() const { return generation_data; }
    const Dictionary &get_game_metadata() const { return metadata; }
    void set_game_metadata(const Dictionary &d) { metadata = d; }
    int get_selected_shape() const { return selected_shape; }
    void set_selected_shape(int s) {
        selected_shape = s;
        inventory.set_active_shape(s);
    }
    int get_selected_block() const { return inventory.selected(); }
    void set_selected_block(int b) { select_hotbar_slot(b); }
    int get_selected_slot() const { return inventory.selected(); }
    Array get_hotbar_slots() const;
    Dictionary get_active_hotbar_slot() const;
    void select_hotbar_slot(int index);
    bool collect_block_to_hotbar(int block_id, int amount = 1);
    bool is_alt_inventory_open() const { return arc_hotbar && arc_hotbar->is_alt_open(); }
    bool is_hotbar_idle_dimmed() const { return arc_hotbar && arc_hotbar->is_idle_dimmed(); }
    bool is_full_map_open() const { return diorama_minimap && diorama_minimap->is_full_map_open(); }
    void toggle_full_map();
    void set_fly_hotkey_enabled(bool v) { fly_hotkey_enabled = v; }
    bool is_fly_hotkey_enabled() const { return fly_hotkey_enabled; }
    void set_noclip_hotkey_enabled(bool v) { noclip_hotkey_enabled = v; }
    bool is_noclip_hotkey_enabled() const { return noclip_hotkey_enabled; }
    float get_crosshair_darkness() const { return crosshair ? crosshair->get_darkness() : 0.0f; }
    int get_break_stage() const;
    int get_dropped_item_count() const { return int(dropped_items.size()); }
    int get_creature_count(const String &key = String()) const { return creatures.count(key.utf8().get_data()); }
    Array get_creature_positions(const String &key = String()) const { return creatures.positions(key.utf8().get_data()); }

    bool is_debug_overlay() const { return debug_overlay_visible; }
    void set_debug_overlay(bool v);
    bool is_debug_perf() const { return debug_perf_panel; }
    void set_debug_perf(bool v) { debug_perf_panel = v; }
    bool is_debug_world() const { return debug_world_panel; }
    void set_debug_world(bool v) { debug_world_panel = v; }
    bool is_debug_chunks() const { return debug_chunks_panel; }
    void set_debug_chunks(bool v) { debug_chunks_panel = v; }
    bool is_debug_physics() const { return debug_physics_panel; }
    void set_debug_physics(bool v) { debug_physics_panel = v; }
    float get_hud_interval() const { return hud_interval; }
    void set_hud_interval(float v) { hud_interval = v; }
};
} // namespace godot
