#include "voxel_core.h"
#include "block_shapes.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
namespace altaria {
static uint64_t hash(int x, int z, int seed) {
    uint64_t n = uint32_t(x) * 0x9e3779b185ebca87ULL ^ uint32_t(z) * 0xc2b2ae3d27d4eb4fULL ^ uint32_t(seed);
    n = (n ^ (n >> 30)) * 0xbf58476d1ce4e5b9ULL;
    return n ^ (n >> 27);
}
static float smooth(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
// Low-frequency patches decide where woods grow; trees gather in them and
// only a few stand alone in open country.
static float value_noise(float x, float z, float scale, int seed) {
    const float fx = x / scale, fz = z / scale;
    const int ix = int(std::floor(fx)), iz = int(std::floor(fz));
    float u = fx - ix, v = fz - iz;
    u = u * u * (3 - 2 * u);
    v = v * v * (3 - 2 * v);
    const auto at = [&](int a, int b) { return float(hash(a, b, seed) & 65535) / 65535.f; };
    return (at(ix, iz) * (1 - u) + at(ix + 1, iz) * u) * (1 - v) + (at(ix, iz + 1) * (1 - u) + at(ix + 1, iz + 1) * u) * v;
}
static float forest_patch(float x, float z, int seed) {
    return value_noise(x, z, 170.f, seed ^ 0x2f6b) * .68f + value_noise(x, z, 53.f, seed ^ 0x51d3) * .32f;
}
std::vector<TreeSite> Generator::tree_sites(int minx, int minz, int maxx, int maxz, bool check_caves) const {
    return plant_sites(minx, minz, maxx, maxz, check_caves, false);
}
std::vector<TreeSite> Generator::shrub_sites(int minx, int minz, int maxx, int maxz, bool check_caves) const {
    return plant_sites(minx, minz, maxx, maxz, check_caves, true);
}
std::vector<TreeSite> Generator::plant_sites(int minx, int minz, int maxx, int maxz, bool check_caves,
                                             bool shrubs) const {
    std::vector<TreeSite> out;
    if (!cfg.vegetation || !registry.find(shrubs ? cfg.bush_block : "altaria:leaves") ||
        (!shrubs && !registry.find("altaria:wood")))
        return out;
    // Trees use a half-spacing grid so woods can be dense; open ground keeps
    // only rare solitary trees.
    const int spacing = shrubs ? 8 : std::max(6, cfg.tree_spacing / 2);
    std::vector<float> xs,zs;
    std::vector<uint64_t> hashes;
    for (int gz = int(std::floor(double(minz) / spacing));
         gz <= int(std::floor(double(maxz - 1) / spacing)); gz++)
        for (int gx = int(std::floor(double(minx) / spacing));
             gx <= int(std::floor(double(maxx - 1) / spacing)); gx++) {
            auto h = hash(gx, gz, shrubs ? int(uint32_t(seed) ^ 0x6dc1423du) : seed);
            const int margin = 1;
            int x = gx * spacing + margin + int((h >> 16) % uint64_t(spacing - 2 * margin));
            int z = gz * spacing + margin + int((h >> 32) % uint64_t(spacing - 2 * margin));
            if (x < minx || x >= maxx || z < minz || z >= maxz)
                continue;
            xs.push_back(float(x));zs.push_back(float(z));hashes.push_back(h);
        }
    const auto candidates = columns(xs, zs, nullptr, false);
    for(size_t i=0;i<candidates.size();i++) {
        const int x=int(xs[i]),z=int(zs[i]);
        const uint64_t h=hashes[i];
        const auto &c=candidates[i];
        auto &biome = cfg.biomes[c.biome];
        float valley_boost = 1.f + .32f * c.valley * biome.humidity;
        float mountain_thin = 1.f - .65f * smooth(.35f, .78f, c.mountain);
        // Humid biomes turn more of the land into woods.
        const float patch = forest_patch(float(x), float(z), seed);
        const float woods = smooth(.70f - .26f * biome.humidity, .80f - .22f * biome.humidity, patch);
        const float edge = smooth(.60f - .26f * biome.humidity, .72f - .22f * biome.humidity, patch);
        const float density = shrubs ? (.10f + .22f * biome.humidity) * (.65f + .35f * c.valley) * (.35f + .9f * edge) :
                                      biome.tree_density * valley_boost * (.03f + 1.6f * woods);
        float effective_density = std::clamp(density * mountain_thin, 0.f, .9f);
        if (float(h & 65535) / 65535 > effective_density || c.height < cfg.sea_level + 2 ||
            c.height > cfg.snow_height - 6 || c.cliff > .24f || c.slope > .46f ||
            c.surface == registry.stone || c.surface == registry.sand)
            continue;
        const int expected_y = int(std::floor(c.height));
        int floor_y = std::min(expected_y + 5, int(std::floor(c.height + cfg.cliff_strength * c.cliff)));
        for (; floor_y >= expected_y - 3; --floor_y)
            if (density_solid(c, x, floor_y, z) && !natural_arch_carved(c, x, floor_y, z))
                break;
        if (floor_y < cfg.sea_level + 1 || std::abs(floor_y - expected_y) > 3 ||
            floor_y < c.water_top || (check_caves && cave_runtime &&
            cave_runtime->sample(cave_runtime->point_query(x, z), x, floor_y, z, c.height).carved))
            continue;

        constexpr RGB leaf_colors[] = {
            {.39f, .66f, .26f}, {.27f, .56f, .35f}, {.56f, .68f, .28f},
            {.40f, .59f, .30f}, {.50f, .41f, .64f}
        };
        Appearance canopy;
        canopy.painted = true;
        canopy.tint = pack_rgb(leaf_colors[(h >> 40) % std::size(leaf_colors)]);
        canopy.preserve = 400;
        canopy.brightness = uint16_t(1130 + (h >> 38) % 220);
        const auto form = shrubs ? TreeForm::Shrub : TreeForm((h >> 50) & 3);
        const int variation = int((h >> 56) % 5);
        const int height = form == TreeForm::Shrub ? 0 : form == TreeForm::Spire ? 10 + variation :
            form == TreeForm::Broad ? 8 + variation : 5 + variation;
        const int radius = form == TreeForm::Shrub ? 1 + int((h >> 61) & 1) :
            form == TreeForm::Broad ? 3 + int((h >> 61) & 1) : form == TreeForm::Round ? 2 + variation / 3 : 2;
        if (floor_y + height + 1 < cfg.max_y)
            out.push_back({{x, floor_y, z}, height, radius, int((h >> 54) & 1), canopy, form, h});
    }
    return out;
}
int TreeSite::crown_depth() const {
    switch (form) {
    case TreeForm::Shrub: return radius;
    case TreeForm::Spire: return std::max(6, height - 2);
    case TreeForm::Open: return 6;
    case TreeForm::Broad: return 5;
    case TreeForm::Round: return std::min(6, height);
    }
    return 1;
}
namespace {
// Deterministic stream from a tree's pattern.
struct TreeRandom {
    uint64_t state;
    uint64_t next() {
        state += 0x9e3779b97f4a7c15ULL;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    float unit() { return float(next() & 0xffffff) / float(0xffffff); }
    int range(int lo, int hi) { return lo + int(next() % uint64_t(hi - lo + 1)); }
};
constexpr int dir_x[4] = {-1, 1, 0, 0}, dir_z[4] = {0, 0, -1, 1};
enum class BranchStyle { Flat, Upturned, Stepped, Drooping };
struct LeafBlob { float x, y, z, rx, ry, rz; };
} // namespace
// Trunk of pillars; branches leave it through one-sided stubs or forks and
// bend with mini-pillar elbows (up, down, stepped). Leaf blobs sit on every
// branch tip and on the crown, so each tree gets its own silhouette.
std::vector<TreePart> Generator::tree_parts(const TreeSite &tree) const {
    std::vector<TreePart> out;
    const auto [x, y, z] = tree.base;
    const bool shrub = tree.form == TreeForm::Shrub;
    const BlockId wood = registry.find("altaria:wood");
    const BlockId leaves = registry.find(shrub ? cfg.bush_block : "altaria:leaves");
    TreeRandom rng{tree.pattern ^ 0x5bd1e995u};
    std::vector<LeafBlob> blobs;
    struct Wood { IVec3 p; uint8_t shape; };
    std::vector<Wood> woods;
    std::vector<IVec3> tips; // Wood ends that must be wrapped in leaves.
    constexpr int reach = 5; // Horizontal limit; tiles gather trees within 6 blocks.
    const auto inside = [&](int dx, int dz) { return std::abs(dx) <= reach && std::abs(dz) <= reach; };
    // Wood stops one block short of the limit so every branch tip keeps room
    // for the leaves that close around it.
    const auto wood_inside = [&](int dx, int dz) { return std::abs(dx) < reach && std::abs(dz) < reach; };

    if (shrub) {
        const float r = float(tree.radius) + .3f;
        blobs.push_back({0, 1.f + r * .35f, 0, r + .2f, r * .8f, r});
        if (rng.unit() < .6f) {
            const int d = rng.range(0, 3);
            blobs.push_back({dir_x[d] * r * .7f, .8f + r * .2f, dir_z[d] * r * .7f, r * .7f, r * .6f, r * .7f});
        }
    } else {
        const int h = tree.height;
        // Spires keep a straight leader with short whorls of flat branches.
        int count = 0, lowest = 0;
        switch (tree.form) {
        case TreeForm::Spire: count = rng.range(4, 6); lowest = 4; break;
        case TreeForm::Broad: count = rng.range(4, 5); lowest = std::max(4, h / 2); break;
        case TreeForm::Open: count = rng.range(3, 4); lowest = std::max(3, h / 2); break;
        default: count = rng.range(2, 3) + (h >= 8); lowest = std::max(3, h / 2); break;
        }
        lowest = std::min(lowest, h - 1);
        const int top = h - 1;
        std::vector<int> used_level(size_t(h + 2), -1); // Direction used at each trunk level.
        int direction = tree.branch_axis == 0 ? int(rng.next() & 1) : 2 + int(rng.next() & 1);
        for (int i = 0; i < count; ++i) {
            const float t = count == 1 ? .5f : float(i) / float(count - 1);
            int level = lowest + int(std::round(t * float(top - lowest)));
            level = std::clamp(level + (tree.form == TreeForm::Spire ? 0 : rng.range(-1, 0)), 2, top);
            // Alternate sides, turning a quarter now and then for a spiral.
            if (i > 0)
                direction = rng.unit() < .6f ? (direction ^ 1) : (direction < 2 ? 2 + int(rng.next() & 1) : int(rng.next() & 1));
            if (used_level[size_t(level)] >= 0) {
                // Only an opposite branch can share a trunk cell (a fork).
                if ((used_level[size_t(level)] ^ 1) != direction) {
                    if (level + 1 > top || used_level[size_t(level + 1)] >= 0)
                        continue;
                    ++level;
                }
            }
            BranchStyle style = BranchStyle::Flat;
            const float roll = rng.unit();
            switch (tree.form) {
            case TreeForm::Spire: style = roll < .75f ? BranchStyle::Flat : BranchStyle::Drooping; break;
            case TreeForm::Broad: style = roll < .45f ? BranchStyle::Stepped : roll < .8f ? BranchStyle::Flat : BranchStyle::Upturned; break;
            case TreeForm::Open: style = roll < .55f ? BranchStyle::Upturned : roll < .8f ? BranchStyle::Drooping : BranchStyle::Stepped; break;
            default: style = roll < .5f ? BranchStyle::Upturned : roll < .8f ? BranchStyle::Stepped : BranchStyle::Flat; break;
            }
            const int dx = dir_x[direction], dz = dir_z[direction];
            const int back = direction ^ 1; // Arm side facing the trunk.
            int length = tree.form == TreeForm::Spire ? rng.range(1, 2) - (level > h - 3 ? 1 : 0) :
                         tree.form == TreeForm::Broad ? rng.range(2, 3) : rng.range(1, 2);
            length = std::max(1, length);
            int cx = 0, cy = level, cz = 0;
            const int axis = direction < 2 ? 0 : 1;
            if (used_level[size_t(level)] >= 0) {
                for (auto &w : woods)
                    if (w.p == IVec3{0, level, 0})
                        w.shape = branch_fork(axis);
            } else {
                woods.push_back({{0, level, 0}, branch_stub(direction)});
            }
            used_level[size_t(level)] = direction;
            const auto step_out = [&](int n) {
                for (int k = 0; k < n && wood_inside(cx + dx, cz + dz); ++k) {
                    cx += dx; cz += dz;
                    woods.push_back({{cx, cy, cz}, horizontal_mini_pillar(axis)});
                }
            };
            const auto rise = [&](int n, bool down) {
                if (!wood_inside(cx + dx, cz + dz))
                    return;
                cx += dx; cz += dz;
                woods.push_back({{cx, cy, cz}, down ? branch_elbow_down(back) : branch_elbow_up(back)});
                for (int k = 0; k < n; ++k) {
                    cy += down ? -1 : 1;
                    woods.push_back({{cx, cy, cz}, uint8_t(BlockShape::MiniPillar)});
                }
            };
            step_out(length);
            switch (style) {
            case BranchStyle::Upturned: rise(rng.range(1, 3), false); break;
            case BranchStyle::Drooping: if (cy > 4) rise(1, true); break;
            case BranchStyle::Stepped:
                // Out, up one, then out again: the shoot bends back to horizontal.
                if (!wood_inside(cx + dx, cz + dz))
                    break;
                rise(0, false);
                ++cy;
                woods.push_back({{cx, cy, cz}, branch_elbow_down(direction)});
                step_out(rng.range(1, 2));
                break;
            default: break;
            }
            const float size = tree.form == TreeForm::Spire ? 1.2f + .3f * float(top - level) / float(std::max(1, top - lowest))
                                                             : 1.8f + 1.f * rng.unit();
            // The leaf blob is centred just past the tip, so the shoot ends
            // inside the foliage rather than poking out beneath it.
            const float lift = style == BranchStyle::Drooping ? .1f : .45f;
            const float ahead = style == BranchStyle::Upturned ? .2f : .5f;
            blobs.push_back({float(cx) + dx * ahead, float(cy) + lift, float(cz) + dz * ahead,
                             size + .4f, size * .85f + .2f, size + .4f});
            tips.push_back({cx, cy, cz});
        }
        for (int dy = 1; dy <= h; ++dy)
            if (used_level[size_t(dy)] < 0)
                woods.push_back({{0, dy, 0}, uint8_t(BlockShape::Pillar)});
        // Crown blob on the leader.
        const float crown = tree.form == TreeForm::Spire ? 1.4f : float(tree.radius) * .8f + .6f;
        blobs.push_back({0, float(h) + (tree.form == TreeForm::Spire ? 1.2f : .9f), 0, crown, crown * .85f + .3f, crown});
        if (tree.form == TreeForm::Spire) {
            blobs.push_back({0, float(h) + 2.6f, 0, .8f, 1.1f, .8f});
            // Tiered cone: wide skirts low down, alternating narrow collars.
            for (int dy = lowest - 1; dy <= h; ++dy) {
                const float t = float(dy - lowest + 1) / float(std::max(1, h - lowest + 1));
                const float r = (2.5f * (1.f - t) + .9f) * ((dy - lowest) % 2 ? .72f : 1.f);
                blobs.push_back({0, float(dy) + .4f, 0, r, .75f, r});
            }
        }
    }

    // Rasterize blobs; keep the closest normalized distance for trimming.
    std::unordered_map<IVec3, float, CoordHash> foliage;
    for (const auto &b : blobs) {
        const int x0 = int(std::floor(b.x - b.rx)), x1 = int(std::ceil(b.x + b.rx));
        const int y0 = int(std::floor(b.y - b.ry)), y1 = int(std::ceil(b.y + b.ry));
        const int z0 = int(std::floor(b.z - b.rz)), z1 = int(std::ceil(b.z + b.rz));
        for (int cy = std::max(shrub ? 1 : 2, y0); cy <= y1; ++cy)
            for (int cz = z0; cz <= z1; ++cz)
                for (int cx = x0; cx <= x1; ++cx) {
                    if (!inside(cx, cz))
                        continue;
                    const float ux = (cx - b.x) / b.rx, uy = (cy - b.y) / b.ry, uz = (cz - b.z) / b.rz;
                    const float d = std::sqrt(ux * ux + uy * uy + uz * uz);
                    if (d > 1.f)
                        continue;
                    const auto hc = hash(x + cx * 31 + cy * 17, z + cz * 13 - cy * 7, int(uint32_t(tree.pattern)));
                    // Dense core, ragged shell.
                    if (d > .62f && float(hc & 255) / 255.f > 1.25f - d)
                        continue;
                    auto [it, added] = foliage.emplace(IVec3{cx, cy, cz}, d);
                    if (!added)
                        it->second = std::min(it->second, d);
                }
    }
    std::unordered_set<IVec3, CoordHash> wood_cells;
    for (const auto &w : woods)
        wood_cells.insert(w.p);
    if (!shrub) {
        tips.push_back({0, tree.height, 0});
        // Close every free face around each tip except the one below the
        // leader; negative distances survive the leaf budget first.
        constexpr int nx[6] = {-1, 1, 0, 0, 0, 0}, ny[6] = {0, 0, 0, 0, 1, -1}, nz[6] = {0, 0, -1, 1, 0, 0};
        for (const auto &t : tips)
            for (int k = 0; k < 6; ++k) {
                const IVec3 p{t.x + nx[k], t.y + ny[k], t.z + nz[k]};
                if (!inside(p.x, p.z) || p.y < 2 || wood_cells.count(p) || (p.x == 0 && p.z == 0 && p.y < tree.height))
                    continue;
                foliage[p] = -1.f;
            }
    }
    std::vector<std::pair<IVec3, float>> leaf_cells(foliage.begin(), foliage.end());
    std::sort(leaf_cells.begin(), leaf_cells.end(), [](const auto &a, const auto &b) {
        if (a.second != b.second)
            return a.second < b.second;
        if (a.first.y != b.first.y)
            return a.first.y < b.first.y;
        if (a.first.z != b.first.z)
            return a.first.z < b.first.z;
        return a.first.x < b.first.x;
    });
    // Whole tree must fit one detachable physics body.
    const size_t budget = 248 - std::min<size_t>(woods.size(), 120);
    if (leaf_cells.size() > budget)
        leaf_cells.resize(budget);
    // Drop ragged-shell leaves that lost contact with the crown: everything
    // kept is face-connected to the wood (or, for shrubs, to the core leaf).
    std::unordered_set<IVec3, CoordHash> kept;
    for (const auto &[p, d] : leaf_cells)
        if (!wood_cells.count(p))
            kept.insert(p);
    std::unordered_set<IVec3, CoordHash> reached;
    std::vector<IVec3> frontier;
    if (shrub) {
        if (!leaf_cells.empty()) {
            frontier.push_back(leaf_cells.front().first);
            reached.insert(leaf_cells.front().first);
        }
    } else {
        frontier.assign(wood_cells.begin(), wood_cells.end());
    }
    while (!frontier.empty()) {
        const IVec3 p = frontier.back();
        frontier.pop_back();
        constexpr int nx[6] = {-1, 1, 0, 0, 0, 0}, ny[6] = {0, 0, 0, 0, 1, -1}, nz[6] = {0, 0, -1, 1, 0, 0};
        for (int k = 0; k < 6; ++k) {
            const IVec3 q{p.x + nx[k], p.y + ny[k], p.z + nz[k]};
            if (kept.count(q) && reached.insert(q).second)
                frontier.push_back(q);
        }
    }
    const auto emit = [&](IVec3 p, BlockId block, uint8_t shape, const Appearance &appearance) {
        if (y + p.y < cfg.max_y)
            out.push_back({{x + p.x, y + p.y, z + p.z}, block, shape, appearance});
    };
    for (const auto &[p, d] : leaf_cells)
        if (reached.count(p))
            emit(p, leaves, 0, tree.canopy);
    for (const auto &w : woods)
        emit(w.p, wood, w.shape, {});
    return out;
}
std::vector<TreePart> Generator::trees(int minx, int minz, int maxx, int maxz) const {
    std::vector<TreePart> out;
    const auto append = [&](const std::vector<TreeSite> &sites) {
        for (const auto &tree : sites)
            for (const auto &part : tree_parts(tree))
                if (part.first.x >= minx && part.first.x < maxx && part.first.z >= minz && part.first.z < maxz)
                    out.push_back(part);
    };
    append(shrub_sites(minx - 3, minz - 3, maxx + 3, maxz + 3, true));
    append(tree_sites(minx - 6, minz - 6, maxx + 6, maxz + 6, true));
    return out;
}
uint8_t Generator::surface_shape(int x, int y, int z, const Column &c) const {
    if (!cfg.surface_shapes || c.cliff > .2f || y != int(std::floor(c.height)))
        return 0;
    float fraction = c.height - std::floor(c.height);
    // Quantize to half-height caps. Flat interiors and cliffs keep full cubes.
    if (fraction < .48f && c.height > cfg.sea_level + 1)
        return 1;
    return 0;
}
} // namespace altaria
