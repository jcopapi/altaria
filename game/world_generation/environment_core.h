#pragma once
#include "voxel_core.h"
namespace altaria {
struct EnvironmentSettings {
    float day_seconds = 1200, start_hour = 9, lapse_c_per_m = .0065f;
    float rain_delta = -4, storm_delta = -9, wind_chill = 1.8f;
    float wind = .7f, wind_strength = .18f, grass_noise_scale = .12f, grass_noise_strength = .22f;
    float sun_energy = 1.35f, moon_energy = .28f, day_ambient = .35f, night_ambient = .14f;
    float cloud_coverage = .48f, cloud_pixel_size = .008f;
    float fog_transition = 1.2f, outline_width = 1.0f, outline_strength = .9f;
    float render_pixel_size = 2, pixelation_max = 2, pixelation_start = 48, pixelation_end = 2400;
    float sky_biome_strength = .85f, outline_highlight = .3f;
    float outline_distance_start=0,outline_distance_end=0;
    float outline_min_depth_gap=4, outline_min_feature_size=3, outline_shoreline_strength=.65f;
    bool outline_enabled = true, pixelation_enabled = true;
    float outline_blacklist_distance=48, godrays_strength=.3f, godrays_reach=.85f;
    int godrays_samples=16;
    bool godrays_enabled=true;
    bool godrays_volumetric=true;
    float godrays_density=.0008f, godrays_length=96, godrays_light_energy=12;
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
