#include "diorama_minimap.h"
#include "../../game/debug/frame_profile.h"
#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace godot {
namespace {

constexpr int kImgSize = 192;
// Buffer being painted and its data pointer (see blend_pixel). Main thread only.
static const godot::PackedByteArray *paint_owner = nullptr;
static uint8_t *paint_data = nullptr;
constexpr int kHalfImg = kImgSize / 2;
constexpr float kMinimapWorldRadius = 52.0f;

constexpr float kExploreFullRadius = 78.0f;
constexpr float kExploreMaxRadius = 92.0f;

constexpr float kFullMinRadius = 36.0f;
constexpr float kFullMaxRadius = 260.0f;
constexpr float kPi = 3.14159265359f;

uint64_t col_key(int x, int z) {
    return (uint64_t(uint32_t(x)) << 32) | uint64_t(uint32_t(z));
}

int floor_div(int a, int b) {
    int q = a / b;
    int r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? (q - 1) : q;
}

uint8_t u8_clamp(float v) {
    return uint8_t(std::clamp(int(std::round(v)), 0, 255));
}

float hash2d(int x, int z) {
    uint64_t n = uint64_t(uint32_t(x)) * 0x9e3779b185ebca87ULL ^ uint64_t(uint32_t(z)) * 0xc2b2ae3d27d4eb4fULL;
    n = (n ^ (n >> 30)) * 0xbf58476d1ce4e5b9ULL;
    n ^= (n >> 27);
    return float(n & 0xFFFFu) / 65535.0f;
}

float smooth_noise2d(float x, float z) {
    int ix = int(std::floor(x));
    int iz = int(std::floor(z));
    float fx = x - float(ix);
    float fz = z - float(iz);
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);
    float a = hash2d(ix, iz);
    float b = hash2d(ix + 1, iz);
    float c = hash2d(ix, iz + 1);
    float d = hash2d(ix + 1, iz + 1);
    return (a * (1.0f - fx) + b * fx) * (1.0f - fz) + (c * (1.0f - fx) + d * fx) * fz;
}

float cloud_fbm(float wx, float wz) {
    float n1 = smooth_noise2d(wx * 0.052f, wz * 0.052f);
    float n2 = smooth_noise2d(wx * 0.125f + 19.3f, wz * 0.125f - 11.7f);
    float n3 = smooth_noise2d(wx * 0.28f - 7.1f, wz * 0.28f + 23.4f);
    return n1 * 0.58f + n2 * 0.29f + n3 * 0.13f;
}

void extract_column_colors(const altaria::Column &c, const altaria::Registry &reg, const altaria::Settings &cfg, int wx,
                           int wz, uint8_t &out_top_r, uint8_t &out_top_g, uint8_t &out_top_b, uint8_t &out_side_r,
                           uint8_t &out_side_g, uint8_t &out_side_b) {
    float r = 92.0f, g = 148.0f, b = 78.0f;
    float sr = 86.0f, sg = 82.0f, sb = 80.0f;

    if (cfg.preset == altaria::WorldPreset::Plain) {
        r = 104.0f;
        g = 114.0f;
        b = 118.0f;
        sr = 72.0f;
        sg = 78.0f;
        sb = 84.0f;
    } else {
        const auto &bdef = reg.get(c.surface);
        if (c.surface == reg.grass) {
            r = std::clamp(c.grass_tint.r * 232.0f, 38.0f, 224.0f);
            g = std::clamp(c.grass_tint.g * 232.0f, 50.0f, 234.0f);
            b = std::clamp(c.grass_tint.b * 232.0f, 36.0f, 212.0f);
            sr = r * 0.56f + 36.0f;
            sg = g * 0.48f + 32.0f;
            sb = b * 0.48f + 32.0f;
        } else if (c.surface == reg.snow) {
            r = 228.0f;
            g = 236.0f;
            b = 246.0f;
            sr = 148.0f;
            sg = 160.0f;
            sb = 178.0f;
        } else if (c.surface == reg.sand) {
            r = 216.0f;
            g = 198.0f;
            b = 142.0f;
            sr = 166.0f;
            sg = 146.0f;
            sb = 102.0f;
        } else if (bdef.id != 0) {
            r = std::clamp(bdef.color.r * 225.0f, 35.0f, 240.0f);
            g = std::clamp(bdef.color.g * 225.0f, 35.0f, 240.0f);
            b = std::clamp(bdef.color.b * 225.0f, 35.0f, 240.0f);
            sr = r * 0.68f;
            sg = g * 0.66f;
            sb = b * 0.72f;
        }
        if (c.cliff > 0.30f) {
            float mix = std::clamp((c.cliff - 0.30f) * 1.45f, 0.0f, 0.82f);
            r = Math::lerp(r, 126.0f, mix);
            g = Math::lerp(g, 122.0f, mix);
            b = Math::lerp(b, 130.0f, mix);
            sr = Math::lerp(sr, 84.0f, mix);
            sg = Math::lerp(sg, 82.0f, mix);
            sb = Math::lerp(sb, 90.0f, mix);
        }
        if (((wx + wz) & 1) == 0) {
            r = std::min(255.0f, r * 1.045f);
            g = std::min(255.0f, g * 1.045f);
            b = std::min(255.0f, b * 1.045f);
        } else {
            r *= 0.965f;
            g *= 0.965f;
            b *= 0.965f;
        }
    }

    out_top_r = u8_clamp(r);
    out_top_g = u8_clamp(g);
    out_top_b = u8_clamp(b);
    out_side_r = u8_clamp(sr);
    out_side_g = u8_clamp(sg);
    out_side_b = u8_clamp(sb);
}

void extract_tree_color(uint32_t packed_tint, uint8_t &out_r, uint8_t &out_g, uint8_t &out_b) {
    auto crgb = altaria::unpack_rgb(packed_tint);
    out_r = u8_clamp(std::clamp(crgb.r * 232.0f, 34.0f, 205.0f));
    out_g = u8_clamp(std::clamp(crgb.g * 238.0f, 70.0f, 224.0f));
    out_b = u8_clamp(std::clamp(crgb.b * 224.0f, 32.0f, 190.0f));
}

} // namespace

void DioramaMinimap::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_point_explored", "wx", "wz"), &DioramaMinimap::is_point_explored);
}

void DioramaMinimap::_ready() {
    set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    set_texture_filter(CanvasItem::TEXTURE_FILTER_NEAREST);
    font_bold = ResourceLoader::get_singleton()->load(
        "res://addons/crabbyconsole/fonts/roboto/static/RobotoMono-Bold.ttf");
    font_regular = ResourceLoader::get_singleton()->load(
        "res://addons/crabbyconsole/fonts/roboto/static/RobotoMono-Regular.ttf");
    border_tex = ResourceLoader::get_singleton()->load("res://game/assets/hud/minimap_border.png");
    arrow_tex = ResourceLoader::get_singleton()->load("res://game/assets/hud/minimap_arrow.png");
    north_tex = ResourceLoader::get_singleton()->load("res://game/assets/hud/north_indicator.png");

    // Extract exact border palette from minimap_border.png (#10121c outer, #2c1e31 inner)
    Color border_outer(0.063f, 0.071f, 0.110f, 1.0f);
    Color border_inner(0.173f, 0.118f, 0.192f, 1.0f);
    if (border_tex.is_valid()) {
        Ref<Image> bimg = border_tex->get_image();
        if (bimg.is_valid()) {
            if (bimg->is_compressed())
                bimg->decompress();
            bimg->convert(Image::FORMAT_RGBA8);
            if (bimg->get_width() >= 18 && bimg->get_height() >= 18) {
                border_outer = bimg->get_pixel(8, 0);
                border_inner = bimg->get_pixel(8, 1);
            }
        }
    }

    // Build crisp 96x96 texel pixel-art minimap ring (1 texel = 2x2 screen px at default scale,
    // matching the exact pixel density of minimap_arrow.png, north_indicator.png & number_sheet.png)
    {
        constexpr int kRingGrid = 96;
        Ref<Image> rimg = Image::create_empty(kRingGrid, kRingGrid, false, Image::FORMAT_RGBA8);
        const float half = float(kRingGrid) * 0.5f;
        const float r_out = half - 0.5f;      // 47.5
        const float r_mid_out = r_out - 1.1f; // 46.4
        const float r_mid_in = r_out - 3.2f;  // 44.3
        const float r_in = r_out - 4.2f;      // 43.3
        for (int y = 0; y < kRingGrid; ++y) {
            for (int x = 0; x < kRingGrid; ++x) {
                float dx = float(x) + 0.5f - half;
                float dy = float(y) + 0.5f - half;
                float d = std::sqrt(dx * dx + dy * dy);
                if (d <= r_out && d > r_mid_out) {
                    rimg->set_pixel(x, y, border_outer);
                } else if (d <= r_mid_out && d > r_mid_in) {
                    rimg->set_pixel(x, y, border_inner);
                } else if (d <= r_mid_in && d > r_in) {
                    rimg->set_pixel(x, y, border_outer);
                } else {
                    rimg->set_pixel(x, y, Color(0, 0, 0, 0));
                }
            }
        }
        ring_tex = ImageTexture::create_from_image(rimg);
    }

    // Build crisp 10x9 pixel-art north_indicator.png (8x7) with a 1-texel #10121c outer silhouette
    // so its #2c1e31 inner outline and #ffffff/#deceed glyph match minimap_arrow.png 1:1
    if (north_tex.is_valid()) {
        Ref<Image> nimg = north_tex->get_image();
        if (nimg.is_valid()) {
            if (nimg->is_compressed())
                nimg->decompress();
            nimg->convert(Image::FORMAT_RGBA8);
            const int nw = nimg->get_width();
            const int nh = nimg->get_height();
            const int bw = nw + 2;
            const int bh = nh + 2;
            Ref<Image> nb = Image::create_empty(bw, bh, false, Image::FORMAT_RGBA8);
            for (int y = 0; y < bh; ++y) {
                for (int x = 0; x < bw; ++x) {
                    int sx = x - 1;
                    int sy = y - 1;
                    if (sx >= 0 && sx < nw && sy >= 0 && sy < nh) {
                        Color c = nimg->get_pixel(sx, sy);
                        if (c.a > 0.1f) {
                            nb->set_pixel(x, y, c);
                            continue;
                        }
                    }
                    bool adj = false;
                    for (int dy = -1; dy <= 1 && !adj; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            int nx = sx + dx;
                            int ny = sy + dy;
                            if (nx >= 0 && nx < nw && ny >= 0 && ny < nh && nimg->get_pixel(nx, ny).a > 0.1f) {
                                adj = true;
                                break;
                            }
                        }
                    }
                    nb->set_pixel(x, y, adj ? border_outer : Color(0, 0, 0, 0));
                }
            }
            north_badge_tex = ImageTexture::create_from_image(nb);
        }
    }
    // Trees are stamped as real leaf/log blocks now; the old sprite is no longer loaded.
    pixels.resize(kImgSize * kImgSize * 4);
    minimap_reveal.resize(kImgSize * kImgSize, 0);
}

void DioramaMinimap::bind_sources(const VoxelWorld *w, const FlyingPlayer *p, const BuiltWorld *b, bool is_pt_br) {
    pt_br = is_pt_br;
    if (world != w) {
        patch_cache.clear();
        explore_cache.clear();
        explore_revision = 0;
        last_revision = UINT64_MAX;
        full_last_revision = UINT64_MAX;
        last_explore_rev = UINT64_MAX;
        full_last_explore_rev = UINT64_MAX;
        last_tile_count = 0;
        last_explore_pos = Vector3(1e9f, 0.0f, 1e9f);
        full_map_dirty = true;
        if (!w && full_map_open)
            set_full_map_open(false);
    }
    world = w;
    player = p;
    built_world = b;
}

void DioramaMinimap::set_placement(Vector2 center, float radius, float alpha) {
    bool changed = minimap_center != center || std::abs(minimap_radius - radius) > 0.25f ||
                   std::abs(fade_alpha - alpha) > 0.01f;
    minimap_center = center;
    minimap_radius = radius;
    fade_alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (changed)
        queue_redraw();
}

void DioramaMinimap::set_full_map_open(bool open) {
    if (full_map_open == open)
        return;
    full_map_open = open;
    dragging_map = false;
    set_mouse_filter(open ? Control::MOUSE_FILTER_STOP : Control::MOUSE_FILTER_IGNORE);
    if (open) {
        if (player) {
            Vector3 p = player->get_position();
            full_center_x = p.x;
            full_center_z = p.z;
            stamp_exploration(p.x, p.z);
        }
        full_map_dirty = true;
        full_refresh_timer = 1.0f;
        open_anim = 0.0f;
    }
    if (on_full_map_changed)
        on_full_map_changed(open);
    queue_redraw();
}

void DioramaMinimap::recenter_full_map() {
    if (!player)
        return;
    Vector3 p = player->get_position();
    full_center_x = p.x;
    full_center_z = p.z;
    full_map_dirty = true;
    full_refresh_timer = 1.0f;
    queue_redraw();
}

void DioramaMinimap::zoom_full_map(float factor) {
    float next_r = std::clamp(full_world_radius * factor, kFullMinRadius, kFullMaxRadius);
    if (std::abs(next_r - full_world_radius) > 0.25f) {
        full_world_radius = next_r;
        full_settle_timer = 0.0f;
        queue_redraw();
    }
}

bool DioramaMinimap::is_point_explored(float wx, float wz) const {
    return sample_explored(int(std::floor(wx)), int(std::floor(wz))) > 0;
}

void DioramaMinimap::stamp_exploration(float px, float pz) {
    if (!world)
        return;

    const int min_p = floor_div(int(std::floor(px - kExploreMaxRadius)), 8);
    const int max_px = floor_div(int(std::ceil(px + kExploreMaxRadius)), 8);
    const int min_pz = floor_div(int(std::floor(pz - kExploreMaxRadius)), 8);
    const int max_pz = floor_div(int(std::ceil(pz + kExploreMaxRadius)), 8);

    bool changed = false;
    for (int pzz = min_pz; pzz <= max_pz; ++pzz) {
        for (int pxx = min_p; pxx <= max_px; ++pxx) {
            const int base_x = pxx * 8;
            const int base_z = pzz * 8;
            if (!world->has_loaded_tile_at(base_x, base_z))
                continue;

            PatchKey key{pxx, pzz};
            ExplorePatch &ep = explore_cache[key];
            for (int lz = 0; lz < 8; ++lz) {
                float dz = float(base_z + lz) + 0.5f - pz;
                for (int lx = 0; lx < 8; ++lx) {
                    float dx = float(base_x + lx) + 0.5f - px;
                    float d = std::sqrt(dx * dx + dz * dz);
                    if (d > kExploreMaxRadius)
                        continue;
                    uint8_t val = 255;
                    if (d > kExploreFullRadius) {
                        float t = 1.0f - (d - kExploreFullRadius) / (kExploreMaxRadius - kExploreFullRadius);
                        t = std::clamp(t, 0.0f, 1.0f);
                        val = u8_clamp(t * t * (3.0f - 2.0f * t) * 255.0f);
                    }
                    int idx = lz * 8 + lx;
                    if (val > ep.vis[idx]) {
                        ep.vis[idx] = val;
                        changed = true;
                    }
                }
            }
        }
    }
    if (changed)
        ++explore_revision;
}

uint8_t DioramaMinimap::sample_explored(int wx, int wz) const {
    PatchKey key{floor_div(wx, 8), floor_div(wz, 8)};
    auto it = explore_cache.find(key);
    if (it == explore_cache.end())
        return 0;
    int lx = wx - key.x * 8;
    int lz = wz - key.z * 8;
    return it->second.vis[lz * 8 + lx];
}

float DioramaMinimap::sample_explored_smooth(float fx, float fz) const {
    int x0 = int(std::floor(fx - 0.5f));
    int z0 = int(std::floor(fz - 0.5f));
    float tx = (fx - 0.5f) - float(x0);
    float tz = (fz - 0.5f) - float(z0);
    float v00 = float(sample_explored(x0, z0));
    float v10 = float(sample_explored(x0 + 1, z0));
    float v01 = float(sample_explored(x0, z0 + 1));
    float v11 = float(sample_explored(x0 + 1, z0 + 1));
    return ((v00 * (1.0f - tx) + v10 * tx) * (1.0f - tz) + (v01 * (1.0f - tx) + v11 * tx) * tz) / 255.0f;
}

DioramaMinimap::Patch &DioramaMinimap::build_patch(PatchKey key) {
    ALTARIA_PROFILE("minimap.build_patch");
    if (patch_cache.size() > 4096) {
        auto oldest = patch_cache.begin();
        for (auto it = patch_cache.begin(); it != patch_cache.end(); ++it) {
            if (it->second.last_used < oldest->second.last_used)
                oldest = it;
        }
        if (oldest != patch_cache.end())
            patch_cache.erase(oldest);
    }

    Patch &patch = patch_cache[key];
    patch.last_used = ++cache_clock;
    patch.trees.clear();

    const auto *gen = world ? world->generator() : nullptr;
    if (!gen)
        return patch;

    const int base_x = key.x * 8;
    const int base_z = key.z * 8;
    std::vector<float> xs(64), zs(64);
    for (int lz = 0; lz < 8; ++lz) {
        for (int lx = 0; lx < 8; ++lx) {
            int i = lz * 8 + lx;
            xs[i] = float(base_x + lx);
            zs[i] = float(base_z + lz);
        }
    }

    auto cols = gen->columns(xs, zs, nullptr, false);
    const auto &reg = gen->blocks();
    const auto &cfg = gen->settings();

    for (int i = 0; i < 64; ++i) {
        const auto &c = cols[i];
        CellSample &s = patch.cells[i];
        s.height = c.height;
        s.water_top = int16_t(std::clamp(c.water_top, -32000, 32000));
        extract_column_colors(c, reg, cfg, base_x + (i & 7), base_z + (i >> 3),
                              s.top_r, s.top_g, s.top_b, s.side_r, s.side_g, s.side_b);
    }

    if (cfg.preset != altaria::WorldPreset::Plain && cfg.vegetation) {
        // Real tree blocks (logs + leaves) raise the column they top, so canopies
        // read as voxel blocks like the rest of the map instead of pasted sprites.
        for (const auto &part : gen->trees(base_x, base_z, base_x + 8, base_z + 8)) {
            const int lx = part.first.x - base_x, lz = part.first.z - base_z;
            if (lx < 0 || lx >= 8 || lz < 0 || lz >= 8)
                continue;
            CellSample &cell = patch.cells[lz * 8 + lx];
            const float top = float(part.first.y + 1);
            if (top <= cell.height)
                continue;
            const auto &def = reg.get(part.second);
            altaria::RGB rgb = part.appearance.painted ? altaria::unpack_rgb(part.appearance.tint) : def.color;
            const float jitter = 0.90f + 0.20f * hash2d(part.first.x * 3 + part.first.y, part.first.z * 5 - part.first.y);
            const bool leaf = def.foliage;
            const float k = (leaf ? 228.0f : 200.0f) * jitter;
            cell.height = top;
            cell.water_top = -1000;
            cell.top_r = u8_clamp(std::clamp(rgb.r * k, 24.0f, 236.0f));
            cell.top_g = u8_clamp(std::clamp(rgb.g * k, 30.0f, 240.0f));
            cell.top_b = u8_clamp(std::clamp(rgb.b * k, 22.0f, 226.0f));
            cell.side_r = u8_clamp(cell.top_r * 0.58f);
            cell.side_g = u8_clamp(cell.top_g * 0.60f);
            cell.side_b = u8_clamp(cell.top_b * 0.62f);
        }
    }

    return patch;
}

const DioramaMinimap::CellSample &DioramaMinimap::sample_terrain(int wx, int wz) {
    PatchKey key{floor_div(wx, 8), floor_div(wz, 8)};
    auto it = patch_cache.find(key);
    Patch &patch = (it == patch_cache.end()) ? build_patch(key) : it->second;
    patch.last_used = ++cache_clock;
    int lx = wx - key.x * 8;
    int lz = wz - key.z * 8;
    return patch.cells[lz * 8 + lx];
}

void DioramaMinimap::collect_overrides(std::unordered_map<uint64_t, LiveOverride> &out, float cx, float cz,
                                       float radius) const {
    if (!world)
        return;
    const auto &reg = world->registry();
    const int px = int(std::floor(cx));
    const int pz = int(std::floor(cz));
    const int r_lim = int(std::ceil(radius)) + 4;

    if (auto edits = world->edits_snapshot()) {
        for (const auto &kv : *edits) {
            if (std::abs(kv.first.x - px) > r_lim || std::abs(kv.first.z - pz) > r_lim)
                continue;
            if (kv.second == 0)
                continue;
            uint64_t k = col_key(kv.first.x, kv.first.z);
            auto &ov = out[k];
            float top = float(kv.first.y + 1);
            if (!ov.has_solid || top > ov.y) {
                ov.has_solid = true;
                ov.y = top;
                const auto &bd = reg.get(kv.second);
                ov.r = u8_clamp(std::clamp(bd.color.r * 235.0f, 40.0f, 245.0f));
                ov.g = u8_clamp(std::clamp(bd.color.g * 235.0f, 40.0f, 245.0f));
                ov.b = u8_clamp(std::clamp(bd.color.b * 235.0f, 40.0f, 245.0f));
            }
        }
    }

    if (built_world) {
        const auto &store = built_world->construction();
        for (const auto &kv : store.blocks) {
            auto p = kv.second.position();
            if (std::abs(p[0] - cx) > radius + 3.0f || std::abs(p[2] - cz) > radius + 3.0f)
                continue;
            int wx = int(std::floor(p[0]));
            int wz = int(std::floor(p[2]));
            uint64_t k = col_key(wx, wz);
            auto &ov = out[k];
            float top = p[1] + 0.85f;
            if (!ov.has_solid || top > ov.y) {
                ov.has_solid = true;
                ov.y = top;
                const auto &bd = reg.get(kv.second.block);
                ov.r = u8_clamp(std::clamp(bd.color.r * 240.0f, 45.0f, 250.0f));
                ov.g = u8_clamp(std::clamp(bd.color.g * 240.0f, 45.0f, 250.0f));
                ov.b = u8_clamp(std::clamp(bd.color.b * 240.0f, 45.0f, 250.0f));
            }
        }
    }
}

void DioramaMinimap::blend_pixel(PackedByteArray &buf, int width, int height, int x, int y, uint8_t r, uint8_t g,
                                 uint8_t b, uint8_t a) {
    if (x < 0 || x >= width || y < 0 || y >= height || a == 0)
        return;
    int off = (y * width + x) * 4;
    // ptrw() is a call into the engine; painting a map makes hundreds of
    // thousands of these, so keep the pointer for the buffer being painted.
    // begin_paint() refreshes it once per image rebuild.
    if (&buf != paint_owner) {
        paint_owner = &buf;
        paint_data = buf.ptrw();
    }
    uint8_t *ptr = paint_data;
    if (a == 255) {
        ptr[off + 0] = r;
        ptr[off + 1] = g;
        ptr[off + 2] = b;
        ptr[off + 3] = 255;
    } else {
        float t = float(a) / 255.0f;
        ptr[off + 0] = u8_clamp(float(ptr[off + 0]) * (1.0f - t) + float(r) * t);
        ptr[off + 1] = u8_clamp(float(ptr[off + 1]) * (1.0f - t) + float(g) * t);
        ptr[off + 2] = u8_clamp(float(ptr[off + 2]) * (1.0f - t) + float(b) * t);
        ptr[off + 3] = 255;
    }
}

void DioramaMinimap::paint_column(PackedByteArray &buf, int width, int height, int sx, int col_span, int base_sy,
                                  float ref_y, float elev, float west_elev, float north_elev, float front_elev,
                                  int water_top, uint8_t top_r, uint8_t top_g, uint8_t top_b, uint8_t side_r,
                                  uint8_t side_g, uint8_t side_b, float lift_scale, int max_cliff, int top_px,
                                  int min_cliff) {
    int q_elev = int(std::floor(elev));
    int q_west = int(std::floor(west_elev));
    int q_north = int(std::floor(north_elev));
    int q_front = int(std::floor(front_elev));

    float disp_elev = float(q_elev) + (elev - float(q_elev)) * 0.25f;
    float dhx = elev - west_elev;
    float dhz = elev - north_elev;
    Vector3 nrm = Vector3(-dhx * 1.35f, 1.0f, -dhz * 1.35f).normalized();
    float shade = std::clamp(0.72f + 0.44f * nrm.dot(Vector3(-0.56f, 0.72f, -0.40f).normalized()), 0.54f, 1.24f);

    if (q_elev > q_west || q_elev > q_north)
        shade = std::min(1.26f, shade * 1.09f);
    else if (q_elev < q_west || q_elev < q_north)
        shade *= 0.86f;

    if ((std::abs(q_elev) % 3) == 0 && (q_elev != q_west || q_elev != q_north))
        shade *= 0.90f;

    int max_up = int(float(height) * 0.28f);
    int max_dn = int(float(height) * 0.24f);
    int lift = int(std::round(std::clamp((disp_elev - ref_y) * lift_scale, -float(max_dn), float(max_up))));
    int sy_top = base_sy - lift;

    float front_drop = std::max(0.0f, disp_elev - front_elev);
    float ridge_drop = std::max(std::abs(dhx), std::abs(dhz));
    int step_cliff = q_elev > q_front ? (q_elev - q_front) : 0;
    top_px = std::max(1, top_px);
    int cliff_px = std::clamp(int(std::round(std::max(front_drop, ridge_drop * 0.65f) * lift_scale)) + step_cliff + min_cliff,
                              min_cliff, max_cliff);

    uint8_t cr = u8_clamp(side_r * 0.78f);
    uint8_t cg = u8_clamp(side_g * 0.78f);
    uint8_t cb = u8_clamp(side_b * 0.82f);
    for (int py = 1; py <= cliff_px; ++py) {
        float grad = 1.0f - (float(py) / float(cliff_px + 2)) * 0.34f;
        uint8_t pr = u8_clamp(cr * grad);
        uint8_t pg = u8_clamp(cg * grad);
        uint8_t pb = u8_clamp(cb * grad);
        for (int dx = 0; dx < col_span; ++dx)
            blend_pixel(buf, width, height, sx + dx, sy_top + top_px - 1 + py, pr, pg, pb);
    }

    // The top face spans the full row pitch: on the zoomed-out world map rows
    // are ~2 px apart and a 1 px top left the dark cliff band visible between
    // every row as horizontal scanlines.
    uint8_t tr = u8_clamp(top_r * shade);
    uint8_t tg = u8_clamp(top_g * shade);
    uint8_t tb = u8_clamp(top_b * shade);
    for (int py = 0; py < top_px; ++py)
        for (int dx = 0; dx < col_span; ++dx)
            blend_pixel(buf, width, height, sx + dx, sy_top + py, tr, tg, tb);

    if (water_top >= q_elev) {
        float depth = float(water_top) - elev;
        int w_lift = int(std::round(std::clamp((float(water_top) - ref_y) * lift_scale, -float(max_dn), float(max_up))));
        int sy_w = base_sy - w_lift;
        bool shore = depth <= 1.6f;
        uint8_t wr = shore ? 114 : 44;
        uint8_t wg = shore ? 210 : 134;
        uint8_t wb = shore ? 236 : 212;
        uint8_t wa = shore ? 205 : 220;
        for (int dx = 0; dx < col_span; ++dx)
            blend_pixel(buf, width, height, sx + dx, sy_w, wr, wg, wb, wa);
    }
}

void DioramaMinimap::draw_diorama_tree(PackedByteArray &buf, int width, int height, int sx, int sy_base, int trunk_px,
                                       float crown_r, uint8_t tr, uint8_t tg, uint8_t tb, const Ref<Image> &sprite) {
    if (sprite.is_valid() && sprite->get_width() == 16 && sprite->get_height() == 16 && crown_r >= 2.2f) {
        // Stamp 16x16 minimap_tree.png at 1:1 native texel resolution so its pixel art & 1px outline are never distorted
        const int x0 = sx - 8;
        const int y0 = sy_base - 15;
        const bool is_green_biome = (tg >= tr && tg >= tb);
        for (int dy = 0; dy < 16; ++dy) {
            for (int dx = 0; dx < 16; ++dx) {
                Color c = sprite->get_pixel(dx, dy);
                if (c.a < 0.2f)
                    continue;
                uint8_t pr = u8_clamp(c.r * 255.0f);
                uint8_t pg = u8_clamp(c.g * 255.0f);
                uint8_t pb = u8_clamp(c.b * 255.0f);
                // Canopy pixels have c.g > c.r (bark pixels have c.r > c.g)
                if (c.g > c.r) {
                    if (is_green_biome) {
                        // Keep artist's hand-picked 5-tier emerald/teal-shadowed ramp with subtle biome modulation
                        float lum = std::clamp((c.r * 0.28f + c.g * 0.58f + c.b * 0.14f) / 0.44f, 0.30f, 1.38f);
                        pr = u8_clamp(pr * 0.72f + float(tr) * lum * 0.28f);
                        pg = u8_clamp(pg * 0.72f + float(tg) * lum * 0.28f);
                        pb = u8_clamp(pb * 0.72f + float(tb) * lum * 0.28f);
                    } else {
                        // Non-green canopy (purple jungle, autumn red/gold): shift all 5 canopy tiers harmoniously
                        bool is_outline = (c.g < 0.30f);
                        float lum = is_outline
                            ? (c.g < 0.20f ? 0.30f : 0.42f)
                            : std::clamp((c.r * 0.28f + c.g * 0.58f + c.b * 0.14f) / 0.44f, 0.56f, 1.34f);
                        pr = u8_clamp(float(tr) * lum);
                        pg = u8_clamp(float(tg) * lum);
                        pb = u8_clamp(float(tb) * lum);
                    }
                }
                blend_pixel(buf, width, height, x0 + dx, y0 + dy, pr, pg, pb, u8_clamp(c.a * 255.0f));
            }
        }
        return;
    }

    int r_i = int(std::ceil(crown_r));
    for (int dx = 0; dx <= std::max(1, r_i); ++dx)
        blend_pixel(buf, width, height, sx + dx, sy_base + 1, 14, 16, 22, 95);

    for (int ty = 0; ty <= trunk_px; ++ty)
        blend_pixel(buf, width, height, sx, sy_base - ty, 98, 66, 44, 245);

    int cy = sy_base - trunk_px - std::max(1, r_i / 2);
    for (int dy = -r_i; dy <= r_i; ++dy) {
        for (int dx = -r_i; dx <= r_i; ++dx) {
            float d = std::sqrt(float(dx * dx + dy * dy));
            if (d > crown_r + 0.15f)
                continue;
            float nx = float(dx) / std::max(1.0f, crown_r);
            float ny = float(dy) / std::max(1.0f, crown_r);
            bool rim = d >= crown_r - 0.85f;
            float light = rim ? 0.56f : std::clamp(0.92f + (-nx * 0.32f - ny * 0.36f), 0.68f, 1.26f);
            uint8_t pr = u8_clamp(float(tr) * light);
            uint8_t pg = u8_clamp(float(tg) * light);
            uint8_t pb = u8_clamp(float(tb) * light);
            blend_pixel(buf, width, height, sx + dx, cy + dy, pr, pg, pb, 255);
        }
    }
}

// Unexplored ground: flat plum fog with a dithered pixel edge (no animated noise,
// so the fog costs one pass and never forces a repaint on its own).
void DioramaMinimap::paint_cloud_overlay(PackedByteArray &buf, const std::vector<uint8_t> &reveal_mask, int width,
                                         int height, float center_x, float center_z, float world_per_px_x,
                                         float world_per_px_z, float rot_cos, float rot_sin, float drift,
                                         float circle_clip_r) {
    (void)rot_cos;
    (void)rot_sin;
    (void)drift;
    static const uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    uint8_t *wptr = buf.ptrw();
    const float half_w = float(width) * 0.5f;
    const float half_h = float(height) * 0.5f;
    // Anchor the dither/checker to the world so panning does not make it crawl.
    const int ox = int(std::floor(center_x / std::max(world_per_px_x, 1e-3f)));
    const int oy = int(std::floor(center_z / std::max(world_per_px_z, 1e-3f)));

    for (int y = 0; y < height; ++y) {
        float dy = float(y) + 0.5f - half_h;
        for (int x = 0; x < width; ++x) {
            int off = (y * width + x) * 4;
            float dx = float(x) + 0.5f - half_w;
            float d = 0.0f;
            if (circle_clip_r > 0.0f) {
                d = std::sqrt(dx * dx + dy * dy);
                if (d >= circle_clip_r + 0.8f) {
                    wptr[off + 3] = 0;
                    continue;
                }
            }

            const uint8_t rev = reveal_mask[y * width + x];
            if (rev < 252) {
                const int gx = x + ox, gy = y + oy;
                const float cover = rev == 0 ? 1.0f : std::clamp((1.0f - float(rev) / 255.0f) * 1.35f, 0.0f, 1.0f);
                const float thr = (float(kBayer[(gx & 3) + ((gy & 3) << 2)]) + 0.5f) / 16.0f;
                if (cover > thr) {
                    // Fog pixel: plum base, faint 8px checker; the dithered fringe is a shade lighter.
                    const bool chk = (((gx >> 3) + (gy >> 3)) & 1) != 0;
                    const bool fringe = cover < 0.85f;
                    wptr[off + 0] = fringe ? 74 : (chk ? 47 : 43);
                    wptr[off + 1] = fringe ? 64 : (chk ? 41 : 37);
                    wptr[off + 2] = fringe ? 96 : (chk ? 68 : 62);
                } else {
                    // Ground just inside the fog edge sits in a soft shadow.
                    const float sh = 1.0f - 0.30f * cover;
                    wptr[off + 0] = u8_clamp(float(wptr[off + 0]) * sh);
                    wptr[off + 1] = u8_clamp(float(wptr[off + 1]) * sh);
                    wptr[off + 2] = u8_clamp(float(wptr[off + 2]) * sh);
                }
                wptr[off + 3] = 255;
            }

            if (circle_clip_r > 0.0f) {
                float rim_t = std::clamp((circle_clip_r - d) / 11.0f, 0.0f, 1.0f);
                float vig = 0.72f + 0.28f * rim_t;
                wptr[off + 0] = u8_clamp(float(wptr[off + 0]) * vig);
                wptr[off + 1] = u8_clamp(float(wptr[off + 1]) * vig);
                wptr[off + 2] = u8_clamp(float(wptr[off + 2]) * vig);
                if (d > circle_clip_r - 0.8f) {
                    float edge_a = std::clamp((circle_clip_r + 0.8f - d) / 1.6f, 0.0f, 1.0f);
                    wptr[off + 3] = u8_clamp(edge_a * 255.0f);
                }
            }
        }
    }
}

void DioramaMinimap::rebuild_image(Vector3 pos, float yaw) {
    if (!world || !world->generator())
        return;

    std::unordered_map<uint64_t, LiveOverride> overrides;
    {
        ALTARIA_PROFILE("minimap.overrides");
        collect_overrides(overrides, pos.x, pos.z, kMinimapWorldRadius);
    }

    // Pre-fill canvas with soft sky-cloud tone (never black!) and clear reveal mask
    paint_owner = nullptr;
    uint8_t *wptr = pixels.ptrw();
    for (int i = 0; i < kImgSize * kImgSize; ++i) {
        wptr[i * 4 + 0] = 205;
        wptr[i * 4 + 1] = 216;
        wptr[i * 4 + 2] = 236;
        wptr[i * 4 + 3] = 255;
    }
    std::fill(minimap_reveal.begin(), minimap_reveal.end(), uint8_t(0));

    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float right_x = cy, right_z = -sy;
    const float fwd_x = -sy, fwd_z = -cy;

    const float px_per_m = 92.0f / kMinimapWorldRadius;
    constexpr float kStep = 0.58f;
    const float lift_scale = 0.85f * (float(kImgSize) / 128.0f);

    const float max_v = (kMinimapWorldRadius + 4.0f) / 0.74f;
    // Reference elevation is the ground height at the center of the minimap (not the flying player's sky Y!),
    // keeping terrain centered under the player arrow even when jumping or flying.
    const float ref_y = sample_terrain(int(std::floor(pos.x)), int(std::floor(pos.z))).height;

    // Local grid of the cells this image can touch: one patch lookup per 8x8
    // patch and one loaded-tile test per patch (a patch never spans tiles),
    // instead of hash lookups for every sample below.
    const int grid_reach = int(std::ceil(max_v + 8.0f));
    const int grid_x0 = int(std::floor(pos.x)) - grid_reach, grid_z0 = int(std::floor(pos.z)) - grid_reach;
    const int grid_size = grid_reach * 2 + 1;
    std::vector<const CellSample *> grid(size_t(grid_size) * size_t(grid_size), nullptr);
    for (int pz = floor_div(grid_z0, 8); pz <= floor_div(grid_z0 + grid_size - 1, 8); ++pz)
        for (int px = floor_div(grid_x0, 8); px <= floor_div(grid_x0 + grid_size - 1, 8); ++px) {
            if (!world->has_loaded_tile_at(px * 8, pz * 8))
                continue;
            PatchKey key{px, pz};
            auto found = patch_cache.find(key);
            Patch &patch = found == patch_cache.end() ? build_patch(key) : found->second;
            patch.last_used = ++cache_clock;
            for (int lz = 0; lz < 8; ++lz)
                for (int lx = 0; lx < 8; ++lx) {
                    const int gx = px * 8 + lx - grid_x0, gz = pz * 8 + lz - grid_z0;
                    if (gx >= 0 && gx < grid_size && gz >= 0 && gz < grid_size)
                        grid[size_t(gz) * size_t(grid_size) + size_t(gx)] = &patch.cells[lz * 8 + lx];
                }
        }
    const auto cell_at = [&](int wx, int wz) -> const CellSample * {
        const int gx = wx - grid_x0, gz = wz - grid_z0;
        if (gx < 0 || gx >= grid_size || gz < 0 || gz >= grid_size)
            return nullptr;
        return grid[size_t(gz) * size_t(grid_size) + size_t(gx)];
    };

    // Paint terrain back-to-front only for loaded & explored cells
    for (float v = max_v; v >= -max_v; v -= kStep) {
        int base_sy = kHalfImg - int(std::round(v * px_per_m * 0.74f));
        float v_scaled = v * 0.74f;
        for (float u = -(kMinimapWorldRadius + 4.0f); u <= (kMinimapWorldRadius + 4.0f); u += kStep) {
            if (u * u + v_scaled * v_scaled > (kMinimapWorldRadius + 4.0f) * (kMinimapWorldRadius + 4.0f))
                continue;

            int sx = kHalfImg + int(std::round(u * px_per_m));
            if (sx < 0 || sx >= kImgSize)
                continue;

            float fx = pos.x + u * right_x + v * fwd_x;
            float fz = pos.z + u * right_z + v * fwd_z;
            int wx = int(std::floor(fx));
            int wz = int(std::floor(fz));

            const CellSample *cell_ptr = cell_at(wx, wz);
            if (!cell_ptr)
                continue;
            const CellSample &cell = *cell_ptr;
            const auto neighbour = [&](int nx, int nz) -> const CellSample & {
                const CellSample *found = cell_at(nx, nz);
                return found ? *found : sample_terrain(nx, nz);
            };
            const CellSample &west = neighbour(wx - 1, wz);
            const CellSample &north = neighbour(wx, wz - 1);
            int front_wx = int(std::floor(pos.x + u * right_x + (v - kStep) * fwd_x));
            int front_wz = int(std::floor(pos.z + u * right_z + (v - kStep) * fwd_z));
            const CellSample &front = neighbour(front_wx, front_wz);

            float elev = cell.height;
            uint8_t top_r = cell.top_r, top_g = cell.top_g, top_b = cell.top_b;
            uint8_t side_r = cell.side_r, side_g = cell.side_g, side_b = cell.side_b;

            auto ov_it = overrides.empty() ? overrides.end() : overrides.find(col_key(wx, wz));
            if (ov_it != overrides.end() && ov_it->second.has_solid && ov_it->second.y >= elev - 1.0f) {
                elev = ov_it->second.y;
                top_r = ov_it->second.r;
                top_g = ov_it->second.g;
                top_b = ov_it->second.b;
                side_r = u8_clamp(top_r * 0.65f);
                side_g = u8_clamp(top_g * 0.65f);
                side_b = u8_clamp(top_b * 0.68f);
            }

            paint_column(pixels, kImgSize, kImgSize, sx, 2, base_sy, ref_y, elev, west.height, north.height,
                         front.height, cell.water_top, top_r, top_g, top_b, side_r, side_g, side_b, lift_scale, 12);

            // Mark reveal buffer along the terrain footprint
            int q_elev = int(std::floor(elev));
            float disp_elev = float(q_elev) + (elev - float(q_elev)) * 0.25f;
            int lift = int(std::round(std::clamp((disp_elev - ref_y) * lift_scale, -float(kImgSize) * 0.24f,
                                                  float(kImgSize) * 0.28f)));
            int sy_top = base_sy - lift;
            for (int py = -2; py <= 12; ++py) {
                int yy = sy_top + py;
                if (yy < 0 || yy >= kImgSize)
                    continue;
                for (int dx = 0; dx < 2 && sx + dx < kImgSize; ++dx)
                    minimap_reveal[yy * kImgSize + sx + dx] = 255;
            }
        }

    }

    float world_per_px = 1.0f / px_per_m;
    ALTARIA_PROFILE("minimap.clouds_upload");
    // Clip at 88.5 px (44.25 texels on 96x96 grid) so the map sits cleanly inside ring_tex (43.3..47.5 texels)
    paint_cloud_overlay(pixels, minimap_reveal, kImgSize, kImgSize, pos.x, pos.z, world_per_px, world_per_px / 0.74f,
                        cy, sy, cloud_time * 1.1f, float(kHalfImg) - 7.5f);

    if (image.is_null()) {
        image = Image::create_from_data(kImgSize, kImgSize, false, Image::FORMAT_RGBA8, pixels);
        texture = ImageTexture::create_from_image(image);
    } else {
        image->set_data(kImgSize, kImgSize, false, Image::FORMAT_RGBA8, pixels);
        texture->update(image);
    }
}

void DioramaMinimap::rebuild_full_map_image() {
    const auto *gen = world ? world->generator() : nullptr;
    if (!gen)
        return;

    Vector2 sz = get_size();
    float aspect = (sz.x > 16.0f && sz.y > 16.0f) ? (sz.x / sz.y) : (16.0f / 9.0f);
    // The image covers kFullPad x the visible span so dragging/zooming can move the
    // finished texture on the GPU and only repaint when it runs out of margin.
    int target_w = int(480.0f * kFullPad);
    int target_h = std::clamp(int(std::round(float(target_w) / std::max(0.5f, aspect))), 180, int(480.0f * kFullPad));
    if (target_w != full_img_w || target_h != full_img_h) {
        full_img_w = target_w;
        full_img_h = target_h;
        full_pixels.resize(full_img_w * full_img_h * 4);
        full_reveal.resize(full_img_w * full_img_h, 0);
        full_image.unref();
        full_texture.unref();
    }

    paint_owner = nullptr;
    uint8_t *wptr = full_pixels.ptrw();
    for (int i = 0; i < full_img_w * full_img_h; ++i) {
        wptr[i * 4 + 0] = 205;
        wptr[i * 4 + 1] = 216;
        wptr[i * 4 + 2] = 236;
        wptr[i * 4 + 3] = 255;
    }
    std::fill(full_reveal.begin(), full_reveal.end(), uint8_t(0));

    const float half_h_m = full_world_radius * kFullPad;
    const float half_w_m = half_h_m * aspect;
    full_built_cx = full_center_x;
    full_built_cz = full_center_z;
    full_built_radius = full_world_radius;
    const float px_per_m = (float(full_img_h) * 0.5f) / half_h_m;

    const int kGridX = int(224.0f * kFullPad);
    const int kGridZ = std::clamp(int(std::round(float(kGridX) * float(full_img_h) / float(full_img_w))), 96, kGridX);
    const float step_x = (half_w_m * 2.0f) / float(kGridX - 2);
    const float step_z = (half_h_m * 2.0f) / float(kGridZ - 2);
    const float origin_x = full_center_x - half_w_m - step_x;
    const float origin_z = full_center_z - half_h_m - step_z;

    std::unordered_map<uint64_t, LiveOverride> overrides;
    collect_overrides(overrides, full_center_x, full_center_z, std::max(half_w_m, half_h_m) + 4.0f);

    // Samples come from the shared patch cache (terrain + real tree blocks).
    // Only explored cells are sampled, so reopening or dragging the map reuses
    // cached patches instead of regenerating ~30k noise columns per repaint.
    // Copied by value: sampling can evict cache entries, which would leave pointers dangling.
    std::vector<CellSample> grid(size_t(kGridX) * size_t(kGridZ));
    std::vector<uint8_t> grid_exp(size_t(kGridX) * size_t(kGridZ), 0);
    for (int gz = 0; gz < kGridZ; ++gz) {
        const int iwz = int(std::floor(origin_z + float(gz) * step_z));
        for (int gx = 0; gx < kGridX; ++gx) {
            const int iwx = int(std::floor(origin_x + float(gx) * step_x));
            const uint8_t e = sample_explored(iwx, iwz);
            if (e == 0)
                continue;
            const size_t idx = size_t(gz) * size_t(kGridX) + size_t(gx);
            grid_exp[idx] = e;
            grid[idx] = sample_terrain(iwx, iwz);
        }
    }
    const float player_ground = player ? sample_terrain(int(std::floor(player->get_position().x)),
                                                        int(std::floor(player->get_position().z))).height
                                       : 0.0f;
    const float ref_y = player_ground;
    const float lift_scale = std::clamp(px_per_m * 0.42f, 0.35f, 1.35f);
    const int col_span = std::max(2, int(std::ceil(step_x * px_per_m)));
    const int row_px = std::max(1, int(std::ceil(step_z * px_per_m)));
    const int half_img_w = full_img_w / 2;
    const int half_img_h = full_img_h / 2;
    const auto height_at = [&](int gx, int gz, float fallback) {
        if (gx < 0 || gx >= kGridX || gz < 0 || gz >= kGridZ)
            return fallback;
        const size_t i = size_t(gz) * size_t(kGridX) + size_t(gx);
        return grid_exp[i] ? grid[i].height : fallback;
    };

    for (int gz = 1; gz < kGridZ - 1; ++gz) {
        float wz = origin_z + float(gz) * step_z;
        int base_sy = half_img_h + int(std::round((wz - full_center_z) * px_per_m));
        for (int gx = 1; gx < kGridX; ++gx) {
            const size_t idx = size_t(gz) * size_t(kGridX) + size_t(gx);
            if (!grid_exp[idx])
                continue;
            const CellSample *cp = &grid[idx];
            float wx_f = origin_x + float(gx) * step_x;
            int sx = half_img_w + int(std::round((wx_f - full_center_x) * px_per_m));
            if (sx < -col_span || sx >= full_img_w)
                continue;
            const int iwx = int(std::floor(wx_f));
            const int iwz = int(std::floor(wz));
            const uint8_t exp_v = grid_exp[idx];
            const CellSample &c = *cp;
            uint8_t top_r = c.top_r, top_g = c.top_g, top_b = c.top_b;
            uint8_t side_r = c.side_r, side_g = c.side_g, side_b = c.side_b;
            float elev = c.height;
            auto ov_it = overrides.empty() ? overrides.end() : overrides.find(col_key(iwx, iwz));
            if (ov_it != overrides.end() && ov_it->second.has_solid && ov_it->second.y >= elev - 1.0f) {
                elev = ov_it->second.y;
                top_r = ov_it->second.r;
                top_g = ov_it->second.g;
                top_b = ov_it->second.b;
                side_r = u8_clamp(top_r * 0.65f);
                side_g = u8_clamp(top_g * 0.65f);
                side_b = u8_clamp(top_b * 0.68f);
            }
            const float west_h = height_at(gx - 1, gz, c.height);
            const float north_h = height_at(gx, gz - 1, c.height);
            const float south_h = height_at(gx, gz + 1, c.height);

            paint_column(full_pixels, full_img_w, full_img_h, sx, col_span, base_sy, ref_y, elev, west_h, north_h,
                         south_h, c.water_top, top_r, top_g, top_b, side_r, side_g, side_b, lift_scale, 10, row_px, 0);

            int q_elev = int(std::floor(elev));
            float disp_elev = float(q_elev) + (elev - float(q_elev)) * 0.25f;
            int max_up = int(float(full_img_h) * 0.28f);
            int max_dn = int(float(full_img_h) * 0.24f);
            int lift = int(std::round(std::clamp((disp_elev - ref_y) * lift_scale, -float(max_dn), float(max_up))));
            int sy_top = base_sy - lift;
            int vert_pad = std::max(10, row_px + 4);
            for (int py = -2; py <= vert_pad; ++py) {
                int yy = sy_top + py;
                if (yy < 0 || yy >= full_img_h)
                    continue;
                for (int dx = 0; dx < col_span && sx + dx < full_img_w; ++dx) {
                    if (sx + dx >= 0)
                        full_reveal[yy * full_img_w + sx + dx] = std::max(full_reveal[yy * full_img_w + sx + dx], exp_v);
                }
            }
        }
    }

    float world_per_px = 1.0f / px_per_m;
    paint_cloud_overlay(full_pixels, full_reveal, full_img_w, full_img_h, full_center_x, full_center_z, world_per_px,
                        world_per_px, 1.0f, 0.0f, 0.0f, -1.0f);

    if (full_image.is_null()) {
        full_image = Image::create_from_data(full_img_w, full_img_h, false, Image::FORMAT_RGBA8, full_pixels);
        full_texture = ImageTexture::create_from_image(full_image);
    } else {
        full_image->set_data(full_img_w, full_img_h, false, Image::FORMAT_RGBA8, full_pixels);
        full_texture->update(full_image);
    }
}

void DioramaMinimap::draw_player_cone_and_chevron(Vector2 pt, float yaw_screen_rad, float sz, float a,
                                                  bool with_cone) {
    float c_rot = std::cos(yaw_screen_rad);
    float s_rot = std::sin(yaw_screen_rad);
    auto rot = [&](Vector2 v) -> Vector2 {
        return pt + Vector2(v.x * c_rot - v.y * s_rot, v.x * s_rot + v.y * c_rot);
    };

    if (with_cone) {
        float cone_len = sz * 4.2f;
        float cone_w = sz * 2.7f;
        PackedVector2Array cone{pt, rot(Vector2(-cone_w, -cone_len)), rot(Vector2(cone_w, -cone_len))};
        PackedColorArray cols{
            Color(1.0f, 0.96f, 0.78f, 0.24f * a),
            Color(1.0f, 0.96f, 0.78f, 0.0f),
            Color(1.0f, 0.96f, 0.78f, 0.0f),
        };
        draw_polygon(cone, cols);
    }

    if (arrow_tex.is_valid()) {
        float px = std::max(2.0f, std::round(sz / 4.2f));
        float asz = 7.0f * px;
        if (std::abs(yaw_screen_rad) < 0.001f) {
            Vector2 p0 = (pt - Vector2(asz * 0.5f, asz * 0.5f)).round();
            draw_texture_rect(arrow_tex, Rect2(p0, Vector2(asz, asz)), false, Color(1.0f, 1.0f, 1.0f, a));
        } else {
            draw_set_transform(pt.round(), yaw_screen_rad, Vector2(1.0f, 1.0f));
            draw_texture_rect(arrow_tex, Rect2(-asz * 0.5f, -asz * 0.5f, asz, asz), false, Color(1.0f, 1.0f, 1.0f, a));
            draw_set_transform(Vector2(), 0.0f, Vector2(1.0f, 1.0f));
        }
        return;
    }

    PackedVector2Array arrow{
        rot(Vector2(0.0f, -sz * 1.2f)),
        rot(Vector2(sz * 0.82f, sz * 0.78f)),
        rot(Vector2(0.0f, sz * 0.28f)),
        rot(Vector2(-sz * 0.82f, sz * 0.78f)),
    };
    draw_colored_polygon(arrow, Color(0.99f, 0.96f, 0.84f, 0.96f * a));
    PackedVector2Array arrow_loop = arrow;
    arrow_loop.push_back(arrow[0]);
    draw_polyline(arrow_loop, Color(0.08f, 0.07f, 0.11f, 0.92f * a), 1.4f, true);
}

void DioramaMinimap::_gui_input(const Ref<InputEvent> &event) {
    if (!full_map_open)
        return;

    Ref<InputEventMouseButton> mb = event;
    if (mb.is_valid()) {
        MouseButton idx = mb->get_button_index();
        if (mb->is_pressed()) {
            if (idx == MouseButton::MOUSE_BUTTON_WHEEL_UP) {
                zoom_full_map(0.85f);
                accept_event();
                return;
            }
            if (idx == MouseButton::MOUSE_BUTTON_WHEEL_DOWN) {
                zoom_full_map(1.0f / 0.85f);
                accept_event();
                return;
            }
            if (idx == MouseButton::MOUSE_BUTTON_LEFT) {
                dragging_map = true;
                accept_event();
                return;
            }
        } else if (idx == MouseButton::MOUSE_BUTTON_LEFT) {
            dragging_map = false;
            full_settle_timer = 0.0f;
            accept_event();
            return;
        }
    }

    Ref<InputEventMouseMotion> mm = event;
    if (mm.is_valid() && dragging_map) {
        Vector2 rel = mm->get_relative();
        float h = std::max(1.0f, get_size().y);
        float meters_per_px = (full_world_radius * 2.0f) / h;
        full_center_x -= rel.x * meters_per_px;
        full_center_z -= rel.y * meters_per_px;
        queue_redraw();
        accept_event();
    }
}

void DioramaMinimap::_process(double raw_delta) {
    ALTARIA_PROFILE("minimap.process");
    if (!is_visible_in_tree() || !world || !player)
        return;
    float dt = std::min(float(raw_delta), 0.05f);
    refresh_timer += dt;
    cloud_time += dt;

    Vector3 pos = player->get_position();
    float yaw = player->get_rotation().y;
    uint64_t rev = world->world_revision();
    if (built_world)
        rev ^= (built_world->construction().revision * 1315423911ULL);

    size_t tile_count = world->loaded_tile_count();
    bool tiles_changed = tile_count != last_tile_count;
    if (tiles_changed || pos.distance_to(last_explore_pos) >= 1.5f) {
        last_tile_count = tile_count;
        last_explore_pos = pos;
        stamp_exploration(pos.x, pos.z);
    }

    if (full_map_open) {
        if (open_anim < 1.0f) {
            open_anim = std::min(1.0f, open_anim + dt / 0.22f);
            queue_redraw();
        }
        full_refresh_timer += dt;
        // Streaming tiles never change the (generator-backed) map; newly explored
        // ground is folded in at most twice a second instead of every frame.
        full_settle_timer += dt;
        if (rev != full_last_revision || (explore_revision != full_last_explore_rev && full_refresh_timer >= 0.5f))
            full_map_dirty = true;
        // The view moved away from what the texture was painted for: repaint once
        // the pan/zoom settles, or right away if the margin is about to run out.
        const float view_dx = std::abs(full_center_x - full_built_cx);
        const float view_dz = std::abs(full_center_z - full_built_cz);
        const float zoom_ratio = full_world_radius / std::max(1.0f, full_built_radius);
        const bool view_changed = view_dx > 0.05f || view_dz > 0.05f || std::abs(zoom_ratio - 1.0f) > 0.002f;
        const float spare = full_built_radius * kFullPad - full_world_radius;
        const float aspect_now = get_size().y > 16.0f ? get_size().x / get_size().y : 16.0f / 9.0f;
        const bool out_of_margin = view_dz > spare * 0.8f || view_dx > (full_built_radius * kFullPad - full_world_radius) * aspect_now * 0.8f ||
                                   zoom_ratio > kFullPad * 0.92f || zoom_ratio < 0.55f;
        if (view_changed && (out_of_margin || (!dragging_map && full_settle_timer >= 0.12f)))
            full_map_dirty = true;
        if (full_texture.is_null() || (full_map_dirty && full_refresh_timer >= 0.03f)) {
            full_refresh_timer = 0.0f;
            full_map_dirty = false;
            full_last_revision = rev;
            full_last_explore_rev = explore_revision;
            rebuild_full_map_image();
            queue_redraw();
        } else {
            last_pos = pos;
            last_yaw = yaw;
            // Cheap redraw (texture + chrome) keeps the locator pulse alive.
            queue_redraw();
        }
        return;
    }

    bool moved = pos.distance_to(last_pos) >= 0.45f;
    bool rotated = std::abs(yaw - last_yaw) >= 0.10f;
    bool edited = rev != last_revision;
    bool explored_new = explore_revision != last_explore_rev;

    // A full repaint costs several milliseconds on the main thread; while
    // moving, about eight per second keep the map smooth enough.
    // Newly explored ground also waits for the next repaint: while flying it
    // changes every metre and used to force a repaint almost every frame.
    if (texture.is_null() || edited || (refresh_timer >= 0.12f && (moved || rotated || explored_new))) {
        refresh_timer = 0.0f;
        last_pos = pos;
        last_yaw = yaw;
        last_revision = rev;
        last_explore_rev = explore_revision;
        ALTARIA_PROFILE("minimap.rebuild");
        rebuild_image(pos, yaw);
        image_yaw = yaw;
        queue_redraw();
    } else if (std::abs(yaw - drawn_yaw) > 0.0005f) {
        // Between repaints the painted image is rotated on the GPU every frame,
        // so turning the camera stays as smooth as the frame rate.
        queue_redraw();
    }
}

void DioramaMinimap::_draw() {
    ALTARIA_PROFILE("minimap.draw");
    Vector2 sz = get_size();

    if (full_map_open) {
        const float e = 1.0f - (1.0f - open_anim) * (1.0f - open_anim) * (1.0f - open_anim);
        // Opens with a short zoom-settle so the map feels like it unfolds.
        const float zoom = 1.04f - 0.04f * e;
        const float px_per_m = (sz.y * 0.5f) / full_world_radius;
        draw_rect(Rect2(Vector2(), sz), Color(0.169f, 0.145f, 0.243f, 0.35f + 0.65f * e));
        if (full_texture.is_valid()) {
            // Painted for (built centre, built radius) with kFullPad margin; place it for the live view.
            const float span = full_built_radius * kFullPad / full_world_radius * zoom;
            Vector2 tex_sz = sz * span;
            Vector2 centre = sz * 0.5f + Vector2(full_built_cx - full_center_x, full_built_cz - full_center_z) * px_per_m * zoom;
            draw_texture_rect(full_texture, Rect2(centre - tex_sz * 0.5f, tex_sz), false, Color(1, 1, 1, 0.35f + 0.65f * e));
        }
        if (player) {
            Vector3 p = player->get_position();
            float psx = sz.x * 0.5f + (p.x - full_center_x) * px_per_m;
            float psy = sz.y * 0.5f + (p.z - full_center_z) * px_per_m;
            const float m = 46.0f;
            Rect2 safe_r = Rect2(Vector2(m, m), sz - Vector2(m, m) * 2.0f);
            bool inside = safe_r.has_point(Vector2(psx, psy));
            Vector2 clamped(std::clamp(psx, m, std::max(m, sz.x - m)), std::clamp(psy, m, std::max(m, sz.y - m)));
            if (inside) {
                // Soft locator pulse so the player is easy to find on a big map.
                const float pulse = std::fmod(cloud_time * 0.9f, 1.0f);
                draw_arc(clamped, 10.0f + 22.0f * pulse, 0.0f, kPi * 2.0f, 40,
                         Color(0.973f, 0.961f, 1.0f, 0.55f * (1.0f - pulse) * e), 2.0f, true);
            }
            draw_player_cone_and_chevron(clamped, -player->get_rotation().y, inside ? 11.0f : 8.5f, e, inside);
        }
        draw_full_map_chrome(sz, px_per_m);
        return;
    }

    if (fade_alpha <= 0.02f || minimap_radius < 8.0f)
        return;

    const float r = std::round(minimap_radius);
    const Vector2 c = minimap_center.round();
    const float a = fade_alpha;

    draw_circle(c + Vector2(0.0f, 2.0f), r - 1.5f, Color(0.02f, 0.02f, 0.04f, 0.45f * a));

    if (texture.is_valid()) {
        const float now_yaw = player ? player->get_rotation().y : image_yaw;
        drawn_yaw = now_yaw;
        float delta = std::remainder(now_yaw - image_yaw, kPi * 2.0f);
        if (std::abs(delta) > 0.0005f) {
            draw_set_transform(c, delta, Vector2(1.0f, 1.0f));
            draw_texture_rect(texture, Rect2(-r, -r, r * 2.0f, r * 2.0f), false, Color(1.0f, 1.0f, 1.0f, a));
            draw_set_transform(Vector2(), 0.0f, Vector2(1.0f, 1.0f));
        } else {
            draw_texture_rect(texture, Rect2(c.x - r, c.y - r, r * 2.0f, r * 2.0f), false, Color(1.0f, 1.0f, 1.0f, a));
        }
    }

    if (ring_tex.is_valid()) {
        Rect2 dst(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        draw_texture_rect(ring_tex, dst, false, Color(1.0f, 1.0f, 1.0f, a));
    } else {
        draw_arc(c, r, 0.0f, kPi * 2.0f, 64, Color(0.14f, 0.10f, 0.15f, 0.94f * a), 4.0f, true);
        draw_arc(c, r - 1.2f, 0.0f, kPi * 2.0f, 64, Color(0.58f, 0.52f, 0.64f, 0.74f * a), 1.35f, true);
    }

    float csz = std::max(8.0f, r * 0.095f);
    draw_player_cone_and_chevron(c, 0.0f, csz, a, false);

}

void DioramaMinimap::draw_frame_box(Rect2 r, float scale, float alpha, bool notch_bottom) {
    (void)notch_bottom;
    const float c = std::round(4.0f * scale);
    auto chamfer = [](Rect2 b, float k) {
        const Vector2 p = b.position, s = b.size;
        return PackedVector2Array{
            Vector2(p.x + k, p.y), Vector2(p.x + s.x - k, p.y), Vector2(p.x + s.x, p.y + k),
            Vector2(p.x + s.x, p.y + s.y - k), Vector2(p.x + s.x - k, p.y + s.y), Vector2(p.x + k, p.y + s.y),
            Vector2(p.x, p.y + s.y - k), Vector2(p.x, p.y + k),
        };
    };
    draw_colored_polygon(chamfer(Rect2(r.position + Vector2(0, 3.0f * scale), r.size), c), Color(0.02f, 0.02f, 0.04f, 0.35f * alpha));
    draw_colored_polygon(chamfer(r, c), Color(0.063f, 0.071f, 0.110f, 0.94f * alpha));
    const float b = std::max(2.0f, std::round(2.0f * scale));
    Rect2 inner(r.position + Vector2(b, b), r.size - Vector2(b, b) * 2.0f);
    PackedVector2Array body = chamfer(inner, std::max(1.0f, c - 1.0f));
    PackedColorArray cols;
    for (int i = 0; i < body.size(); ++i)
        cols.push_back(body[i].y < inner.get_center().y ? Color(0.23f, 0.16f, 0.26f, 0.95f * alpha)
                                                        : Color(0.13f, 0.09f, 0.15f, 0.95f * alpha));
    draw_polygon(body, cols);
    draw_line(Vector2(inner.position.x + c, inner.position.y + 0.5f),
              Vector2(inner.position.x + inner.size.x - c, inner.position.y + 0.5f), Color(0.87f, 0.81f, 0.93f, 0.24f * alpha), 1.0f);
}

void DioramaMinimap::draw_full_map_chrome(Vector2 sz, float px_per_m) {
    const float scale = std::clamp(sz.y / 900.0f, 0.72f, 1.35f);
    const float e = 1.0f - (1.0f - open_anim) * (1.0f - open_anim);
    const Color ink(0.063f, 0.071f, 0.110f, 1.0f);
    const Color plum(0.173f, 0.118f, 0.192f, 1.0f);
    const Color lilac(0.87f, 0.81f, 0.93f, 1.0f);
    const Color white(0.973f, 0.961f, 1.0f, 1.0f);

    // Edge vignette: darkens the cloud sea toward the frame so the eye rests on the map.
    const float vw = 90.0f * scale;
    const Color v_out(0.06f, 0.05f, 0.10f, 0.42f * e), v_in(0.06f, 0.05f, 0.10f, 0.0f);
    draw_polygon(PackedVector2Array{Vector2(0, 0), Vector2(sz.x, 0), Vector2(sz.x, vw), Vector2(0, vw)},
                 PackedColorArray{v_out, v_out, v_in, v_in});
    draw_polygon(PackedVector2Array{Vector2(0, sz.y - vw), Vector2(sz.x, sz.y - vw), Vector2(sz.x, sz.y), Vector2(0, sz.y)},
                 PackedColorArray{v_in, v_in, v_out, v_out});
    draw_polygon(PackedVector2Array{Vector2(0, 0), Vector2(vw, 0), Vector2(vw, sz.y), Vector2(0, sz.y)},
                 PackedColorArray{v_out, v_in, v_in, v_out});
    draw_polygon(PackedVector2Array{Vector2(sz.x - vw, 0), Vector2(sz.x, 0), Vector2(sz.x, sz.y), Vector2(sz.x - vw, sz.y)},
                 PackedColorArray{v_in, v_out, v_out, v_in});

    // Pixel frame: #10121c / #2c1e31 / #10121c bands like the minimap ring, plus a lilac hairline.
    const float inset = std::round(12.0f * scale);
    const float band = std::max(2.0f, std::round(2.0f * scale));
    Rect2 fr(Vector2(inset, inset), sz - Vector2(inset, inset) * 2.0f);
    auto frame = [&](Rect2 r, float w, Color col) {
        draw_rect(Rect2(r.position, Vector2(r.size.x, w)), col);
        draw_rect(Rect2(Vector2(r.position.x, r.position.y + r.size.y - w), Vector2(r.size.x, w)), col);
        draw_rect(Rect2(r.position, Vector2(w, r.size.y)), col);
        draw_rect(Rect2(Vector2(r.position.x + r.size.x - w, r.position.y), Vector2(w, r.size.y)), col);
    };
    Color c_ink = ink, c_plum = plum, c_lilac = lilac;
    c_ink.a = e;
    c_plum.a = e;
    c_lilac.a = 0.35f * e;
    frame(fr.grow(band * 2.0f), band, c_ink);
    frame(fr.grow(band), band, c_plum);
    frame(fr, band, c_ink);
    frame(fr.grow(-band), 1.0f, c_lilac);
    // Corner studs.
    const float stud = std::round(7.0f * scale);
    for (int q = 0; q < 4; ++q) {
        Vector2 cpos((q & 1) ? fr.position.x + fr.size.x : fr.position.x, (q & 2) ? fr.position.y + fr.size.y : fr.position.y);
        Rect2 st(cpos - Vector2(stud, stud) * 0.5f, Vector2(stud, stud));
        draw_rect(st.grow(band), c_ink);
        draw_rect(st, c_plum);
        draw_rect(Rect2(st.position + Vector2(band * 0.5f, band * 0.5f), Vector2(band, band)), Color(lilac.r, lilac.g, lilac.b, 0.6f * e));
    }

}

} // namespace godot
