#pragma once
#include "environment_core.h"
#include "voxel_world.h"
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
namespace godot {
class WorldAtmosphere : public Node3D {
    GDCLASS(WorldAtmosphere, Node3D)
    VoxelWorld *world = nullptr;
    WorldEnvironment *node = nullptr;
    DirectionalLight3D *sun = nullptr, *moon = nullptr;
    Ref<ShaderMaterial> sky, cartoon;
    SubViewport *render_view = nullptr;
    Camera3D *render_camera = nullptr, *source_camera = nullptr;
    TextureRect *presentation = nullptr;
    Color sky_zenith;
    Vector3 sun_direction;
    float godrays_visibility=0;
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
    Dictionary sample_temperature(Vector3 position) const;
    Dictionary state() const;
    Dictionary save() const;
};
} // namespace godot
