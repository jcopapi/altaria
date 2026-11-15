#include "stamina_ring.h"
#include "../../game/debug/frame_profile.h"
#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/resource_loader.hpp>

namespace godot {
namespace {
constexpr float kPi = 3.14159265359f;
constexpr float kTwoPi = 6.28318530718f;
} // namespace

void CrosshairDot::_ready() {
    set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
}

void CrosshairDot::_process(double raw_delta) {
    if (!is_visible_in_tree())
        return;
    float dt = std::min(float(raw_delta), 0.05f);
    if (hit_pulse > 0.0f) {
        hit_pulse = std::max(0.0f, hit_pulse - dt * 5.5f);
        queue_redraw();
    }
    float next = darkness + (target_darkness - darkness) * (1.0f - std::exp(-16.0f * dt));
    if (std::abs(next - target_darkness) < 0.005f)
        next = target_darkness;
    if (std::abs(next - darkness) > 0.001f) {
        darkness = next;
        queue_redraw();
    }
}

void CrosshairDot::_draw() {
    const float scale = std::clamp(get_size().y / 900.0f, 0.72f, 1.35f);
    const Vector2 c = (get_size() * 0.5f).round();
    const float dot_sz = std::max(4.0f, std::round(4.0f * scale));
    const float half = std::floor(dot_sz * 0.5f);
    const float b1 = std::max(1.0f, std::round(1.5f * scale));
    const float b2 = b1 + std::max(1.0f, std::round(1.5f * scale));
    // Match stamina_circle.png palette: #10121c outer border + #2c1e31 inner border + #f8f5ff center
    const Color fg = Color(0.973f, 0.961f, 1.0f, 0.98f).lerp(Color(0.063f, 0.071f, 0.110f, 0.98f), darkness);
    const Color inner_rim = Color(0.173f, 0.118f, 0.192f, 0.96f).lerp(Color(0.173f, 0.118f, 0.192f, 0.98f), darkness);
    const Color outer_rim = Color(0.063f, 0.071f, 0.110f, 0.95f).lerp(Color(0.973f, 0.961f, 1.0f, 0.88f), darkness);
    // Stepped pixel-art outer outline (chamfered corners like stamina_circle.png)
    draw_rect(Rect2(c.x - half - b2, c.y - half - b1, dot_sz + b2 * 2.0f, dot_sz + b1 * 2.0f), outer_rim, true);
    draw_rect(Rect2(c.x - half - b1, c.y - half - b2, dot_sz + b1 * 2.0f, dot_sz + b2 * 2.0f), outer_rim, true);
    // Inner dark plum-slate outline ring
    draw_rect(Rect2(c.x - half - b1, c.y - half - b1, dot_sz + b1 * 2.0f, dot_sz + b1 * 2.0f), inner_rim, true);
    // Center dot
    draw_rect(Rect2(c.x - half, c.y - half, dot_sz, dot_sz), fg, true);

    if (hit_pulse > 0.01f) {
        // Ticks spring outward (ease-out) and fade; stronger hits travel further.
        const float t = 1.0f - hit_pulse;
        const float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        const float px = std::max(2.0f, std::round(2.0f * scale));
        // Ticks sit right against the dot's outline and only nudge 1-2 px outward.
        const float dist = half + b2 + px * 0.5f + std::round((1.0f + hit_strength) * ease) * px * 0.5f;
        const float a = std::min(1.0f, hit_pulse * 1.6f);
        const Color tick = fg;
        const Color rim = outer_rim;
        const int len = 2;
        for (int q = 0; q < 4; ++q) {
            const float sx = (q & 1) ? 1.0f : -1.0f;
            const float sy = (q & 2) ? 1.0f : -1.0f;
            for (int k = 0; k < len; ++k) {
                const Vector2 p(c.x + sx * (dist + k * px) - px * 0.5f, c.y + sy * (dist + k * px) - px * 0.5f);
                draw_rect(Rect2(p - Vector2(px * 0.5f, px * 0.5f), Vector2(px * 2.0f, px * 2.0f)),
                          Color(rim.r, rim.g, rim.b, rim.a * a), true);
            }
            for (int k = 0; k < len; ++k) {
                const Vector2 p(c.x + sx * (dist + k * px) - px * 0.5f, c.y + sy * (dist + k * px) - px * 0.5f);
                draw_rect(Rect2(p, Vector2(px, px)), Color(tick.r, tick.g, tick.b, tick.a * a), true);
            }
        }
    }
}

void StaminaRing::_ready() {
    set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    set_texture_filter(CanvasItem::TEXTURE_FILTER_NEAREST);
    circle_tex = ResourceLoader::get_singleton()->load("res://game/assets/hud/stamina_circle.png");
}

void StaminaRing::_process(double raw_delta) {
    ALTARIA_PROFILE("stamina.process");
    if (!is_visible_in_tree())
        return;
    float dt = std::min(float(raw_delta), 0.05f);
    if (!player) {
        visibility_alpha = 0.0f;
        return;
    }

    float target_ratio = player->get_stamina_ratio();
    bool exhausted = player->is_stamina_exhausted();
    bool climbing = player->is_climbing() || player->is_mantling();
    bool flying = player->is_flying();

    display_ratio = display_ratio + (target_ratio - display_ratio) * (1.0f - std::exp(-22.0f * dt));
    if (std::abs(display_ratio - target_ratio) < 0.002f)
        display_ratio = target_ratio;

    if (prev_ratio < 0.995f && target_ratio >= 0.995f && !exhausted) {
        complete_flash = 1.0f;
        full_hold_timer = 0.85f;
    }
    if (was_exhausted && !exhausted) {
        complete_flash = 1.0f;
        full_hold_timer = 0.95f;
    }
    prev_ratio = target_ratio;
    was_exhausted = exhausted;

    if (complete_flash > 0.0f)
        complete_flash = std::max(0.0f, complete_flash - dt * 2.8f);

    bool active_use = !flying && (target_ratio < 0.995f || exhausted || climbing);
    if (active_use) {
        full_hold_timer = 0.85f;
        visibility_alpha = visibility_alpha + (1.0f - visibility_alpha) * (1.0f - std::exp(-18.0f * dt));
    } else if (full_hold_timer > 0.0f && !flying) {
        full_hold_timer = std::max(0.0f, full_hold_timer - dt);
        visibility_alpha = visibility_alpha + (1.0f - visibility_alpha) * (1.0f - std::exp(-14.0f * dt));
    } else {
        visibility_alpha = visibility_alpha + (0.0f - visibility_alpha) * (1.0f - std::exp(-8.5f * dt));
    }

    pulse_time += dt * (exhausted ? 9.5f : 6.0f);
    if (pulse_time > kTwoPi * 100.0f)
        pulse_time = std::fmod(pulse_time, kTwoPi);

    if (visibility_alpha > 0.01f)
        queue_redraw();
}

void StaminaRing::_draw() {
    ALTARIA_PROFILE("stamina.draw");
    if (!player || visibility_alpha <= 0.015f)
        return;

    const float scale = std::clamp(get_size().y / 900.0f, 0.72f, 1.35f);
    const float px = std::max(3.0f, std::round(3.5f * scale));
    const float sprite_sz = 12.0f * px;
    const Vector2 center = (get_size() * 0.5f).round();
    const Vector2 origin = (center - Vector2(sprite_sz * 0.5f, sprite_sz * 0.5f)).round();
    const float alpha = std::clamp(visibility_alpha, 0.0f, 1.0f);
    const bool exhausted = player->is_stamina_exhausted();
    const float ratio = std::clamp(display_ratio, 0.0f, 1.0f);

    // Exact 20 transparent channel texels of stamina_circle.png (12x12) in clockwise order from top
    static constexpr int kChannelTexels[20][2] = {
        {6, 2}, {7, 2}, {8, 3}, {9, 4}, {9, 5}, {9, 6}, {9, 7}, {8, 8}, {7, 9}, {6, 9},
        {5, 9}, {4, 9}, {3, 8}, {2, 7}, {2, 6}, {2, 5}, {2, 4}, {3, 3}, {4, 2}, {5, 2},
    };

    const Color track_bg(0.063f, 0.071f, 0.110f, 0.62f * alpha);
    for (int i = 0; i < 20; ++i) {
        Rect2 cell(origin.x + float(kChannelTexels[i][0]) * px,
                   origin.y + float(kChannelTexels[i][1]) * px, px, px);
        draw_rect(cell, track_bg, true);
    }

    if (ratio > 0.005f) {
        float flash = 0.5f + 0.5f * std::sin(pulse_time);
        Color base_col, hi_col;
        if (exhausted) {
            base_col = Color(0.86f, 0.18f + 0.12f * flash, 0.20f, (0.85f + 0.15f * flash) * alpha);
            hi_col = Color(1.0f, 0.42f + 0.15f * flash, 0.36f, (0.85f + 0.15f * flash) * alpha);
        } else if (ratio < 0.26f) {
            float warn = 0.35f + 0.65f * flash;
            base_col = Color(0.92f * warn + 0.15f * (1.0f - warn), 0.68f - 0.30f * warn, 0.18f, alpha);
            hi_col = Color(1.0f * warn + 0.35f * (1.0f - warn), 0.84f - 0.25f * warn, 0.32f, alpha);
        } else {
            // Matches minimap_tree.png pixel-art emerald & highlight greens (#26854c -> #5ab552)
            base_col = Color(0.149f + 0.35f * complete_flash,
                             0.522f + 0.38f * complete_flash,
                             0.298f + 0.42f * complete_flash, alpha);
            hi_col = Color(0.353f + 0.40f * complete_flash,
                           0.710f + 0.25f * complete_flash,
                           0.322f + 0.45f * complete_flash, alpha);
        }

        const float total_segs = ratio * 20.0f;
        for (int i = 0; i < 20; ++i) {
            float seg_a = std::clamp(total_segs - float(i), 0.0f, 1.0f);
            if (seg_a <= 0.01f)
                continue;
            // Upper half of ring gets pixel highlight tone, lower half gets mid emerald tone
            bool upper = kChannelTexels[i][1] <= 5;
            Color c = upper ? hi_col : base_col;
            c.a *= seg_a;
            Rect2 cell(origin.x + float(kChannelTexels[i][0]) * px,
                       origin.y + float(kChannelTexels[i][1]) * px, px, px);
            draw_rect(cell, c, true);
        }
    }

    if (circle_tex.is_valid()) {
        Rect2 dst(origin, Vector2(sprite_sz, sprite_sz));
        Color tint = exhausted
            ? Color(1.0f, 0.72f + 0.28f * (0.5f + 0.5f * std::sin(pulse_time)), 0.72f, alpha)
            : Color(1.0f, 1.0f, 1.0f, alpha);
        draw_texture_rect(circle_tex, dst, false, tint);
    }
}

} // namespace godot
