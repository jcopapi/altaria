#pragma once
#include "environment_core.h"
#include "voxel_world.h"
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/gpu_particles3d.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/compositor.hpp>
#include <godot_cpp/classes/compositor_effect.hpp>
namespace godot {
class OmniLight3D;
class WorldAtmosphere : public Node3D {
    GDCLASS(WorldAtmosphere, Node3D)
    VoxelWorld *world = nullptr;
    WorldEnvironment *node = nullptr;
    DirectionalLight3D *sun = nullptr, *moon = nullptr;
    OmniLight3D *following_light = nullptr;
    GPUParticles3D *embers = nullptr; // ash and embers drifting in the underworld
    Ref<ShaderMaterial> sky, cartoon;
    Ref<Compositor> compositor;
    Ref<CompositorEffect> lens_flare_effect;
    SubViewport *render_view = nullptr;
    Camera3D *render_camera = nullptr, *source_camera = nullptr;
    TextureRect *presentation = nullptr;
    Color sky_zenith;
    Vector3 sun_direction;
    float godrays_visibility=0;
    // 1 under open sky, 0 in a cave; eases between them so entering a cave
    // dims the sun and fog instead of switching them off in one frame.
    float outdoor=1;
    // 1 inside the open underworld layer, eased like outdoor.
    float underworld=0;
    // 0 at surface/mouth .. 1 in deep pitch-black caves (eased smoothly).
    float cave_darkness=0;
    float compute_cave_darkness_target() const;
    // underwater is instantaneous; lava eases smoothly.
    float underwater=0, in_lava=0;
    void update_submersion(float delta);
    bool outdoor_initialized=false;
    altaria::FogSample fog;
    bool fog_initialized = false, fog_override = false;
    altaria::FogSample manual_fog;
    altaria::EnvironmentSettings settings;
    altaria::Weather weather = altaria::Weather::Clear;
    float hour = 9, wind = .7f, elapsed = 0, update_timer = 1;
    bool running = true;
    Array lights;
    void update_lighting(float delta = .1f);
    void update_render_view();

  protected:
    static void _bind_methods() {}

  public:
    bool configure(VoxelWorld *world, const Dictionary &config, const Dictionary &saved, String &error);
    void _process(double delta) override;
    const altaria::EnvironmentSettings &get_settings() const {
        return settings;
    }
    void attach_camera(Camera3D *camera);
    bool set_fog(Color color, float start, float end);
    void reset_fog() {
        fog_override = false;
        fog_initialized = false;
        update_timer = 1;
    }
    void set_outline(bool value) {
        settings.outline_enabled = value;
        update_timer = 1;
    }
    void set_pixelation(bool value) {
        settings.pixelation_enabled = value;
        update_timer = 1;
    }
    void set_godrays(bool value) { settings.godrays_enabled=value; update_timer=1; }
    bool set_hour(float value);
    void set_running(bool value) {
        running = value;
    }
    bool set_weather(const String &value);
    bool set_wind(float value);
    void add_light(Vector3 position, Color color, float energy, float radius);
    bool is_following_light_on() const;
    void set_loading_world(bool loading);
    void set_following_light(bool enabled, std::optional<Color> color = {},
                             std::optional<float> energy = {}, std::optional<float> radius = {});
    Color sample_hand_light(Vector3 position) const;
    Dictionary sample_temperature(Vector3 position) const;
    Dictionary state() const;
    Dictionary save() const;
};
} // namespace godot
