#pragma once
#include "game/player/flying_player.h"
#include "game/world_generation/built_world.h"
#include "game/world_generation/voxel_world.h"
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace godot {

class DioramaMinimap : public Control {
    GDCLASS(DioramaMinimap, Control)

    struct CellSample {
        float height = 0.0f;
        int16_t water_top = -1000;
        uint8_t top_r = 90, top_g = 140, top_b = 85;
        uint8_t side_r = 65, side_g = 70, side_b = 78;
    };

    struct TreeNode {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        uint8_t height = 4;
        uint8_t radius = 2;
        uint8_t r = 48, g = 128, b = 56;
    };

    struct LiveOverride {
        float y = -9999.0f;
        uint8_t r = 140, g = 140, b = 140;
        bool has_solid = false;
    };

    struct PatchKey {
        int32_t x = 0, z = 0;
        bool operator==(const PatchKey &o) const { return x == o.x && z == o.z; }
    };
    struct PatchHash {
        size_t operator()(const PatchKey &k) const {
            uint64_t h = uint64_t(uint32_t(k.x)) * 73856093ULL ^ uint64_t(uint32_t(k.z)) * 19349663ULL;
            return size_t(h);
        }
    };
    struct Patch {
        CellSample cells[64];
        std::vector<TreeNode> trees;
        uint64_t last_used = 0;
    };
    struct ExplorePatch {
        uint8_t vis[64]{};
    };

    const VoxelWorld *world = nullptr;
    const FlyingPlayer *player = nullptr;
    const BuiltWorld *built_world = nullptr;

    // Circular HUD minimap buffers
    Ref<Image> image;
    Ref<ImageTexture> texture;
    PackedByteArray pixels;
    std::vector<uint8_t> minimap_reveal;

    // Full-screen world map state & buffers
    bool full_map_open = false;
    bool full_map_dirty = true;
    bool dragging_map = false;
    float full_center_x = 0.0f;
    float full_center_z = 0.0f;
    float full_world_radius = 128.0f; // vertical half-span in meters
    float full_refresh_timer = 0.0f;
    float full_settle_timer = 1.0f;
    float full_built_cx = 0.0f, full_built_cz = 0.0f, full_built_radius = 128.0f;
    static constexpr float kFullPad = 1.45f;
    int full_img_w = 0;
    int full_img_h = 0;
    Ref<Image> full_image;
    Ref<ImageTexture> full_texture;
    PackedByteArray full_pixels;
    std::vector<uint8_t> full_reveal;
    std::function<void(bool)> on_full_map_changed;

    std::unordered_map<PatchKey, Patch, PatchHash> patch_cache;
    std::unordered_map<PatchKey, ExplorePatch, PatchHash> explore_cache;
    uint64_t cache_clock = 0;
    uint64_t explore_revision = 0;
    uint64_t last_revision = UINT64_MAX;
    uint64_t full_last_revision = UINT64_MAX;
    uint64_t last_explore_rev = UINT64_MAX;
    uint64_t full_last_explore_rev = UINT64_MAX;
    size_t last_tile_count = 0;
    Vector3 last_pos;
    Vector3 last_explore_pos{1e9f, 0.0f, 1e9f};
    float last_yaw = 999.0f;
    float image_yaw = 0.0f;   // yaw the circular image was painted at
    float drawn_yaw = 999.0f; // yaw used for the last draw
    float refresh_timer = 0.0f;
    float cloud_time = 0.0f;
    float fade_alpha = 1.0f;

    Vector2 minimap_center{0.0f, 0.0f};
    float minimap_radius = 104.0f;
    Ref<Font> font_bold;
    Ref<Font> font_regular;
    bool pt_br = false;
    float open_anim = 1.0f;
    void draw_full_map_chrome(Vector2 sz, float px_per_m);
    void draw_frame_box(Rect2 r, float scale, float alpha, bool notch_bottom);
    Ref<Texture2D> border_tex;
    Ref<Texture2D> ring_tex;
    Ref<Texture2D> north_badge_tex;
    Ref<Texture2D> arrow_tex;
    Ref<Texture2D> north_tex;
    Ref<Image> tree_sprite_img;

    const CellSample &sample_terrain(int wx, int wz);
    Patch &build_patch(PatchKey key);
    void stamp_exploration(float px, float pz);
    uint8_t sample_explored(int wx, int wz) const;
    float sample_explored_smooth(float fx, float fz) const;

    void collect_overrides(std::unordered_map<uint64_t, LiveOverride> &out, float cx, float cz, float radius) const;
    void rebuild_image(Vector3 pos, float yaw);
    void rebuild_full_map_image();

    static void blend_pixel(PackedByteArray &buf, int width, int height, int x, int y, uint8_t r, uint8_t g, uint8_t b,
                            uint8_t a = 255);
    static void draw_diorama_tree(PackedByteArray &buf, int width, int height, int sx, int sy_base, int trunk_px,
                                  float crown_r, uint8_t tr, uint8_t tg, uint8_t tb, const Ref<Image> &sprite);
    static void paint_column(PackedByteArray &buf, int width, int height, int sx, int col_span, int base_sy, float ref_y,
                             float elev, float west_elev, float north_elev, float front_elev, int water_top,
                             uint8_t top_r, uint8_t top_g, uint8_t top_b, uint8_t side_r, uint8_t side_g, uint8_t side_b,
                             float lift_scale, int max_cliff, int top_px = 1, int min_cliff = 2);
    static void paint_cloud_overlay(PackedByteArray &buf, const std::vector<uint8_t> &reveal_mask, int width, int height,
                                    float center_x, float center_z, float world_per_px_x, float world_per_px_z,
                                    float rot_cos, float rot_sin, float drift, float circle_clip_r);
    void draw_player_cone_and_chevron(Vector2 pt, float yaw_screen_rad, float size_px, float alpha, bool with_cone);

  protected:
    static void _bind_methods();

  public:
    void _ready() override;
    void _process(double delta) override;
    void _draw() override;
    void _gui_input(const Ref<InputEvent> &event) override;

    void bind_sources(const VoxelWorld *w, const FlyingPlayer *p, const BuiltWorld *b, bool is_pt_br = true);
    void set_on_full_map_changed(std::function<void(bool)> cb) { on_full_map_changed = std::move(cb); }
    void set_placement(Vector2 center, float radius, float alpha = 1.0f);

    void set_full_map_open(bool open);
    bool is_full_map_open() const { return full_map_open; }
    void recenter_full_map();
    void zoom_full_map(float factor);
    bool is_point_explored(float wx, float wz) const;

    float radius() const { return minimap_radius; }
    Vector2 center() const { return minimap_center; }
};

} // namespace godot
