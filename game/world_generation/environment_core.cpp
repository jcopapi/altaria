#include "environment_core.h"
#include <algorithm>
#include <cmath>
namespace altaria {
FogSample biome_fog(const Settings &s, int seed, float x, float z, float daylight) {
    auto region = BiomeStage(s, seed).regions({x}, {z})[0];
    const auto &a = s.biomes[region.primary], &b = s.biomes[region.secondary];
    auto mix = [](RGB a, RGB b, float t) {
        return RGB{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
    };
    return {mix(mix(b.fog_night, b.fog_day, daylight), mix(a.fog_night, a.fog_day, daylight),
                region.primary_weight),
            b.fog_start + (a.fog_start - b.fog_start) * region.primary_weight,
            b.fog_end + (a.fog_end - b.fog_end) * region.primary_weight};
}
TemperatureSample temperature_at(const Generator &g, const EnvironmentSettings &s, std::array<float, 3> p,
                                 float hour, Weather weather, float wind,
                                 const std::function<BlockId(int, int, int)> &query) {
    const auto &cfg = g.settings();
    BiomeStage stage(cfg, g.seed_value());
    auto mix = stage.regions({p[0]}, {p[2]})[0];
    auto &a = cfg.biomes[mix.primary];
    auto &b = cfg.biomes[mix.secondary];
    float day = a.day_c * mix.primary_weight + b.day_c * (1 - mix.primary_weight);
    float night = a.night_c * mix.primary_weight + b.night_c * (1 - mix.primary_weight);
    // Daily peak at 14:00, minimum at 02:00, continuous through midnight.
    float daylight = .5f + .5f * std::cos((hour - 14) * 6.28318530718f / 24);
    float weather_delta = weather == Weather::Rain    ? s.rain_delta
                          : weather == Weather::Storm ? s.storm_delta
                                                      : 0;
    TemperatureSample out;
    out.ambient = night + (day - night) * daylight - std::max(0.f, p[1] - cfg.sea_level) * s.lapse_c_per_m +
                  weather_delta;
    float radius = 0, cold = 0, warm = 0;
    for (auto &block : g.blocks().blocks)
        radius = std::max(radius, block.thermal_radius);
    int r = int(std::ceil(radius));
    for (int z = int(std::floor(p[2])) - r; z <= int(std::floor(p[2])) + r; z++)
        for (int y = int(std::floor(p[1])) - r; y <= int(std::floor(p[1])) + r; y++)
            for (int x = int(std::floor(p[0])) - r; x <= int(std::floor(p[0])) + r; x++) {
                float dx = p[0] - (x + .5f), dy = p[1] - (y + .5f), dz = p[2] - (z + .5f),
                      d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > radius * radius)
                    continue;
                const auto &block = g.blocks().get(query(x, y, z));
                if (block.thermal_radius <= 0 || d2 >= block.thermal_radius * block.thermal_radius)
                    continue;
                float falloff = 1 - std::sqrt(d2) / block.thermal_radius;
                float effect = block.thermal_delta * falloff * falloff;
                // Strongest source of each sign prevents a wall of snow adding unbounded cold.
                cold = std::min(cold, effect);
                warm = std::max(warm, effect);
            }
    out.cold = cold;
    out.warm = warm;
    out.blocks = cold + warm;
    out.feels_like = out.ambient + out.blocks -
                     std::max(0.f, wind) * s.wind_chill * (weather == Weather::Clear ? .35f : 1.f);
    return out;
}
} // namespace altaria
