#pragma once
#include "voxel_core.h"
namespace altaria {
struct EnvironmentSettings {
    float day_seconds = 1200, start_hour = 9, lapse_c_per_m = .0065f;
    float rain_delta = -4, storm_delta = -9, wind_chill = 1.8f;
    float wind = .7f, wind_strength = .18f, grass_noise_scale = .12f, grass_noise_strength = .22f;
    float sun_energy = 1.2f, moon_energy = .006f, day_ambient = .006f, night_ambient = .002f;
    float cloud_coverage = .48f, cloud_pixel_size = .008f;
    // Cloud deck in world Y. Only rare summits reach above it.
    float cloud_base_y = 232, cloud_thickness = 50, cloud_above_opacity = .4f;
    // Stylized lighting palette (linear 0..1 colors, data-driven via environment.json).
    RGB sun_color_day{1.f, .93f, .78f}, sun_color_dusk{1.f, .52f, .38f}, moon_color{.55f, .62f, 1.f};
    RGB sky_day_zenith{.16f, .46f, .95f}, sky_day_horizon{.62f, .86f, 1.f};
    RGB sky_night_zenith{.02f, .025f, .09f}, sky_night_horizon{.09f, .07f, .22f};
    RGB sky_dusk_glow{1.f, .42f, .52f};
    RGB ambient_day{.62f, .70f, 1.f}, ambient_night{.30f, .30f, .62f};
    RGB sky_fill_day{.44f, .42f, .66f}, sky_fill_night{.05f, .06f, .16f};
    RGB grade_shadow_tint{.46f, .36f, .78f}, grade_highlight_tint{1.f, .90f, .72f};
    RGB cloud_lit_color{1.04f, 1.f, .96f}, cloud_shadow_color{.56f, .55f, .86f}, cloud_dusk_color{1.f, .60f, .66f};
    float grade_strength = .35f, saturation = 1.2f, contrast = 1.05f, glow_intensity = .55f;
    float sun_halo = .6f, cloud_bands = 4;
    float fog_transition = 1.2f, outline_width = 1.0f, outline_strength = .9f;
    float render_pixel_size = 2, pixelation_max = 2, pixelation_start = 48, pixelation_end = 2400;
    float sky_biome_strength = .85f, outline_highlight = .3f;
    float outline_distance_start=0,outline_distance_end=0;
    float outline_min_depth_gap=4, outline_min_feature_size=3, outline_shoreline_strength=.65f;
    bool outline_enabled = true, pixelation_enabled = true;
    // Ink along depth silhouettes; water shorelines keep their own contour.
    bool outline_silhouettes = true;
    // Screen-space ambient occlusion: darkens every concave corner, including
    // LOD steps and narrow cave gaps, and costs a full-screen pass.
    bool ssao_enabled = false;
    float outline_blacklist_distance=48, godrays_strength=.3f, godrays_reach=.85f;
    int godrays_samples=16;
    bool godrays_enabled=true;
    bool godrays_volumetric=true;
    float godrays_density=.00038f, godrays_length=96, godrays_light_energy=8;
    int max_bodies = 40, max_parts = 192;
    float physics_distance = 100, shadow_distance = 220;
};
struct FogSample {
    RGB color;
    float start = 1800, end = 5600;
};
FogSample biome_fog(const Settings &settings, int seed, float x, float z, float daylight);
enum class Weather { Clear, Rain, Storm };
struct TemperatureSample {
    float ambient = 0, blocks = 0, feels_like = 0, cold = 0, warm = 0;
};
TemperatureSample temperature_at(const Generator &g, const EnvironmentSettings &settings,
                                 std::array<float, 3> p, float hour, Weather weather, float wind,
                                 const std::function<BlockId(int, int, int)> &query);
} // namespace altaria
