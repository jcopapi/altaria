#pragma once
#include <functional>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
namespace godot {
class FlyingPlayer : public Node3D {
    GDCLASS(FlyingPlayer, Node3D)
    Camera3D *camera = nullptr;
    std::function<bool(Vector3, Vector3)> solid;
    Vector3 velocity;
    float pitch = 0, sensitivity = .0022f, height = 1.8f, width = .6f, walk_speed = 6, fly_speed = 20,
          sprint = 3, jump_speed = 10, gravity = 28;
    bool active = false, flying = true, grounded = false, jump_held = false;
    int jumps = 0;
    bool collides(Vector3 position) const;

  protected:
    static void _bind_methods() {}

  public:
    void configure(const Dictionary &definition, std::function<bool(Vector3, Vector3)> query);
    void _ready() override;
    void _physics_process(double delta) override;
    void _unhandled_input(const Ref<InputEvent> &event) override;
    void set_active(bool value) {
        active = value;
        jump_held = true;
        velocity = Vector3();
    }
    bool is_flying() const {
        return flying;
    }
    int get_jump_count() const {
        return jumps;
    }
    bool is_grounded() const {
        return grounded;
    }
    void set_flying(bool value) {
        flying = value;
        velocity = Vector3();
        jumps = 0;
    }
    void set_speed(float value) {
        fly_speed = value;
    }
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
                    Vector3(width, height - .002f, width));
    }
};
} // namespace godot
