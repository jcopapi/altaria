#pragma once
#include "game/debug/debug_console.h"
#include "game/player/flying_player.h"
#include "game/world_generation/built_world.h"
#include "game/world_generation/physics_blocks.h"
#include "game/world_generation/terrain/terrain_surface.h"
#include "game/world_generation/voxel_world.h"
#include "game/world_generation/world_atmosphere.h"
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/item_list.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/world_environment.hpp>
namespace godot {
class AltariaGame : public Node {
    GDCLASS(AltariaGame, Node)
    CanvasLayer *overlay = nullptr;
    Control *menu = nullptr, *hud = nullptr;
    Label *status = nullptr, *telemetry = nullptr, *hotbar_label = nullptr, *crosshair = nullptr;
    LineEdit *world_name = nullptr, *seed_input = nullptr;
    ItemList *world_list = nullptr;
    Node3D *stage = nullptr;
    VoxelWorld *world = nullptr;
    FlyingPlayer *player = nullptr;
    WorldAtmosphere *atmosphere = nullptr;
    PhysicsBlocks *physics_blocks = nullptr;
    BuiltWorld *built_world = nullptr;
    TerrainSurface *terrain_surface = nullptr;
    uint64_t selected_built_id = 0;
    int selected_shape = 0, shape_rotation = 0, selection_shape = -1;
    uint64_t selection_preview_hash = 0;
    int selection_preview_shape = -1, selection_preview_cells = 0;
    MeshInstance3D *selection = nullptr;
    bool selection_is_physical = false;
    WorldEnvironment *world_environment = nullptr;
    DirectionalLight3D *sun = nullptr;
    altaria::DebugConsole console;
    Dictionary generation_data, blocks_data, entity_data, metadata;
    Array saved_worlds;
    std::vector<altaria::BlockId> hotbar;
    int selected_block = 0;
    uint64_t sequence = 0;
    String world_path, language = "en";
    bool data_ready = false, exiting = false, generation_reported = false;
    float hud_timer = 0, save_timer = 0, reach = 9;
    String local(const String &key) const;
    void build_menu();
    void build_hud();
    void on_create();
    void on_load();
    void on_resume();
    void on_worlds();
    void on_quit();
    void on_language(int64_t index);
    void refresh_worlds();
    bool load_world(const String &path);
    void create_stage();
    void stop_world();
    void set_game_input(bool active);
    void submit_console(const String &command);
    void on_console_gui_input(const Ref<InputEvent> &event);
    void update_target(bool edit = false, bool place = false);
    uint64_t target_built() const;
    bool build_against(Vector3 hit, Vector3 normal, Vector3 source, uint8_t source_shape);
    bool build_over_terrain(uint64_t built,altaria::IVec3 cell);
    bool release_built(uint64_t id);
    void show_selection(altaria::BlockId block, uint8_t shape, const Transform3D &transform);
    void show_terrain_preview(altaria::IVec3 center, altaria::BlockId center_block);
    bool toggle_terrain_at(altaria::IVec3 cell, int mode = -1);
    altaria::RayHit target_block() const;
    BuiltWorld::Hit pick_built(Vector3 origin,Vector3 direction,float reach) const;
    void show_smooth_selection(uint64_t built,altaria::IVec3 cell);
    void show_smooth_preview(uint64_t built,altaria::IVec3 cell);
    void update_hud();
    bool spawn_entity(const String &key, const Dictionary &saved = Dictionary());

  protected:
    static void _bind_methods();
    void _notification(int what);

  public:
    void _ready() override;
    void _process(double delta) override;
    void _input(const Ref<InputEvent> &event) override;
    void _exit_tree() override;
    bool start_world(const String &name, int64_t seed);
    bool start_random_world();
    bool open_world(const String &path) {
        return load_world(path);
    }
    bool save_world();
    int64_t place_block(Vector3 position, int block, const String &shape = "cube", int rotation = 0);
    Array get_built_blocks() const;
    bool remove_built_block(int64_t id);
    bool set_voxel(int x, int y, int z, int block);
    int get_voxel(int x, int y, int z) const;
    bool paint_voxel(int x, int y, int z, const String &color, double preserve = .35, double brightness = 1);
    bool terrain_built(int64_t id, int mode = -1);
    Dictionary terrain_hit(Vector3 origin,Vector3 direction,double distance=100) const;
    bool shape_voxel(int x, int y, int z, const String &shape, int rotation = 0);
    Dictionary get_temperature(Vector3 position) const;
    bool release_voxel(int x, int y, int z, bool tree = true);
    bool overlay_voxel(int x, int y, int z, const String &key);
    Dictionary get_voxel_appearance(int x, int y, int z) const;
    String execute_command(const String &text);
    Dictionary get_debug_state() const;
    String get_world_path() const {
        return world_path;
    }
};
} // namespace godot
