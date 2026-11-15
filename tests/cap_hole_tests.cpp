// Shoots rays from open cave air next to generated shapes (pillars, slabs)
// and checks that every ray meets a mesh triangle before it enters a fully
// solid cell: a ray that slips into rock without crossing a face has found a
// hole in the surface.
#include "game/world_generation/block_shapes.h"
#include "game/world_generation/voxel_core.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
#include <cstdlib>
using namespace altaria;
static Registry blocks() {
    Registry r;
    std::string error;
    const char *ids[] = {"air", "stone", "dirt", "grass", "sand", "water", "snow", "bedrock"};
    for (int i = 0; i < 8; i++) {
        BlockDef b;
        b.id = i;
        b.key = ids[i];
        b.solid = i != 0 && i != 5;
        b.water = i == 5;
        assert(r.add(b, error));
    }
    BlockDef grass;
    grass.id = 35;
    grass.key = "altaria:cave_grass";
    grass.grass_on_rock = true;
    grass.texture_layer = 3;
    assert(r.add(grass, error));
    BlockDef lava;
    lava.id = 13;
    lava.key = "altaria:lava";
    lava.solid = false;
    lava.liquid = true;
    lava.lava = true;
    assert(r.add(lava, error));
    r.lava = 13;
    return r;
}
struct Tri { float a[3], b[3], c[3]; };
static bool hit(const Tri &t, const float o[3], const float d[3], float limit) {
    float e1[3], e2[3], p[3], q[3], s[3];
    for (int i = 0; i < 3; ++i) { e1[i] = t.b[i] - t.a[i]; e2[i] = t.c[i] - t.a[i]; }
    p[0] = d[1] * e2[2] - d[2] * e2[1]; p[1] = d[2] * e2[0] - d[0] * e2[2]; p[2] = d[0] * e2[1] - d[1] * e2[0];
    const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (std::abs(det) < 1e-9f) return false;
    const float inv = 1.f / det;
    for (int i = 0; i < 3; ++i) s[i] = o[i] - t.a[i];
    const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
    if (u < -1e-4f || u > 1 + 1e-4f) return false;
    q[0] = s[1] * e1[2] - s[2] * e1[1]; q[1] = s[2] * e1[0] - s[0] * e1[2]; q[2] = s[0] * e1[1] - s[1] * e1[0];
    const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (v < -1e-4f || u + v > 1 + 1e-4f) return false;
    const float t0 = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    return t0 > -1e-4f && t0 <= limit;
}
int main(int argc, char **argv) {
    caves::NoiseSettings ns;
    ns.enabled = true;
    Settings cfg;
    cfg.min_y = -704;
    cfg.vegetation = false;
    cfg.caves.enabled = true;
    cfg.caves.noise = ns;
    auto r = blocks();
    Generator g(cfg, r, 1337);
    size_t rays = 0, holes = 0, caps_total = 0;
    std::mt19937 rng(7);
    for (TileKey key : {TileKey{0, 0, 0}, TileKey{-64, 160, 0}, TileKey{96, -64, 0}}) {
        auto tile = build_tile(key, 0, g, std::make_shared<EditMap>(), std::make_shared<AppearanceMap>(), false,
                               VerticalWindow{VerticalWindow::underground, -240, -60});
        const auto &v = *tile.volume;
        std::vector<Tri> tris;
        for (const Surface *s : {&tile.solid, &tile.water})
            for (size_t i = 0; i + 2 < s->indices.size(); i += 3) {
                Tri t;
                const Vertex *p[3] = {&s->vertices[s->indices[i]], &s->vertices[s->indices[i + 1]], &s->vertices[s->indices[i + 2]]};
                for (int k = 0; k < 3; ++k) {
                    float *dst = k == 0 ? t.a : k == 1 ? t.b : t.c;
                    dst[0] = p[k]->x; dst[1] = p[k]->y; dst[2] = p[k]->z;
                }
                tris.push_back(t);
            }
        // Bucket triangles by cell for the ray marcher.
        std::unordered_map<int64_t, std::vector<uint32_t>> cells;
        auto cell_key = [](int x, int y, int z) { return (int64_t(x + 64) << 40) | (int64_t(y + 4096) << 16) | int64_t(z + 64); };
        for (uint32_t i = 0; i < tris.size(); ++i) {
            const auto &t = tris[i];
            int lo[3], hi[3];
            for (int a = 0; a < 3; ++a) {
                lo[a] = int(std::floor(std::min({t.a[a], t.b[a], t.c[a]}) - .01f));
                hi[a] = int(std::floor(std::max({t.a[a], t.b[a], t.c[a]}) + .01f));
            }
            for (int x = lo[0]; x <= hi[0]; ++x)
                for (int y = lo[1]; y <= hi[1]; ++y)
                    for (int z = lo[2]; z <= hi[2]; ++z)
                        cells[cell_key(x, y, z)].push_back(i);
        }
        auto solid_cell = [&](int x, int y, int z) {
            if (x < 0 || x > 31 || z < 0 || z > 31) return false;
            const int iy = y - v.min_y;
            if (iy < 0 || iy >= v.height) return false;
            const auto index = (size_t(z + 1) * (v.height + 2) + size_t(iy + 1)) * 34 + size_t(x + 1);
            return r.get(v.at(x, iy, z)).solid && !v.shapes.count(index);
        };
        int printed = 0;
        for (const auto &[index, code] : v.shapes) {
            (void)code;
            const int cx = int(index % 34) - 1, cy = int(index / 34 % (v.height + 2)) - 1, cz = int(index / (34 * (v.height + 2))) - 1;
            if (cx < 2 || cx > 29 || cz < 2 || cz > 29) continue;
            ++caps_total;
            // Start in open air beside the shape.
            float origin[3] = {cx + .5f, float(v.min_y + cy) + 1.6f, cz + .5f};
            {
                bool placed = false;
                for (auto [ox, oz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                    const int qx = cx + ox, qz = cz + oz, qy = cy;
                    if (r.get(v.at(qx, qy, qz)).solid || r.get(v.at(qx, qy, qz)).liquid) continue;
                    origin[0] = qx + .5f; origin[1] = float(v.min_y + qy) + .5f; origin[2] = qz + .5f;
                    placed = true;
                    break;
                }
                if (!placed) continue;
            }
            for (int k = 0; k < 64; ++k) {
                // Random rays sweep the shape and its neighbours.
                std::uniform_real_distribution<float> uni(-1.f, 1.f);
                float d[3] = {uni(rng), uni(rng), uni(rng)};
                const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                for (float &c : d) c /= len;
                ++rays;
                bool met = false;
                for (float t = 0; t < 4.f && !met; t += .02f) {
                    const float p[3] = {origin[0] + d[0] * t, origin[1] + d[1] * t, origin[2] + d[2] * t};
                    const int px = int(std::floor(p[0])), py = int(std::floor(p[1])), pz = int(std::floor(p[2]));
                    for (int ox = -1; ox <= 1 && !met; ++ox)
                        for (int oy = -1; oy <= 1 && !met; ++oy)
                            for (int oz = -1; oz <= 1 && !met; ++oz) {
                                auto it = cells.find(cell_key(px + ox, py + oy, pz + oz));
                                if (it == cells.end()) continue;
                                for (auto i : it->second)
                                    if (hit(tris[i], origin, d, t + .03f)) { met = true; break; }
                            }
                    if (met) break;
                    // Reached deep inside a full rock cell (past its faces) without a hit.
                    const float fx = p[0] - px, fy = p[1] - py, fz = p[2] - pz;
                    if (solid_cell(px, py, pz) && fx > .1f && fx < .9f && fy > .1f && fy < .9f && fz > .1f && fz < .9f) {
                        ++holes;
                        if (printed++ < 6)
                            std::cout << "hole: shape at " << key.x + cx << "," << v.min_y + cy << "," << key.z + cz
                                      << " shape " << int(code) << " ray enters rock at " << key.x + px << "," << py << ","
                                      << key.z + pz << "\n";
                        if (printed == 1)
                            for (int dy = 1; dy >= -1; --dy) {
                                std::cout << " y" << py + dy << ":";
                                for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
                                    const int qx = px + dx, qy = py + dy - v.min_y, qz = pz + dz;
                                    const auto idx = (size_t(qz + 1) * (v.height + 2) + size_t(qy + 1)) * 34 + size_t(qx + 1);
                                    auto sh = v.shapes.find(idx);
                                    std::cout << " " << v.at(qx, qy, qz) << "/" << (sh == v.shapes.end() ? 0 : int(sh->second));
                                }
                                std::cout << "\n";
                            }
                        break;
                    }
                }
            }
        }
    }
    std::cout << "shapes " << caps_total << " rays " << rays << " holes " << holes << "\n";
    if (holes) return 1;
    std::cout << "PASS shape hole tests\n";
}
