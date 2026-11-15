#include "flying_player.h"
#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

namespace godot {

namespace {
Vector3 horizontal_part(const Vector3 &v) {
    return Vector3(v.x, 0.0f, v.z);
}

float exp_smooth(float current, float target, float rate, float dt) {
    return Math::lerp(current, target, 1.0f - std::exp(-rate * dt));
}
} // namespace

void FlyingPlayer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_active", "value"), &FlyingPlayer::set_active);
    ClassDB::bind_method(D_METHOD("set_flying", "value"), &FlyingPlayer::set_flying);
    ClassDB::bind_method(D_METHOD("is_flying"), &FlyingPlayer::is_flying);
    ClassDB::bind_method(D_METHOD("is_grounded"), &FlyingPlayer::is_grounded);
    ClassDB::bind_method(D_METHOD("get_jump_count"), &FlyingPlayer::get_jump_count);
    ClassDB::bind_method(D_METHOD("set_view", "yaw", "pitch"), &FlyingPlayer::set_view);
    ClassDB::bind_method(D_METHOD("get_camera"), &FlyingPlayer::get_camera);
    ClassDB::bind_method(D_METHOD("is_crouching"), &FlyingPlayer::is_crouching);
    ClassDB::bind_method(D_METHOD("is_sliding"), &FlyingPlayer::is_sliding);
    ClassDB::bind_method(D_METHOD("is_climbing"), &FlyingPlayer::is_climbing);
    ClassDB::bind_method(D_METHOD("is_mantling"), &FlyingPlayer::is_mantling);
    ClassDB::bind_method(D_METHOD("is_swimming"), &FlyingPlayer::is_swimming);
    ClassDB::bind_method(D_METHOD("is_in_water"), &FlyingPlayer::is_in_water);
    ClassDB::bind_method(D_METHOD("is_underwater"), &FlyingPlayer::is_underwater);
    ClassDB::bind_method(D_METHOD("is_in_foliage"), &FlyingPlayer::is_in_foliage);
    ClassDB::bind_method(D_METHOD("get_foliage_factor"), &FlyingPlayer::get_foliage_factor);
    ClassDB::bind_method(D_METHOD("get_stamina"), &FlyingPlayer::get_stamina);
    ClassDB::bind_method(D_METHOD("get_max_stamina"), &FlyingPlayer::get_max_stamina);
    ClassDB::bind_method(D_METHOD("get_stamina_ratio"), &FlyingPlayer::get_stamina_ratio);
    ClassDB::bind_method(D_METHOD("is_stamina_exhausted"), &FlyingPlayer::is_stamina_exhausted);
    ClassDB::bind_method(D_METHOD("get_movement_state_name"), &FlyingPlayer::get_movement_state_name);
    ClassDB::bind_method(D_METHOD("get_horizontal_speed"), &FlyingPlayer::get_horizontal_speed);
    ClassDB::bind_method(D_METHOD("set_speed", "value"), &FlyingPlayer::set_speed);
    ClassDB::bind_method(D_METHOD("get_speed"), &FlyingPlayer::get_speed);
    ClassDB::bind_method(D_METHOD("set_noclip", "value"), &FlyingPlayer::set_noclip);
    ClassDB::bind_method(D_METHOD("is_noclip"), &FlyingPlayer::is_noclip);
    ClassDB::bind_method(D_METHOD("set_god", "value"), &FlyingPlayer::set_god);
    ClassDB::bind_method(D_METHOD("is_god"), &FlyingPlayer::is_god);
    ClassDB::bind_method(D_METHOD("set_gravity", "value"), &FlyingPlayer::set_gravity);
    ClassDB::bind_method(D_METHOD("get_gravity"), &FlyingPlayer::get_gravity);
    ClassDB::bind_method(D_METHOD("reset_debug_parameters"), &FlyingPlayer::reset_debug_parameters);
}

void FlyingPlayer::configure(const Dictionary &d, std::function<bool(Vector3, Vector3)> q) {
    solid = std::move(q);
    height = std::clamp(float(d.get("height", 1.8)), .5f, 4.f);
    eye_height = std::clamp(float(d.get("eye_height", 1.65)), .2f, height);
    crouch_height = std::clamp(float(d.get("crouch_height", 1.15)), .4f, height);
    crouch_eye_height = std::clamp(float(d.get("crouch_eye_height", 1.0)), .2f, crouch_height);
    slide_height = std::clamp(float(d.get("slide_height", 0.95)), .4f, crouch_height);
    slide_eye_height = std::clamp(float(d.get("slide_eye_height", 0.78)), .2f, slide_height);
    current_height = height;
    current_eye_height = eye_height;

    width = std::clamp(float(d.get("width", .6)), .2f, 2.f);
    walk_speed = std::clamp(float(d.get("walk_speed", 6.5)), 1.f, 40.f);
    run_speed = std::clamp(float(d.get("run_speed", 11.5)), walk_speed, 50.f);
    crouch_speed = std::clamp(float(d.get("crouch_speed", 3.6)), 1.f, walk_speed);
    swim_speed = std::clamp(float(d.get("swim_speed", 5.6)), 1.f, 25.f);
    swim_sprint_speed = std::clamp(float(d.get("swim_sprint_speed", 9.4)), swim_speed, 35.f);
    swim_vertical_speed = std::clamp(float(d.get("swim_vertical_speed", 5.2)), 1.f, 20.f);
    fly_speed = std::clamp(float(d.get("fly_speed", 20)), 1.f, 150.f);
    default_fly_speed = fly_speed;
    sprint = std::clamp(float(d.get("sprint_multiplier", 2.5)), 1.f, 5.f);
    slide_boost = std::clamp(float(d.get("slide_boost", 3.5)), 0.f, 15.f);
    slide_max_speed = std::clamp(float(d.get("slide_max_speed", 17.5)), run_speed, 40.f);
    slide_flat_cap = std::clamp(run_speed + slide_boost, run_speed, slide_max_speed);
    slide_min_speed = std::clamp(walk_speed + 0.7f, 2.f, run_speed);
    slide_exit_speed = std::clamp(crouch_speed + 1.0f, 1.f, slide_min_speed);

    jump_speed = std::clamp(float(d.get("jump_speed", 10)), 1.f, 30.f);
    gravity = std::clamp(float(d.get("gravity", 28)), 1.f, 100.f);
    default_gravity = gravity;
    base_fov = std::clamp(float(d.get("base_fov", 80.0)), 60.f, 110.f);
    current_fov = base_fov;
    sensitivity = std::clamp(float(d.get("mouse_sensitivity", .0022)), .0001f, .02f);

    if (!camera) {
        camera = memnew(Camera3D);
        camera->set_position(Vector3(0, current_eye_height, 0));
        camera->set_near(.08);
        camera->set_far(6000);
        camera->set_fov(base_fov);
        add_child(camera);
    }
}

void FlyingPlayer::_ready() {
    cam_base_y = current_eye_height;
    if (camera)
        camera->make_current();

    footstep_player = memnew(AudioStreamPlayer3D);
    footstep_player->set_name("FootstepSpatialAudio");
    Ref<Script> spatial_audio_script =
        ResourceLoader::get_singleton()->load("res://addons/spatial_audio_3d/spatial_audio_3d.gd");
    if (spatial_audio_script.is_valid()) {
        footstep_player->set_script(spatial_audio_script);
    }
    footstep_player->set("max_distance", 40.0);
    footstep_player->set("volume_db", 0.0);
    footstep_player->set("max_raycast_distance", 35);
    footstep_player->set("collision_mask", 1);
    footstep_player->set("reverb_fadeintime", 0.2);
    footstep_player->set("reverb_fadeouttime", 0.3);
    footstep_player->set("roomsize_multiplicator", 7.0);
    footstep_player->set_position(Vector3(0, 0.25f, 0));
    add_child(footstep_player);

    for (int i = 1; i <= 7; i++) {
        String path = "res://sounds/vostokfootsteps/fc" + String::num_int64(i) + ".ogg";
        Ref<AudioStream> stream = ResourceLoader::get_singleton()->load(path);
        if (stream.is_valid()) {
            footstep_sounds.push_back(stream);
        }
    }
}

void FlyingPlayer::_process(double delta) {
    if (!camera || !active)
        return;
    const float dt = std::min(float(delta), 0.1f);
    if (flying || climbing || mantling || noclip) {
        cam_step_offset = 0.0f;
    } else {
        cam_step_offset = exp_smooth(cam_step_offset, 0.0f, 15.0f, dt);
        if (std::abs(cam_step_offset) < 0.001f)
            cam_step_offset = 0.0f;
    }
    camera->set_position(Vector3(cam_base_x, cam_base_y + cam_step_offset, 0.0f));
}

void FlyingPlayer::set_flying(bool value) {
    flying = value;
    cam_step_offset = 0.0f;
    velocity = Vector3();
    jumps = 0;
    crouching = false;
    sliding = false;
    climbing = false;
    mantling = false;
    swimming = false;
    in_water = false;
    underwater = false;
    water_submergence = 0.0f;
    if (flying) {
        current_height = height;
        state = MovementState::Fly;
        stamina = max_stamina;
        stamina_exhausted = false;
    } else {
        state = grounded ? MovementState::Walk : MovementState::Air;
    }
}

String FlyingPlayer::get_movement_state_name() const {
    switch (state) {
    case MovementState::Fly:
        return "FLY";
    case MovementState::Slide:
        return "SLIDE";
    case MovementState::Crouch:
        return "CROUCH";
    case MovementState::Sprint:
        return "SPRINT";
    case MovementState::Climb:
        return "CLIMB";
    case MovementState::Mantle:
        return "MANTLE";
    case MovementState::Swim:
        return "SWIM";
    case MovementState::Air:
        return "AIR";
    case MovementState::Walk:
    default:
        return "WALK";
    }
}

void FlyingPlayer::reset_debug_parameters() {
    noclip = false;
    god_mode = false;
    fly_speed = default_fly_speed;
    gravity = default_gravity;
}

void FlyingPlayer::set_view(float yaw, float p) {
    pitch = std::clamp(p, -1.55f, 1.55f);
    set_rotation(Vector3(0, yaw, 0));
    if (camera)
        camera->set_rotation(Vector3(pitch, 0, cam_roll));
}

void FlyingPlayer::_unhandled_input(const Ref<InputEvent> &event) {
    if (!active || Input::get_singleton()->get_mouse_mode() != Input::MOUSE_MODE_CAPTURED)
        return;
    Ref<InputEventMouseMotion> motion = event;
    if (motion.is_valid()) {
        auto m = motion->get_relative();
        rotate_y(-m.x * sensitivity);
        pitch = std::clamp(pitch - m.y * sensitivity, -1.55f, 1.55f);
        if (camera)
            camera->set_rotation(Vector3(std::clamp(pitch + land_pitch, -1.55f, 1.55f), 0, cam_roll));
    }
}

bool FlyingPlayer::collides_with_height(Vector3 p, float h) const {
    if (noclip || !solid)
        return false;
    float r = width * .5f - .001f;
    return solid(p + Vector3(-r, .001f, -r), p + Vector3(r, h - .001f, r));
}

bool FlyingPlayer::collides(Vector3 p) const {
    return collides_with_height(p, current_height);
}

bool FlyingPlayer::contains_block(int x, int y, int z) const {
    auto p = get_position();
    return x + 1 > p.x - width * .5f && x < p.x + width * .5f && y + 1 > p.y && y < p.y + current_height &&
           z + 1 > p.z - width * .5f && z < p.z + width * .5f;
}

float FlyingPlayer::probe_floor_y(Vector3 p, float px, float pz) const {
    if (noclip || !solid)
        return p.y;
    float top = p.y + 0.65f;
    float bot = p.y - 0.85f;
    if (collides_with_height(Vector3(px, top, pz), 0.1f))
        return p.y;
    if (!collides_with_height(Vector3(px, bot, pz), 0.1f))
        return p.y;
    float lo = bot, hi = top;
    for (int k = 0; k < 8; k++) {
        float mid = (lo + hi) * .5f;
        if (collides_with_height(Vector3(px, mid, pz), 0.1f))
            lo = mid;
        else
            hi = mid;
    }
    return hi;
}

bool FlyingPlayer::probe_wall(Vector3 p, Vector3 &out_normal) const {
    if (noclip || !solid)
        return false;
    Vector3 fwd = -get_basis().get_column(2);
    fwd.y = 0.0f;
    if (fwd.length_squared() < 1e-4f)
        return false;
    fwd = fwd.normalized();

    const float reach = width * 0.5f + 0.18f;
    auto hits_band = [&](Vector3 test_p) {
        return solid(test_p + Vector3(-width * 0.28f, 0.35f, -width * 0.28f),
                     test_p + Vector3(width * 0.28f, 1.25f, width * 0.28f));
    };

    if (!hits_band(p + fwd * reach))
        return false;

    Vector3 n(0.0f, 0.0f, 0.0f);
    if (hits_band(p + Vector3(reach, 0.0f, 0.0f)))
        n.x -= 1.0f;
    if (hits_band(p + Vector3(-reach, 0.0f, 0.0f)))
        n.x += 1.0f;
    if (hits_band(p + Vector3(0.0f, 0.0f, reach)))
        n.z -= 1.0f;
    if (hits_band(p + Vector3(0.0f, 0.0f, -reach)))
        n.z += 1.0f;

    if (n.length_squared() < 1e-4f) {
        out_normal = -fwd;
        return true;
    }
    n = n.normalized();
    if (n.dot(-fwd) < 0.25f) {
        out_normal = -fwd;
    } else {
        out_normal = n;
    }
    return true;
}

bool FlyingPlayer::try_find_ledge(Vector3 p, Vector3 wall_dir, Vector3 &out_land_pos, bool from_climb) const {
    if (noclip || !solid)
        return false;
    wall_dir.y = 0.0f;
    if (wall_dir.length_squared() < 1e-4f)
        return false;
    wall_dir = wall_dir.normalized();

    const float min_top = p.y + (from_climb ? 0.20f : 0.56f);
    const float max_top = p.y + (from_climb ? 1.55f : 2.05f);

    for (float step_fwd : {width * 0.5f + 0.24f, width * 0.5f + 0.44f}) {
        Vector3 probe_xz = p + wall_dir * step_fwd;
        float prev_air = max_top + 0.20f;
        if (collides_with_height(Vector3(probe_xz.x, prev_air, probe_xz.z), 0.1f))
            continue;

        bool found_surface = false;
        float solid_y = min_top;
        for (float sy = max_top; sy >= min_top - 0.01f; sy -= 0.20f) {
            if (collides_with_height(Vector3(probe_xz.x, sy, probe_xz.z), 0.1f)) {
                solid_y = sy;
                found_surface = true;
                break;
            }
            prev_air = sy;
        }
        if (!found_surface)
            continue;

        float lo = solid_y, hi = prev_air;
        for (int k = 0; k < 8; ++k) {
            float mid = (lo + hi) * 0.5f;
            if (collides_with_height(Vector3(probe_xz.x, mid, probe_xz.z), 0.1f))
                lo = mid;
            else
                hi = mid;
        }
        float top_y = hi;
        if (top_y < min_top || top_y > max_top)
            continue;

        // Ensure head clearance directly above current position before rising to top_y
        if (collides_with_height(Vector3(p.x, top_y + 0.02f, p.z), std::max(0.4f, crouch_height)))
            continue;

        Vector3 land_pos(probe_xz.x, top_y + 0.02f, probe_xz.z);
        if (!collides_with_height(land_pos, crouch_height)) {
            out_land_pos = land_pos;
            return true;
        }
    }
    return false;
}

bool FlyingPlayer::consume_stamina(float amount) {
    if (flying || god_mode) {
        stamina = max_stamina;
        stamina_exhausted = false;
        return true;
    }
    stamina = std::max(0.0f, stamina - amount);
    stamina_regen_delay = 0.55f;
    if (stamina <= 0.0f) {
        stamina = 0.0f;
        stamina_exhausted = true;
        return false;
    }
    return !stamina_exhausted;
}

void FlyingPlayer::update_stamina_regen(float dt, bool consuming_now, float horiz_speed) {
    if (flying || god_mode) {
        stamina = max_stamina;
        stamina_exhausted = false;
        stamina_regen_delay = 0.0f;
        return;
    }
    if (consuming_now)
        return;
    if (stamina_regen_delay > 0.0f) {
        stamina_regen_delay = std::max(0.0f, stamina_regen_delay - dt);
        return;
    }
    float rate = stamina_exhausted
        ? (swimming ? 16.0f : 28.0f)
        : (swimming
            ? (horiz_speed < 0.8f ? 22.0f : 12.0f)
            : (grounded ? (horiz_speed < 0.5f ? 42.0f : 34.0f) : 20.0f));
    stamina = std::min(max_stamina, stamina + rate * dt);
    if (stamina_exhausted && stamina >= max_stamina) {
        stamina_exhausted = false;
    }
}

void FlyingPlayer::update_camera_feel(float dt, float horiz_speed, float strafe_input, float slide_bank_sign) {
    if (!camera)
        return;

    float target_eye = eye_height;
    if (!flying) {
        if (sliding)
            target_eye = slide_eye_height;
        else if (crouching)
            target_eye = crouch_eye_height;
        else if (swimming)
            target_eye = swim_eye_height;
    }
    current_eye_height = exp_smooth(current_eye_height, target_eye, 10.5f, dt);

    land_dip = exp_smooth(land_dip, 0.0f, 9.5f, dt);
    land_pitch = exp_smooth(land_pitch, 0.0f, 8.5f, dt);

    const bool moving_on_ground = !flying && ((grounded && !sliding && !swimming && horiz_speed > 0.6f) ||
                                  (climbing && velocity.length() > 0.6f));
    const bool active_swim_bob = !flying && swimming && (horiz_speed > 0.4f || std::abs(velocity.y) > 0.4f);
    bob_weight = exp_smooth(bob_weight, (moving_on_ground || active_swim_bob || swimming) ? 1.0f : 0.0f, 8.0f, dt);

    float bob_x = 0.0f, bob_y = 0.0f, bob_roll = 0.0f;
    if (bob_weight > 0.01f) {
        float cadence = swimming
            ? (horiz_speed > swim_speed * 1.15f ? 6.6f : (horiz_speed > 0.4f ? 4.4f : 1.8f))
            : (crouching ? 6.5f : (climbing ? 9.0f : (state == MovementState::Sprint ? 11.2f : 8.5f)));
        float ref_speed = swimming
            ? std::max(velocity.length(), 1.2f)
            : (climbing ? velocity.length() : horiz_speed);
        float speed_factor = swimming
            ? std::clamp(ref_speed / std::max(swim_speed, 1.0f), 0.65f, 1.50f)
            : std::clamp(ref_speed / std::max(walk_speed, 1.0f), 0.45f, 1.55f);
        bob_phase += dt * cadence * speed_factor;
        constexpr float two_pi = 6.28318530718f;
        if (bob_phase > two_pi * 100.0f)
            bob_phase = std::fmod(bob_phase, two_pi);

        float amp = swimming
            ? (horiz_speed > 0.4f ? 0.022f : 0.012f)
            : (crouching ? 0.009f : (climbing ? 0.014f : (state == MovementState::Sprint ? 0.024f : 0.015f)));
        bob_y = std::sin(bob_phase * (swimming ? 1.0f : 2.0f)) * amp * bob_weight;
        bob_x = std::cos(bob_phase * (swimming ? 0.5f : 1.0f)) * amp * 0.45f * bob_weight;
        bob_roll = std::cos(bob_phase * (swimming ? 0.5f : 1.0f)) * (swimming ? 0.009f : 0.002f) * bob_weight;
    } else {
        bob_phase = 0.0f;
    }

    float target_roll = 0.0f;
    if (flying) {
        target_roll = -strafe_input * 0.016f;
    } else if (swimming) {
        target_roll = -strafe_input * 0.022f;
    } else if (sliding) {
        float dir_bank = std::abs(strafe_input) > 0.1f ? -strafe_input : slide_bank_sign;
        target_roll = dir_bank * 0.032f;
    } else if (climbing) {
        target_roll = -strafe_input * 0.016f;
    } else {
        target_roll = -strafe_input * 0.012f;
    }
    cam_roll = exp_smooth(cam_roll, target_roll + bob_roll, 7.5f, dt);

    float target_fov = base_fov;
    if (flying) {
        target_fov = base_fov + std::clamp(horiz_speed * 0.15f, 0.0f, 6.0f);
    } else if (swimming) {
        const float boost_fov = horiz_speed > swim_speed + 0.6f
            ? std::clamp((horiz_speed - swim_speed) * 0.9f, 0.0f, 4.2f)
            : 0.0f;
        target_fov = base_fov - (underwater ? 2.5f : 0.8f) + boost_fov;
    } else if (sliding) {
        target_fov = base_fov + std::clamp((horiz_speed - walk_speed) * 0.55f, 2.5f, 7.5f);
    } else if (climbing || mantling) {
        target_fov = base_fov - 0.8f;
    } else if (crouching && horiz_speed < walk_speed) {
        target_fov = base_fov - 1.2f;
    } else if (horiz_speed > walk_speed + 0.4f) {
        target_fov = base_fov + std::clamp((horiz_speed - walk_speed) * 0.45f, 0.0f, 5.5f);
    }
    current_fov = exp_smooth(current_fov, target_fov, 5.5f, dt);

    impact_kick = std::max(0.0f, impact_kick - dt * 9.0f);
    const float kick = impact_kick * impact_kick;
    cam_base_x = bob_x;
    cam_base_y = current_eye_height + land_dip + bob_y - kick * 0.012f;
    camera->set_fov(current_fov);
    camera->set_position(Vector3(cam_base_x, cam_base_y + cam_step_offset, 0.0f));
    camera->set_rotation(Vector3(std::clamp(pitch + land_pitch - kick * 0.010f, -1.55f, 1.55f), 0.0f,
                                 cam_roll + kick * 0.006f * impact_kick_sign));
}

void FlyingPlayer::_physics_process(double raw_delta) {
    if (!active || riding)
        return;
    float dt = std::min(float(raw_delta), .05f);
    Vector3 p = get_position();
    if (!noclip && stream_ready && !stream_ready(p)) {
        velocity = Vector3();
        return;
    }

    auto in = Input::get_singleton();
    float strafe_input = float(in->is_physical_key_pressed(Key::KEY_D)) - float(in->is_physical_key_pressed(Key::KEY_A));
    float forward_input = float(in->is_physical_key_pressed(Key::KEY_S)) - float(in->is_physical_key_pressed(Key::KEY_W));
    Vector3 wish(strafe_input, 0.0f, forward_input);
    wish = get_basis().xform(wish);
    wish.y = 0.0f;
    if (wish.length_squared() > 0.0f)
        wish = wish.normalized();

    bool space = in->is_physical_key_pressed(Key::KEY_SPACE);
    bool space_pressed = space && !jump_held;
    bool shift = in->is_physical_key_pressed(Key::KEY_SHIFT);
    bool crouch_held = in->is_physical_key_pressed(Key::KEY_C) || in->is_physical_key_pressed(Key::KEY_CTRL);
    bool crouch_pressed = crouch_held && !crouch_was_held;
    crouch_was_held = crouch_held;

    if (slide_cooldown > 0.0f)
        slide_cooldown = std::max(0.0f, slide_cooldown - dt);
    if (climb_cooldown > 0.0f)
        climb_cooldown = std::max(0.0f, climb_cooldown - dt);

    bool stamina_used_now = false;

    LiquidState lstate{};
    if (liquid_state_query) {
        lstate = liquid_state_query(p + Vector3(-width * 0.5f, 0.001f, -width * 0.5f),
                                    p + Vector3(width * 0.5f, std::max(current_height, height * 0.85f) - 0.001f, width * 0.5f));
    } else if (liquid_query) {
        bool any_liq = false;
        lstate.current = liquid_query(p + Vector3(-width * 0.5f, 0.001f, -width * 0.5f),
                                      p + Vector3(width * 0.5f, current_height - 0.001f, width * 0.5f),
                                      any_liq);
        lstate.in_water = any_liq;
        lstate.submergence = any_liq ? 1.0f : 0.0f;
    }

    in_water = lstate.in_water;
    water_submergence = lstate.submergence;

    const bool touching_foliage = foliage_query && !noclip &&
        foliage_query(p + Vector3(-width * 0.45f, 0.08f, -width * 0.45f),
                      p + Vector3(width * 0.45f, std::max(current_height, height * 0.85f) - 0.05f, width * 0.45f));
    in_foliage = touching_foliage;
    foliage_slow = exp_smooth(foliage_slow, touching_foliage ? 1.0f : 0.0f, 11.0f, dt);
    const float foliage_move_scale = (flying || noclip) ? 1.0f : (1.0f - 0.56f * foliage_slow);

    if (!crouch_held)
        slide_resume_pending = false;

    if (flying) {
        crouching = false;
        sliding = false;
        slide_resume_pending = false;
        climbing = false;
        mantling = false;
        swimming = false;
        underwater = false;
        current_height = height;
        state = MovementState::Fly;

        float speed = fly_speed * (shift ? sprint : 1.0f);
        wish.y = float(space) - float(crouch_held);
        if (wish.length_squared() > 1.0f)
            wish = wish.normalized();
        velocity = velocity.lerp(wish * speed, 1.0f - std::exp(-12.0f * dt));
    } else if (mantling) {
        state = MovementState::Mantle;
        crouching = collides_with_height(mantle_end, height);
        sliding = false;
        slide_resume_pending = false;
        climbing = false;
        swimming = false;
        underwater = false;
        current_height = crouching ? crouch_height : height;
        stamina_used_now = true;

        mantle_timer += dt;
        float t = std::clamp(mantle_timer / mantle_duration, 0.0f, 1.0f);
        auto smooth01 = [](float a, float b, float x) {
            float u = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
            return u * u * (3.0f - 2.0f * u);
        };
        float up_t = smooth01(0.0f, 0.55f, t);
        float fwd_t = smooth01(0.55f, 1.0f, t);
        Vector3 next_pos(Math::lerp(mantle_start.x, mantle_end.x, fwd_t),
                         Math::lerp(mantle_start.y, mantle_end.y, up_t),
                         Math::lerp(mantle_start.z, mantle_end.z, fwd_t));
        if (!noclip && stream_ready && !stream_ready(next_pos)) {
            mantling = false;
            velocity = Vector3();
            jump_held = space;
            return;
        }
        if (!collides(next_pos)) {
            p = next_pos;
        }
        p.x = std::clamp(p.x, -999990.f, 999990.f);
        p.z = std::clamp(p.z, -999990.f, 999990.f);
        p.y = std::clamp(p.y, minimum_y, 3000.f);
        set_position(p);
        velocity = Vector3();
        if (t >= 1.0f) {
            mantling = false;
            grounded = true;
            jumps = 0;
            land_dip = -0.045f;
        }
        jump_held = space;
        update_stamina_regen(dt, false, 0.0f);
        update_camera_feel(dt, 0.0f, strafe_input, 1.0f);
        return;
    } else {
        // Timers for responsive ground/air feel
        if (grounded) {
            coyote_timer = 0.12f;
            jumps = 0;
        } else {
            coyote_timer = std::max(0.0f, coyote_timer - dt);
        }

        if (space_pressed)
            jump_buffer_timer = 0.12f;
        else
            jump_buffer_timer = std::max(0.0f, jump_buffer_timer - dt);

        Vector3 horiz = horizontal_part(velocity);
        float horiz_speed = horiz.length();

        auto begin_mantle = [&](Vector3 land_pos, float cost) {
            mantling = true;
            climbing = false;
            sliding = false;
            swimming = false;
            underwater = false;
            state = MovementState::Mantle;
            mantle_start = p;
            mantle_end = land_pos;
            mantle_timer = 0.0f;
            mantle_duration = std::clamp(0.16f + (land_pos.y - p.y) * 0.055f, 0.17f, 0.28f);
            velocity = Vector3();
            jumps = 0;
            cam_step_offset = std::clamp(cam_step_offset - 0.12f, -0.75f, 0.75f);
            consume_stamina(cost);
        };

        // Swimming state transitions (enter in waist/chest-deep water, exit smoothly on dry land or shallow ankles)
        const bool chest_submerged = lstate.in_water &&
            (lstate.has_surface ? (lstate.surface_y >= p.y + height * 0.45f) : (lstate.submergence >= 0.48f));
        const bool sustain_swim = lstate.in_water &&
            (lstate.has_surface ? (lstate.surface_y >= p.y + 0.58f) : (lstate.submergence >= 0.30f));

        if (!swimming) {
            if (chest_submerged || (lstate.in_water && !grounded && lstate.submergence >= 0.36f)) {
                swimming = true;
                climbing = false;
                sliding = false;
                crouching = false;
                jumps = 0;
                if (velocity.y < -4.0f) {
                    const float splash = std::clamp((-velocity.y - 4.0f) / 16.0f, 0.0f, 1.0f);
                    velocity.y *= 0.38f;
                    land_dip = std::min(land_dip, -0.07f * splash);
                }
            }
        } else if (!sustain_swim || (grounded && !chest_submerged && !crouch_held)) {
            swimming = false;
            underwater = false;
            crouching = collides_with_height(p, height);
            current_height = crouching ? crouch_height : height;
        }

        if (swimming) {
            state = MovementState::Swim;
            climbing = false;
            sliding = false;
            slide_resume_pending = false;
            crouching = false;
            jumps = 0;
            coyote_timer = 0.0f;
            if (!collides_with_height(p, swim_height))
                current_height = swim_height;
            else
                current_height = crouch_height;

            const float eye_check_h = std::min(current_eye_height, swim_eye_height);
            underwater = lstate.has_surface
                ? (p.y + eye_check_h <= lstate.surface_y - 0.02f)
                : (lstate.submergence >= 0.85f);

            // Check if pressing against a shoreline ledge to mantle out onto dry ground
            if (!underwater && (space_pressed || (space && forward_input < -0.25f))) {
                Vector3 fwd_look = -get_basis().get_column(2);
                fwd_look.y = 0.0f;
                Vector3 probe_dir = wish.length_squared() > 0.01f
                    ? wish
                    : (fwd_look.length_squared() > 1e-4f ? fwd_look.normalized() : Vector3(0, 0, -1));
                Vector3 wnorm;
                Vector3 ledge_pos;
                if (probe_wall(p, wnorm) && (-wnorm).dot(probe_dir) > 0.15f &&
                    try_find_ledge(p, -wnorm, ledge_pos, false)) {
                    begin_mantle(ledge_pos, 5.0f);
                    jump_held = space;
                    return;
                }
            }

            const bool swim_input = wish.length_squared() > 0.01f || space || crouch_held;
            bool swim_sprinting = shift && !stamina_exhausted && stamina > 0.0f && swim_input;
            if (swim_input && !god_mode && !stamina_exhausted && stamina > 0.0f) {
                stamina_used_now = true;
                const float drain = swim_sprinting ? 15.5f : 5.5f;
                if (!consume_stamina(drain * dt)) {
                    swim_sprinting = false;
                }
            }

            const float target_swim_speed = stamina_exhausted
                ? (swim_speed * 0.72f)
                : (swim_sprinting ? swim_sprint_speed : swim_speed);

            // Horizontal 3D swim drive (damped fluid acceleration)
            Vector3 desired_horiz = wish * target_swim_speed;
            if (underwater && std::abs(pitch) > 0.25f && wish.length_squared() > 0.01f && std::abs(forward_input) > 0.05f) {
                desired_horiz *= std::max(0.55f, std::cos(pitch));
            }
            const float h_rate = wish.length_squared() > 0.01f ? 6.8f : 4.2f;
            horiz = horiz.lerp(desired_horiz, 1.0f - std::exp(-h_rate * dt));
            horiz += horizontal_part(lstate.current) * dt;

            velocity.x = horiz.x;
            velocity.z = horiz.z;

            // Vertical 3D swim + pitch dive + buoyancy + water surface jump
            float pitch_drive = 0.0f;
            if (forward_input < -0.05f) {
                if (underwater && std::abs(pitch) > 0.15f) {
                    // Full 3D pitch steering while submerged
                    pitch_drive = std::sin(pitch) * (-forward_input) * target_swim_speed * 0.90f;
                } else if (!underwater && pitch < -0.42f) {
                    // Looking decisively downward (~24 deg+) breaks the waterline into a dive
                    pitch_drive = std::sin(pitch) * (-forward_input) * target_swim_speed * 0.92f;
                }
            }

            const bool at_surface = lstate.has_surface && (p.y + eye_check_h >= lstate.surface_y - 0.34f);
            if (space) {
                if (at_surface && lstate.has_surface) {
                    const bool wall_ahead = wish.length_squared() > 0.01f &&
                        collides_with_height(p + wish * 0.24f, current_height);
                    if (space_pressed || wall_ahead) {
                        const float leap = jump_speed * (wall_ahead ? 0.90f : 0.82f);
                        if (velocity.y < leap) {
                            if (space_pressed && !god_mode) {
                                consume_stamina(8.0f);
                                stamina_used_now = true;
                            }
                            velocity.y = leap;
                        }
                    } else {
                        // Treading / bobbing slightly higher at surface when holding Space
                        const float tread_y = lstate.surface_y - eye_check_h + 0.32f;
                        const float tread_vy = std::clamp((tread_y - p.y) * 8.0f, -1.5f, 3.8f);
                        velocity.y = exp_smooth(velocity.y, tread_vy, 8.0f, dt);
                    }
                } else {
                    const float up_speed = swim_vertical_speed * (swim_sprinting ? 1.25f : 1.0f);
                    velocity.y = exp_smooth(velocity.y, up_speed, 7.5f, dt);
                }
            } else if (crouch_held) {
                const float down_speed = -swim_vertical_speed * (swim_sprinting ? 1.25f : 1.0f);
                velocity.y = exp_smooth(velocity.y, down_speed, 7.5f, dt);
            } else if (std::abs(pitch_drive) > 0.15f) {
                velocity.y = exp_smooth(velocity.y, pitch_drive, 6.5f, dt);
            } else if (lstate.has_surface && p.y + eye_check_h >= lstate.surface_y - 0.75f) {
                // Smooth surface buoyancy: floats with eyes naturally above the waterline
                const float target_float_y = lstate.surface_y - eye_check_h + 0.16f;
                const float float_vy = std::clamp((target_float_y - p.y) * 7.5f, -3.2f, 4.6f);
                velocity.y = exp_smooth(velocity.y, float_vy, 6.0f, dt);
            } else {
                // Near-neutral underwater sink/damping when cruising deep below the surface
                velocity.y = exp_smooth(velocity.y, -0.35f, 5.0f, dt);
            }
            velocity.y += lstate.current.y * dt;
        }

        // Handle active wall climbing first
        if (!swimming && climbing) {
            Vector3 wnorm;
            bool on_wall = probe_wall(p, wnorm);
            if (crouch_pressed) {
                climbing = false;
                climb_cooldown = 0.35f;
                velocity = wall_normal * 2.0f;
            } else if (!on_wall) {
                Vector3 ledge_pos;
                if (forward_input < -0.1f && try_find_ledge(p, -wall_normal, ledge_pos, true)) {
                    begin_mantle(ledge_pos, 4.0f);
                    jump_held = space;
                    return;
                }
                climbing = false;
                climb_cooldown = 0.22f;
            } else {
                wall_normal = wnorm;
                Vector3 ledge_pos;
                if ((forward_input < -0.1f || space_pressed) &&
                    try_find_ledge(p, -wall_normal, ledge_pos, true) &&
                    ledge_pos.y <= p.y + 1.48f) {
                    begin_mantle(ledge_pos, 5.0f);
                    jump_held = space;
                    return;
                }
                if (space_pressed) {
                    consume_stamina(16.0f);
                    stamina_used_now = true;
                    climbing = false;
                    jumps = 1;
                    if (forward_input > 0.25f) {
                        climb_cooldown = 0.28f;
                        velocity = wall_normal * 7.5f + Vector3(0.0f, 8.8f, 0.0f);
                    } else {
                        climb_cooldown = 0.16f;
                        velocity = -wall_normal * 1.4f + Vector3(0.0f, 8.8f, 0.0f);
                    }
                    horiz = horizontal_part(velocity);
                    horiz_speed = horiz.length();
                } else {
                    state = MovementState::Climb;
                    crouching = false;
                    sliding = false;
                    grounded = false;
                    coyote_timer = 0.0f;

                    Vector3 wall_right = wall_normal.cross(Vector3(0.0f, 1.0f, 0.0f));
                    if (wall_right.length_squared() > 1e-4f)
                        wall_right = wall_right.normalized();
                    float climb_vy = (-forward_input) > 0.0f ? (-forward_input) * 4.2f : (-forward_input) * 4.8f;
                    float shimmy_vx = strafe_input * 3.0f;
                    bool climb_moving = std::abs(climb_vy) > 0.1f || std::abs(shimmy_vx) > 0.1f;

                    velocity = wall_right * shimmy_vx + Vector3(0.0f, climb_vy, 0.0f) - wall_normal * 1.6f;
                    horiz = horizontal_part(velocity);
                    horiz_speed = horiz.length();

                    float drain = (climb_moving ? 14.0f : 2.5f) * dt;
                    stamina_used_now = true;
                    if (!consume_stamina(drain)) {
                        climbing = false;
                        climb_cooldown = 0.45f;
                        velocity = wall_normal * 2.0f;
                    }
                }
            }
        }

        if (!swimming && !climbing && !mantling) {
            // Try air ledge mantle or wall climb attach
            if (!grounded && !stamina_exhausted && stamina > 0.0f && !crouch_held) {
                Vector3 fwd_look = -get_basis().get_column(2);
                fwd_look.y = 0.0f;
                Vector3 probe_dir = wish.length_squared() > 0.01f
                    ? wish
                    : (fwd_look.length_squared() > 1e-4f ? fwd_look.normalized() : Vector3(0, 0, -1));
                Vector3 wnorm;
                if (probe_wall(p, wnorm) && (-wnorm).dot(probe_dir) > 0.15f) {
                    Vector3 ledge_pos;
                    if ((wish.dot(-wnorm) > 0.15f || space) && velocity.y > -10.0f &&
                        try_find_ledge(p, -wnorm, ledge_pos, false)) {
                        begin_mantle(ledge_pos, 8.0f);
                        jump_held = space;
                        return;
                    }
                    if (climb_cooldown <= 0.0f && velocity.y <= 3.8f && (wish.dot(-wnorm) > 0.25f || space)) {
                        climbing = true;
                        sliding = false;
                        slide_resume_pending = false;
                        crouching = false;
                        jumps = 0;
                        wall_normal = wnorm;
                        state = MovementState::Climb;
                        velocity = -wall_normal * 1.2f;
                        stamina_used_now = true;
                    }
                }
            }
        }

        if (!swimming && !climbing && !mantling) {
        // Crouch & Slide transitions
        if (grounded) {
            if (sliding) {
                slide_timer += dt;
                if (!crouch_held || horiz_speed < slide_exit_speed) {
                    sliding = false;
                    slide_resume_pending = false;
                    slide_cooldown = 0.55f;
                    crouching = crouch_held || collides_with_height(p, height);
                }
            } else {
                const bool resume_slide = slide_resume_pending && crouch_held && horiz_speed >= slide_min_speed;
                const bool wants_slide = resume_slide || crouch_pressed || (crouch_held && !crouching && wish.length_squared() > 0.0f);
                if (wants_slide && horiz_speed >= slide_min_speed && (resume_slide || slide_cooldown <= 0.0f)) {
                    sliding = true;
                    crouching = true;
                    slide_timer = 0.0f;
                    slide_resume_pending = false;
                    if (resume_slide) {
                        Vector3 slide_dir = horiz.length_squared() > 0.001f ? horiz.normalized() : wish;
                        horiz = slide_dir * std::clamp(horiz_speed, slide_min_speed, slide_flat_cap);
                    } else {
                        Vector3 slide_dir = wish.length_squared() > 0.0f ? wish : (horiz / horiz_speed);
                        float boosted = std::min(horiz_speed + slide_boost, slide_flat_cap);
                        boosted = std::max(boosted, slide_min_speed + slide_boost * 0.5f);
                        horiz = slide_dir * boosted;
                    }
                    horiz_speed = horiz.length();
                } else if (crouch_held) {
                    slide_resume_pending = false;
                    crouching = true;
                } else if (crouching && !collides_with_height(p, height)) {
                    slide_resume_pending = false;
                    crouching = false;
                } else {
                    slide_resume_pending = false;
                }
            }
        } else {
            // In air: keep crouch stance if held or forced by ceiling, exit active slide state
            if (sliding) {
                sliding = false;
                slide_cooldown = 0.45f;
                if (crouch_held)
                    slide_resume_pending = true;
            }
            if (crouch_held)
                crouching = true;
            else if (crouching && !collides_with_height(p, height))
                crouching = false;
        }

        // Update physical collision height
        float target_height = sliding ? slide_height : (crouching ? crouch_height : height);
        if (target_height < current_height || !collides_with_height(p, target_height))
            current_height = target_height;

        // Horizontal movement with momentum preservation & anti-bunny-hop caps
        if (grounded) {
            if (sliding) {
                state = MovementState::Slide;
                if (horiz_speed > 0.1f) {
                    Vector3 dir = horiz / horiz_speed;
                    // Allow subtle carving with A/D while sliding
                    if (wish.length_squared() > 0.0f) {
                        dir = (dir + wish * (2.2f * dt)).normalized();
                    }

                    // Measure ground slope to reward downhill slides and resist uphill slides
                    float y_front = probe_floor_y(p, p.x + dir.x * 0.4f, p.z + dir.z * 0.4f);
                    float y_back = probe_floor_y(p, p.x - dir.x * 0.4f, p.z - dir.z * 0.4f);
                    float slope = std::clamp((y_front - y_back) / 0.8f, -1.0f, 1.0f);

                    if (slope < -0.04f) {
                        horiz_speed += (-slope) * gravity * 0.55f * dt;
                    } else if (slope > 0.04f) {
                        horiz_speed -= slope * gravity * 1.15f * dt;
                    } else {
                        horiz_speed = std::max(0.0f, horiz_speed - 3.6f * dt);
                    }
                    if (foliage_slow > 0.01f)
                        horiz_speed = std::max(0.0f, horiz_speed - 14.0f * foliage_slow * dt);
                    horiz_speed = std::clamp(horiz_speed, 0.0f, slide_max_speed);
                    horiz = dir * horiz_speed;
                }
            } else {
                const bool sprinting = shift && !crouching && !stamina_exhausted && stamina > 0.0f &&
                                       forward_input <= 0.1f && wish.length_squared() > 0.0f;
                const float target_speed = (crouching ? crouch_speed : (sprinting ? run_speed : walk_speed)) * foliage_move_scale;
                state = crouching ? MovementState::Crouch : (sprinting ? MovementState::Sprint : MovementState::Walk);

                if (sprinting && horiz_speed > walk_speed * 0.65f) {
                    stamina_used_now = true;
                    if (!consume_stamina(15.5f * dt)) {
                        state = MovementState::Walk;
                    }
                }

                if (wish.length_squared() > 0.0f) {
                    if (horiz_speed > target_speed + 0.25f) {
                        // Preserve momentum above target speed while bleeding excess smoothly (not instant snap!)
                        float bled_speed = std::max(target_speed, horiz_speed - 12.0f * dt);
                        Vector3 steered = (horiz / horiz_speed).lerp(wish, 1.0f - std::exp(-8.0f * dt)).normalized();
                        horiz = steered * bled_speed;
                    } else {
                        Vector3 desired = wish * target_speed;
                        float accel_rate = crouching ? 10.0f : 14.5f;
                        horiz = horiz.lerp(desired, 1.0f - std::exp(-accel_rate * dt));
                    }
                } else {
                    // Momentum deceleration: smooth glide to stop without feeling slippery
                    float stop_rate = crouching ? 12.0f : 15.0f;
                    horiz = horiz.lerp(Vector3(), 1.0f - std::exp(-stop_rate * dt));
                    if (horiz.length_squared() < 0.002f)
                        horiz = Vector3();
                }
            }
        } else {
            state = MovementState::Air;
            // Air control without bunny-hop speed stacking:
            // Only accelerate in wish direction up to air_cap; preserve pre-existing momentum above cap with gentle drag.
            float air_cap = ((shift && !crouching && !stamina_exhausted) ? run_speed : walk_speed) * foliage_move_scale;
            if (wish.length_squared() > 0.0f) {
                float current_proj = horiz.dot(wish);
                float add_speed = air_cap - current_proj;
                if (add_speed > 0.0f) {
                    float accel_step = std::min(18.0f * dt, add_speed);
                    horiz += wish * accel_step;
                }
            }
            if (touching_foliage && velocity.y < 0.0f) {
                velocity.y *= std::max(0.0f, 1.0f - 2.8f * foliage_slow * dt);
            }
            // Gentle aerial momentum drag when above normal sprint speed to prevent exploits
            const float active_drag_cap = touching_foliage ? air_cap : run_speed;
            float new_speed = horiz.length();
            if (new_speed > active_drag_cap) {
                const float drag_rate = touching_foliage ? 5.5f : 0.35f;
                float dragged = std::max(active_drag_cap, new_speed * std::max(0.0f, 1.0f - drag_rate * dt));
                horiz = horiz * (dragged / new_speed);
            }
        }

        velocity.x = horiz.x;
        velocity.z = horiz.z;
        velocity.y -= gravity * dt;

        // Jump execution (coyote + buffered ground jump, plus controlled 2nd air jump)
        const bool can_spend_jump_stamina = god_mode || (!stamina_exhausted && stamina > 0.0f);
        if (jump_buffer_timer > 0.0f && coyote_timer > 0.0f && can_spend_jump_stamina) {
            if (!god_mode) {
                consume_stamina(12.0f);
                stamina_used_now = true;
            }
            velocity.y = jump_speed;
            jumps = 1;
            grounded = false;
            coyote_timer = 0.0f;
            jump_buffer_timer = 0.0f;
            if (sliding) {
                sliding = false;
                slide_cooldown = 0.55f;
                if (crouch_held)
                    slide_resume_pending = true;
                // Cap slide-jump carryover so momentum feels great without bunny-hop abuse
                Vector3 carry = horizontal_part(velocity);
                float carry_speed = carry.length();
                float max_slide_jump = run_speed + slide_boost * 0.7f;
                if (carry_speed > max_slide_jump) {
                    carry = carry * (max_slide_jump / carry_speed);
                    velocity.x = carry.x;
                    velocity.z = carry.z;
                }
            }
        } else if (space_pressed && !grounded && jumps < 2 && can_spend_jump_stamina) {
            if (!god_mode) {
                consume_stamina(15.0f);
                stamina_used_now = true;
            }
            velocity.y = jump_speed;
            jumps++;
            jump_buffer_timer = 0.0f;
        }
        } // end !climbing && !mantling
    }
    jump_held = space;

    if (!noclip && stream_ready && !stream_ready(p + velocity * dt)) {
        velocity = Vector3();
        return;
    }

    if (lstate.in_lava || (lstate.in_water && !swimming)) {
        velocity += lstate.current * dt;
        if (!flying) {
            if (lstate.in_lava) {
                velocity.x *= std::max(0.0f, 1.0f - 4.2f * dt);
                velocity.z *= std::max(0.0f, 1.0f - 4.2f * dt);
                velocity.y += gravity * 0.68f * dt;
                if (space)
                    velocity.y = std::min(velocity.y + jump_speed * 1.3f * dt, jump_speed * 0.7f);
            } else {
                // Shallow wading drag when ankle/knee deep without full swimming
                const float wade_drag = 1.0f + lstate.submergence * 2.2f;
                velocity.x *= std::max(0.0f, 1.0f - wade_drag * dt);
                velocity.z *= std::max(0.0f, 1.0f - wade_drag * dt);
            }
        }
    }

    const bool was_grounded = grounded;
    const float pre_impact_vy = velocity.y;

    Vector3 move = velocity * dt;
    int steps = std::max(1, int(std::ceil(move.length() / .3f)));
    steps = std::min(steps, 128);
    move /= steps;
    grounded = false;
    for (int i = 0; i < steps; i++)
        for (int axis = 0; axis < 3; axis++) {
            Vector3 next = p;
            next[axis] += move[axis];
            if (!collides(next))
                p = next;
            else {
                bool stepped = false;
                if (!flying && !climbing && (!swimming || !crouch_held) && axis != 1 && velocity.y <= (swimming ? 1.5f : 0.0f)) {
                    for (float rise = .0625f; rise <= .5625f; rise += .0625f) {
                        Vector3 up = next + Vector3(0, rise, 0);
                        if (!collides(up) && !collides(p + Vector3(0, rise, 0))) {
                            const float before_step_y = p.y;
                            p = up;
                            cam_step_offset = std::clamp(cam_step_offset - (p.y - before_step_y), -0.75f, 0.75f);
                            stepped = true;
                            break;
                        }
                    }
                }
                if (stepped)
                    continue;
                // Resolve contact to millimetres instead of leaving a whole movement substep gap.
                Vector3 low = p, high = next;
                for (int j = 0; j < 10; j++) {
                    Vector3 mid = (low + high) * .5f;
                    if (collides(mid))
                        high = mid;
                    else
                        low = mid;
                }
                p = low;
                if (axis == 1 && move.y < 0)
                    grounded = true;
                velocity[axis] = 0;
                move[axis] = 0;
            }
        }
    // Floor snap: stay glued to ramps and rounded voxel slopes when moving downhill instead of micro-falling
    if (!flying && !climbing && !swimming && !noclip && was_grounded && !grounded && velocity.y <= 0.0f) {
        Vector3 snap_probe = p - Vector3(0.0f, 0.65f, 0.0f);
        if (collides(snap_probe)) {
            const float before_snap_y = p.y;
            Vector3 low = p, high = snap_probe;
            for (int j = 0; j < 10; j++) {
                Vector3 mid = (low + high) * 0.5f;
                if (collides(mid))
                    high = mid;
                else
                    low = mid;
            }
            p = low;
            cam_step_offset = std::clamp(cam_step_offset - (p.y - before_snap_y), -0.75f, 0.75f);
            grounded = true;
            velocity.y = 0.0f;
        }
    }

    if (climbing && grounded) {
        climbing = false;
        state = MovementState::Walk;
    }

    p.x = std::clamp(p.x, -999990.f, 999990.f);
    p.z = std::clamp(p.z, -999990.f, 999990.f);
    p.y = std::clamp(p.y, minimum_y, 3000.f);
    set_position(p);

    if (!flying && !swimming && !was_grounded && grounded && pre_impact_vy < -3.5f) {
        const float land_horiz_speed = Vector2(velocity.x, velocity.z).length();
        const bool preserve_slide_landing = slide_resume_pending && crouch_held && land_horiz_speed >= slide_min_speed;
        if (!sliding && !preserve_slide_landing) {
            float fall_factor = std::clamp((-pre_impact_vy - 4.0f) / 18.0f, 0.0f, 0.55f);
            float damp = 1.0f - fall_factor;
            if (wish.length_squared() == 0.0f)
                damp *= 0.65f;
            velocity.x *= damp;
            velocity.z *= damp;
        }
        float impact = std::clamp((-pre_impact_vy - 3.5f) / 18.0f, 0.0f, 1.0f);
        land_dip = std::min(land_dip, -0.09f * impact);
        land_pitch = std::min(land_pitch, -0.014f * impact);
    }

    const float final_horiz_speed = Vector2(velocity.x, velocity.z).length();
    update_stamina_regen(dt, stamina_used_now, final_horiz_speed);
    float slide_bank_sign = 1.0f;
    if (sliding && final_horiz_speed > 0.1f) {
        Vector3 right = get_basis().get_column(0);
        slide_bank_sign = right.dot(horizontal_part(velocity)) >= 0.0f ? -1.0f : 1.0f;
    }
    update_camera_feel(dt, final_horiz_speed, strafe_input, slide_bank_sign);

    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs) {
        rs->global_shader_parameter_set("sgt_player_position", p / 50.0f);
        rs->global_shader_parameter_set("sgt_player_mov", velocity * dt / 50.0f);
    }

    if (!flying && !sliding && !swimming && grounded && final_horiz_speed > 1.2f) {
        float step_interval = state == MovementState::Sprint ? 0.29f : (crouching ? 0.52f : 0.43f);
        footstep_timer += dt;
        if (footstep_timer >= step_interval) {
            footstep_timer = 0;
            if (footstep_player && footstep_sounds.size() > 0) {
                Ref<AudioStream> sound = footstep_sounds[footstep_index];
                footstep_index = (footstep_index + 1) % footstep_sounds.size();
                footstep_player->call("do_set_stream", sound);
                footstep_player->call("do_play");
            }
        }
    } else {
        footstep_timer = 0.25f;
    }
}

} // namespace godot
