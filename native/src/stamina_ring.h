#pragma once
#include "game/player/flying_player.h"
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/texture2d.hpp>

namespace godot {

class CrosshairDot : public Control {
    GDCLASS(CrosshairDot, Control)

    float target_darkness = 0.0f;
    float darkness = 0.0f;
    float hit_pulse = 0.0f;
    float hit_strength = 0.0f;

  protected:
    static void _bind_methods() {}

  public:
    void _ready() override;
    void _process(double delta) override;
    void _draw() override;

    void set_target_darkness(float d) { target_darkness = std::clamp(d, 0.0f, 1.0f); }
    float get_darkness() const { return darkness; }
    // Brief hit-marker: four pixel ticks spring out from the dot on impact.
    void pulse(float strength) {
        hit_strength = std::clamp(strength, 0.0f, 1.0f);
        hit_pulse = 1.0f;
        queue_redraw();
    }
};

class StaminaRing : public Control {
    GDCLASS(StaminaRing, Control)

    const FlyingPlayer *player = nullptr;
    Ref<Texture2D> circle_tex;
    float display_ratio = 1.0f;
    float prev_ratio = 1.0f;
    float visibility_alpha = 0.0f;
    float full_hold_timer = 0.0f;
    float pulse_time = 0.0f;
    float complete_flash = 0.0f;
    bool was_exhausted = false;

  protected:
    static void _bind_methods() {}

  public:
    void _ready() override;
    void _process(double delta) override;
    void _draw() override;

    void bind_player(const FlyingPlayer *p) { player = p; }
};

} // namespace godot
