#pragma once
#include "game/player/sandbox_inventory.h"
#include "game/world_generation/voxel_world.h"
#include <array>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <unordered_map>

namespace godot {

// Semi-circular bottom-anchored hotbar and extensible Alt radial sub-menu.
class ArcHotbar : public Control {
    GDCLASS(ArcHotbar, Control)

    const altaria::SandboxInventory *inventory = nullptr;
    const VoxelWorld *world = nullptr;
    bool pt_br = false;
    int active_shape = 0;
    int active_rotation = 0;
    bool unlimited_blocks = false;

    bool alt_open = false;
    std::string active_alt_menu_id = "block_shapes";
    int alt_hovered = 0;

    float alt_anim = 0.0f;
    float shake_timer = 0.0f;
    float confirm_flash = 0.0f;
    float idle_timer = 0.0f;
    float idle_blend = 0.0f;
    std::array<float, altaria::HOTBAR_SLOT_COUNT> slot_pop{};
    std::array<float, altaria::HOTBAR_SLOT_COUNT> slot_pickup{};
    std::array<float, 16> alt_pop{};
    int wand_mode = 0;
    altaria::BlockId wand_material = 0;
    bool wand_material_air = false;
    altaria::BlockId wand_replace_target = 0;
    bool wand_replace_air = false;
    bool wand_has_sel = false;
    Vector3i wand_sel_size;
    bool wand_has_clip = false;
    int wand_clip_blocks = 0;
    int wand_paste_rot = 0;
    String wand_feedback;
    float wand_feedback_timer = 0.0f;
    uint64_t last_signature = 0;
    uint64_t content_signature() const;

    Ref<Font> font_regular;
    Ref<Font> font_bold;
    std::array<Ref<Texture2D>, 10> digit_textures;
    std::unordered_map<int, Ref<Texture2D>> texture_cache;
    std::unordered_map<int, Ref<Texture2D>> deposit_icon_cache;
    SubViewport *icon_viewport = nullptr;
    Node3D *icon_stage = nullptr;
    int icon_capture_block = 0;
    int icon_wait_frames = 0;

    void ensure_icon_viewport();
    void clear_icon_stage();
    void step_deposit_icon_bake();
    void load_number_sheet();
    void draw_pixel_number(Vector2 anchor, int value, float px_scale, int align_mode, Color tint,
                           float rotation_rad = 0.0f);

    Ref<Texture2D> texture_for_block(altaria::BlockId id);
    Color tint_for_block(altaria::BlockId id) const;
    String block_display_name(altaria::BlockId id) const;
    String shape_display_name(int shape_id) const;

    Vector2 arc_center(float scale) const;

    void draw_wedge(Vector2 center, float r_in, float r_out, float a0, float a1, Color fill, Color border,
                    float border_w);
    void draw_iso_quad(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float shade, Color tint, float alpha,
                       const Ref<Texture2D> &tex, bool draw_outline = true);
    void draw_iso_tri(Vector2 p0, Vector2 p1, Vector2 p2, float shade, Color tint, float alpha,
                      const Ref<Texture2D> &tex, bool draw_outline = true);
    void draw_iso_box(Vector2 c, float sz, Vector3 bmin, Vector3 bmax, Color tint, float alpha,
                      const Ref<Texture2D> &tex);
    void draw_shape_preview(Vector2 c, float sz, int shape_id, Color tint, float alpha, const Ref<Texture2D> &tex);
    void draw_deposit_preview(Vector2 c, float sz, altaria::BlockId block_id, float alpha);
    void draw_tool_preview(Vector2 c, float sz, const std::string &tool_id, float alpha);
    void draw_wand_mode_preview(Vector2 c, float sz, const std::string &mode_id, float alpha);
    void draw_wand_status_pill(Vector2 center, float scale, float alpha);
    void draw_center_plaque(Vector2 center, float scale, const String &title, const String &subtitle, float alpha);

  protected:
    static void _bind_methods() {}

  public:
    void _ready() override;
    void _process(double delta) override;
    void _draw() override;

    void bind_sources(const altaria::SandboxInventory *inv, const VoxelWorld *voxel_world, bool portuguese);
    void set_state(int shape, int rotation);
    void set_wand_state(int mode, altaria::BlockId material, altaria::BlockId replace_target, bool has_sel,
                        Vector3i sel_size, bool has_clip, int clip_blocks, int paste_rot,
                        bool mat_air = false, bool target_air = false);
    void show_wand_feedback(const String &message);
    void set_unlimited_blocks(bool enabled);
    void notify_activity();
    void trigger_pickup_anim(int slot_idx);
    void trigger_shake();
    void trigger_confirm();
    void minimap_rect(Vector2 &out_center, float &out_radius) const;
    bool is_idle_dimmed() const { return idle_blend > 0.5f; }
    float get_idle_alpha() const { return 1.0f - 0.62f * idle_blend; }
    float get_idle_drop() const;

    bool is_alt_open() const { return alt_open; }
    int hovered_alt_index() const { return alt_hovered; }
    void open_alt_menu(const std::string &menu_id, int initial_shape_id);
    void close_alt_menu();
    void cycle_alt_option(int delta);
    bool set_alt_option(int idx);
};

} // namespace godot
