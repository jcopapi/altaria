#include "arc_hotbar.h"
#include "../../game/debug/frame_profile.h"
#include "game/world_generation/block_shapes.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/world_environment.hpp>

namespace godot {
namespace {

constexpr float kPi = 3.14159265359f;
constexpr float kTwoPi = 6.28318530718f;
constexpr float kDegToRad = 0.01745329252f;
constexpr float kStartDeg = -172.0f;
constexpr float kEndDeg = -8.0f;

float exp_lerp(float cur, float target, float rate, float dt) {
    return cur + (target - cur) * (1.0f - std::exp(-rate * dt));
}

float ease_out_cubic(float t) {
    float inv = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - inv * inv * inv;
}

} // namespace

void ArcHotbar::_ready() {
    set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    set_texture_filter(CanvasItem::TEXTURE_FILTER_NEAREST);
    font_regular = ResourceLoader::get_singleton()->load(
        "res://addons/crabbyconsole/fonts/roboto/static/RobotoMono-Regular.ttf");
    font_bold = ResourceLoader::get_singleton()->load(
        "res://addons/crabbyconsole/fonts/roboto/static/RobotoMono-Bold.ttf");
    load_number_sheet();
}

void ArcHotbar::load_number_sheet() {
    Ref<Texture2D> sheet_tex = ResourceLoader::get_singleton()->load("res://game/assets/hud/number_sheet.png");
    if (sheet_tex.is_null())
        return;
    Ref<Image> src = sheet_tex->get_image();
    if (src.is_null())
        return;
    if (src->is_compressed())
        src->decompress();
    src->convert(Image::FORMAT_RGBA8);

    // Sheet order: 1, 2, 3, 4, 5, 6, 7, 8, 9, 0
    struct GlyphDef {
        int digit;
        int x;
        int w;
    };
    constexpr GlyphDef defs[10] = {
        {1, 0, 3},  {2, 2, 5},  {3, 6, 5},  {4, 10, 5}, {5, 14, 5},
        {6, 18, 5}, {7, 22, 5}, {8, 26, 5}, {9, 30, 5}, {0, 34, 5},
    };
    const int sw = src->get_width();
    const int sh = src->get_height();
    for (const auto &g : defs) {
        if (g.x + g.w > sw || sh < 7)
            continue;
        Ref<Image> sub = Image::create_empty(g.w, 7, false, Image::FORMAT_RGBA8);
        for (int y = 0; y < 7; ++y) {
            for (int x = 0; x < g.w; ++x) {
                Color px = src->get_pixel(g.x + x, y);
                bool is_core = (x >= 1 && x <= g.w - 2 && y >= 1 && y <= 5) && (px.r > 0.55f && px.a > 0.1f);
                bool near_core = is_core;
                if (!near_core) {
                    for (int dy = -1; dy <= 1 && !near_core; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            int lx = x + dx;
                            int ly = y + dy;
                            if (lx >= 1 && lx <= g.w - 2 && ly >= 1 && ly <= 5) {
                                Color n = src->get_pixel(g.x + lx, ly);
                                if (n.r > 0.55f && n.a > 0.1f)
                                    near_core = true;
                            }
                        }
                    }
                }
                sub->set_pixel(x, y, near_core ? Color(px.r, px.g, px.b, 1.0f) : Color(0, 0, 0, 0));
            }
        }
        digit_textures[g.digit] = ImageTexture::create_from_image(sub);
    }
}

void ArcHotbar::draw_pixel_number(Vector2 anchor, int value, float px_scale, int align_mode, Color tint,
                                  float rotation_rad) {
    if (value < 0)
        value = 0;
    px_scale = std::max(2.0f, std::round(px_scale));
    int digits[12];
    int count = 0;
    if (value == 0) {
        digits[count++] = 0;
    } else {
        while (value > 0 && count < 12) {
            digits[count++] = value % 10;
            value /= 10;
        }
        std::reverse(digits, digits + count);
    }

    // Share the 1-texel dark outline between consecutive digits just like number_sheet.png
    const float overlap = -1.0f * px_scale;
    float total_w = 0.0f;
    for (int i = 0; i < count; ++i) {
        Ref<Texture2D> t = digit_textures[digits[i]];
        if (t.is_valid())
            total_w += float(t->get_width()) * px_scale + (i + 1 < count ? overlap : 0.0f);
    }
    if (total_w <= 0.0f)
        return;

    const float h = 7.0f * px_scale;
    if (std::abs(rotation_rad) > 0.001f) {
        draw_set_transform(anchor, rotation_rad, Vector2(1.0f, 1.0f));
        float x = 0.0f;
        if (align_mode == 0)
            x = -total_w * 0.5f;
        else if (align_mode > 0)
            x = -total_w;
        float y = -h * 0.5f;
        for (int i = 0; i < count; ++i) {
            Ref<Texture2D> t = digit_textures[digits[i]];
            if (t.is_null())
                continue;
            float w = float(t->get_width()) * px_scale;
            draw_texture_rect(t, Rect2(std::round(x), std::round(y), std::round(w), std::round(h)), false, tint);
            x += w + overlap;
        }
        draw_set_transform(Vector2(), 0.0f, Vector2(1.0f, 1.0f));
        return;
    }

    float x = anchor.x;
    if (align_mode == 0)
        x -= total_w * 0.5f;
    else if (align_mode > 0)
        x -= total_w;
    float y = anchor.y - h * 0.5f;

    for (int i = 0; i < count; ++i) {
        Ref<Texture2D> t = digit_textures[digits[i]];
        if (t.is_null())
            continue;
        float w = float(t->get_width()) * px_scale;
        draw_texture_rect(t, Rect2(std::round(x), std::round(y), std::round(w), std::round(h)), false, tint);
        x += w + overlap;
    }
}

void ArcHotbar::bind_sources(const altaria::SandboxInventory *inv, const VoxelWorld *voxel_world, bool portuguese) {
    if (world != voxel_world) {
        texture_cache.clear();
        deposit_icon_cache.clear();
        icon_capture_block = 0;
        icon_wait_frames = 0;
        clear_icon_stage();
    }
    inventory = inv;
    world = voxel_world;
    pt_br = portuguese;
}

void ArcHotbar::clear_icon_stage() {
    if (!icon_stage)
        return;
    for (int i = icon_stage->get_child_count() - 1; i >= 0; --i) {
        Node *child = icon_stage->get_child(i);
        icon_stage->remove_child(child);
        memdelete(child);
    }
}

void ArcHotbar::ensure_icon_viewport() {
    if (icon_viewport)
        return;
    DisplayServer *ds = DisplayServer::get_singleton();
    if (!ds || ds->get_name() == String("headless"))
        return;

    icon_viewport = memnew(SubViewport);
    icon_viewport->set_name("DepositIconViewport");
    icon_viewport->set_size(Vector2i(96, 96));
    icon_viewport->set_transparent_background(true);
    icon_viewport->set_use_own_world_3d(true);
    icon_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
    add_child(icon_viewport);

    Ref<Environment> env;
    env.instantiate();
    env->set_background(Environment::BG_CLEAR_COLOR);
    env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
    env->set_ambient_light_color(Color(1.0f, 1.0f, 1.0f));
    env->set_ambient_light_energy(1.25f);
    auto *world_env = memnew(WorldEnvironment);
    world_env->set_environment(env);
    icon_viewport->add_child(world_env);

    auto *light = memnew(DirectionalLight3D);
    light->set_param(Light3D::PARAM_ENERGY, 0.85f);
    light->look_at_from_position(Vector3(2.0f, 3.2f, 1.6f), Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
    icon_viewport->add_child(light);

    auto *cam = memnew(Camera3D);
    cam->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
    cam->set_size(1.12f);
    cam->set_near(0.05f);
    cam->set_far(20.0f);
    cam->look_at_from_position(Vector3(2.15f, 1.75f, 2.15f), Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
    icon_viewport->add_child(cam);
    cam->make_current();

    icon_stage = memnew(Node3D);
    icon_viewport->add_child(icon_stage);
}

void ArcHotbar::step_deposit_icon_bake() {
    if (!world || world->get_deposit_models().empty())
        return;
    ensure_icon_viewport();
    if (!icon_viewport || !icon_stage)
        return;

    if (icon_capture_block != 0) {
        if (++icon_wait_frames < 2)
            return;
        Ref<ViewportTexture> vtex = icon_viewport->get_texture();
        Ref<Image> img = vtex.is_valid() ? vtex->get_image() : Ref<Image>();
        if (img.is_valid() && !img->is_empty()) {
            deposit_icon_cache[icon_capture_block] = ImageTexture::create_from_image(img);
            queue_redraw();
        }
        clear_icon_stage();
        icon_capture_block = 0;
        icon_wait_frames = 0;
        icon_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
        return;
    }

    for (const auto &[block_id, model] : world->get_deposit_models()) {
        if (deposit_icon_cache.count(int(block_id)) || model.parts.empty())
            continue;
        clear_icon_stage();
        const float max_dim = std::max({model.bounds.size.x, model.bounds.size.y, model.bounds.size.z, 0.001f});
        const float fit_scale = 0.88f / max_dim;
        const Vector3 center = model.bounds.get_center();
        const Transform3D norm_xf(Basis().scaled(Vector3(fit_scale, fit_scale, fit_scale)), -center * fit_scale);
        for (const auto &part : model.parts) {
            if (part.mesh.is_null())
                continue;
            auto *mi = memnew(MeshInstance3D);
            mi->set_mesh(part.mesh);
            mi->set_transform(norm_xf * part.transform);
            icon_stage->add_child(mi);
        }
        icon_capture_block = int(block_id);
        icon_wait_frames = 0;
        icon_viewport->set_update_mode(SubViewport::UPDATE_ONCE);
        break;
    }
}

void ArcHotbar::set_state(int shape, int rotation) {
    if (active_rotation != rotation)
        notify_activity();
    active_shape = shape;
    active_rotation = rotation;
}

void ArcHotbar::set_wand_state(int mode, altaria::BlockId material, altaria::BlockId replace_target, bool has_sel,
                               Vector3i sel_size, bool has_clip, int clip_blocks, int paste_rot,
                               bool mat_air, bool target_air) {
    wand_mode = mode;
    wand_material = material;
    wand_material_air = mat_air;
    wand_replace_target = replace_target;
    wand_replace_air = target_air;
    wand_has_sel = has_sel;
    wand_sel_size = sel_size;
    wand_has_clip = has_clip;
    wand_clip_blocks = clip_blocks;
    wand_paste_rot = paste_rot;
}

void ArcHotbar::show_wand_feedback(const String &message) {
    wand_feedback = message;
    wand_feedback_timer = 2.6f;
    queue_redraw();
}

void ArcHotbar::notify_activity() {
    idle_timer = 0.0f;
}

void ArcHotbar::set_unlimited_blocks(bool enabled) {
    unlimited_blocks = enabled;
    queue_redraw();
}

void ArcHotbar::trigger_pickup_anim(int slot_idx) {
    notify_activity();
    if (slot_idx >= 0 && slot_idx < altaria::HOTBAR_SLOT_COUNT) {
        slot_pickup[slot_idx] = 1.0f;
    }
}

void ArcHotbar::trigger_shake() {
    notify_activity();
    shake_timer = 0.32f;
}

void ArcHotbar::trigger_confirm() {
    notify_activity();
    confirm_flash = 1.0f;
}

void ArcHotbar::open_alt_menu(const std::string &menu_id, int initial_shape_id) {
    notify_activity();
    if (!inventory)
        return;
    const auto *def = inventory->find_alt_menu(menu_id);
    if (!def || def->options.empty())
        return;
    alt_open = true;
    active_alt_menu_id = menu_id;
    alt_hovered = 0;
    for (int i = 0; i < int(def->options.size()); ++i) {
        if (def->options[i].shape_id == initial_shape_id) {
            alt_hovered = i;
            break;
        }
    }
    alt_pop.fill(0.0f);
    if (alt_hovered < int(alt_pop.size()))
        alt_pop[alt_hovered] = 1.0f;
}

void ArcHotbar::close_alt_menu() {
    alt_open = false;
}

float ArcHotbar::get_idle_drop() const {
    const float scale = std::clamp(get_size().y / 900.0f, 0.72f, 1.35f);
    return 15.0f * scale * idle_blend;
}

void ArcHotbar::minimap_rect(Vector2 &out_center, float &out_radius) const {
    Vector2 sz = get_size();
    const float scale = std::clamp(sz.y / 900.0f, 0.72f, 1.35f);
    out_center = Vector2(sz.x * 0.5f, sz.y - 34.0f * scale + get_idle_drop());
    out_radius = 90.0f * scale;
}

Vector2 ArcHotbar::arc_center(float scale) const {
    Vector2 sz = get_size();
    float shake_x = shake_timer > 0.0f ? std::sin(shake_timer * 55.0f) * 7.0f * (shake_timer / 0.32f) : 0.0f;
    return Vector2(sz.x * 0.5f + shake_x, sz.y - 34.0f * scale + 15.0f * scale * idle_blend);
}

void ArcHotbar::cycle_alt_option(int delta) {
    notify_activity();
    if (!alt_open || !inventory)
        return;
    const auto *def = inventory->find_alt_menu(active_alt_menu_id);
    if (!def || def->options.empty())
        return;
    int n = int(def->options.size());
    alt_hovered = ((alt_hovered + delta) % n + n) % n;
}

bool ArcHotbar::set_alt_option(int idx) {
    notify_activity();
    if (!alt_open || !inventory)
        return false;
    const auto *def = inventory->find_alt_menu(active_alt_menu_id);
    if (!def || idx < 0 || idx >= int(def->options.size()))
        return false;
    alt_hovered = idx;
    return true;
}

Ref<Texture2D> ArcHotbar::texture_for_block(altaria::BlockId id) {
    if (id == 0 || !world)
        return Ref<Texture2D>();
    auto cached = texture_cache.find(int(id));
    if (cached != texture_cache.end())
        return cached->second;

    Ref<Texture2D> tex;
    const auto &reg = world->registry();
    const auto &blk = reg.get(id);
    if (blk.texture_layer >= 0 && blk.texture_layer < int(reg.textures.size())) {
        const auto &path = reg.textures[blk.texture_layer].path;
        if (!path.empty())
            tex = ResourceLoader::get_singleton()->load(String(path.c_str()));
    }
    texture_cache[int(id)] = tex;
    return tex;
}

Color ArcHotbar::tint_for_block(altaria::BlockId id) const {
    if (id == 0 || !world)
        return Color(1, 1, 1, 1);
    const auto &blk = world->registry().get(id);
    if (blk.tintable) {
        return Color(blk.color.r, blk.color.g, blk.color.b, 1.0f);
    }
    return Color(1.0f, 1.0f, 1.0f, 1.0f);
}

String ArcHotbar::block_display_name(altaria::BlockId id) const {
    if (id == 0 || !world)
        return pt_br ? "Vazio" : "Empty";
    const auto &blk = world->registry().get(id);
    const std::string &n = (pt_br && !blk.name_pt.empty()) ? blk.name_pt : blk.name;
    return String::utf8(n.c_str());
}

String ArcHotbar::shape_display_name(int shape_id) const {
    if (inventory) {
        const auto *def = inventory->find_alt_menu("block_shapes");
        if (def) {
            for (const auto &opt : def->options) {
                if (opt.shape_id == shape_id) {
                    return String::utf8((pt_br ? opt.label_pt : opt.label_en).c_str());
                }
            }
        }
    }
    return String(altaria::shape_name(shape_id));
}

void ArcHotbar::draw_wedge(Vector2 center, float r_in, float r_out, float a0, float a1, Color fill, Color border,
                           float border_w) {
    constexpr int segs = 12;
    PackedVector2Array pts;
    pts.resize((segs + 1) * 2);
    for (int i = 0; i <= segs; ++i) {
        float t = float(i) / float(segs);
        float a = a0 + (a1 - a0) * t;
        Vector2 dir(std::cos(a), std::sin(a));
        pts[i] = center + dir * r_out;
        pts[(segs * 2 + 1) - i] = center + dir * r_in;
    }
    draw_colored_polygon(pts, fill);
    if (border.a > 0.01f && border_w > 0.0f) {
        PackedVector2Array loop = pts;
        loop.push_back(pts[0]);
        draw_polyline(loop, border, border_w, true);
    }
}

void ArcHotbar::draw_iso_quad(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, float shade, Color tint, float alpha,
                              const Ref<Texture2D> &tex, bool draw_outline) {
    Color c(tint.r * shade, tint.g * shade, tint.b * shade, alpha);
    PackedVector2Array pts{p0, p1, p2, p3};
    PackedColorArray cols{c, c, c, c};
    if (tex.is_valid()) {
        PackedVector2Array uvs{Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1)};
        draw_polygon(pts, cols, uvs, tex);
    } else {
        draw_colored_polygon(pts, c);
    }
    if (draw_outline) {
        PackedVector2Array edge{p0, p1, p2, p3, p0};
        draw_polyline(edge, Color(0.05f, 0.04f, 0.07f, 0.65f * alpha), 1.15f, true);
    }
}

void ArcHotbar::draw_iso_tri(Vector2 p0, Vector2 p1, Vector2 p2, float shade, Color tint, float alpha,
                             const Ref<Texture2D> &tex, bool draw_outline) {
    Color c(tint.r * shade, tint.g * shade, tint.b * shade, alpha);
    PackedVector2Array pts{p0, p1, p2};
    PackedColorArray cols{c, c, c};
    if (tex.is_valid()) {
        PackedVector2Array uvs{Vector2(0.5f, 0), Vector2(1, 1), Vector2(0, 1)};
        draw_polygon(pts, cols, uvs, tex);
    } else {
        draw_colored_polygon(pts, c);
    }
    if (draw_outline) {
        PackedVector2Array edge{p0, p1, p2, p0};
        draw_polyline(edge, Color(0.05f, 0.04f, 0.07f, 0.65f * alpha), 1.15f, true);
    }
}

void ArcHotbar::draw_iso_box(Vector2 c, float sz, Vector3 bmin, Vector3 bmax, Color tint, float alpha,
                             const Ref<Texture2D> &tex) {
    const Vector2 dx(0.866f, 0.45f);
    const Vector2 dz(-0.866f, 0.45f);
    const Vector2 dy(0.0f, -0.96f);
    auto proj = [&](float x, float y, float z) {
        return c + dx * ((x - 0.5f) * sz) + dz * ((z - 0.5f) * sz) + dy * ((y - 0.5f) * sz);
    };

    draw_iso_quad(proj(bmin.x, bmax.y, bmin.z), proj(bmax.x, bmax.y, bmin.z),
                  proj(bmax.x, bmax.y, bmax.z), proj(bmin.x, bmax.y, bmax.z),
                  1.00f, tint, alpha, tex);
    draw_iso_quad(proj(bmax.x, bmax.y, bmin.z), proj(bmax.x, bmin.y, bmin.z),
                  proj(bmax.x, bmin.y, bmax.z), proj(bmax.x, bmax.y, bmax.z),
                  0.80f, tint, alpha, tex);
    draw_iso_quad(proj(bmin.x, bmax.y, bmax.z), proj(bmax.x, bmax.y, bmax.z),
                  proj(bmax.x, bmin.y, bmax.z), proj(bmin.x, bmin.y, bmax.z),
                  0.62f, tint, alpha, tex);
}

void ArcHotbar::draw_deposit_preview(Vector2 c, float sz, altaria::BlockId block_id, float alpha) {
    auto cached = deposit_icon_cache.find(int(block_id));
    if (cached != deposit_icon_cache.end() && cached->second.is_valid()) {
        const float side = sz * 2.05f;
        draw_texture_rect(cached->second, Rect2(c - Vector2(side, side) * 0.5f, Vector2(side, side)), false,
                          Color(1.0f, 1.0f, 1.0f, alpha));
        return;
    }

    // Fallback faceted icon (e.g. headless mode or first GPU capture frame)
    Color tint = tint_for_block(block_id);
    if (world) {
        const auto &blk = world->registry().get(block_id);
        if (!blk.tintable)
            tint = Color(blk.color.r, blk.color.g, blk.color.b, 1.0f);
    }
    PackedVector2Array gem{
        c + Vector2(0.0f, -sz * 0.58f),
        c + Vector2(sz * 0.56f, -sz * 0.14f),
        c + Vector2(sz * 0.42f, sz * 0.50f),
        c + Vector2(-sz * 0.44f, sz * 0.48f),
        c + Vector2(-sz * 0.58f, -sz * 0.10f),
    };
    draw_colored_polygon(gem, Color(tint.r, tint.g, tint.b, alpha));
    PackedVector2Array loop = gem;
    loop.push_back(gem[0]);
    draw_polyline(loop, Color(0.05f, 0.04f, 0.07f, 0.80f * alpha), 1.3f, true);
}

void ArcHotbar::draw_shape_preview(Vector2 c, float sz, int shape_id, Color tint, float alpha,
                                   const Ref<Texture2D> &tex) {
    const Vector2 dx(0.866f, 0.45f);
    const Vector2 dz(-0.866f, 0.45f);
    const Vector2 dy(0.0f, -0.96f);
    auto proj = [&](float x, float y, float z) {
        return c + dx * ((x - 0.5f) * sz) + dz * ((z - 0.5f) * sz) + dy * ((y - 0.5f) * sz);
    };

    switch (shape_id) {
    case int(altaria::BlockShape::Slab):
        draw_iso_box(c, sz, Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.45f, 1.0f), tint, alpha, tex);
        break;
    case int(altaria::BlockShape::VerticalSlab):
        draw_iso_box(c, sz, Vector3(0.46f, 0.0f, 0.0f), Vector3(0.96f, 1.0f, 1.0f), tint, alpha, tex);
        break;
    case int(altaria::BlockShape::Pillar):
        draw_iso_box(c, sz, Vector3(0.22f, 0.0f, 0.22f), Vector3(0.78f, 1.0f, 0.78f), tint, alpha, tex);
        break;
    case int(altaria::BlockShape::MiniPillar):
        draw_iso_box(c, sz, Vector3(0.35f, 0.0f, 0.35f), Vector3(0.65f, 1.0f, 0.65f), tint, alpha, tex);
        break;
    case int(altaria::BlockShape::Stairs):
        draw_iso_box(c, sz, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.50f, 1.0f, 1.0f), tint, alpha, tex);
        draw_iso_box(c, sz, Vector3(0.50f, 0.0f, 0.0f), Vector3(1.0f, 0.48f, 1.0f), tint, alpha, tex);
        break;
    case int(altaria::BlockShape::Ramp):
        draw_iso_quad(proj(0.0f, 0.96f, 0.0f), proj(1.0f, 0.06f, 0.0f),
                      proj(1.0f, 0.06f, 1.0f), proj(0.0f, 0.96f, 1.0f),
                      0.95f, tint, alpha, tex);
        draw_iso_tri(proj(0.0f, 0.96f, 1.0f), proj(1.0f, 0.06f, 1.0f), proj(0.0f, 0.0f, 1.0f),
                     0.64f, tint, alpha, tex);
        break;
    case int(altaria::BlockShape::Roof):
        draw_iso_quad(proj(0.5f, 0.96f, 0.0f), proj(1.0f, 0.14f, 0.0f),
                      proj(1.0f, 0.14f, 1.0f), proj(0.5f, 0.96f, 1.0f),
                      0.90f, tint, alpha, tex);
        draw_iso_tri(proj(0.5f, 0.96f, 1.0f), proj(1.0f, 0.14f, 1.0f), proj(0.0f, 0.14f, 1.0f),
                     0.64f, tint, alpha, tex);
        break;
    case int(altaria::BlockShape::Cube):
    default:
        draw_iso_box(c, sz, Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f), tint, alpha, tex);
        break;
    }
}

void ArcHotbar::draw_tool_preview(Vector2 c, float sz, const std::string &tool_id, float alpha) {
    const Color outline(0.063f, 0.071f, 0.110f, 0.92f * alpha);
    const Color wood_light(0.65f, 0.42f, 0.25f, alpha);
    const Color wood_dark(0.45f, 0.27f, 0.16f, alpha);
    const Color stone_light(0.76f, 0.81f, 0.87f, alpha);
    const Color stone_mid(0.56f, 0.62f, 0.71f, alpha);
    const Color stone_dark(0.39f, 0.44f, 0.53f, alpha);

    auto poly_with_border = [&](const PackedVector2Array &pts, Color fill) {
        draw_colored_polygon(pts, fill);
        PackedVector2Array loop = pts;
        loop.push_back(pts[0]);
        draw_polyline(loop, outline, 1.35f, true);
    };

    // Diagonal wooden handle from bottom-left to top-right
    const Vector2 u(0.7071f * sz, -0.7071f * sz);
    const Vector2 v(0.7071f * sz, 0.7071f * sz);
    auto pt = [&](float along, float perp) { return c + u * along + v * perp; };

    PackedVector2Array shaft{
        pt(-0.56f, -0.09f),
        pt(0.48f, -0.09f),
        pt(0.48f, 0.09f),
        pt(-0.56f, 0.09f),
    };
    poly_with_border(shaft, wood_light);

    PackedVector2Array pommel{
        pt(-0.62f, -0.13f),
        pt(-0.46f, -0.13f),
        pt(-0.46f, 0.13f),
        pt(-0.62f, 0.13f),
    };
    poly_with_border(pommel, wood_dark);

    if (tool_id == "world_wand") {
        // Golden ring collar below the floating miniature world orb
        const Color gold_mid(0.95f, 0.72f, 0.22f, alpha);
        const Color gold_hi(1.0f, 0.90f, 0.45f, alpha);
        const Color orb_ocean(0.18f, 0.72f, 0.96f, alpha);
        const Color orb_land(0.32f, 0.78f, 0.34f, alpha);
        const Color orb_glow(0.84f, 0.97f, 1.0f, 0.90f * alpha);

        PackedVector2Array collar{
            pt(0.08f, -0.18f),
            pt(0.24f, -0.18f),
            pt(0.24f, 0.18f),
            pt(0.08f, 0.18f),
        };
        poly_with_border(collar, gold_mid);
        PackedVector2Array collar_hi{
            pt(0.12f, -0.12f),
            pt(0.21f, -0.12f),
            pt(0.21f, 0.04f),
            pt(0.12f, 0.04f),
        };
        draw_colored_polygon(collar_hi, gold_hi);

        const Vector2 orb_c = pt(0.46f, 0.0f);
        const float r = sz * 0.27f;
        draw_circle(orb_c, r + 1.4f, outline);
        draw_circle(orb_c, r, orb_ocean);
        PackedVector2Array land{
            orb_c + Vector2(-r * 0.45f, -r * 0.25f),
            orb_c + Vector2(r * 0.18f, -r * 0.55f),
            orb_c + Vector2(r * 0.52f, -r * 0.05f),
            orb_c + Vector2(0.0f, r * 0.45f),
            orb_c + Vector2(-r * 0.50f, r * 0.20f),
        };
        draw_colored_polygon(land, orb_land);
        draw_circle(orb_c + Vector2(-r * 0.34f, -r * 0.34f), r * 0.24f, orb_glow);
    } else if (tool_id == "axe") {
        // Stone axe head: poll on top-left + flared curved cutting blade on bottom-right
        PackedVector2Array poll{
            pt(0.14f, -0.28f),
            pt(0.42f, -0.24f),
            pt(0.42f, -0.06f),
            pt(0.14f, -0.06f),
        };
        poly_with_border(poll, stone_dark);

        PackedVector2Array blade{
            pt(0.08f, -0.12f),
            pt(0.46f, -0.12f),
            pt(0.52f, 0.24f),
            pt(0.28f, 0.48f),
            pt(-0.02f, 0.34f),
        };
        poly_with_border(blade, stone_mid);

        PackedVector2Array edge_hi{
            pt(0.46f, 0.12f),
            pt(0.52f, 0.24f),
            pt(0.28f, 0.48f),
            pt(0.02f, 0.34f),
            pt(0.22f, 0.28f),
        };
        draw_colored_polygon(edge_hi, stone_light);
    } else {
        // Stone pickaxe head: curved double-pointed stone head perpendicular to handle
        PackedVector2Array head{
            pt(0.32f, -0.56f),
            pt(0.46f, -0.24f),
            pt(0.48f, 0.0f),
            pt(0.46f, 0.24f),
            pt(0.32f, 0.56f),
            pt(0.14f, 0.50f),
            pt(0.22f, 0.16f),
            pt(0.22f, -0.16f),
            pt(0.14f, -0.50f),
        };
        poly_with_border(head, stone_mid);

        PackedVector2Array collar{
            pt(0.18f, -0.15f),
            pt(0.46f, -0.15f),
            pt(0.46f, 0.15f),
            pt(0.18f, 0.15f),
        };
        poly_with_border(collar, stone_light);
    }
}

void ArcHotbar::draw_wand_mode_preview(Vector2 c, float sz, const std::string &mode_id, float alpha) {
    const Color outline(0.05f, 0.04f, 0.07f, 0.85f * alpha);
    const Color cyan(0.22f, 0.92f, 0.84f, alpha);
    const Color amber(0.98f, 0.74f, 0.24f, alpha);
    const Color emerald(0.32f, 0.84f, 0.48f, alpha);
    const Color coral(0.95f, 0.34f, 0.36f, alpha);
    const Color stone_c(0.70f, 0.74f, 0.80f, alpha);

    const Vector2 dx(0.866f, 0.45f);
    const Vector2 dz(-0.866f, 0.45f);
    const Vector2 dy(0.0f, -0.96f);
    auto proj = [&](float x, float y, float z) {
        return c + dx * ((x - 0.5f) * sz) + dz * ((z - 0.5f) * sz) + dy * ((y - 0.5f) * sz);
    };
    auto draw_wire_box = [&](Vector3 bmin, Vector3 bmax, Color col, float w = 1.5f) {
        Vector2 p000 = proj(bmin.x, bmin.y, bmin.z), p100 = proj(bmax.x, bmin.y, bmin.z);
        Vector2 p101 = proj(bmax.x, bmin.y, bmax.z), p001 = proj(bmin.x, bmin.y, bmax.z);
        Vector2 p010 = proj(bmin.x, bmax.y, bmin.z), p110 = proj(bmax.x, bmax.y, bmin.z);
        Vector2 p111 = proj(bmax.x, bmax.y, bmax.z), p011 = proj(bmin.x, bmax.y, bmax.z);
        draw_polyline(PackedVector2Array{p000, p100, p101, p001, p000}, col, w, true);
        draw_polyline(PackedVector2Array{p010, p110, p111, p011, p010}, col, w, true);
        draw_line(p000, p010, col, w, true);
        draw_line(p100, p110, col, w, true);
        draw_line(p101, p111, col, w, true);
        draw_line(p001, p011, col, w, true);
    };

    if (mode_id == "select") {
        draw_wire_box(Vector3(0.05f, 0.05f, 0.05f), Vector3(0.95f, 0.95f, 0.95f), cyan, 1.7f);
        draw_circle(proj(0.05f, 0.05f, 0.95f), sz * 0.14f, cyan);
        draw_circle(proj(0.95f, 0.95f, 0.05f), sz * 0.14f, amber);
    } else if (mode_id == "fill") {
        draw_iso_box(c, sz * 0.95f, Vector3(0.08f, 0.08f, 0.08f), Vector3(0.92f, 0.92f, 0.92f),
                     Color(0.32f, 0.84f, 0.72f, alpha), alpha, Ref<Texture2D>());
    } else if (mode_id == "replace") {
        draw_iso_box(c + Vector2(-sz * 0.24f, sz * 0.16f), sz * 0.62f, Vector3(0, 0, 0), Vector3(1, 1, 1), stone_c,
                     alpha, Ref<Texture2D>());
        draw_iso_box(c + Vector2(sz * 0.22f, -sz * 0.16f), sz * 0.62f, Vector3(0, 0, 0), Vector3(1, 1, 1), cyan,
                     alpha, Ref<Texture2D>());
        draw_line(c + Vector2(-sz * 0.28f, -sz * 0.24f), c + Vector2(sz * 0.05f, -sz * 0.42f), amber, 2.0f, true);
    } else if (mode_id == "walls") {
        draw_wire_box(Vector3(0.06f, 0.06f, 0.06f), Vector3(0.94f, 0.94f, 0.94f), Color(0.55f, 0.62f, 0.72f, 0.55f * alpha), 1.1f);
        draw_iso_quad(proj(0.06f, 0.94f, 0.94f), proj(0.94f, 0.94f, 0.94f),
                      proj(0.94f, 0.06f, 0.94f), proj(0.06f, 0.06f, 0.94f), 0.78f, amber, alpha, Ref<Texture2D>());
        draw_iso_quad(proj(0.94f, 0.94f, 0.06f), proj(0.94f, 0.06f, 0.06f),
                      proj(0.94f, 0.06f, 0.94f), proj(0.94f, 0.94f, 0.94f), 0.95f, amber, alpha, Ref<Texture2D>());
    } else if (mode_id == "hollow") {
        draw_iso_box(c, sz * 0.96f, Vector3(0.05f, 0.05f, 0.05f), Vector3(0.95f, 0.95f, 0.95f),
                     Color(0.56f, 0.70f, 0.88f, 0.52f * alpha), 0.55f * alpha, Ref<Texture2D>());
        draw_wire_box(Vector3(0.28f, 0.28f, 0.28f), Vector3(0.72f, 0.72f, 0.72f), outline, 1.5f);
    } else if (mode_id == "copy_paste") {
        draw_wire_box(Vector3(-0.08f, -0.05f, 0.15f), Vector3(0.58f, 0.62f, 0.80f), cyan, 1.4f);
        draw_iso_box(c + Vector2(sz * 0.20f, -sz * 0.14f), sz * 0.60f, Vector3(0, 0, 0), Vector3(1, 1, 1), emerald,
                     alpha, Ref<Texture2D>());
    } else if (mode_id == "stack") {
        draw_iso_box(c + Vector2(0.0f, sz * 0.28f), sz * 0.65f, Vector3(0, 0, 0), Vector3(1, 0.48f, 1), stone_c,
                     alpha, Ref<Texture2D>());
        draw_iso_box(c, sz * 0.65f, Vector3(0, 0, 0), Vector3(1, 0.48f, 1), cyan, alpha, Ref<Texture2D>());
        draw_iso_box(c + Vector2(0.0f, -sz * 0.28f), sz * 0.65f, Vector3(0, 0, 0), Vector3(1, 0.48f, 1), amber,
                     alpha, Ref<Texture2D>());
    } else {
        draw_wire_box(Vector3(0.10f, 0.10f, 0.10f), Vector3(0.90f, 0.90f, 0.90f), coral, 1.6f);
        draw_line(c + Vector2(-sz * 0.24f, -sz * 0.24f), c + Vector2(sz * 0.24f, sz * 0.24f), coral, 2.2f, true);
        draw_line(c + Vector2(-sz * 0.24f, sz * 0.24f), c + Vector2(sz * 0.24f, -sz * 0.24f), coral, 2.2f, true);
    }
}

void ArcHotbar::draw_wand_status_pill(Vector2 center, float scale, float alpha) {
    if (alpha <= 0.02f || !inventory)
        return;
    const auto *def = inventory->find_alt_menu("wand_modes");
    String mode_name = "Select";
    String mode_help;
    if (def && wand_mode >= 0 && wand_mode < int(def->options.size())) {
        const auto &opt = def->options[wand_mode];
        mode_name = String::utf8((pt_br ? opt.label_pt : opt.label_en).c_str());
        mode_help = String::utf8((pt_br ? opt.desc_pt : opt.desc_en).c_str());
    }
    String mat_name = wand_material_air
        ? String(pt_br ? "Ar" : "Air")
        : (wand_material > 0 ? block_display_name(wand_material) : String(pt_br ? "Pedra" : "Stone"));
    String line1 = (pt_br ? String("Varinha: ") : String("Wand: ")) + mode_name;
    if (wand_mode == int(altaria::WandMode::Replace)) {
        String tgt_name = wand_replace_air
            ? String(pt_br ? "Ar" : "Air")
            : (wand_replace_target > 0 ? block_display_name(wand_replace_target) : String("*"));
        line1 += String::utf8("  ·  ") + tgt_name + " -> " + mat_name;
    } else if (wand_mode == int(altaria::WandMode::Fill) || wand_mode == int(altaria::WandMode::Walls) ||
               wand_mode == int(altaria::WandMode::Hollow)) {
        line1 += String::utf8("  ·  ") + mat_name;
    } else if (wand_mode == int(altaria::WandMode::CopyPaste)) {
        line1 += wand_has_clip
            ? String::utf8("  ·  ") + String::num_int64(wand_clip_blocks) + (pt_br ? " blocos (" : " blocks (") +
                  String::num_int64(wand_paste_rot * 90) + String::utf8("°)")
            : (pt_br ? String::utf8("  ·  Área não copiada") : String::utf8("  ·  Clipboard empty"));
    }
    String line2 = wand_has_sel
        ? (pt_br ? String::utf8("Seleção: ") : String("Area: ")) + String::num_int64(wand_sel_size.x) +
              String::utf8("×") + String::num_int64(wand_sel_size.y) + String::utf8("×") +
              String::num_int64(wand_sel_size.z) + " (" +
              String::num_int64(int64_t(wand_sel_size.x) * wand_sel_size.y * wand_sel_size.z) + ")"
        : (pt_br ? String::utf8("Sem seleção · Segure Alt: modo") : String::utf8("No area · Hold Alt: mode"));
    if (wand_feedback_timer > 0.0f && !wand_feedback.is_empty())
        line2 = wand_feedback;

    Ref<Font> f_title = font_bold.is_valid() ? font_bold : font_regular;
    Ref<Font> f_sub = font_regular.is_valid() ? font_regular : font_bold;
    const int sz1 = std::max(12, int(std::round(13.5f * scale)));
    const int sz2 = std::max(10, int(std::round(11.0f * scale)));
    const int sz3 = std::max(10, int(std::round(10.5f * scale)));
    float max_w = 340.0f * scale;
    if (f_title.is_valid())
        max_w = std::max(max_w, f_title->get_string_size(line1, HORIZONTAL_ALIGNMENT_LEFT, -1, sz1).x + 30.0f * scale);
    if (f_sub.is_valid()) {
        max_w = std::max(max_w, f_sub->get_string_size(line2, HORIZONTAL_ALIGNMENT_LEFT, -1, sz2).x + 30.0f * scale);
        if (!mode_help.is_empty())
            max_w = std::max(max_w, f_sub->get_string_size(mode_help, HORIZONTAL_ALIGNMENT_LEFT, -1, sz3).x + 30.0f * scale);
    }

    const float box_h = mode_help.is_empty() ? (40.0f * scale) : (56.0f * scale);
    Vector2 box_size(max_w, box_h);
    Vector2 box_pos(center.x - box_size.x * 0.5f, center.y - (168.0f * scale + box_h));
    float chamfer = 4.0f * scale;
    PackedVector2Array bg{
        Vector2(box_pos.x + chamfer, box_pos.y),
        Vector2(box_pos.x + box_size.x - chamfer, box_pos.y),
        Vector2(box_pos.x + box_size.x, box_pos.y + chamfer),
        Vector2(box_pos.x + box_size.x, box_pos.y + box_size.y - chamfer),
        Vector2(box_pos.x + box_size.x - chamfer, box_pos.y + box_size.y),
        Vector2(box_pos.x + chamfer, box_pos.y + box_size.y),
        Vector2(box_pos.x, box_pos.y + box_size.y - chamfer),
        Vector2(box_pos.x, box_pos.y + chamfer),
    };
    draw_colored_polygon(bg, Color(0.08f, 0.10f, 0.14f, 0.78f * alpha));
    PackedVector2Array loop = bg;
    loop.push_back(bg[0]);
    draw_polyline(loop, Color(0.22f, 0.88f, 0.82f, 0.72f * alpha), 1.25f, true);

    if (f_title.is_valid()) {
        Vector2 tpos(box_pos.x, box_pos.y + 17.0f * scale);
        draw_string_outline(f_title, tpos, line1, HORIZONTAL_ALIGNMENT_CENTER, box_size.x, sz1, 3,
                            Color(0, 0, 0, 0.88f * alpha));
        draw_string(f_title, tpos, line1, HORIZONTAL_ALIGNMENT_CENTER, box_size.x, sz1,
                    Color(0.86f, 0.98f, 0.96f, alpha));
    }
    if (f_sub.is_valid()) {
        Vector2 spos(box_pos.x, box_pos.y + 33.0f * scale);
        Color c2 = wand_feedback_timer > 0.0f ? Color(0.98f, 0.86f, 0.42f, alpha)
                                              : Color(0.78f, 0.86f, 0.90f, 0.92f * alpha);
        draw_string_outline(f_sub, spos, line2, HORIZONTAL_ALIGNMENT_CENTER, box_size.x, sz2, 2,
                            Color(0, 0, 0, 0.85f * alpha));
        draw_string(f_sub, spos, line2, HORIZONTAL_ALIGNMENT_CENTER, box_size.x, sz2, c2);
        if (!mode_help.is_empty()) {
            Vector2 hpos(box_pos.x, box_pos.y + 49.0f * scale);
            draw_string_outline(f_sub, hpos, mode_help, HORIZONTAL_ALIGNMENT_CENTER, box_size.x, sz3, 2,
                                Color(0, 0, 0, 0.85f * alpha));
            draw_string(f_sub, hpos, mode_help, HORIZONTAL_ALIGNMENT_CENTER, box_size.x, sz3,
                        Color(0.68f, 0.78f, 0.84f, 0.88f * alpha));
        }
    }
}

void ArcHotbar::draw_center_plaque(Vector2 center, float scale, const String &title, const String &subtitle,
                                   float alpha) {
    if (alpha <= 0.02f)
        return;
    Ref<Font> f_title = font_bold.is_valid() ? font_bold : font_regular;
    Ref<Font> f_sub = font_regular.is_valid() ? font_regular : font_bold;
    const int sz_title = std::max(13, int(std::round(17.5f * scale)));
    const int sz_sub = std::max(10, int(std::round(12.0f * scale)));
    float max_w = 276.0f * scale;
    if (f_title.is_valid())
        max_w = std::max(max_w, f_title->get_string_size(title, HORIZONTAL_ALIGNMENT_LEFT, -1, sz_title).x + 34.0f * scale);
    if (f_sub.is_valid())
        max_w = std::max(max_w, f_sub->get_string_size(subtitle, HORIZONTAL_ALIGNMENT_LEFT, -1, sz_sub).x + 34.0f * scale);
    Vector2 box_size(max_w, 45.0f * scale);
    Vector2 box_pos(center.x - box_size.x * 0.5f, center.y - 211.0f * scale);
    float chamfer = 4.0f * scale;
    PackedVector2Array bg{
        Vector2(box_pos.x + chamfer, box_pos.y),
        Vector2(box_pos.x + box_size.x - chamfer, box_pos.y),
        Vector2(box_pos.x + box_size.x, box_pos.y + chamfer),
        Vector2(box_pos.x + box_size.x, box_pos.y + box_size.y - chamfer),
        Vector2(box_pos.x + box_size.x - chamfer, box_pos.y + box_size.y),
        Vector2(box_pos.x + chamfer, box_pos.y + box_size.y),
        Vector2(box_pos.x, box_pos.y + box_size.y - chamfer),
        Vector2(box_pos.x, box_pos.y + chamfer),
    };
    Color fill(0.11f, 0.075f, 0.11f, 0.78f * alpha);
    Color border(0.28f + 0.50f * confirm_flash, 0.22f + 0.50f * confirm_flash, 0.30f + 0.45f * confirm_flash,
                 (0.55f + 0.40f * confirm_flash) * alpha);
    draw_colored_polygon(bg, fill);
    PackedVector2Array loop = bg;
    loop.push_back(bg[0]);
    draw_polyline(loop, border, 1.25f, true);

    if (f_title.is_valid()) {
        Vector2 tpos(box_pos.x, box_pos.y + 23.0f * scale);
        draw_string_outline(f_title, tpos, title, HORIZONTAL_ALIGNMENT_CENTER, box_size.x, sz_title, 3,
                            Color(0, 0, 0, 0.85f * alpha));
        draw_string(f_title, tpos, title, HORIZONTAL_ALIGNMENT_CENTER, box_size.x, sz_title,
                    Color(0.96f, 0.94f, 0.98f, alpha));
    }
    if (f_sub.is_valid()) {
        Vector2 spos(box_pos.x, box_pos.y + 40.0f * scale);
        draw_string_outline(f_sub, spos, subtitle, HORIZONTAL_ALIGNMENT_CENTER, box_size.x, sz_sub, 2,
                            Color(0, 0, 0, 0.85f * alpha));
        draw_string(f_sub, spos, subtitle, HORIZONTAL_ALIGNMENT_CENTER, box_size.x, sz_sub,
                    Color(0.84f, 0.81f, 0.88f, 0.92f * alpha));
    }
}

void ArcHotbar::_process(double raw_delta) {
    ALTARIA_PROFILE("hotbar.process");
    if (!is_visible_in_tree() || !inventory)
        return;
    step_deposit_icon_bake();
    float dt = std::min(float(raw_delta), 0.05f);
    alt_anim = exp_lerp(alt_anim, alt_open ? 1.0f : 0.0f, alt_open ? 18.0f : 15.0f, dt);
    if (shake_timer > 0.0f)
        shake_timer = std::max(0.0f, shake_timer - dt);
    if (confirm_flash > 0.0f)
        confirm_flash = std::max(0.0f, confirm_flash - dt * 3.5f);
    if (wand_feedback_timer > 0.0f)
        wand_feedback_timer = std::max(0.0f, wand_feedback_timer - dt);

    idle_timer = 0.0f;
    idle_blend = 0.0f;

    for (int i = 0; i < altaria::HOTBAR_SLOT_COUNT; ++i) {
        float target = (i == inventory->selected() && !alt_open) ? 1.0f : 0.0f;
        slot_pop[i] = exp_lerp(slot_pop[i], target, 16.0f, dt);
        if (slot_pickup[i] > 0.0f)
            slot_pickup[i] = std::max(0.0f, slot_pickup[i] - dt * 3.8f);
    }

    const auto *def = inventory->find_alt_menu(active_alt_menu_id);
    int alt_count = def ? std::min(int(def->options.size()), int(alt_pop.size())) : 0;
    bool animating = alt_anim > 0.002f && alt_anim < 0.998f;
    animating = animating || shake_timer > 0.0f || confirm_flash > 0.0f || wand_feedback_timer > 0.0f;
    animating = animating || (idle_blend > 0.002f && idle_blend < 0.998f);
    for (int i = 0; i < altaria::HOTBAR_SLOT_COUNT; ++i) {
        const float target = (i == inventory->selected() && !alt_open) ? 1.0f : 0.0f;
        animating = animating || std::abs(slot_pop[i] - target) > 0.002f || slot_pickup[i] > 0.0f;
    }
    for (int i = 0; i < alt_count; ++i) {
        alt_pop[i] = exp_lerp(alt_pop[i], i == alt_hovered ? 1.0f : 0.0f, 18.0f, dt);
        animating = animating || std::abs(alt_pop[i] - (i == alt_hovered ? 1.0f : 0.0f)) > 0.002f;
    }
    // Repaint only while something moves or the inventory changed; a static
    // hotbar used to redraw every polygon, number and texture each frame.
    const uint64_t sig = content_signature();
    if (animating || sig != last_signature) {
        last_signature = sig;
        queue_redraw();
    }
}

uint64_t ArcHotbar::content_signature() const {
    if (!inventory)
        return 0;
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
    mix(uint64_t(inventory->selected()));
    mix(uint64_t(active_shape) | (uint64_t(active_rotation) << 8) | (uint64_t(unlimited_blocks) << 16) |
        (uint64_t(alt_open) << 17) | (uint64_t(alt_hovered & 0xff) << 18));
    for (int i = 0; i < altaria::HOTBAR_SLOT_COUNT; ++i) {
        const auto &s = inventory->slot(i);
        mix(uint64_t(s.block) | (uint64_t(uint32_t(s.count)) << 16) | (uint64_t(s.shape_id & 0xff) << 48));
        mix(std::hash<std::string>{}(s.tool_id));
    }
    mix(uint64_t(wand_mode & 0xff) | (uint64_t(wand_material) << 8) | (uint64_t(wand_replace_target) << 24) |
        (uint64_t(wand_has_sel) << 40) | (uint64_t(wand_has_clip) << 41) | (uint64_t(wand_paste_rot & 3) << 42) |
        (uint64_t(wand_feedback_timer > 0.0f) << 44) | (uint64_t(wand_material_air) << 45) |
        (uint64_t(wand_replace_air) << 46));
    mix(uint64_t(wand_sel_size.x) ^ (uint64_t(wand_sel_size.y) << 16) ^ (uint64_t(wand_sel_size.z) << 32));
    mix(uint64_t(get_size().x) | (uint64_t(get_size().y) << 32));
    return h;
}

void ArcHotbar::_draw() {
    ALTARIA_PROFILE("hotbar.draw");
    if (!inventory)
        return;

    const float scale = std::clamp(get_size().y / 900.0f, 0.72f, 1.35f);
    const Vector2 center = arc_center(scale);
    const float main_alpha = (1.0f - ease_out_cubic(alt_anim)) * get_idle_alpha();
    const float alt_alpha = ease_out_cubic(alt_anim);

    if (main_alpha > 0.01f) {
        const float r_in = (110.0f - 4.0f * (1.0f - (1.0f - ease_out_cubic(alt_anim)))) * scale;
        const float r_out = (154.0f - 4.0f * (1.0f - (1.0f - ease_out_cubic(alt_anim)))) * scale;
        const float total_rad = (kEndDeg - kStartDeg) * kDegToRad;
        const float slot_step = total_rad / float(altaria::HOTBAR_SLOT_COUNT);
        const float gap_rad = 2.0f * kDegToRad;

        for (int i = 0; i < altaria::HOTBAR_SLOT_COUNT; ++i) {
            const auto &s = inventory->slot(i);
            const bool selected = i == inventory->selected();
            const float pop = slot_pop[i];
            const float pick = ease_out_cubic(slot_pickup[i]);
            const float lift = (pop * 4.5f + pick * 5.0f) * scale;
            const float a0 = kStartDeg * kDegToRad + float(i) * slot_step + gap_rad * 0.5f;
            const float a1 = kStartDeg * kDegToRad + float(i + 1) * slot_step - gap_rad * 0.5f;
            const float mid_a = (a0 + a1) * 0.5f;

            Color fill = s.empty()
                ? Color(0.11f + 0.14f * pop, 0.075f + 0.12f * pop, 0.11f + 0.16f * pop,
                        (0.68f + 0.14f * pop) * main_alpha)
                : Color(0.11f + 0.21f * pop + 0.14f * pick, 0.075f + 0.205f * pop + 0.12f * pick,
                        0.11f + 0.26f * pop + 0.08f * pick, (0.79f + 0.12f * pop) * main_alpha);
            Color border = selected
                ? Color(0.95f, 0.94f, 0.98f, 0.96f * main_alpha)
                : Color(0.05f + 0.25f * pick, 0.03f + 0.22f * pick, 0.05f + 0.15f * pick,
                        (0.42f + 0.45f * pick) * main_alpha);
            draw_wedge(center, r_in + pop * 1.5f * scale, r_out + lift, a0, a1, fill, border,
                       selected ? 2.0f : 1.0f);

            Vector2 dir(std::cos(mid_a), std::sin(mid_a));
            Vector2 icon_pos = center + dir * ((r_in + r_out + lift) * 0.5f);

            if (!s.empty()) {
                float iso_sz = (18.0f + 2.0f * pop + 3.5f * pick) * scale;
                if (s.is_tool()) {
                    draw_tool_preview(icon_pos, iso_sz * 1.15f, s.tool_id, main_alpha);
                } else {
                    const bool is_deposit = world && !world->registry().get(s.block).deposit_model.empty();
                    if (is_deposit) {
                        draw_deposit_preview(icon_pos, iso_sz, s.block, main_alpha);
                    } else {
                        int draw_shape = selected ? active_shape : s.shape_id;
                        draw_shape_preview(icon_pos, iso_sz, draw_shape, tint_for_block(s.block), main_alpha,
                                           texture_for_block(s.block));
                    }

                    if (!unlimited_blocks) {
                        if (digit_textures[1].is_valid()) {
                            draw_pixel_number(icon_pos + Vector2(15.0f * scale, 11.0f * scale), s.count,
                                              2.0f * scale, 1, Color(1.0f, 1.0f, 1.0f, main_alpha));
                        } else {
                            Ref<Font> f = font_bold.is_valid() ? font_bold : font_regular;
                            if (f.is_valid()) {
                                int c_sz = std::max(10, int(std::round(11.0f * scale)));
                                String cnt = String::num_int64(s.count);
                                Vector2 cnt_pos = icon_pos + Vector2(-7.0f * scale, 18.0f * scale);
                                draw_string_outline(f, cnt_pos, cnt, HORIZONTAL_ALIGNMENT_RIGHT, 28.0f * scale, c_sz, 3,
                                                    Color(0, 0, 0, 0.9f * main_alpha));
                                draw_string(f, cnt_pos, cnt, HORIZONTAL_ALIGNMENT_RIGHT, 28.0f * scale, c_sz,
                                            Color(0.98f, 0.96f, 0.88f, main_alpha));
                            }
                        }
                    }
                }
            }
        }
        const auto &active = inventory->active_slot();
        if (active.tool_id == "world_wand") {
            draw_wand_status_pill(center, scale, main_alpha);
        }
    }

    if (alt_alpha > 0.01f) {
        const auto *def = inventory->find_alt_menu(active_alt_menu_id);
        const auto &held = inventory->active_slot();
        if (def && !def->options.empty() && (held.has_block() || held.tool_id == "world_wand" || def->id == "wand_modes")) {
            const int n = int(def->options.size());
            const float morph = 0.93f + 0.07f * alt_alpha;
            const float r_in = 110.0f * scale * morph;
            const float r_out = 154.0f * scale * morph;
            const float total_rad = (kEndDeg - kStartDeg) * kDegToRad;
            const float base_step = total_rad / float(altaria::HOTBAR_SLOT_COUNT);
            const float step = (n <= altaria::HOTBAR_SLOT_COUNT) ? base_step : (total_rad / float(n));
            const float alt_start = -0.5f * kPi - (step * float(n)) * 0.5f;
            const float gap_rad = 2.0f * kDegToRad;

            const bool wand_menu = (def->id == "wand_modes");
            Ref<Texture2D> block_tex = held.has_block() ? texture_for_block(held.block) : Ref<Texture2D>();
            Color block_tint = held.has_block() ? tint_for_block(held.block) : Color(1, 1, 1, 1);

            for (int i = 0; i < n; ++i) {
                const auto &opt = def->options[i];
                const bool selected = i == alt_hovered;
                const float pop = i < int(alt_pop.size()) ? alt_pop[i] : 0.0f;
                const float lift = pop * 4.5f * scale;
                const float a0 = alt_start + float(i) * step + gap_rad * 0.5f;
                const float a1 = alt_start + float(i + 1) * step - gap_rad * 0.5f;
                const float mid_a = (a0 + a1) * 0.5f;

                Color fill = Color(0.11f + 0.21f * pop, 0.075f + 0.205f * pop, 0.11f + 0.26f * pop,
                                   (0.79f + 0.12f * pop) * alt_alpha);
                Color border = selected
                    ? Color(0.95f, 0.94f, 0.98f, 0.96f * alt_alpha)
                    : Color(0.05f, 0.03f, 0.05f, 0.42f * alt_alpha);
                draw_wedge(center, r_in + pop * 1.5f * scale, r_out + lift, a0, a1, fill, border,
                           selected ? 2.0f : 1.0f);

                Vector2 dir(std::cos(mid_a), std::sin(mid_a));
                Vector2 icon_pos = center + dir * ((r_in + r_out + lift) * 0.5f);
                float iso_sz = (18.0f + 2.0f * pop) * scale;
                if (wand_menu)
                    draw_wand_mode_preview(icon_pos, iso_sz, opt.id, alt_alpha);
                else
                    draw_shape_preview(icon_pos, iso_sz, opt.shape_id, block_tint, alt_alpha, block_tex);
            }

            const auto &sel_opt = def->options[std::clamp(alt_hovered, 0, n - 1)];
            String opt_title = String::utf8((pt_br ? sel_opt.label_pt : sel_opt.label_en).c_str());
            const std::string &raw_desc = pt_br ? sel_opt.desc_pt : sel_opt.desc_en;
            String opt_sub = !raw_desc.empty()
                ? String::utf8(raw_desc.c_str())
                : (pt_br ? String::utf8("Escolha uma opção.") : String::utf8("Choose an option."));
            draw_center_plaque(center, scale, opt_title, opt_sub, alt_alpha);
        }
    }
}

} // namespace godot
