#pragma once
#include <algorithm>
#include <functional>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {
class FlyingPlayer : public Node3D {
    GDCLASS(FlyingPlayer, Node3D)

  public:
    struct LiquidState {
        bool in_water = false;
        bool in_lava = false;
        float submergence = 0.0f;
        float surface_y = 0.0f;
        bool has_surface = false;
        Vector3 current;
    };

    enum class MovementState {
        Walk,
        Sprint,
        Crouch,
        Slide,
        Air,
        Climb,
        Mantle,
        Swim,
        Fly,
    };

  private:
    Camera3D *camera = nullptr;
    AudioStreamPlayer3D *footstep_player = nullptr;
    Array footstep_sounds;
    float footstep_timer = 0;
    int footstep_index = 0;
    std::function<bool(Vector3, Vector3)> solid;
    std::function<Vector3(Vector3, Vector3, bool &)> liquid_query;
    std::function<LiquidState(Vector3, Vector3)> liquid_state_query;
    std::function<bool(Vector3, Vector3)> foliage_query;
    std::function<bool(Vector3)> stream_ready;

    Vector3 velocity;
    float pitch = 0, sensitivity = .0022f, height = 1.8f, width = .6f, minimum_y = -510.0f;
    float eye_height = 1.65f, crouch_height = 1.15f, crouch_eye_height = 1.0f;
    float slide_height = 0.95f, slide_eye_height = 0.78f;
    float swim_height = 1.35f, swim_eye_height = 1.12f;
    float current_height = 1.8f, current_eye_height = 1.65f;

    float walk_speed = 6.5f, run_speed = 11.5f, crouch_speed = 3.6f, fly_speed = 20.0f;
    float swim_speed = 5.6f, swim_sprint_speed = 9.4f, swim_vertical_speed = 5.2f;
    float sprint = 2.5f, jump_speed = 10.0f, gravity = 28.0f;
    float slide_boost = 3.5f, slide_min_speed = 7.2f, slide_exit_speed = 4.6f;
    float slide_flat_cap = 15.0f, slide_max_speed = 21.0f;
    float slide_cooldown = 0.0f, slide_timer = 0.0f;

    float base_fov = 80.0f, current_fov = 80.0f;
    float cam_roll = 0.0f, land_dip = 0.0f, land_pitch = 0.0f, cam_step_offset = 0.0f;
    float cam_base_x = 0.0f, cam_base_y = 1.65f;
    // Short tool-impact kick: decays quickly, adds a tiny pitch/roll jolt on hit.
    float impact_kick = 0.0f, impact_kick_sign = 1.0f;
    float bob_phase = 0.0f, bob_weight = 0.0f;
    float coyote_timer = 0.0f, jump_buffer_timer = 0.0f;

    // Stamina (BOTW-style)
    float max_stamina = 100.0f, stamina = 100.0f, stamina_regen_delay = 0.0f;
    bool stamina_exhausted = false;

    // Wall climb & ledge mantle
    bool climbing = false, mantling = false;
    Vector3 wall_normal, mantle_start, mantle_end;
    float climb_cooldown = 0.0f, mantle_timer = 0.0f, mantle_duration = 0.20f;

    bool active = false, flying = false, grounded = false, jump_held = false;
    bool crouching = false, sliding = false, crouch_was_held = false, slide_resume_pending = false;
    bool swimming = false, in_water = false, underwater = false;
    float water_submergence = 0.0f;
    bool in_foliage = false;
    float foliage_slow = 0.0f;
    bool noclip = false, god_mode = false, riding = false;
    float default_fly_speed = 20.0f, default_gravity = 28.0f;
    int jumps = 0;
    MovementState state = MovementState::Walk;

    bool collides_with_height(Vector3 position, float h) const;
    bool collides(Vector3 position) const;
    float probe_floor_y(Vector3 p, float px, float pz) const;
    bool probe_wall(Vector3 p, Vector3 &out_normal) const;
    bool try_find_ledge(Vector3 p, Vector3 wall_dir, Vector3 &out_land_pos, bool from_climb) const;
    bool consume_stamina(float amount);
    void update_stamina_regen(float dt, bool consuming_now, float horiz_speed);
    void update_camera_feel(float dt, float horiz_speed, float strafe_input, float slide_bank_sign);

  protected:
    static void _bind_methods();

  public:
    void add_impact_kick(float strength) {
        impact_kick = std::max(impact_kick, std::clamp(strength, 0.0f, 1.0f));
        impact_kick_sign = -impact_kick_sign;
    }
    void configure(const Dictionary &definition, std::function<bool(Vector3, Vector3)> query);
    void set_liquid_query(std::function<Vector3(Vector3, Vector3, bool &)> query) {
        liquid_query = std::move(query);
    }
    void set_liquid_state_query(std::function<LiquidState(Vector3, Vector3)> query) {
        liquid_state_query = std::move(query);
    }
    void set_foliage_query(std::function<bool(Vector3, Vector3)> query) {
        foliage_query = std::move(query);
    }
    void set_stream_query(std::function<bool(Vector3)> query) {
        stream_ready = std::move(query);
    }
    void set_minimum_y(float value) {
        minimum_y = value;
    }
    void _ready() override;
    void _process(double delta) override;
    void _physics_process(double delta) override;
    void _unhandled_input(const Ref<InputEvent> &event) override;
    void set_active(bool value) {
        active = value;
        jump_held = true;
        crouch_was_held = true;
        slide_resume_pending = false;
        climbing = false;
        mantling = false;
        swimming = false;
        in_water = false;
        underwater = false;
        water_submergence = 0.0f;
        in_foliage = false;
        foliage_slow = 0.0f;
        velocity = Vector3();
        cam_step_offset = 0.0f;
        cam_base_x = 0.0f;
        cam_base_y = current_eye_height;
    }
    bool is_flying() const {
        return flying;
    }
    // While riding a mount the entity layer owns the position; only mouse look stays active.
    void set_riding(bool value) {
        riding = value;
        velocity = Vector3();
        grounded = false;
        jump_held = true;
        crouch_was_held = true;
    }
    bool is_riding() const {
        return riding;
    }
    bool is_grounded() const {
        return grounded;
    }
    bool is_crouching() const {
        return crouching;
    }
    bool is_sliding() const {
        return sliding;
    }
    bool is_climbing() const {
        return climbing;
    }
    bool is_mantling() const {
        return mantling;
    }
    bool is_swimming() const {
        return swimming;
    }
    bool is_in_water() const {
        return in_water;
    }
    bool is_underwater() const {
        return underwater;
    }
    bool is_in_foliage() const {
        return in_foliage;
    }
    float get_foliage_factor() const {
        return foliage_slow;
    }
    Vector3 get_wall_normal() const {
        return wall_normal;
    }
    Vector3 get_mantle_end() const {
        return mantle_end;
    }
    float get_stamina() const {
        return stamina;
    }
    float get_max_stamina() const {
        return max_stamina;
    }
    float get_stamina_ratio() const {
        return max_stamina > 0.0f ? std::clamp(stamina / max_stamina, 0.0f, 1.0f) : 1.0f;
    }
    bool is_stamina_exhausted() const {
        return stamina_exhausted;
    }
    int get_jump_count() const {
        return jumps;
    }
    void set_flying(bool value);
    void set_speed(float value) {
        fly_speed = value;
    }
    float get_speed() const {
        return fly_speed;
    }
    float get_horizontal_speed() const {
        return Vector2(velocity.x, velocity.z).length();
    }
    String get_movement_state_name() const;
    bool is_sprinting() const { return state == MovementState::Sprint; }
    bool is_noclip() const {
        return noclip;
    }
    void set_noclip(bool value) {
        noclip = value;
    }
    bool is_god() const {
        return god_mode;
    }
    void set_god(bool value) {
        god_mode = value;
    }
    float get_gravity() const {
        return gravity;
    }
    void set_gravity(float value) {
        gravity = value;
    }
    void reset_debug_parameters();
    Camera3D *get_camera() const {
        return camera;
    }
    float get_pitch() const {
        return pitch;
    }
    void set_view(float yaw, float p);
    bool contains_block(int x, int y, int z) const;
    AABB body_bounds() const {
        return AABB(get_position() + Vector3(-width * .5f, .001f, -width * .5f),
                    Vector3(width, current_height - .002f, width));
    }
};
} // namespace godot
