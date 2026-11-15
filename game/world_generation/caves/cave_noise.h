#pragma once
#include <FastNoise/FastNoise.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace altaria::caves {
// Organic caves that complement the planned chamber graph: irregular halls
// with rock pillars, winding branching tunnels, low crawlspaces, tall narrow
// fissures, enclosed lava seas and, at the bottom of the world, the open
// underworld (a floor and a roof with giant pillars over a lava ocean).
struct NoiseSettings {
    bool enabled = false; // caves.json turns it on for game worlds
    float surface_margin = 12, surface_fade = 40;
    float cheese_scale = 92, cheese_threshold = .78f, deep_threshold = .1f;
    float tunnel_scale = 124, tunnel_width = .12f;
    float squeeze_scale = 52, squeeze_width = .085f;
    float fissure_scale = 38, fissure_width = .06f;
    float region_scale = 150;
    float pillar_spacing = 24;
    int hell_floor = -606, hell_ceiling = -470, hell_lava = -592;
    float hell_floor_amplitude = 24, hell_ceiling_amplitude = 22, hell_scale = 140;
    float hell_pillar_spacing = 64, transition_height = 120;
    std::vector<int> lava_seas{-170, -290, -390};
    float lava_sea_scale = 260, lava_sea_threshold = .3f, lava_sea_depth = 14, lava_sea_height = 30;
    float spike_chance = .07f, hell_spike_chance = .14f, pillar_chance = .018f;
    // Share of underworld pillars that are hollow descent stairs, and the
    // height above the underworld roof where their shafts begin.
    float descent_chance = .3f, descent_height = 110, roof_seal = 14;
    float lava_fall_chance = .35f;
};
bool valid_noise(const NoiseSettings &settings, int min_y, std::string &error);
enum class NoiseCell : uint8_t { solid = 0, air = 1, lava = 2 };
// Hanging and standing rock for one vertical run of open cave air.
struct Spikes {
    int down = 0, up = 0; // stalactite length from the roof, stalagmite length from the floor
    bool pillar = false;  // the whole run becomes a thin floor-to-roof column
};
inline constexpr int spike_scan = 24;
class NoiseField {
    NoiseSettings cfg;
    int seed, min_y;
    uint64_t identity = 0; // keys the scalar sample cache per field instance
    FastNoise::SmartNode<> cheese, tunnel_a, tunnel_b, width, squeeze, fissure, region;
    FastNoise::SmartNode<> hell_floor, hell_roof, seas;
    static constexpr int lattice = 4;
  public:
    struct Channels {
        float cheese, tunnel_a, tunnel_b, width, squeeze, fissure, region;
    };
    struct Columns {
        float floor, roof;
        std::array<float, 3> seas;
    };
    // Pillar candidates near one column; the horizontal part of every test is
    // fixed per column, so fills compute it once instead of once per cell.
    struct Pillar { float distance2, base, phase; };
    struct Pillars {
        Pillar hall[9], hell[9];
        int halls = 0, hells = 0;
        bool sea = false;
        bool fall = false; // lava pours from the underworld roof here
        // Descent pillar: a hollow giant pillar with a spiral stair inside.
        // descent marks columns near one (doorway, windows, top chamber).
        bool descent = false, stair = false;
        float stair_radius = 0, stair_angle = 0, exit_angle = 0;
    };
  private:
    void lattice3(int count, const float *x, const float *y, const float *z, std::vector<Channels> &out) const;
    void lattice2(int count, const float *x, const float *z, std::vector<Columns> &out) const;
    NoiseCell classify(int y, float surface, const Channels &c, const Columns &k, const Pillars &p) const;
    Pillars pillars(int x, int z) const;
  public:
    NoiseField(NoiseSettings settings, int seed, int min_y);
    const NoiseSettings &settings() const { return cfg; }
    // Lowest cell any noise cave may open.
    int lowest() const;
    bool hell(int y) const { return y <= cfg.hell_ceiling + int(cfg.hell_ceiling_amplitude) + 2; }
    // Fill out[(iz*nx+ix)*ny+iy] for cells (x0+ix*step, y0+iy*ystep, z0+iz*step).
    // surface[iz*nx+ix] is the column's terrain height. Identical to at().
    void fill(int x0, int z0, int step, int nx, int nz, int y0, int ystep, int ny,
              const float *surface, std::vector<uint8_t> &out) const;
    NoiseCell at(int x, int y, int z, float surface) const;
    // floor/roof are the rock cells below/above the run (or INT_MIN/INT_MAX when
    // unknown or not rock); run is the open length capped at spike_scan.
    Spikes spikes(int x, int z, int floor, int roof, int run) const;
    // True inside the shaft of a descent stair pillar (tests, probes).
    bool descent_shaft(int x, int z) const { return pillars(x, z).stair; }
};
} // namespace altaria::caves
