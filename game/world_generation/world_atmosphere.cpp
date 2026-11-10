#include "world_atmosphere.h"
#include "world_data.h"
#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/sky.hpp>
namespace godot {
using namespace altaria;
bool WorldAtmosphere::configure(VoxelWorld *w, const Dictionary &d, const Dictionary &saved, String &error) {
    world = w;
    // Presentation preferences apply to existing worlds too. Generation,
    // climate and physics retain their saved snapshot.
    Dictionary visual = read_json("res://game/world_generation/data/environment.json", error);
    if (!error.is_empty())
        return false;
    Dictionary config = d.duplicate();
    for (const char *key : {"outline_width", "outline_strength", "outline_highlight", "render_pixel_size",
                            "pixelation_max", "pixelation_start", "pixelation_end", "sky_biome_strength",
                            "moon_energy", "night_ambient", "outline_blacklist_distance", "outline_distance_start", "outline_distance_end",
                            "outline_min_depth_gap", "outline_min_feature_size", "outline_shoreline_strength", "godrays_enabled",
                            "godrays_strength", "godrays_samples", "godrays_reach", "godrays_volumetric",
                            "godrays_density", "godrays_length", "godrays_light_energy"})
        if (visual.has(key))
            config[key] = visual[key];
    if (!parse_environment(config, settings, error))
        return false;
    hour = settings.start_hour;
    wind = settings.wind;
    if (saved.has("hour"))
        set_hour(saved["hour"]);
    running = saved.get("running", true);
    set_weather(saved.get("weather", "clear"));
    set_wind(saved.get("wind", wind));
    elapsed = saved.get("elapsed", 0);
    if (!std::isfinite(elapsed) || elapsed < 0)
        elapsed = 0;
    node = memnew(WorldEnvironment);
    Ref<Environment> env;
    env.instantiate();
    Ref<Sky> resource;
    resource.instantiate();
    resource->set_radiance_size(Sky::RADIANCE_SIZE_64);
    resource->set_process_mode(Sky::PROCESS_MODE_INCREMENTAL);
    Ref<Shader> shader =
        ResourceLoader::get_singleton()->load("res://game/world_generation/shaders/fantasy_sky.gdshader");
    if (shader.is_null()) {
        error = "Cannot load fantasy sky shader";
        return false;
    }
    sky.instantiate();
    sky->set_shader(shader);
    resource->set_material(sky);
    env->set_sky(resource);
    env->set_background(Environment::BG_SKY);
    env->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
    env->set_reflection_source(Environment::REFLECTION_SOURCE_DISABLED);
    env->set_tonemapper(Environment::TONE_MAPPER_FILMIC);
    env->set_ssao_enabled(true);
    env->set_ssao_radius(1.4);
    env->set_ssao_intensity(1.15);
    env->set_ssao_power(1.2);
    env->set_glow_enabled(true);
    env->set_glow_intensity(.35);
    env->set_glow_hdr_bleed_threshold(1.4);
    env->set_adjustment_enabled(true);
    env->set_adjustment_saturation(1.07);
    env->set_adjustment_contrast(1.03);
    env->set_fog_enabled(true);
    env->set_fog_mode(Environment::FOG_MODE_DEPTH);
    env->set_fog_depth_begin(std::min(world->settings().fog_start, world->settings().view_distance * .72f));
    env->set_fog_depth_end(world->settings().view_distance * .94f);
    env->set_fog_sky_affect(.12);
    env->set_volumetric_fog_density(settings.godrays_density);
    env->set_volumetric_fog_length(settings.godrays_length);
    env->set_volumetric_fog_albedo(Color(.96,.98,1));
    env->set_volumetric_fog_anisotropy(.72);
    env->set_volumetric_fog_gi_inject(0);
    env->set_volumetric_fog_ambient_inject(0);
    env->set_volumetric_fog_sky_affect(0);
    env->set_volumetric_fog_temporal_reprojection_enabled(true);
    env->set_volumetric_fog_temporal_reprojection_amount(.85);
    node->set_environment(env);
    add_child(node);
    sun = memnew(DirectionalLight3D);
    sun->set_name("Sun");
    sun->set_shadow(true);
    sun->set_param(Light3D::PARAM_SHADOW_MAX_DISTANCE, settings.shadow_distance);
    add_child(sun);
    moon = memnew(DirectionalLight3D);
    moon->set_name("Moon");
    moon->set_color(Color(.52, .64, 1.0));
    moon->set_shadow(false);
    add_child(moon);
    Array saved_lights = saved.get("lights", Array());
    for (int i = 0; i < std::min(int(saved_lights.size()), 16); i++) {
        Dictionary l = saved_lights[i];
        Array p = l.get("position", Array());
        String c = l.get("color", "#ffffff");
        if (p.size() == 3 && Color::html_is_valid(c))
            add_light(Vector3(p[0], p[1], p[2]), Color::html(c),
                      std::clamp(float(l.get("energy", 2)), .1f, 20.f),
                      std::clamp(float(l.get("radius", 12)), 1.f, 64.f));
    }
    settings.outline_enabled = saved.get("outline_enabled", settings.outline_enabled);
    settings.pixelation_enabled = saved.get("pixelation_enabled", settings.pixelation_enabled);
    settings.godrays_enabled = saved.get("godrays_enabled", settings.godrays_enabled);
    if (saved.has("fog_override")) {
        Dictionary f = saved["fog_override"];
        String color = f.get("color", "#ffffff");
        if (Color::html_is_valid(color))
            set_fog(Color::html(color), f.get("start", 1800), f.get("end", 5600));
    }
    update_lighting();
    return true;
}
void WorldAtmosphere::attach_camera(Camera3D *camera) {
    source_camera = camera;
    render_view = memnew(SubViewport);
    render_view->set_name("WorldRenderViewport");
    render_view->set_world_3d(camera->get_world_3d());
    render_view->set_update_mode(SubViewport::UPDATE_ALWAYS);
    render_view->set_disable_input(true);
    render_view->set_use_taa(false);
    render_view->set_screen_space_aa(Viewport::SCREEN_SPACE_AA_DISABLED);
    render_view->set_msaa_3d(Viewport::MSAA_DISABLED);
    add_child(render_view);
    render_camera = memnew(Camera3D);
    render_camera->set_name("WorldRenderCamera");
    render_view->add_child(render_camera);
    render_camera->make_current();
    auto layer = memnew(CanvasLayer);
    layer->set_name("WorldPresentation");
    layer->set_layer(1);
    add_child(layer);
    presentation = memnew(TextureRect);
    presentation->set_name("NearestWorldImage");
    presentation->set_texture_filter(CanvasItem::TEXTURE_FILTER_NEAREST);
    presentation->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    presentation->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
    presentation->set_stretch_mode(TextureRect::STRETCH_SCALE);
    presentation->set_texture(render_view->get_texture());
    layer->add_child(presentation);
    // Simulation, mouse look and UI stay in the original viewport/world. Only
    // rendering moves to the low-resolution camera, avoiding scaled input.
    camera->clear_current(false);
    update_render_view();
    auto node = memnew(MeshInstance3D);
    node->set_name("CartoonWorldPostProcess");
    Ref<QuadMesh> mesh;
    mesh.instantiate();
    mesh->set_size(Vector2(2, 2));
    cartoon.instantiate();
    Ref<Shader> shader =
        ResourceLoader::get_singleton()->load("res://game/world_generation/shaders/cartoon_world.gdshader");
    cartoon->set_shader(shader);
    cartoon->set_render_priority(-120);
    node->set_mesh(mesh);
    node->set_material_override(cartoon);
    node->set_extra_cull_margin(10000);
    node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    render_camera->add_child(node);
    node->set_position(Vector3(0, 0, -.2));
    cartoon->set_shader_parameter("outline_width", settings.outline_width);
    cartoon->set_shader_parameter("outline_strength", settings.outline_strength);
    cartoon->set_shader_parameter("outline_highlight", settings.outline_highlight);
    cartoon->set_shader_parameter("outline_distance_start",settings.outline_distance_start);
    cartoon->set_shader_parameter("outline_distance_end",settings.outline_distance_end);
    cartoon->set_shader_parameter("outline_min_depth_gap",settings.outline_min_depth_gap);
    cartoon->set_shader_parameter("outline_min_feature_size",settings.outline_min_feature_size);
    cartoon->set_shader_parameter("outline_shoreline_strength",settings.outline_shoreline_strength);
    cartoon->set_shader_parameter("outline_blacklist_distance", settings.outline_blacklist_distance);
    cartoon->set_shader_parameter("godrays_strength", settings.godrays_strength);
    cartoon->set_shader_parameter("godrays_samples", settings.godrays_samples);
    cartoon->set_shader_parameter("godrays_reach", settings.godrays_reach);
    cartoon->set_shader_parameter("pixelation_start", settings.pixelation_start);
    cartoon->set_shader_parameter("pixelation_end", settings.pixelation_end);
    cartoon->set_shader_parameter("pixelation_max", settings.pixelation_max);
    update_lighting();
}
void WorldAtmosphere::update_render_view() {
    if (!render_view || !source_camera)
        return;
    Vector2 output = source_camera->get_viewport()->get_visible_rect().size;
    int divisor = settings.pixelation_enabled ? std::max(1, int(std::round(settings.render_pixel_size))) : 1;
    Vector2i size(std::max(2, int(std::ceil(output.x / divisor))),
                 std::max(2, int(std::ceil(output.y / divisor))));
    if (render_view->get_size() != size)
        render_view->set_size(size);
    presentation->set_size(output);
    render_camera->set_global_transform(source_camera->get_global_transform());
    render_camera->set_fov(source_camera->get_fov());
    render_camera->set_near(source_camera->get_near());
    render_camera->set_far(source_camera->get_far());
    render_camera->set_keep_aspect_mode(source_camera->get_keep_aspect_mode());
    render_camera->set_cull_mask(source_camera->get_cull_mask());
    if(cartoon.is_valid()) {
        Vector3 point=source_camera->get_global_position()+sun_direction*1000;
        Vector2 uv=source_camera->unproject_position(point)/output;
        float visibility=source_camera->is_position_behind(point) ? 0.f : godrays_visibility;
        cartoon->set_shader_parameter("sun_screen",uv);
        cartoon->set_shader_parameter("godrays_visibility",visibility);
    }
}
bool WorldAtmosphere::set_fog(Color color, float start, float end) {
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end <= start + 1 || end > 20000)
        return false;
    manual_fog = {{color.r, color.g, color.b}, start, end};
    fog_override = true;
    fog_initialized = false;
    update_timer = 1;
    return true;
}
void WorldAtmosphere::update_lighting(float delta) {
    float angle = (hour - 6) * 6.28318530718f / 24;
    Vector3 direction = Vector3(std::cos(angle), std::sin(angle), -.35f).normalized();
    sun_direction=direction;
    float day = std::clamp((direction.y + .10f) / .40f, 0.f, 1.f);
    float dusk = 1 - std::clamp(std::abs(direction.y) / .38f, 0.f, 1.f);
    auto position = world->viewer_position();
    auto target = fog_override
                      ? manual_fog
                      : biome_fog(world->settings(), world->last_seed(), position.x, position.z, day);
    if (!fog_override) {
        float scale = weather == Weather::Storm ? .65f : weather == Weather::Rain ? .85f : 1;
        target.start *= scale;
        target.end *= scale;
    }
    target.end = std::min(target.end, world->settings().view_distance * .94f);
    target.start = std::min(target.start, target.end - 1);
    float blend =
        fog_initialized && settings.fog_transition > 0 ? 1 - std::exp(-delta / settings.fog_transition) : 1;
    fog.color = {fog.color.r + (target.color.r - fog.color.r) * blend,
                 fog.color.g + (target.color.g - fog.color.g) * blend,
                 fog.color.b + (target.color.b - fog.color.b) * blend};
    fog.start += (target.start - fog.start) * blend;
    fog.end += (target.end - fog.end) * blend;
    fog_initialized = true;
    Color horizon(fog.color.r, fog.color.g, fog.color.b);
    // Tint the whole sky dome, not just the narrow horizon band. Keep a dark,
    // readable night palette while allowing each biome's hue to remain visible.
    Color palette = horizon;
    float value = std::max({palette.r, palette.g, palette.b, .001f});
    palette = Color(palette.r / value, palette.g / value, palette.b / value);
    Color themed = Color(palette.r * (.09f + day * .37f), palette.g * (.09f + day * .37f),
                         palette.b * (.14f + day * .48f));
    sky_zenith = Color(.025, .03, .09).lerp(Color(.16, .37, .67), day)
                      .lerp(themed, settings.sky_biome_strength);
    sun->look_at_from_position(Vector3(), -direction, Vector3(0, 0, 1));
    moon->look_at_from_position(Vector3(), direction, Vector3(0, 0, 1));
    float overcast = weather == Weather::Storm ? .42f : weather == Weather::Rain ? .7f : 1;
    godrays_visibility=day*std::sqrt(std::max(0.f,direction.y))*(.65f+.9f*dusk)*overcast;
    sun->set_color(Color(1, .94, .82).lerp(Color(1, .57, .32), dusk));
    sun->set_param(Light3D::PARAM_ENERGY, std::max(0.f, direction.y) * settings.sun_energy * overcast);
    sun->set_shadow(!world->is_fullbright());
    moon->set_param(Light3D::PARAM_ENERGY, (1 - day) * settings.moon_energy);
    auto env = node->get_environment();
    env->set_volumetric_fog_enabled(settings.godrays_enabled && settings.godrays_volumetric &&
                                    !world->is_fullbright() && day>.05f);
    sun->set_param(Light3D::PARAM_VOLUMETRIC_FOG_ENERGY,settings.godrays_light_energy);
    moon->set_param(Light3D::PARAM_VOLUMETRIC_FOG_ENERGY,0);
    env->set_ambient_light_color(Color(.40, .44, .69).lerp(Color(.66, .76, .91), day)
                                    .lerp(horizon, .15));
    env->set_ambient_light_energy(settings.night_ambient +
                                  day * (settings.day_ambient - settings.night_ambient));
    env->set_fog_light_color(horizon);
    env->set_fog_depth_begin(fog.start);
    env->set_fog_depth_end(fog.end);
    sky->set_shader_parameter("daylight", day);
    sky->set_shader_parameter("sun_direction", direction);
    sky->set_shader_parameter("cloud_time", elapsed);
    sky->set_shader_parameter("horizon_color", horizon);
    sky->set_shader_parameter("zenith_color", sky_zenith);
    sky->set_shader_parameter("biome_strength", settings.sky_biome_strength);
    sky->set_shader_parameter(
        "cloud_coverage", std::min(.85f, settings.cloud_coverage + (weather == Weather::Clear ? 0 : .22f)));
    sky->set_shader_parameter("cloud_pixel_size", settings.cloud_pixel_size);
    if (cartoon.is_valid()) {
        cartoon->set_shader_parameter("outlines", settings.outline_enabled);
        cartoon->set_shader_parameter("pixelation", settings.pixelation_enabled);
        cartoon->set_shader_parameter("fog_start", fog.start);
        cartoon->set_shader_parameter("fog_end", fog.end);
        cartoon->set_shader_parameter("key_light_direction", day > .5f ? direction : -direction);
        cartoon->set_shader_parameter("godrays",settings.godrays_enabled && !world->is_fullbright());
        cartoon->set_shader_parameter("godrays_color",sun->get_color());
    }
    world->set_visual_environment(horizon, elapsed, settings.wind_strength * wind, settings.grass_noise_scale,
                                  settings.grass_noise_strength, fog.start, fog.end);
}
void WorldAtmosphere::_process(double delta) {
    update_render_view();
    elapsed += delta;
    if (running)
        hour = std::fmod(hour + float(delta) * 24 / settings.day_seconds, 24.f);
    update_timer += delta;
    // Grass runs every frame; environment and radiance refresh at 10 Hz.
    if (update_timer >= .1f) {
        float dt = update_timer;
        update_timer = 0;
        update_lighting(dt);
    }
    world->set_visual_environment(node->get_environment()->get_fog_light_color(), elapsed,
                                  settings.wind_strength * wind, settings.grass_noise_scale,
                                  settings.grass_noise_strength, fog.start, fog.end);
}
bool WorldAtmosphere::set_hour(float v) {
    if (!std::isfinite(v) || v < 0 || v >= 24)
        return false;
    hour = v;
    fog_initialized = false;
    update_timer = 1;
    return true;
}
bool WorldAtmosphere::set_wind(float v) {
    if (!std::isfinite(v) || v < 0 || v > 3)
        return false;
    wind = v;
    update_timer = 1;
    return true;
}
bool WorldAtmosphere::set_weather(const String &v) {
    if (v == "clear")
        weather = Weather::Clear;
    else if (v == "rain")
        weather = Weather::Rain;
    else if (v == "storm")
        weather = Weather::Storm;
    else
        return false;
    update_timer = 1;
    return true;
}
void WorldAtmosphere::add_light(Vector3 p, Color c, float energy, float radius) {
    if (lights.size() >= 16)
        return;
    auto light = memnew(OmniLight3D);
    light->set_color(c);
    light->set_param(Light3D::PARAM_ENERGY, energy);
    light->set_param(Light3D::PARAM_RANGE, radius);
    light->set_shadow(false);
    light->set_position(p);
    add_child(light);
    Dictionary record;
    Array xyz;
    xyz.push_back(p.x);
    xyz.push_back(p.y);
    xyz.push_back(p.z);
    record["position"] = xyz;
    record["color"] = "#" + c.to_html(false);
    record["energy"] = energy;
    record["radius"] = radius;
    lights.push_back(record);
}
Dictionary WorldAtmosphere::sample_temperature(Vector3 p) const {
    auto t = temperature_at(*world->generator(), settings, {p.x, p.y, p.z}, hour, weather, wind,
                            [this](int x, int y, int z) { return world->block_at(x, y, z); });
    Dictionary out;
    out["ambient_c"] = t.ambient;
    out["block_delta_c"] = t.blocks;
    out["cold_delta_c"] = t.cold;
    out["warm_delta_c"] = t.warm;
    out["feels_like_c"] = t.feels_like;
    return out;
}
Dictionary WorldAtmosphere::state() const {
    Dictionary out;
    out["hour"] = hour;
    out["running"] = running;
    out["weather"] = weather == Weather::Clear ? "clear" : weather == Weather::Rain ? "rain" : "storm";
    out["wind"] = wind;
    out["wind_time"] = elapsed;
    out["colored_lights"] = lights.size();
    out["fog_color"] = Color(fog.color.r, fog.color.g, fog.color.b);
    out["fog_start"] = fog.start;
    out["fog_end"] = fog.end;
    out["fog_mode"] = fog_override ? "manual" : "biome";
    out["outline_enabled"] = settings.outline_enabled;
    out["pixelation_enabled"] = settings.pixelation_enabled;
    out["godrays_enabled"] = settings.godrays_enabled;
    out["sky_zenith"] = sky_zenith;
    if (render_view) {
        out["world_render_size"] = render_view->get_size();
        out["world_render_pixel_size"] = settings.pixelation_enabled ? settings.render_pixel_size : 1;
    }
    return out;
}
Dictionary WorldAtmosphere::save() const {
    auto out = state();
    out["elapsed"] = elapsed;
    out["lights"] = lights;
    // Color is a render value in state(); metadata uses portable strings.
    out.erase("fog_color");
    out.erase("sky_zenith");
    out.erase("world_render_size");
    out.erase("world_render_pixel_size");
    if (fog_override) {
        Dictionary manual;
        manual["color"] =
            "#" + Color(manual_fog.color.r, manual_fog.color.g, manual_fog.color.b).to_html(false);
        manual["start"] = manual_fog.start;
        manual["end"] = manual_fog.end;
        out["fog_override"] = manual;
    }
    return out;
}
} // namespace godot
