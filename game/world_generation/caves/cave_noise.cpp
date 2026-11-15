#include "cave_noise.h"
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
namespace altaria::caves {
static uint64_t mix(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
static uint64_t hash(int seed, int x, int y, int z, uint64_t salt) {
    uint64_t h = mix(uint64_t(uint32_t(seed)) ^ (salt * 0x9e3779b97f4a7c15ULL));
    h = mix(h ^ uint32_t(x));
    h = mix(h ^ uint32_t(y));
    return mix(h ^ uint32_t(z));
}
static float unit(uint64_t h) { return float((h >> 40) & 0xffffff) / 16777216.f; }
static int floor_div(int a, int b) { return a / b - (a % b != 0 && (a < 0) != (b < 0)); }
static float smooth(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
static FastNoise::SmartNode<> simplex(float scale, int octaves = 1) {
    auto source = FastNoise::New<FastNoise::Simplex>();
    source->SetScale(scale);
    if (octaves <= 1)
        return source;
    auto fractal = FastNoise::New<FastNoise::FractalFBm>();
    fractal->SetSource(source);
    fractal->SetOctaveCount(octaves);
    fractal->SetGain(.5f);
    return fractal;
}
bool valid_noise(const NoiseSettings &s, int min_y, std::string &error) {
    auto finite = [](float v) { return std::isfinite(v); };
    for (float v : {s.surface_margin, s.surface_fade, s.cheese_scale, s.cheese_threshold, s.deep_threshold,
                    s.tunnel_scale, s.tunnel_width, s.squeeze_scale, s.squeeze_width, s.fissure_scale,
                    s.fissure_width, s.region_scale, s.pillar_spacing, s.hell_floor_amplitude,
                    s.hell_ceiling_amplitude, s.hell_scale, s.hell_pillar_spacing, s.transition_height,
                    s.lava_sea_scale, s.lava_sea_threshold, s.lava_sea_depth, s.lava_sea_height, s.spike_chance,
                    s.hell_spike_chance, s.pillar_chance})
        if (!finite(v)) {
            error = "Cave noise settings must be finite";
            return false;
        }
    if (s.surface_margin < 4 || s.surface_fade < 1 || s.cheese_scale < 8 || s.tunnel_scale < 8 ||
        s.squeeze_scale < 8 || s.fissure_scale < 8 || s.region_scale < 8 || s.hell_scale < 8 ||
        s.lava_sea_scale < 8 || s.pillar_spacing < 8 || s.hell_pillar_spacing < 24 || s.transition_height < 1) {
        error = "Cave noise scales are too small";
        return false;
    }
    if (s.hell_floor - s.hell_floor_amplitude < min_y + 12 || s.hell_lava <= s.hell_floor - s.hell_floor_amplitude ||
        s.hell_ceiling - s.hell_ceiling_amplitude <= s.hell_floor + s.hell_floor_amplitude + 24 ||
        s.lava_sea_threshold <= 0 || s.lava_sea_threshold >= 1 || s.lava_seas.size() > 3) {
        error = "Invalid underworld or lava sea levels";
        return false;
    }
    for (int level : s.lava_seas)
        if (level - s.lava_sea_depth <= s.hell_ceiling + s.hell_ceiling_amplitude + 8) {
            error = "Lava seas must lie above the underworld roof";
            return false;
        }
    for (float chance : {s.spike_chance, s.hell_spike_chance, s.pillar_chance})
        if (chance < 0 || chance > 1) {
            error = "Cave spike chances must be within 0..1";
            return false;
        }
    return true;
}
NoiseField::NoiseField(NoiseSettings settings, int value, int floor) : cfg(std::move(settings)), seed(value), min_y(floor) {
    static std::atomic<uint64_t> instances{0};
    identity = mix(++instances * 0xd1b54a32d192ed03ULL);
    cheese = simplex(cfg.cheese_scale, 3);
    tunnel_a = simplex(cfg.tunnel_scale);
    tunnel_b = simplex(cfg.tunnel_scale);
    width = simplex(64);
    squeeze = simplex(cfg.squeeze_scale);
    fissure = simplex(cfg.fissure_scale);
    region = simplex(cfg.region_scale);
    hell_floor = simplex(cfg.hell_scale, 3);
    hell_roof = simplex(cfg.hell_scale * .8f, 3);
    seas = simplex(cfg.lava_sea_scale, 2);
}
int NoiseField::lowest() const { return int(std::floor(cfg.hell_floor - cfg.hell_floor_amplitude)) - 1; }
void NoiseField::lattice3(int n, const float *x, const float *y, const float *z, std::vector<Channels> &out) const {
    out.resize(n);
    std::vector<float> values(n), sy(n);
    auto run = [&](const FastNoise::SmartNode<> &node, float squash, int salt, float Channels::*field) {
        for (int i = 0; i < n; ++i)
            sy[i] = y[i] * squash;
        node->GenPositionArray3D(values.data(), n, x, sy.data(), z, 0, 0, 0, seed + salt);
        for (int i = 0; i < n; ++i)
            out[i].*field = values[i];
    };
    run(cheese, 1.6f, 7, &Channels::cheese);
    run(tunnel_a, 1.25f, 11, &Channels::tunnel_a);
    run(tunnel_b, 1.25f, 23, &Channels::tunnel_b);
    run(width, 1.f, 37, &Channels::width);
    run(squeeze, 4.f, 41, &Channels::squeeze);
    run(fissure, .3f, 53, &Channels::fissure);
    run(region, .5f, 67, &Channels::region);
}
void NoiseField::lattice2(int n, const float *x, const float *z, std::vector<Columns> &out) const {
    out.resize(n);
    std::vector<float> values(n);
    hell_floor->GenPositionArray2D(values.data(), n, x, z, 0, 0, seed + 71);
    for (int i = 0; i < n; ++i)
        out[i].floor = values[i];
    hell_roof->GenPositionArray2D(values.data(), n, x, z, 0, 0, seed + 79);
    for (int i = 0; i < n; ++i)
        out[i].roof = values[i];
    for (size_t s = 0; s < 3; ++s) {
        if (s < cfg.lava_seas.size())
            seas->GenPositionArray2D(values.data(), n, x, z, 0, 0, seed + 83 + int(s) * 7);
        else
            std::fill(values.begin(), values.end(), -1.f);
        for (int i = 0; i < n; ++i)
            out[i].seas[s] = values[i];
    }
}
// One shared operation order keeps scalar and batched samples bit-identical.
static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
static constexpr float stair_shaft = 5.5f;
NoiseField::Pillars NoiseField::pillars(int x, int z) const {
    Pillars out;
    {
        const float spacing = cfg.pillar_spacing;
        const int cx = int(std::floor(x / spacing)), cz = int(std::floor(z / spacing));
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const uint64_t h = hash(seed, cx + dx, 0, cz + dz, 101);
                const float kind = unit(h);
                if (kind >= .78f)
                    continue;
                const float px = (cx + dx + .2f + .6f * unit(h + 1)) * spacing;
                const float pz = (cz + dz + .2f + .6f * unit(h + 2)) * spacing;
                // Most are broad columns; a fifth are one-block "mini" columns.
                const float radius = kind < .6f ? 1.7f + 2.8f * unit(h + 3) : .62f;
                const float ox = x + .5f - px, oz = z + .5f - pz;
                const float d2 = ox * ox + oz * oz;
                if (d2 < radius * radius)
                    out.hall[out.halls++] = {d2, radius, 6.283f * unit(h + 4)};
            }
    }
    {
        const float spacing = 30;
        const int cx = int(std::floor(x / spacing)), cz = int(std::floor(z / spacing));
        for (int dz = -1; dz <= 1 && !out.sea; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const uint64_t h = hash(seed, cx + dx, 1, cz + dz, 131);
                if (unit(h) >= .45f)
                    continue;
                const float px = (cx + dx + .2f + .6f * unit(h + 1)) * spacing;
                const float pz = (cz + dz + .2f + .6f * unit(h + 2)) * spacing;
                const float radius = 1.4f + 2.4f * unit(h + 3);
                const float ox = x + .5f - px, oz = z + .5f - pz;
                if (ox * ox + oz * oz < radius * radius) {
                    out.sea = true;
                    break;
                }
            }
    }
    {
        const float spacing = cfg.hell_pillar_spacing;
        const int cx = int(std::floor(x / spacing)), cz = int(std::floor(z / spacing));
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const uint64_t h = hash(seed, cx + dx, 2, cz + dz, 151);
                if (unit(h) >= .62f)
                    continue;
                const float px = (cx + dx + .25f + .5f * unit(h + 1)) * spacing;
                const float pz = (cz + dz + .25f + .5f * unit(h + 2)) * spacing;
                // Mostly columns a few metres across, now and then a massive one.
                const float size = unit(h + 3);
                const bool descent = unit(h + 5) < cfg.descent_chance;
                float base = 3.5f + 7.f * size + (size > .85f ? 9.f : 0.f);
                if (descent)
                    base = std::max(base, stair_shaft + 6.f);
                const float ox = x + .5f - px, oz = z + .5f - pz;
                const float d2 = ox * ox + oz * oz;
                if (descent && d2 < 32.f * 32.f && (!out.descent || d2 < out.stair_radius * out.stair_radius)) {
                    out.descent = true;
                    out.stair = d2 < stair_shaft * stair_shaft;
                    out.stair_radius = std::sqrt(d2);
                    out.stair_angle = std::atan2(oz, ox);
                    out.exit_angle = 6.2831853f * unit(h + 6) - 3.1415927f;
                }
                // Roughness (<6), flare (<23) and wobble (1.3) bound the radius.
                const float reach = base + 31.f;
                if (d2 < reach * reach)
                    out.hell[out.hells++] = {d2, base, 6.283f * unit(h + 4)};
            }
    }
    {
        const float spacing = 72;
        const int cx = int(std::floor(x / spacing)), cz = int(std::floor(z / spacing));
        for (int dz = -1; dz <= 1 && !out.fall; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const uint64_t h = hash(seed, cx + dx, 3, cz + dz, 241);
                if (unit(h) >= cfg.lava_fall_chance)
                    continue;
                const float px = (cx + dx + .15f + .7f * unit(h + 1)) * spacing;
                const float pz = (cz + dz + .15f + .7f * unit(h + 2)) * spacing;
                const float radius = 1.1f + 1.3f * unit(h + 3);
                const float ox = x + .5f - px, oz = z + .5f - pz;
                if (ox * ox + oz * oz < radius * radius) {
                    out.fall = true;
                    break;
                }
            }
    }
    return out;
}
// One revolution of the descent stair per this many metres, on a ledge this wide.
static constexpr float stair_pitch = 9.f, stair_ledge = 2.6f;
static float angle_gap(float a, float b) {
    float d = std::fmod(std::abs(a - b), 6.2831853f);
    return d > 3.1415927f ? 6.2831853f - d : d;
}
NoiseCell NoiseField::classify(int y, float surface, const Channels &c, const Columns &k, const Pillars &p) const {
    if (y <= min_y + 6)
        return NoiseCell::solid;
    const float depth = surface - y;
    if (!(depth >= cfg.surface_margin))
        return NoiseCell::solid;
    const float floor = cfg.hell_floor + k.floor * cfg.hell_floor_amplitude;
    const float roof = cfg.hell_ceiling + k.roof * cfg.hell_ceiling_amplitude + 4.f * c.cheese;
    if (y <= floor)
        return NoiseCell::solid;
    const float descent_top = cfg.hell_ceiling + cfg.descent_height;
    if (p.descent) {
        // Landing chamber where the stair begins, under the deep caves.
        const float rise = float(y) - descent_top;
        if (rise > -6.f && p.stair_radius * p.stair_radius + rise * rise * 1.8f < 100.f)
            return NoiseCell::air;
        if (y < roof - 5.f) {
            const float above_floor = float(y) - floor;
            // Doorway at the base, onto the underworld floor.
            if (above_floor > 0.f && above_floor < 5.f && angle_gap(p.stair_angle, p.exit_angle) < .3f)
                return NoiseCell::air;
            // Windows over the lava sea, one per landing, turning as it climbs.
            const float landing = std::floor(above_floor / 26.f);
            if (above_floor > 14.f && above_floor - landing * 26.f < 3.f && p.stair_radius < 26.f &&
                angle_gap(p.stair_angle, p.exit_angle + 3.1415927f + landing * 2.1f) < .2f)
                return NoiseCell::air;
        }
    }
    if (p.stair && y < descent_top + 6.f) {
        // A spiral ledge around a central drop, from a chamber below the deep
        // caves down to the underworld floor, all inside one hollow pillar.
        const float turn = (float(y) - cfg.hell_floor) / stair_pitch - p.stair_angle / 6.2831853f;
        const bool ledge = p.stair_radius > stair_shaft - stair_ledge && turn - std::floor(turn) < 1.f / stair_pitch;
        return ledge ? NoiseCell::solid : NoiseCell::air;
    }
    if (y < roof) {
        if (p.hells) {
            const float roughness = 4.f * c.cheese + 2.f * c.tunnel_a;
            const float low = std::max(0.f, 1.f - (y - floor) / 24.f), high = std::max(0.f, 1.f - (roof - y) / 30.f);
            for (int i = 0; i < p.hells; ++i) {
                float radius = p.hell[i].base + roughness;
                radius += 10.f * low * low + 13.f * high * high;
                radius += 1.3f * std::sin(y * .09f + p.hell[i].phase);
                if (p.hell[i].distance2 < radius * radius)
                    return NoiseCell::solid;
            }
        }
        if (p.fall)
            return NoiseCell::lava;
        return y <= cfg.hell_lava ? NoiseCell::lava : NoiseCell::air;
    }
    // The underworld roof stays whole: the descent pillars are the way down.
    if (y < roof + cfg.roof_seal)
        return NoiseCell::solid;
    const float fade = smooth(cfg.surface_margin, cfg.surface_margin + cfg.surface_fade, depth);
    const float open = 1.f - std::clamp((y - roof) / cfg.transition_height, 0.f, 1.f);
    // Lava seas: a bowl under each sea's mask, and every other cave crossing
    // the sea's area fills with the same lava below its level, so the sea
    // fills the whole cavern. A rock ring and floor keep that lava enclosed.
    int fill_level = INT_MIN;
    for (size_t i = 0; i < cfg.lava_seas.size(); ++i) {
        const int level = cfg.lava_seas[i];
        const float mask = k.seas[i];
        if (y > level + cfg.lava_sea_height + 2 || y < level - 46)
            continue;
        // Rock wraps the descent stair where it passes a lava sea.
        if (p.descent && mask > cfg.lava_sea_threshold - .22f)
            return NoiseCell::solid;
        const float shore = (mask - cfg.lava_sea_threshold) / (1.f - cfg.lava_sea_threshold);
        if (shore > 0) {
            const float profile = smooth(0.f, .7f, shore);
            const float below = cfg.lava_sea_depth * profile;
            // At least a few blocks of air over all of the lava, so the roof
            // never sinks onto the shore and hides part of the sea.
            const float above = std::max(cfg.lava_sea_height * profile * (.72f + .28f * c.cheese), 3.5f);
            if (y > level - below && y < level + above) {
                if (y > level - below + 1 && p.sea)
                    return NoiseCell::solid;
                return y <= level ? NoiseCell::lava : NoiseCell::air;
            }
        }
        if (mask > cfg.lava_sea_threshold - .14f) {
            if (y < level - 40)
                return NoiseCell::solid; // floor under the flooded caves
            if (y <= level)
                fill_level = level;
        } else if (mask > cfg.lava_sea_threshold - .22f && y <= level + 2) {
            return NoiseCell::solid; // ring around the flooded area
        }
    }
    const NoiseCell carved = fill_level != INT_MIN ? NoiseCell::lava : NoiseCell::air;
    const float threshold = lerp(cfg.cheese_threshold, cfg.deep_threshold, open * open) - .1f * c.region;
    if (c.cheese * fade > threshold) {
        bool pillar = false;
        for (int i = 0; i < p.halls && !pillar; ++i) {
            const float radius = p.hall[i].base * (.8f + .2f * std::sin(y * .23f + p.hall[i].phase));
            pillar = p.hall[i].distance2 < radius * radius;
        }
        if (!pillar)
            return carved;
    }
    // Region style: tunnels everywhere, wider in hall regions, thinner where the
    // rock turns into crawlspaces or fissures.
    const float tunnel = cfg.tunnel_width * (.28f + 1.1f * (c.width * .5f + .5f)) * (1.f - .35f * std::abs(c.region)) * fade;
    if (std::abs(c.tunnel_a) < tunnel && std::abs(c.tunnel_b) < tunnel)
        return carved;
    if (c.region > .12f && c.width > -.1f) {
        const float gate = smooth(.12f, .4f, c.region) * smooth(-.1f, .3f, c.width);
        if (std::abs(c.squeeze) < cfg.squeeze_width * gate * fade && std::abs(c.tunnel_a) < .45f)
            return carved;
    }
    if (c.region < -.12f && std::abs(c.tunnel_b) < .3f) {
        const float gate = smooth(.12f, .4f, -c.region) * smooth(.3f, .1f, std::abs(c.tunnel_b));
        if (std::abs(c.fissure) < cfg.fissure_width * gate * fade)
            return carved;
    }
    return NoiseCell::solid;
}
static NoiseField::Channels mix3(const NoiseField::Channels &a, const NoiseField::Channels &b, float t) {
    return {lerp(a.cheese, b.cheese, t),     lerp(a.tunnel_a, b.tunnel_a, t), lerp(a.tunnel_b, b.tunnel_b, t),
            lerp(a.width, b.width, t),       lerp(a.squeeze, b.squeeze, t),   lerp(a.fissure, b.fissure, t),
            lerp(a.region, b.region, t)};
}
static NoiseField::Columns mix2(const NoiseField::Columns &a, const NoiseField::Columns &b, float t) {
    return {lerp(a.floor, b.floor, t), lerp(a.roof, b.roof, t),
            {lerp(a.seas[0], b.seas[0], t), lerp(a.seas[1], b.seas[1], t), lerp(a.seas[2], b.seas[2], t)}};
}
NoiseCell NoiseField::at(int x, int y, int z, float surface) const {
    if (y <= lowest() || !(surface - y >= cfg.surface_margin) || y <= min_y + 6)
        return NoiseCell::solid;
    const int lx = floor_div(x, lattice), ly = floor_div(y, lattice), lz = floor_div(z, lattice);
    float px[8], py[8], pz[8];
    for (int i = 0; i < 8; ++i) {
        px[i] = float((lx + (i & 1)) * lattice);
        py[i] = float((ly + (i >> 1 & 1)) * lattice);
        pz[i] = float((lz + (i >> 2)) * lattice);
    }
    // Scalar callers scan columns cell by cell, so neighbouring samples share
    // lattice corners. A small per-thread cache keeps each corner's noise; a
    // lattice point's value is independent of the batch it was computed in.
    struct Slot3 { uint64_t key = 0; Channels value; };
    struct Slot2 { uint64_t key = 0; Columns value; };
    thread_local std::array<Slot3, 2048> cache3;
    thread_local std::array<Slot2, 512> cache2;
    auto key_of = [&](int a, int b, int c, uint64_t salt) {
        return mix(identity ^ salt ^ (uint64_t(uint32_t(a)) << 1)) ^ mix(uint64_t(uint32_t(b)) * 0x9e3779b97f4a7c15ULL) ^
               mix(uint64_t(uint32_t(c)) + 0x632be59bd9b4e019ULL) | 1;
    };
    Channels c[8];
    Columns k[4];
    {
        float mx[8], my[8], mz[8];
        int missing[8], count = 0;
        uint64_t keys[8];
        for (int i = 0; i < 8; ++i) {
            keys[i] = key_of(lx + (i & 1), ly + (i >> 1 & 1), lz + (i >> 2), 3);
            auto &slot = cache3[keys[i] % cache3.size()];
            if (slot.key == keys[i])
                c[i] = slot.value;
            else {
                mx[count] = px[i], my[count] = py[i], mz[count] = pz[i];
                missing[count++] = i;
            }
        }
        if (count) {
            std::vector<Channels> fresh;
            lattice3(count, mx, my, mz, fresh);
            for (int j = 0; j < count; ++j) {
                c[missing[j]] = fresh[j];
                cache3[keys[missing[j]] % cache3.size()] = {keys[missing[j]], fresh[j]};
            }
        }
    }
    {
        float qx[4] = {px[0], px[1], px[0], px[1]}, qz[4] = {pz[0], pz[0], pz[4], pz[4]};
        float mx[4], mz[4];
        int missing[4], count = 0;
        uint64_t keys[4];
        for (int i = 0; i < 4; ++i) {
            keys[i] = key_of(lx + (i & 1), 0, lz + (i >> 1), 2);
            auto &slot = cache2[keys[i] % cache2.size()];
            if (slot.key == keys[i])
                k[i] = slot.value;
            else {
                mx[count] = qx[i], mz[count] = qz[i];
                missing[count++] = i;
            }
        }
        if (count) {
            std::vector<Columns> fresh;
            lattice2(count, mx, mz, fresh);
            for (int j = 0; j < count; ++j) {
                k[missing[j]] = fresh[j];
                cache2[keys[missing[j]] % cache2.size()] = {keys[missing[j]], fresh[j]};
            }
        }
    }
    const float fx = float(x - lx * lattice) / lattice, fy = float(y - ly * lattice) / lattice;
    const float fz = float(z - lz * lattice) / lattice;
    // Index bit 0 is x, bit 1 y, bit 2 z.
    // x, then z, then y: fills interpolate each lattice level of a column once.
    const auto low = mix3(mix3(c[0], c[1], fx), mix3(c[4], c[5], fx), fz);
    const auto high = mix3(mix3(c[2], c[3], fx), mix3(c[6], c[7], fx), fz);
    const auto channels = mix3(low, high, fy);
    const auto columns = mix2(mix2(k[0], k[1], fx), mix2(k[2], k[3], fx), fz);
    return classify(y, surface, channels, columns, pillars(x, z));
}
void NoiseField::fill(int x0, int z0, int step, int nx, int nz, int y0, int ystep, int ny, const float *surface,
                      std::vector<uint8_t> &out) const {
    out.assign(size_t(nx) * nz * ny, uint8_t(NoiseCell::solid));
    if (nx <= 0 || nz <= 0 || ny <= 0)
        return;
    float top = -1e9f;
    for (int i = 0; i < nx * nz; ++i)
        if (std::isfinite(surface[i]))
            top = std::max(top, surface[i]);
    const int ylo = std::max({y0, lowest() + 1, min_y + 7});
    const int yhi = std::min(y0 + (ny - 1) * ystep, int(std::floor(top - cfg.surface_margin)));
    if (ylo > yhi)
        return;
    const int lx0 = floor_div(x0, lattice), lx1 = floor_div(x0 + (nx - 1) * step, lattice) + 1;
    const int lz0 = floor_div(z0, lattice), lz1 = floor_div(z0 + (nz - 1) * step, lattice) + 1;
    const int ly0 = floor_div(ylo, lattice), ly1 = floor_div(yhi, lattice) + 1;
    const int sx = lx1 - lx0 + 1, sy = ly1 - ly0 + 1, sz = lz1 - lz0 + 1;
    std::vector<float> px, py, pz;
    px.reserve(size_t(sx) * sy * sz);
    py.reserve(px.capacity());
    pz.reserve(px.capacity());
    for (int z = 0; z < sz; ++z)
        for (int y = 0; y < sy; ++y)
            for (int x = 0; x < sx; ++x) {
                px.push_back(float((lx0 + x) * lattice));
                py.push_back(float((ly0 + y) * lattice));
                pz.push_back(float((lz0 + z) * lattice));
            }
    std::vector<Channels> c;
    lattice3(int(px.size()), px.data(), py.data(), pz.data(), c);
    std::vector<float> qx, qz;
    for (int z = 0; z < sz; ++z)
        for (int x = 0; x < sx; ++x) {
            qx.push_back(float((lx0 + x) * lattice));
            qz.push_back(float((lz0 + z) * lattice));
        }
    std::vector<Columns> k;
    lattice2(int(qx.size()), qx.data(), qz.data(), k);
    auto at3 = [&](int x, int y, int z) -> const Channels & { return c[(size_t(z) * sy + y) * sx + x]; };
    std::vector<Channels> level(sy);
    for (int iz = 0; iz < nz; ++iz)
        for (int ix = 0; ix < nx; ++ix) {
            const float height = surface[iz * nx + ix];
            const int x = x0 + ix * step, z = z0 + iz * step;
            const int lx = floor_div(x, lattice) - lx0, lz = floor_div(z, lattice) - lz0;
            const float fx = float(x - (lx + lx0) * lattice) / lattice, fz = float(z - (lz + lz0) * lattice) / lattice;
            const auto columns = mix2(mix2(k[size_t(lz) * sx + lx], k[size_t(lz) * sx + lx + 1], fx),
                                      mix2(k[size_t(lz + 1) * sx + lx], k[size_t(lz + 1) * sx + lx + 1], fx), fz);
            uint8_t *cells = out.data() + (size_t(iz) * nx + ix) * ny;
            const int top_y = std::min(yhi, int(std::floor(height - cfg.surface_margin)));
            if (top_y < ylo)
                continue;
            for (int ly = 0; ly < sy; ++ly)
                level[ly] = mix3(mix3(at3(lx, ly, lz), at3(lx + 1, ly, lz), fx),
                                 mix3(at3(lx, ly, lz + 1), at3(lx + 1, ly, lz + 1), fx), fz);
            const auto nearby = pillars(x, z);
            for (int iy = 0; iy < ny; ++iy) {
                const int y = y0 + iy * ystep;
                if (y < ylo || y > top_y || !(height - y >= cfg.surface_margin))
                    continue;
                const int ly = floor_div(y, lattice) - ly0;
                const float fy = float(y - (ly + ly0) * lattice) / lattice;
                cells[iy] = uint8_t(classify(y, height, mix3(level[ly], level[ly + 1], fy), columns, nearby));
            }
        }
}
Spikes NoiseField::spikes(int x, int z, int floor, int roof, int run) const {
    Spikes out;
    if (run <= 0)
        return out;
    const bool has_floor = floor != INT_MIN, has_roof = roof != INT_MAX;
    // Clusters: some ceilings bristle with dripstone while others stay bare.
    const float cluster = .25f + 1.75f * unit(hash(seed, floor_div(x, 7), 3, floor_div(z, 7), 171));
    if (has_floor && has_roof && run >= 3 && run <= 9 &&
        unit(hash(seed, x, floor, z, 181)) < cfg.pillar_chance * cluster) {
        out.pillar = true;
        return out;
    }
    if (has_roof) {
        const bool deep = hell(roof);
        const uint64_t h = hash(seed, x, roof, z, 191);
        if (unit(h) < (deep ? cfg.hell_spike_chance : cfg.spike_chance) * cluster) {
            const int longest = deep ? 9 : 5;
            int length = 1 + int((h >> 8) % uint64_t(longest));
            length = std::min(length, run - 3);
            if (length > 0)
                out.down = length;
        }
    }
    if (has_floor) {
        const bool deep = hell(floor);
        const uint64_t h = hash(seed, x, floor, z, 211);
        if (unit(h) < (deep ? cfg.hell_spike_chance : cfg.spike_chance) * .6f * cluster) {
            const int longest = deep ? 5 : 3;
            int length = 1 + int((h >> 8) % uint64_t(longest));
            length = std::min(length, run - out.down - 2);
            if (length > 0)
                out.up = length;
        }
    }
    return out;
}
} // namespace altaria::caves
