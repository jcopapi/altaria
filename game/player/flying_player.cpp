#include "flying_player.h"
#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/math.hpp>
namespace godot {
void FlyingPlayer::configure(const Dictionary &d, std::function<bool(Vector3, Vector3)> q) {
    solid = std::move(q);
    height = std::clamp(float(d.get("height", 1.8)), .5f, 4.f);
    width = std::clamp(float(d.get("width", .6)), .2f, 2.f);
    walk_speed = std::clamp(float(d.get("walk_speed", 6)), 1.f, 40.f);
    fly_speed = std::clamp(float(d.get("fly_speed", 20)), 1.f, 150.f);
    sprint = std::clamp(float(d.get("sprint_multiplier", 3)), 1.f, 5.f);
    jump_speed = std::clamp(float(d.get("jump_speed", 10)), 1.f, 30.f);
    gravity = std::clamp(float(d.get("gravity", 28)), 1.f, 100.f);
    sensitivity = std::clamp(float(d.get("mouse_sensitivity", .0022)), .0001f, .02f);
    if (!camera) {
        camera = memnew(Camera3D);
        camera->set_position(Vector3(0, std::clamp(float(d.get("eye_height", 1.65)), .2f, height), 0));
        camera->set_near(.08);
        camera->set_far(6000);
        camera->set_fov(80);
        add_child(camera);
    }
}
void FlyingPlayer::_ready() {
    if (camera)
        camera->make_current();
}
void FlyingPlayer::set_view(float yaw, float p) {
    pitch = std::clamp(p, -1.55f, 1.55f);
    set_rotation(Vector3(0, yaw, 0));
    if (camera)
        camera->set_rotation(Vector3(pitch, 0, 0));
}
void FlyingPlayer::_unhandled_input(const Ref<InputEvent> &event) {
    if (!active || Input::get_singleton()->get_mouse_mode() != Input::MOUSE_MODE_CAPTURED)
        return;
    Ref<InputEventMouseMotion> motion = event;
    if (motion.is_valid()) {
        auto m = motion->get_relative();
        rotate_y(-m.x * sensitivity);
        pitch = std::clamp(pitch - m.y * sensitivity, -1.55f, 1.55f);
        camera->set_rotation(Vector3(pitch, 0, 0));
    }
}
bool FlyingPlayer::collides(Vector3 p) const {
    if (!solid)
        return false;
    float r = width * .5f - .001f;
    return solid(p + Vector3(-r, .001f, -r), p + Vector3(r, height - .001f, r));
}
bool FlyingPlayer::contains_block(int x, int y, int z) const {
    auto p = get_position();
    return x + 1 > p.x - width * .5f && x < p.x + width * .5f && y + 1 > p.y && y < p.y + height &&
           z + 1 > p.z - width * .5f && z < p.z + width * .5f;
}
void FlyingPlayer::_physics_process(double raw_delta) {
    if (!active)
        return;
    float dt = std::min(float(raw_delta), .05f);
    auto in = Input::get_singleton();
    Vector3 wish(
        float(in->is_physical_key_pressed(Key::KEY_D)) - float(in->is_physical_key_pressed(Key::KEY_A)), 0,
        float(in->is_physical_key_pressed(Key::KEY_S)) - float(in->is_physical_key_pressed(Key::KEY_W)));
    wish = get_basis().xform(wish);
    if (wish.length_squared() > 0)
        wish = wish.normalized();
    bool space = in->is_physical_key_pressed(Key::KEY_SPACE);
    float speed =
        (flying ? fly_speed : walk_speed) * (in->is_physical_key_pressed(Key::KEY_SHIFT) ? sprint : 1);
    if (flying) {
        wish.y = float(space) - float(in->is_physical_key_pressed(Key::KEY_CTRL));
        if (wish.length_squared() > 1)
            wish = wish.normalized();
        velocity = velocity.lerp(wish * speed, 1 - std::exp(-12 * dt));
    } else {
        velocity.x = wish.x * speed;
        velocity.z = wish.z * speed;
        velocity.y -= gravity * dt;
        if (grounded)
            jumps = 0;
        if (space && !jump_held && jumps < 2) {
            velocity.y = jump_speed;
            jumps++;
            grounded = false;
        }
    }
    jump_held = space;
    Vector3 p = get_position();
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
                if (!flying && axis != 1 && velocity.y <= 0) {
                    for (float rise = .0625f; rise <= .5625f; rise += .0625f) {
                        Vector3 up = next + Vector3(0, rise, 0);
                        if (!collides(up) && !collides(p + Vector3(0, rise, 0))) {
                            p = up;
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
    p.x = std::clamp(p.x, -999990.f, 999990.f);
    p.z = std::clamp(p.z, -999990.f, 999990.f);
    p.y = std::clamp(p.y, -510.f, 3000.f);
    set_position(p);
}
} // namespace godot
