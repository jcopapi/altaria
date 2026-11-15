#include "world_atmosphere.h"
#include "../debug/frame_profile.h"
#include "world_data.h"
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/gradient_texture1_d.hpp>
#include <godot_cpp/classes/gradient.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/particle_process_material.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/sky.hpp>
#include <godot_cpp/classes/script.hpp>
namespace godot {
using namespace altaria;
static Color to_color(const RGB &c) { return Color(c.r, c.g, c.b); }
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
                            "moon_energy", "day_ambient", "night_ambient", "outline_blacklist_distance", "outline_distance_start", "outline_distance_end",
                            "outline_min_depth_gap", "outline_min_feature_size", "outline_shoreline_strength", "outline_silhouettes", "ssao_enabled", "godrays_enabled",
                            "godrays_strength", "godrays_samples", "godrays_reach", "godrays_volumetric",
                            "godrays_density", "godrays_length", "godrays_light_energy", "sun_energy", "max_parts",
                            "cloud_coverage", "cloud_pixel_size", "cloud_base_y", "cloud_thickness", "cloud_bands", "cloud_above_opacity",
                            "sun_color_day", "sun_color_dusk", "moon_color", "sky_day_zenith", "sky_day_horizon",
                            "sky_night_zenith", "sky_night_horizon", "sky_dusk_glow", "ambient_day", "ambient_night",
                            "sky_fill_day", "sky_fill_night", "grade_shadow_tint", "grade_highlight_tint",
                            "grade_strength", "saturation", "contrast", "glow_intensity", "sun_halo",
                            "cloud_lit_color", "cloud_shadow_color", "cloud_dusk_color"})
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
    env->set_ambient_light_sky_contribution(0.f);
    env->set_reflection_source(Environment::REFLECTION_SOURCE_DISABLED);
    env->set_tonemapper(Environment::TONE_MAPPER_FILMIC);
    env->set_ssao_enabled(settings.ssao_enabled);
    env->set_ssao_radius(1.4);
    env->set_ssao_intensity(.65);
    env->set_ssao_power(1.0);
    env->set_glow_enabled(true);
    env->set_glow_intensity(settings.glow_intensity);
    env->set_glow_bloom(.08);
    env->set_glow_hdr_bleed_threshold(1.4);
    env->set_adjustment_enabled(true);
    env->set_adjustment_saturation(settings.saturation);
    env->set_adjustment_contrast(settings.contrast);
    env->set_fog_enabled(true);
    env->set_fog_mode(Environment::FOG_MODE_DEPTH);
    env->set_fog_depth_begin(std::min(world->settings().fog_start, world->settings().view_distance * .72f));
    env->set_fog_depth_end(world->settings().view_distance * .94f);
    env->set_fog_sky_affect(0.f);
    env->set_volumetric_fog_density(settings.godrays_density);
    env->set_volumetric_fog_length(settings.godrays_length);
    env->set_volumetric_fog_albedo(Color(.96,.98,1));
    env->set_volumetric_fog_anisotropy(.72);
    env->set_volumetric_fog_gi_inject(0);
    env->set_volumetric_fog_ambient_inject(0);
    env->set_volumetric_fog_sky_affect(0);
    node->set_environment(env);
    compositor.instantiate();
    Ref<Script> flare_script = ResourceLoader::get_singleton()->load("res://addons/lens_effects/lens_flare_compositor_effect.gd");
    if (flare_script.is_valid()) {
        lens_flare_effect = flare_script->call("new");
        if (lens_flare_effect.is_valid()) {
            TypedArray<CompositorEffect> effects;
            effects.push_back(lens_flare_effect);
            compositor->set_compositor_effects(effects);
        }
    }
    node->set_compositor(compositor);
    add_child(node);
    if (world->settings().preset == WorldPreset::Default) {
        // Ash and embers rise through the underworld's hot air around the viewer.
        embers = memnew(GPUParticles3D);
        embers->set_name("UnderworldEmbers");
        embers->set_amount(700);
        embers->set_lifetime(7.0);
        embers->set_pre_process_time(4.0);
        embers->set_use_local_coordinates(false);
        embers->set_visibility_aabb(AABB(Vector3(-48, -24, -48), Vector3(96, 48, 96)));
        embers->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        Ref<ParticleProcessMaterial> motion;
        motion.instantiate();
        motion->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
        motion->set_emission_box_extents(Vector3(38, 16, 38));
        motion->set_direction(Vector3(0, 1, 0));
        motion->set_spread(70);
        motion->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, .3);
        motion->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 1.4);
        motion->set_gravity(Vector3(0, .35, 0));
        motion->set_param_min(ParticleProcessMaterial::PARAM_SCALE, .5);
        motion->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 1.6);
        motion->set_turbulence_enabled(true);
        motion->set_turbulence_noise_strength(1.2);
        motion->set_turbulence_noise_scale(6.0);
        Ref<Gradient> fade;
        fade.instantiate();
        fade->set_color(0, Color(1.f, .62f, .2f, 0.f));
        fade->set_color(1, Color(.3f, .27f, .25f, 0.f));
        fade->add_point(.12f, Color(1.f, .48f, .12f, 1.f));
        fade->add_point(.7f, Color(.42f, .36f, .32f, .8f));
        Ref<GradientTexture1D> ramp;
        ramp.instantiate();
        ramp->set_gradient(fade);
        motion->set_color_ramp(ramp);
        embers->set_process_material(motion);
        Ref<QuadMesh> flake;
        flake.instantiate();
        flake->set_size(Vector2(.07f, .07f));
        Ref<StandardMaterial3D> look;
        look.instantiate();
        look->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        look->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
        look->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        look->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        flake->set_material(look);
        embers->set_draw_pass_mesh(0, flake);
        embers->set_emitting(false);
        embers->hide();
        add_child(embers);
    }
    sun = memnew(DirectionalLight3D);
    sun->set_name("Sun");
    sun->set_shadow(true);
    // Two cascades are enough for a 140 m range and halve the shadow passes.
    sun->set_shadow_mode(DirectionalLight3D::SHADOW_PARALLEL_2_SPLITS);
    sun->set_param(Light3D::PARAM_SHADOW_BLUR, 1.6);
    sun->set_param(Light3D::PARAM_SHADOW_MAX_DISTANCE, settings.shadow_distance);
    add_child(sun);
    moon = memnew(DirectionalLight3D);
    moon->set_name("Moon");
    moon->set_color(to_color(settings.moon_color));
    moon->set_shadow(true);
    moon->set_shadow_mode(DirectionalLight3D::SHADOW_PARALLEL_2_SPLITS);
    moon->set_param(Light3D::PARAM_SHADOW_MAX_DISTANCE, settings.shadow_distance);
    add_child(moon);
    Array saved_lights = saved.get("lights", Array());
    for (int i = 0; i < std::min(int(saved_lights.size()), 16); i++) {
        Dictionary l = saved_lights[i];
        Array p = l.get("position", Array());
        String c = l.get("color", "#ffffff");
        if (p.size() == 3 && Color::html_is_valid(c))
            add_light(Vector3(p[0], p[1], p[2]), Color::html(c),
                      std::clamp(float(l.get("energy", 2)), .1f, 20.f),
                      std::clamp(float(l.get("radius", 12)), 1.f, 192.f));
    }
    settings.outline_enabled = saved.get("outline_enabled", settings.outline_enabled);
    settings.pixelation_enabled = saved.get("pixelation_enabled", settings.pixelation_enabled);
    settings.godrays_enabled = saved.get("godrays_enabled", settings.godrays_enabled);
    if (world->settings().preset == WorldPreset::Structure)
        settings.godrays_enabled = false;
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
    if (!camera)
        return;
    source_camera = camera;
    render_view = memnew(SubViewport);
    render_view->set_name("WorldRenderViewport");
    if (camera->is_inside_tree()) {
        render_view->set_world_3d(camera->get_world_3d());
    }
    render_view->set_update_mode(SubViewport::UPDATE_ALWAYS);
    render_view->set_disable_input(true);
    render_view->set_use_taa(false);
    render_view->set_screen_space_aa(Viewport::SCREEN_SPACE_AA_DISABLED);
    render_view->set_msaa_3d(Viewport::MSAA_DISABLED);
    add_child(render_view);
    render_camera = memnew(Camera3D);
    render_camera->set_name("WorldRenderCamera");
    if (compositor.is_valid())
        render_camera->set_compositor(compositor);
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
    cartoon->set_shader_parameter("silhouette_outlines",settings.outline_silhouettes);
    cartoon->set_shader_parameter("outline_blacklist_distance", settings.outline_blacklist_distance);
    cartoon->set_shader_parameter("godrays_strength", settings.godrays_strength);
    cartoon->set_shader_parameter("godrays_samples", settings.godrays_samples);
    cartoon->set_shader_parameter("godrays_reach", settings.godrays_reach);
    cartoon->set_shader_parameter("pixelation_start", settings.pixelation_start);
    cartoon->set_shader_parameter("pixelation_end", settings.pixelation_end);
    cartoon->set_shader_parameter("pixelation_max", settings.pixelation_max);
    cartoon->set_shader_parameter("grade_shadow_tint", to_color(settings.grade_shadow_tint));
    cartoon->set_shader_parameter("grade_highlight_tint", to_color(settings.grade_highlight_tint));
    cartoon->set_shader_parameter("grade_strength", settings.grade_strength);
    cartoon->set_shader_parameter("cloud_bands", settings.cloud_bands);
    cartoon->set_shader_parameter("cloud_lit_color", to_color(settings.cloud_lit_color));
    cartoon->set_shader_parameter("cloud_shadow_color", to_color(settings.cloud_shadow_color));
    cartoon->set_shader_parameter("cloud_dusk_color", to_color(settings.cloud_dusk_color));
    update_lighting();
}
void WorldAtmosphere::update_render_view() {
    if (!render_view || !source_camera || !source_camera->is_inside_tree() || !source_camera->get_viewport())
        return;
    if (!render_view->get_world_3d().is_valid() && source_camera->get_world_3d().is_valid()) {
        render_view->set_world_3d(source_camera->get_world_3d());
    }
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
    if (lens_flare_effect.is_valid() && render_camera && sun && render_view) {
        Vector3 cam_fwd = -render_camera->get_global_transform().basis.get_column(2).normalized();
        Vector3 sun_fwd = sun->get_global_transform().basis.get_column(2).normalized();
        float dir_dot = cam_fwd.dot(sun_fwd);
        Vector3 pt = sun->get_global_transform().basis.get_column(2) * std::max(render_camera->get_near(), 1.0f) + render_camera->get_global_position();
        Vector2 r_size = Vector2(render_view->get_size());
        Vector2 screen_pos = (r_size.x > 0 && r_size.y > 0) ? (render_camera->unproject_position(pt) / r_size) : Vector2(0.5f, 0.5f);
        lens_flare_effect->set("sun_position", screen_pos);
        lens_flare_effect->set("sun_dir_sign", dir_dot);
        lens_flare_effect->set("sun_color", sun->get_color());
        lens_flare_effect->set("enabled", settings.godrays_enabled && godrays_visibility > .02f && !world->is_fullbright() &&
            !world->is_underground_streaming());
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
float WorldAtmosphere::compute_cave_darkness_target() const {
    if (!world || world->in_underworld())
        return 0.f;
    const Vector3 eye = source_camera && source_camera->is_inside_tree()
                            ? source_camera->get_global_position()
                            : world->viewer_position() + Vector3(0.f, 1.5f, 0.f);
    const float sky_exp = world->sky_exposure_at(eye);
    if (!world->is_underground_streaming()) {
        if (sky_exp >= 0.85f)
            return 0.f;
        return std::clamp((0.85f - sky_exp) / 0.85f * 0.65f, 0.f, 0.55f);
    }
    if (sky_exp > 0.01f)
        return std::clamp(0.45f + (1.f - sky_exp) * 0.38f, 0.45f, 0.83f);
    return 1.f;
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
    const Color clear_sky = to_color(settings.sky_night_zenith).lerp(to_color(settings.sky_day_zenith), day);
    sky_zenith = clear_sky.lerp(themed, settings.sky_biome_strength * .35f);
    const Color sky_horizon = to_color(settings.sky_night_horizon).lerp(to_color(settings.sky_day_horizon), day)
                                  .lerp(horizon, settings.sky_biome_strength * .30f);
    sun->look_at_from_position(Vector3(), -direction, Vector3(0, 0, 1));
    moon->look_at_from_position(Vector3(), direction, Vector3(0, 0, 1));
    float overcast = weather == Weather::Storm ? .42f : weather == Weather::Rain ? .7f : 1;
    godrays_visibility=day*std::sqrt(std::max(0.f,direction.y))*(.65f+.9f*dusk)*overcast;
    // Daylight and ambient shading are world-wide. Rock casts its own
    // shadows, including where a streamed cave volume ends inside solid rock.
    const bool outside = !world->is_underground_streaming();
    const float darkness_target = compute_cave_darkness_target();
    {
        const float target = outside ? 1.f : 0.f;
        const float ease = outdoor_initialized ? 1.f - std::exp(-delta / .45f) : 1.f;
        outdoor += (target - outdoor) * ease;
        if (std::abs(target - outdoor) < .01f)
            outdoor = target;
        const float hot = world->in_underworld() ? 1.f : 0.f;
        underworld += (hot - underworld) * ease;
        if (std::abs(hot - underworld) < .01f)
            underworld = hot;
        const float depth_below = std::max(0.f, world->surface_ground_height() - world->viewer_position().y);
        const float dark_tau = darkness_target > cave_darkness
                                   ? std::max(0.45f, 1.15f - depth_below * 0.015f)
                                   : 0.50f;
        const float dark_ease = outdoor_initialized ? 1.f - std::exp(-delta / dark_tau) : 1.f;
        cave_darkness += (darkness_target - cave_darkness) * dark_ease;
        if (std::abs(darkness_target - cave_darkness) < .004f)
            cave_darkness = darkness_target;
        outdoor_initialized = true;
    }
    sun->set_color(to_color(settings.sun_color_day).lerp(to_color(settings.sun_color_dusk), dusk));
    // Underground, unloaded rock casts no shadow, so sunlight would leak into
    // caves; only local lights (lava, lamps, the explorer light) apply there.
    const float sun_energy = std::max(0.f, direction.y) * settings.sun_energy * overcast * outdoor;
    const float moon_energy = (1 - day) * settings.moon_energy * outdoor;
    sun->set_param(Light3D::PARAM_ENERGY, sun_energy);
    moon->set_param(Light3D::PARAM_ENERGY, moon_energy);
    // A dark light still renders its shadow cascades; only the light that is
    // actually shining keeps its shadow map (they hand over at dusk/dawn).
    sun->set_shadow(!world->is_fullbright() && sun_energy > .02f);
    moon->set_shadow(!world->is_fullbright() && moon_energy > .02f && sun_energy <= .02f);
    sun->set_visible(sun_energy > 0.f);
    moon->set_visible(moon_energy > 0.f);
    auto env = node->get_environment();
    env->set_volumetric_fog_enabled(settings.godrays_enabled && settings.godrays_volumetric &&
                                    !world->is_fullbright() && outside && day>.05f);
    sun->set_param(Light3D::PARAM_VOLUMETRIC_FOG_ENERGY,settings.godrays_light_energy);
    moon->set_param(Light3D::PARAM_VOLUMETRIC_FOG_ENERGY,0);
    const float cave_fade = 1.f - cave_darkness * (1.f - underworld);
    const float mouth_twilight = (1.f - outdoor) * std::pow(std::max(0.f, cave_fade), 1.35f) * (1.f - underworld);
    Color amb_color = to_color(settings.ambient_night).lerp(to_color(settings.ambient_day), day).lerp(horizon, .15);
    amb_color = amb_color.lerp(Color(.18f, .22f, .30f), mouth_twilight);
    env->set_ambient_light_color(amb_color);
    const float night_ambient = settings.night_ambient;
    const float base_ambient = night_ambient + day * (settings.day_ambient - night_ambient);
    env->set_ambient_light_energy(base_ambient * cave_fade + (0.032f + 0.052f * day) * mouth_twilight);
    env->set_fog_enabled(!world->is_fullbright());
    const float fog_light = .035f + .82f * day;
    // Underground, sight transitions smoothly from cool cave twilight near the mouth
    // down to 100% pitch darkness at depth; the underworld keeps its hot orange haze.
    if (embers) {
        const bool hot = underworld > .01f && !world->is_fullbright();
        if (hot) {
            embers->set_global_position(world->viewer_position() + Vector3(0, 2, 0));
            embers->set_amount_ratio(underworld);
        }
        if (embers->is_emitting() != hot) {
            embers->set_emitting(hot);
            embers->set_visible(hot);
        }
    }
    const float sight = world->underground_sight();
    const float cave_start = Math::lerp(sight * .45f, sight * .22f, cave_darkness);
    const float cave_end = Math::lerp(sight * 1.15f, sight * .65f, cave_darkness);
    const float under_start = Math::lerp(cave_start, 34.f, underworld);
    const float under_end = Math::lerp(cave_end, 190.f, underworld);
    const Color cave_mist = Color(.032f, .038f, .052f).lerp(Color(0.f, 0.f, 0.f), cave_darkness);
    const Color under_color = cave_mist.lerp(Color(.62f, .25f, .09f), underworld);
    const Color sky_fog(horizon.r * fog_light, horizon.g * fog_light, horizon.b * fog_light);
    env->set_fog_light_color(under_color.lerp(sky_fog, outdoor));
    // Underground the background is fog too, so the haze (or darkness) has no
    // hard edge where streamed geometry ends; interpolate 1/distance for smooth pull-in.
    env->set_fog_sky_affect(1.f - outdoor * outdoor);
    const float inv_begin = Math::lerp(1.f / std::max(under_start, 0.5f), 1.f / std::max(fog.start, 1.f), outdoor);
    const float inv_finish = Math::lerp(1.f / std::max(under_end, 1.f), 1.f / std::max(fog.end, 2.f), outdoor);
    const float fog_begin = 1.f / std::max(inv_begin, 1e-5f);
    const float fog_finish = 1.f / std::max(inv_finish, 1e-5f);
    env->set_fog_depth_begin(fog_begin);
    env->set_fog_depth_end(std::max(fog_finish, fog_begin + 1.f));
    sky->set_shader_parameter("daylight", day);
    sky->set_shader_parameter("surface_streamed", world->surface_in_streamed_height());
    sky->set_shader_parameter("sun_direction", direction);
    sky->set_shader_parameter("cloud_time", elapsed);
    sky->set_shader_parameter("horizon_color", sky_horizon);
    sky->set_shader_parameter("zenith_color", sky_zenith);
    sky->set_shader_parameter("dusk_glow", to_color(settings.sky_dusk_glow));
    sky->set_shader_parameter("sun_halo", settings.sun_halo);
    sky->set_shader_parameter("sun_tint", sun->get_color());
    if (cartoon.is_valid()) {
        const int seed = world->last_seed();
        const float cloud_base = settings.cloud_base_y;
        cartoon->set_shader_parameter("world_clouds", world->settings().preset != WorldPreset::Structure &&
                                      !world->is_fullbright() && world->surface_in_streamed_height());
        cartoon->set_shader_parameter("cloud_base", cloud_base);
        cartoon->set_shader_parameter("cloud_top", cloud_base + settings.cloud_thickness);
        cartoon->set_shader_parameter("cloud_dusk", dusk);
        cartoon->set_shader_parameter("cloud_above_opacity", settings.cloud_above_opacity);
        cartoon->set_shader_parameter("cloud_cell", settings.cloud_pixel_size * 500.f);
        cartoon->set_shader_parameter("cloud_coverage",
            std::min(.85f, settings.cloud_coverage + (weather == Weather::Clear ? 0.f : .22f)));
        cartoon->set_shader_parameter("cloud_daylight", day * (.72f + .28f * overcast));
        cartoon->set_shader_parameter("cloud_sun_direction", direction);
        cartoon->set_shader_parameter("cloud_drift", Vector2((seed % 997) * 3.f + elapsed * wind * 2.f,
            (seed % 751) * 3.f + elapsed * wind * .68f));
        cartoon->set_shader_parameter("outlines", settings.outline_enabled);
        cartoon->set_shader_parameter("pixelation", settings.pixelation_enabled && outside);
        cartoon->set_shader_parameter("fog_start", fog_begin);
        cartoon->set_shader_parameter("fog_end", std::max(fog_finish, fog_begin + 1.f));
        cartoon->set_shader_parameter("heat_haze", underworld);
        cartoon->set_shader_parameter("key_light_direction", day > .5f ? direction : -direction);
        cartoon->set_shader_parameter("godrays", settings.godrays_enabled && outside && !world->is_fullbright());
        cartoon->set_shader_parameter("godrays_color",sun->get_color());
    }
    float light_range = 0.f;
    if (!outside) {
        if (following_light && following_light->is_visible())
            light_range = following_light->get_param(Light3D::PARAM_RANGE);
        for (int i = 0; i < lights.size(); ++i) {
            Dictionary record = lights[i];
            Array xyz = record["position"];
            const Vector3 point{float(xyz[0]), float(xyz[1]), float(xyz[2])};
            const float radius = record["radius"];
            const float distance = point.distance_to(position);
            if (distance <= 160.f + radius)
                light_range = std::max(light_range, distance + radius);
        }
    }
    world->set_cave_light_range(light_range);
    // Sky-exposed surfaces get a soft, colored fill so toon shadows read as
    // lavender instead of black. It stays on underground: each vertex carries
    // its sky exposure, which fades through a cave mouth and is 0 in enclosed
    // chambers, so the opening keeps its daylight gradient as you walk in.
    const Color fill = to_color(settings.sky_fill_night).lerp(to_color(settings.sky_fill_day), day) *
                       (.75f + .25f * overcast) * (1.f - cave_darkness * (1.f - underworld));
    world->set_sky_fill(Vector3(fill.r, fill.g, fill.b));
    world->set_visual_environment(elapsed, settings.wind_strength * wind, settings.grass_noise_scale,
                                  settings.grass_noise_strength, fog.end);
}
// Eases the post-process submersion effects as the camera enters or leaves
// water or lava. One block lookup per frame.
void WorldAtmosphere::update_submersion(float delta) {
    float water_target = 0.f, lava_target = 0.f;
    if (world && source_camera && source_camera->is_inside_tree()) {
        const Vector3 eye = source_camera->get_global_position();
        const auto &block = world->registry().get(
            world->block_at(int(std::floor(eye.x)), int(std::floor(eye.y)), int(std::floor(eye.z))));
        if (block.lava)
            lava_target = 1.f;
        else if (block.water || block.liquid)
            water_target = 1.f;
    }
    const float ease = 1.f - std::exp(-delta / .12f);
    underwater = water_target;
    in_lava += (lava_target - in_lava) * ease;
    if (std::abs(lava_target - in_lava) < .01f)
        in_lava = lava_target;
    if (cartoon.is_valid()) {
        cartoon->set_shader_parameter("underwater", world && world->is_fullbright() ? 0.f : underwater);
        cartoon->set_shader_parameter("in_lava", world && world->is_fullbright() ? 0.f : in_lava);
    }
}
void WorldAtmosphere::_process(double delta) {
    ALTARIA_PROFILE("atmosphere.process");
    update_submersion(float(delta));
    { ALTARIA_PROFILE("atmosphere.render_view"); update_render_view(); }
    elapsed += delta;
    if (running)
        hour = std::fmod(hour + float(delta) * 24 / settings.day_seconds, 24.f);
    update_timer += delta;
    // Grass runs every frame; environment and radiance refresh at 10 Hz.
    // While the cave transition eases, refresh every frame so it stays smooth.
    const float outdoor_target = world && world->is_underground_streaming() ? 0.f : 1.f;
    const float underworld_target = world && world->in_underworld() ? 1.f : 0.f;
    const float darkness_target = compute_cave_darkness_target();
    if (update_timer >= .1f ||
        (outdoor_initialized &&
         (outdoor != outdoor_target || underworld != underworld_target || cave_darkness != darkness_target))) {
        float dt = update_timer;
        update_timer = 0;
        ALTARIA_PROFILE("atmosphere.lighting");
        update_lighting(dt);
    } else
        world->set_wind_time(elapsed);
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
    light->set_shadow(true);
    light->set_shadow_caster_mask(~TERRAIN_SKY_SHADOW_LAYER);
    light->set_param(Light3D::PARAM_VOLUMETRIC_FOG_ENERGY, 0.f);
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
bool WorldAtmosphere::is_following_light_on() const {
    return following_light && following_light->is_visible();
}
void WorldAtmosphere::set_loading_world(bool loading) {
    if (render_view)
        render_view->set_update_mode(loading ? SubViewport::UPDATE_DISABLED : SubViewport::UPDATE_ALWAYS);
    if (presentation)
        presentation->set_visible(!loading);
}
void WorldAtmosphere::set_following_light(bool enabled, std::optional<Color> color,
                                          std::optional<float> energy, std::optional<float> radius) {
    if (!following_light) {
        if (!enabled || !source_camera)
            return;
        following_light = memnew(OmniLight3D);
        following_light->set_name("ExplorationLight");
        following_light->set_color(Color(1.f, .88f, .70f));
        following_light->set_param(Light3D::PARAM_ENERGY, 4.5f);
        following_light->set_param(Light3D::PARAM_RANGE, 24.f);
        following_light->set_param(Light3D::PARAM_VOLUMETRIC_FOG_ENERGY, 0.f);
        following_light->set_shadow(true);
        following_light->set_shadow_caster_mask(~TERRAIN_SKY_SHADOW_LAYER);
        source_camera->add_child(following_light);
    }
    if (color)
        following_light->set_color(*color);
    if (energy)
        following_light->set_param(Light3D::PARAM_ENERGY, *energy);
    if (radius)
        following_light->set_param(Light3D::PARAM_RANGE, *radius);
    following_light->set_visible(enabled);
    update_timer = 1;
}
Color WorldAtmosphere::sample_hand_light(Vector3 p) const {
    if (!world || world->is_fullbright())
        return Color(1.0f, 1.0f, 1.0f, 1.0f);

    // Base ambient + directional sun/moon lighting scaled by outdoor exposure
    Color accum(0.16f, 0.15f, 0.20f, 1.0f);
    if (node && node->get_environment().is_valid()) {
        Ref<Environment> env = node->get_environment();
        Color amb = env->get_ambient_light_color();
        float amb_e = std::clamp(env->get_ambient_light_energy(), 0.12f, 1.2f);
        accum.r = std::max(accum.r, amb.r * amb_e);
        accum.g = std::max(accum.g, amb.g * amb_e);
        accum.b = std::max(accum.b, amb.b * amb_e);
    }
    if (sun && outdoor > 0.01f) {
        float se = std::clamp(float(sun->get_param(Light3D::PARAM_ENERGY)) * outdoor * 0.55f, 0.0f, 0.95f);
        Color sc = sun->get_color();
        accum.r += sc.r * se;
        accum.g += sc.g * se;
        accum.b += sc.b * se;
    }
    if (moon && outdoor > 0.01f) {
        float me = std::clamp(float(moon->get_param(Light3D::PARAM_ENERGY)) * outdoor * 0.45f, 0.0f, 0.55f);
        Color mc = moon->get_color();
        accum.r += mc.r * me;
        accum.g += mc.g * me;
        accum.b += mc.b * me;
    }
    if (underworld > 0.01f) {
        accum.r += 0.45f * underworld;
        accum.g += 0.14f * underworld;
        accum.b += 0.08f * underworld;
    }

    // Personal ExplorationLight ("light follow [color]")
    Color point_rgb(0.0f, 0.0f, 0.0f, 1.0f);
    float point_weight = 0.0f;
    if (following_light && following_light->is_visible()) {
        Color fc = following_light->get_color();
        float fe = std::clamp(float(following_light->get_param(Light3D::PARAM_ENERGY)) * 0.28f, 0.1f, 1.4f);
        point_rgb.r += fc.r * fe;
        point_rgb.g += fc.g * fe;
        point_rgb.b += fc.b * fe;
        point_weight += fe;
    }

    // Placed world OmniLight3D sources ("light [color]")
    for (int i = 0; i < lights.size(); ++i) {
        Dictionary record = lights[i];
        Array xyz = record["position"];
        if (xyz.size() < 3)
            continue;
        const Vector3 lpos{float(xyz[0]), float(xyz[1]), float(xyz[2])};
        const float radius = std::max(0.5f, float(record["radius"]));
        const float dist = p.distance_to(lpos);
        if (dist >= radius)
            continue;
        const Color lcol = Color::from_string(String(record["color"]), Color(1, 1, 1));
        const float energy = float(record["energy"]);
        const float atten = std::pow(1.0f - dist / radius, 1.35f) * std::clamp(energy * 0.36f, 0.1f, 1.8f);
        point_rgb.r += lcol.r * atten;
        point_rgb.g += lcol.g * atten;
        point_rgb.b += lcol.b * atten;
        point_weight += atten;
    }

    // Normalize base environment light to [0..1]
    float base_max = std::max({accum.r, accum.g, accum.b, 1.0f});
    Color result(accum.r / base_max, accum.g / base_max, accum.b / base_max, 1.0f);

    // Blend colored point lights strongly so colored lights (e.g. red light) visibly tint the hands
    if (point_weight > 0.001f) {
        float pmax = std::max({point_rgb.r, point_rgb.g, point_rgb.b, 0.001f});
        Color pnorm(point_rgb.r / pmax, point_rgb.g / pmax, point_rgb.b / pmax, 1.0f);
        float blend = std::clamp(point_weight / (point_weight + 0.55f), 0.0f, 0.88f);
        result = result.lerp(pnorm, blend);
        float lift = std::clamp(0.75f + 0.35f * point_weight, 0.75f, 1.15f);
        result.r = std::clamp(result.r * lift, 0.12f, 1.15f);
        result.g = std::clamp(result.g * lift, 0.12f, 1.15f);
        result.b = std::clamp(result.b * lift, 0.12f, 1.15f);
    } else {
        result.r = std::clamp(result.r, 0.16f, 1.05f);
        result.g = std::clamp(result.g, 0.16f, 1.05f);
        result.b = std::clamp(result.b, 0.18f, 1.05f);
    }
    return result;
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
    out["outdoor_light"] = outdoor; // 1 open sky .. 0 cave, eased
    out["underworld"] = underworld;
    out["cave_darkness"] = cave_darkness;
    out["ambient_light_energy"] = node && node->get_environment().is_valid() ? node->get_environment()->get_ambient_light_energy() : 0.f;
    out["effective_fog_color"] = node && node->get_environment().is_valid() ? node->get_environment()->get_fog_light_color() : Color();
    out["underwater"] = underwater;
    out["in_lava"] = in_lava;
    out["underground_sight"] = world ? world->underground_sight() : 0.f;
    out["sun_energy"] = sun ? float(sun->get_param(Light3D::PARAM_ENERGY)) : 0.f;
    out["weather"] = weather == Weather::Clear ? "clear" : weather == Weather::Rain ? "rain" : "storm";
    out["wind"] = wind;
    out["wind_time"] = elapsed;
    out["colored_lights"] = lights.size();
    out["following_light"] = is_following_light_on();
    if (following_light)
        out["following_light_color"] = "#" + following_light->get_color().to_html(false);
    out["fog_color"] = Color(fog.color.r, fog.color.g, fog.color.b);
    out["fog_start"] = fog.start;
    out["fog_end"] = fog.end;
    out["fog_mode"] = world->is_underground_streaming() ? "off" : fog_override ? "manual" : "biome";
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
    out.erase("effective_fog_color");
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
