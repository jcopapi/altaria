#include "block_shapes.h"
#include "voxel_core.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace altaria {
static double elapsed(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}
static void quad(Surface &s, const std::array<std::array<float, 3>, 4> &p, RGB color,
                 std::array<float, 3> n) {
    int32_t base = int32_t(s.vertices.size());
    for (int i = 0; i < 4; i++) {
        const auto &v = p[i];
        s.vertices.push_back({v[0], v[1], v[2], n[0], n[1], n[2], float(i == 1 || i == 2), float(i >= 2),
                              color.r, color.g, color.b});
    }
    float a[3], b[3];
    for (int i = 0; i < 3; i++) {
        a[i] = p[1][i] - p[0][i];
        b[i] = p[2][i] - p[0][i];
    }
    float facing = (a[1] * b[2] - a[2] * b[1]) * n[0] + (a[2] * b[0] - a[0] * b[2]) * n[1] +
                   (a[0] * b[1] - a[1] * b[0]) * n[2];
    const int cw[] = {0, 2, 1, 0, 3, 2}, ccw[] = {0, 1, 2, 0, 2, 3};
    auto order = facing > 0 ? cw : ccw;
    for (int i = 0; i < 6; i++)
        s.indices.push_back(base + order[i]);
}
static size_t voxel_index(const Volume &v, int x, int y, int z) {
    return ((z + 1) * (v.height + 2) + (y + 1)) * 34 + x + 1;
}
static std::shared_ptr<Volume> make_volume(TileKey key, const Generator &g,
                                           std::shared_ptr<const EditMap> edits,
                                           std::shared_ptr<const AppearanceMap> surfaces,
                                           StageTimings &timings) {
    const auto &s = g.settings();
    const auto &r = g.blocks();
    int step = 1 << key.level, ystep = std::min(step, 4), ny = (s.max_y - s.min_y) / ystep;
    auto v = std::make_shared<Volume>();
    v->step = step;
    v->y_step = ystep;
    v->min_y = s.min_y;
    v->height = ny;
    v->water_surface = s.sea_level + 1;
    v->data.resize(34 * (ny + 2) * 34, r.air);
    std::vector<float> x, z;
    for (int iz = -1; iz <= 32; iz++)
        for (int ix = -1; ix <= 32; ix++) {
            x.push_back(float(key.x + ix * step));
            z.push_back(float(key.z + iz * step));
        }
    v->columns = g.columns(x, z, &timings);
    const auto &cols = v->columns;
    auto clock = std::chrono::steady_clock::now();
    std::vector<float> dx, dy, dz, noise;
    std::vector<size_t> indices;
    for (int iz = -1; iz <= 32; iz++)
        for (int ix = -1; ix <= 32; ix++) {
            const auto &c = cols[(iz + 1) * 34 + ix + 1];
            float band = s.cliff_strength * c.cliff + 1;
            int w_top = std::max(s.sea_level, c.water_top);
            for (int iy = 0; iy < ny; iy++) {
                int y = s.min_y + iy * ystep;
                size_t i = voxel_index(*v, ix, iy, iz);
                if (y == s.min_y) {
                    v->data[i] = r.bedrock;
                    continue;
                }
                if (std::abs(y - c.height) <= band) {
                    dx.push_back(float(key.x + ix * step));
                    dy.push_back(float(y));
                    dz.push_back(float(key.z + iz * step));
                    indices.push_back(i);
                } else if (y < c.height)
                    v->data[i] = r.stone;
                else if (y <= w_top)
                    v->data[i] = r.water;
            }
        }
    g.density_samples(noise, dx, dy, dz);
    for (size_t j = 0; j < indices.size(); j++) {
        int ix = (int(dx[j]) - key.x) / step, iz = (int(dz[j]) - key.z) / step;
        const auto &c = cols[(iz + 1) * 34 + ix + 1];
        int w_top = std::max(s.sea_level, c.water_top);
        float y = dy[j];
        v->data[indices[j]] = y <= c.height + noise[j] * s.cliff_strength * c.cliff
                                  ? r.stone
                                  : (y <= w_top ? r.water : r.air);
    }
    if (s.generator_version >= 2) {
        for (int iz = -1; iz <= 32; iz++)
            for (int ix = -1; ix <= 32; ix++) {
                const auto &c = cols[(iz + 1) * 34 + ix + 1];
                if (c.arch_mask < .18f)
                    continue;
                int w_top = std::max(s.sea_level, c.water_top);
                int wx = key.x + ix * step, wz = key.z + iz * step;
                int top_iy = std::min(ny - 1, int(std::ceil((c.height - s.min_y) / ystep)));
                int bot_iy = std::max(1, int(std::floor((s.sea_level + 6 - s.min_y) / ystep)));
                for (int iy = bot_iy; iy <= top_iy; iy++) {
                    int wy = s.min_y + iy * ystep;
                    if (g.natural_arch_carved(c, wx, wy, wz))
                        v->data[voxel_index(*v, ix, iy, iz)] = wy <= w_top ? r.water : r.air;
                }
            }
    }
    timings.terrain_ms += elapsed(clock);
    clock = std::chrono::steady_clock::now();
    for (int iz = -1; iz <= 32; iz++)
        for (int ix = -1; ix <= 32; ix++)
            for (int iy = 1; iy < ny; iy++) {
                size_t i = voxel_index(*v, ix, iy, iz);
                if (!r.get(v->data[i]).solid)
                    continue;
                int wx = key.x + ix * step, wy = s.min_y + iy * ystep, wz = key.z + iz * step;
                const auto &c = v->columns[(iz + 1) * 34 + ix + 1];
                if (c.arch_mask >= .18f && g.natural_arch_carved(c, wx, wy + ystep, wz)) {
                    v->data[i] = r.stone;
                    continue;
                }
                bool exposed = !r.get(v->at(ix, iy + 1, iz)).solid;
                v->data[i] = g.material(c, wy, exposed);
            }
    // Vegetation is a separate, deterministic pass, before player modifications.
    if (key.level <= s.tree_lod_max) {
        auto trees = g.trees(key.x - step, key.z - step, key.x + 33 * step, key.z + 33 * step);
        for (const auto &part : trees) {
            int ix = int(std::floor(float(part.first.x - key.x) / step));
            int iz = int(std::floor(float(part.first.z - key.z) / step));
            int iy = (part.first.y - s.min_y) / ystep;
            if (ix >= -1 && ix <= 32 && iz >= -1 && iz <= 32 && iy >= 0 && iy < ny) {
                auto i = voxel_index(*v, ix, iy, iz);
                if (v->data[i] == r.air || r.get(v->data[i]).foliage || r.get(part.second).wood)
                    v->data[i] = part.second;
            }
        }
    }
    if (s.surface_shapes && key.level == 0) {
        for (int iz = -1; iz <= 32; iz++)
            for (int ix = -1; ix <= 32; ix++) {
                const auto &c = cols[(iz + 1) * 34 + ix + 1];
                int top_scan = std::min(ny - 2, int(std::ceil(c.height + s.cliff_strength * c.cliff - s.min_y)) + 1);
                int bot_scan = std::max(1, int(std::floor(c.height - s.cliff_strength * c.cliff - s.min_y)) - 1);
                for (int iy = top_scan; iy >= bot_scan; iy--) {
                    const auto &def = r.get(v->at(ix, iy, iz));
                    if (!def.solid || def.wood || def.foliage)
                        continue;
                    if (v->at(ix, iy + 1, iz) == r.air) {
                        auto shape = g.surface_shape(key.x + ix, s.min_y + iy, key.z + iz, c);
                        if (shape)
                            v->shapes[voxel_index(*v, ix, iy, iz)] = shape;
                    }
                    break;
                }
            }
    }
    // Derive the natural modifier only on the original exposed top layer.
    // This runs BEFORE edits: mining a cap must not promote the buried base.
    if(s.procedural_terrain && key.level==0) {
        auto natural=[&](int x,int y,int z) {
            const auto &b=r.get(v->at(x,y,z));return b.solid && !b.wood && !b.foliage;
        };
        for(int z=0;z<32;z++)for(int x=0;x<32;x++)for(int y=1;y<ny-2;y++) {
            IVec3 p{key.x+x,s.min_y+y,key.z+z};
            if(edits->count(p) || !natural(x,y,z))continue;
            if(natural(x,y-1,z) && !r.get(v->at(x,y+1,z)).solid)v->terrain_candidates.push_back(p);
        }
    }
    // Stable ordering makes downsampled edits independent of hash table iteration order.
    std::vector<std::pair<IVec3, BlockId>> sorted(edits->begin(), edits->end());
    std::sort(sorted.begin(), sorted.end(), [](auto &a, auto &b) {
        if (a.first.y != b.first.y)
            return a.first.y < b.first.y;
        if (a.first.z != b.first.z)
            return a.first.z < b.first.z;
        return a.first.x < b.first.x;
    });
    for (const auto &e : sorted) {
        auto p = e.first;
        int ix = int(std::floor(float(p.x - key.x) / step)), iz = int(std::floor(float(p.z - key.z) / step)),
            iy = (p.y - s.min_y) / ystep;
        if (ix >= -1 && ix <= 32 && iz >= -1 && iz <= 32 && iy >= 0 && iy < ny) {
            auto i = voxel_index(*v, ix, iy, iz);
            v->data[i] = e.second;
            v->shapes.erase(i); // Placed materials start as cubes, independent of terrain caps.
        }
    }
    std::vector<std::pair<IVec3, Appearance>> appearance_sorted(surfaces->begin(), surfaces->end());
    std::sort(appearance_sorted.begin(), appearance_sorted.end(), [](auto &a, auto &b) {
        if (a.first.y != b.first.y)
            return a.first.y < b.first.y;
        if (a.first.z != b.first.z)
            return a.first.z < b.first.z;
        return a.first.x < b.first.x;
    });
    for (const auto &e : appearance_sorted) {
        auto p = e.first;
        int ix = int(std::floor(float(p.x - key.x) / step)), iz = int(std::floor(float(p.z - key.z) / step)),
            iy = (p.y - s.min_y) / ystep;
        if (ix >= -1 && ix <= 32 && iz >= -1 && iz <= 32 && iy >= 0 && iy < ny) {
            auto i = voxel_index(*v, ix, iy, iz);
            v->surfaces[i] = e.second;
            if (e.second.terrain > 0 && key.level==0) {v->shapes[i]=32;continue;}
            if (key.level == 0 && e.second.shape != 255) {
                if (e.second.shape)
                    v->shapes[i] = pack_shape(e.second.shape, e.second.rotation);
                else
                    v->shapes.erase(i);
            }
        }
    }
    auto solid_terrain_at = [&](int qx, int qy, int qz) -> bool {
        if (qy < 0 || qy >= ny)
            return false;
        BlockId id;
        if (qx >= -1 && qx <= 32 && qz >= -1 && qz <= 32) {
            id = v->at(qx, qy, qz);
        } else {
            IVec3 wp{key.x + qx * step, s.min_y + qy * ystep, key.z + qz * step};
            auto eit = edits->find(wp);
            id = eit != edits->end() ? eit->second : g.terrain_sample(wp.x, wp.y, wp.z);
        }
        const auto &def = r.get(id);
        if (!def.solid || def.foliage)
            return false;
        if (def.wood) {
            if (qx >= -1 && qx <= 32 && qz >= -1 && qz <= 32) {
                auto sit = v->surfaces.find(voxel_index(*v, qx, qy, qz));
                return sit != v->surfaces.end() && shape_type(pack_shape(sit->second.shape, sit->second.rotation)) == 8;
            }
            return false;
        }
        return true;
    };
    // Resolve authored world ramps and Terrain blocks after edits as well as generated caps.
    std::vector<std::pair<size_t,uint8_t>> connected;
    std::vector<size_t> flat_natural;
    for(auto &[index,code]:v->shapes) {
        auto style=v->surfaces.find(index);if(style!=v->surfaces.end() && style->second.derived_ramp)continue;
        int ix=int(index%34)-1,iy=int(index/34%(ny+2))-1,iz=int(index/(34*(ny+2)))-1;
        if(style!=v->surfaces.end() && style->second.terrain>0)continue;
        if (shape_type(code) == 8) {
            uint8_t resolved = connect_terrain([&](int dx, int dy, int dz) {
                return solid_terrain_at(ix + dx, iy + dy, iz + dz);
            });
            bool authored = (style != v->surfaces.end() && style->second.shape != 255);
            if (!authored && resolved == terrain_shape({2, 2, 2, 2}))
                flat_natural.push_back(index);
            else
                connected.push_back({index, resolved});
            continue;
        }
        auto spec=ramp_spec(code);if(!spec.valid)continue;
        if(ix<0 || ix>=32 || iz<0 || iz>=32)continue;
        auto shape=connect_ramp(code,[&](int dx,int dz) {
            for(int offset:{0,-1,1}) {
                auto neighbor=v->shapes.find(voxel_index(*v,ix+dx,iy+offset,iz+dz));
                if(neighbor==v->shapes.end() || !r.get(v->at(ix+dx,iy+offset,iz+dz)).solid)continue;
                auto s=ramp_spec(neighbor->second);
                if(s.valid && std::abs(offset+s.low()-spec.low())<.001f)return neighbor->second;
            }
            return uint8_t(0);
        });
        connected.push_back({index,shape});
    }
    for(auto &[index,shape]:connected)v->shapes[index]=shape;
    for(auto index:flat_natural)v->shapes.erase(index);
    timings.surface_ms += elapsed(clock);
    return v;
}
struct Face {
    int signed_block = 0;
    uint32_t tint = 0xffffff;
    uint16_t preserve = 650, brightness = 1350;
    int overlay_layer = -1;
    bool painted = false, tintable = true, sea_water = false;
    bool operator==(const Face &f) const {
        return signed_block == f.signed_block && tint == f.tint && preserve == f.preserve &&
               brightness == f.brightness && overlay_layer == f.overlay_layer && painted == f.painted &&
               tintable == f.tintable && sea_water == f.sea_water;
    }
};
static Appearance appearance(const Volume &v, int x, int y, int z) {
    auto it = v.surfaces.find(voxel_index(v, x, y, z));
    return it == v.surfaces.end() ? Appearance() : it->second;
}
static Face face_style(int signed_id, int axis, const int *p, const Volume &v, const Registry &r) {
    BlockId id = BlockId(std::abs(signed_id) - 1);
    const auto &b = r.get(id);
    const auto &c = v.columns[(p[2] + 1) * 34 + p[0] + 1];
    auto a = appearance(v, p[0], p[1], p[2]);
    Face out;
    out.signed_block = signed_id;
    out.sea_water = b.water && (v.min_y + p[1] * v.y_step < v.water_surface);
    out.painted = a.painted || b.foliage || b.tintable;
    out.tint = a.painted ? a.tint : pack_rgb(c.grass_tint);
    out.preserve = a.painted ? a.preserve : uint16_t(c.preserve * 1000);
    out.brightness = a.painted ? a.brightness : uint16_t(c.brightness * 1000);
    out.tintable = b.tintable;
    int overlay = a.overlay == 65535 ? b.overlay : a.overlay;
    if (overlay == 0 && a.overlay == 65535 && axis != 1 && p[1] + 1 < v.height) {
        BlockId above_id = v.at(p[0], p[1] + 1, p[2]);
        const auto &above = r.get(above_id);
        if (above_id != id && above.solid && above.transition_overlay > 0) {
            overlay = above.transition_overlay;
            auto above_a = appearance(v, p[0], p[1] + 1, p[2]);
            if (!out.painted && above_a.painted) {
                out.tint = above_a.tint;
                out.preserve = above_a.preserve;
                out.brightness = above_a.brightness;
            }
        }
    }
    if (overlay > 0 && overlay < int(r.overlays.size())) {
        auto &o = r.overlays[overlay];
        out.overlay_layer = axis == 1 ? (signed_id > 0 ? o.top : o.bottom) : o.side;
        if (out.overlay_layer >= 0)
            out.tintable = o.tintable;
    }
    if (!out.painted && (out.overlay_layer < 0 || !out.tintable)) {
        out.tint = 0xffffff;
        out.preserve = 350;
        out.brightness = 1000;
    }
    return out;
}
static void style_quad(Surface &s, const Face &face, const Registry &r) {
    auto &b = r.get(BlockId(std::abs(face.signed_block) - 1));
    RGB tint = unpack_rgb(face.tint);
    for (size_t i = s.vertices.size() - 4; i < s.vertices.size(); i++) {
        auto &v = s.vertices[i];
        v.tr = tint.r;
        v.tg = tint.g;
        v.tb = tint.b;
        v.preserve = face.preserve / 1000.f;
        v.brightness = face.brightness / 1000.f;
        v.texture = float(b.texture_layer);
        v.overlay = float(face.overlay_layer);
        v.painted = face.painted ? 1.f : 0.f;
        v.overlay_tint = face.tintable ? 1.f : 0.f;
        v.decal = !b.distant_outline ? 1.f : b.water ? 2.f : 0.f;
        if (b.texture_layer > 0)
            v.r = v.g = v.b = 1;
    }
}
static void greedy(MeshResult &mesh, const Volume &v, const Registry &r) {
    int dims[] = {32, v.height, 32};
    float scale[] = {float(v.step), float(v.y_step), float(v.step)};
    std::vector<Face> mask;
    for (int d = 0; d < 3; d++) {
        int u = (d + 1) % 3, w = (d + 2) % 3, du = dims[u], dw = dims[w];
        mask.resize(du * dw);
        int p[3] = {};
        for (int plane = -1; plane < dims[d]; plane++) {
            p[d] = plane;
            for (int j = 0; j < dw; j++)
                for (int i = 0; i < du; i++) {
                    p[u] = i;
                    p[w] = j;
                    BlockId a = v.at(p[0], p[1], p[2]);
                    p[d]++;
                    BlockId b = v.at(p[0], p[1], p[2]);
                    p[d]--;
                    auto shape_a = v.shapes.count(voxel_index(v, p[0], p[1], p[2]));
                    p[d]++;
                    auto shape_b = v.shapes.count(voxel_index(v, p[0], p[1], p[2]));
                    p[d]--;
                    if (shape_a)
                        a = r.air;
                    if (shape_b)
                        b = r.air;
                    if(d==1 && shape_b && !shape_a) {
                        auto packed=v.shapes.at(voxel_index(v,p[0],p[1]+1,p[2]));
                        int type=shape_type(packed);
                        if((type==1 || type==3 || type==4 || type==5 || type==8) && appearance(v,p[0],p[1]+1,p[2]).terrain<=0)
                            b=v.at(p[0],p[1]+1,p[2]); // Full-footprint base hides the cube below.
                    }
                    const auto &ba = r.get(a);
                    const auto &bb = r.get(b);
                    int value = 0;
                    if (plane >= 0 && a != r.air && ((ba.solid && !bb.solid) || (ba.water && b == r.air)))
                        value = int(a) + 1;
                    else if (plane + 1 < dims[d] && b != r.air &&
                             ((bb.solid && !ba.solid) || (bb.water && a == r.air)))
                        value = -(int(b) + 1);
                    if (value) {
                        if (value < 0)
                            p[d]++;
                        mask[j * du + i] = face_style(value, d, p, v, r);
                        if (value < 0)
                            p[d]--;
                    } else
                        mask[j * du + i] = Face();
                }
            for (int j = 0; j < dw; j++)
                for (int i = 0; i < du;) {
                    Face style = mask[j * du + i];
                    if (!style.signed_block) {
                        i++;
                        continue;
                    }
                    int width = 1;
                    while (i + width < du && mask[j * du + i + width] == style)
                        width++;
                    int height = 1;
                    bool done = false;
                    while (j + height < dw && !done) {
                        for (int k = 0; k < width; k++)
                            if (!(mask[(j + height) * du + i + k] == style)) {
                                done = true;
                                break;
                            }
                        if (!done)
                            height++;
                    }
                    std::array<std::array<float, 3>, 4> points{};
                    for (auto &pt : points) {
                        pt[d] = (plane + 1) * scale[d];
                        pt[u] = i * scale[u];
                        pt[w] = j * scale[w];
                    }
                    points[1][u] += width * scale[u];
                    points[2][u] += width * scale[u];
                    points[2][w] += height * scale[w];
                    points[3][w] += height * scale[w];
                    for (auto &pt : points) {
                        pt[1] += v.min_y;
                        if (style.sea_water)
                            pt[1] = std::min(pt[1], float(v.water_surface));
                    }
                    BlockId id = BlockId(std::abs(style.signed_block) - 1);
                    std::array<float, 3> normal{};
                    normal[d] = style.signed_block > 0 ? 1.f : -1.f;
                    auto &surface = r.get(id).water ? mesh.water : mesh.solid;
                    quad(surface, points, r.get(id).color, normal);
                    style_quad(surface, style, r);
                    for (int y = 0; y < height; y++)
                        for (int x = 0; x < width; x++)
                            mask[(j + y) * du + i + x] = Face();
                    i += width;
                }
        }
    }
}
void append_block_surface(Surface &out, std::array<float, 3> position, uint8_t shape, BlockId id,
                          const Appearance &a, const Column &c, const Registry &r, int transition_overlay) {
    auto &b = r.get(id);
    for (auto &face : shape_faces(shape)) {
        std::array<float, 3> center{};
        for (auto &p : face.points)
            for (int i = 0; i < 3; i++)
                center[i] += p[i] * .25f;
        std::array<float, 3> lo, hi;
        for (int i = 0; i < 3; i++) {
            lo[i] = center[i] + face.normal[i] * .002f - .0001f;
            hi[i] = lo[i] + .0002f;
        }
        if (shape_intersects(shape, lo, hi))
            continue; // Remove internal faces between stair/roof hulls.
        auto points = face.points;
        for (auto &p : points)
            for (int i = 0; i < 3; i++)
                p[i] += position[i];
        quad(out, points, b.color, face.normal);
        Face style;
        style.signed_block = int(id) + 1;
        style.painted = a.painted || b.foliage || b.tintable;
        style.tint = a.painted ? a.tint : pack_rgb(c.grass_tint);
        style.preserve = a.painted ? a.preserve : uint16_t(c.preserve * 1000);
        style.brightness = a.painted ? a.brightness : uint16_t(c.brightness * 1000);
        style.tintable = b.tintable;
        int overlay = a.overlay == 65535 ? b.overlay : a.overlay;
        if (overlay == 0 && a.overlay == 65535 && std::abs(face.normal[1]) <= .45f)
            overlay = transition_overlay;
        if (overlay > 0 && overlay < int(r.overlays.size())) {
            auto &o = r.overlays[overlay];
            style.overlay_layer = face.normal[1] > .45f ? o.top : face.normal[1] < -.5f ? o.bottom : o.side;
            if (style.overlay_layer >= 0)
                style.tintable = o.tintable;
        }
        if (!style.painted && (style.overlay_layer < 0 || !style.tintable)) {
            style.tint = 0xffffff;
            style.preserve = 350;
            style.brightness = 1000;
        }
        style_quad(out, style, r);
        for (size_t i = out.vertices.size() - 4; i < out.vertices.size(); i++) {
            auto &v = out.vertices[i];
            v.wind = shape_top(shape, v.x - position[0], v.z - position[2]);
        }
    }
}
static void partial_blocks(MeshResult &mesh, const Volume &v, const Registry &r) {
    for (int z = 0; z < 32; z++)
        for (int x = 0; x < 32; x++)
            for (int y = 0; y < v.height; y++) {
                auto it = v.shapes.find(voxel_index(v, x, y, z));
                if (it == v.shapes.end() || !r.get(v.at(x, y, z)).solid)
                    continue;
                const auto &c = v.columns[(z + 1) * 34 + x + 1];
                auto a = appearance(v, x, y, z);
                if(a.terrain>0)continue;
                BlockId id = v.at(x, y, z);
                int transition_overlay = 0;
                if (y + 1 < v.height) {
                    BlockId above_id = v.at(x, y + 1, z);
                    const auto &above = r.get(above_id);
                    if (above_id != id && above.solid && above.transition_overlay > 0)
                        transition_overlay = above.transition_overlay;
                }
                Surface block;
                append_block_surface(block, {float(x), float(v.min_y + y*v.y_step), float(z)}, it->second,
                                     id, a, c, r, transition_overlay);
                // Keep only exposed faces on cell boundaries. Inset and sloped faces remain visible.
                for (size_t f = 0; f < block.vertices.size(); f += 4) {
                    auto &n = block.vertices[f];
                    int axis = std::abs(n.nx) > .99f   ? 0
                               : std::abs(n.ny) > .99f ? 1
                               : std::abs(n.nz) > .99f ? 2
                                                       : -1;
                    bool covered = false;
                    float bottom_y = float(v.min_y + y * v.y_step);
                    if (axis == 0 || axis == 2) {
                        bool degenerate = true;
                        for (int i = 0; i < 4; i++)
                            if (std::abs(block.vertices[f + i].y - bottom_y) > .001f)
                                degenerate = false;
                        if (degenerate)
                            covered = true;
                    }
                    if (!covered && axis >= 0) {
                        float normal = axis == 0 ? n.nx : axis == 1 ? n.ny : n.nz;
                        float boundary = float(axis == 0   ? x
                                               : axis == 1 ? v.min_y + y*v.y_step
                                                           : z) +
                                         (normal > 0 ? 1 : 0);
                        bool on_boundary = true;
                        for (int i = 0; i < 4; i++) {
                            auto &p = block.vertices[f + i];
                            float value = axis == 0 ? p.x : axis == 1 ? p.y : p.z;
                            on_boundary &= std::abs(value - boundary) < .001f;
                        }
                        int q[3] = {x, y, z};
                        q[axis] += normal > 0 ? 1 : -1;
                        if (on_boundary && r.get(v.at(q[0], q[1], q[2])).solid) {
                            auto q_it = v.shapes.find(voxel_index(v, q[0], q[1], q[2]));
                            if (q_it == v.shapes.end()) {
                                covered = true;
                            } else if (shape_type(it->second) == 8 && shape_type(q_it->second) == 8) {
                                auto h_self = terrain_heights(it->second);
                                auto h_nb = terrain_heights(q_it->second);
                                if (axis == 0 && normal > 0)
                                    covered = h_nb[0] >= h_self[3] - .001f && h_nb[1] >= h_self[2] - .001f;
                                else if (axis == 0 && normal < 0)
                                    covered = h_nb[3] >= h_self[0] - .001f && h_nb[2] >= h_self[1] - .001f;
                                else if (axis == 2 && normal > 0)
                                    covered = h_nb[0] >= h_self[1] - .001f && h_nb[3] >= h_self[2] - .001f;
                                else if (axis == 2 && normal < 0)
                                    covered = h_nb[1] >= h_self[0] - .001f && h_nb[2] >= h_self[3] - .001f;
                                else if (axis == 1 && normal < 0)
                                    covered = q_it->second == terrain_shape({2, 2, 2, 2});
                            }
                        }
                    }
                    if (covered)
                        continue;
                    int base = int(mesh.solid.vertices.size());
                    for (int i = 0; i < 4; i++)
                    {
                        auto vertex=block.vertices[f+i];
                        vertex.x*=v.step; vertex.z*=v.step;
                        float bottom=float(v.min_y+y*v.y_step);
                        vertex.y=bottom+(vertex.y-bottom)*v.y_step;
                        vertex.wind*=v.y_step;
                        float nx=vertex.nx/v.step, ny=vertex.ny/v.y_step, nz=vertex.nz/v.step;
                        float length=std::sqrt(nx*nx+ny*ny+nz*nz);
                        vertex.nx=nx/length; vertex.ny=ny/length; vertex.nz=nz/length;
                        mesh.solid.vertices.push_back(vertex);
                    }
                    for (size_t i = (f / 4) * 6; i < (f / 4 + 1) * 6; i++)
                        mesh.solid.indices.push_back(base + block.indices[i] - int(f));
                }
            }
}
static uint64_t decoration_hash(int x, int y, int z, int seed) {
    uint64_t h = uint32_t(x) * 0x9e3779b185ebca87ULL ^ uint32_t(z) * 0xc2b2ae3d27d4eb4fULL ^
                 uint32_t(y) * 0x165667b19e3779f9ULL ^ uint32_t(seed);
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
    return h ^ (h >> 27);
}
static void decorations(MeshResult &mesh, const Volume &v, const Generator &g) {
    if (mesh.key.level != 0)
        return;
    const auto &r = g.blocks();
    std::vector<int> automatic;
    for (auto &o : r.overlays)
        if (o.decal && o.id > 0)
            automatic.push_back(o.id);
    if (automatic.empty())
        return;
    for (int z = 0; z < 32; z++)
        for (int x = 0; x < 32; x++)
            for (int y = v.height - 1; y >= 1; y--) {
                BlockId id = v.at(x, y, z);
                if (!r.get(id).solid || v.at(x, y + 1, z) != r.air)
                    continue;
                auto a = appearance(v, x, y, z);
                const auto &c = v.columns[(z + 1) * 34 + x + 1];
                int world_y = v.min_y + y;
                auto hash = decoration_hash(mesh.key.x + x, world_y, mesh.key.z + z, g.seed_value());
                int overlay = a.decoration;
                if (overlay == 65535) {
                    if (float(hash & 65535) / 65535 > c.pebbles)
                        continue;
                    overlay = automatic[(hash >> 16) % automatic.size()];
                }
                if (overlay <= 0 || overlay >= int(r.overlays.size()) || !r.overlays[overlay].decal)
                    continue;
                const auto &o = r.overlays[overlay];
                float size = .72f + float((hash >> 32) & 255) / 255 * .25f;
                float startx = x + .5f - size / 2, startz = z + .5f - size / 2, top = world_y + 1.012f;
                auto shape_it = v.shapes.find(voxel_index(v, x, y, z));
                if (shape_it != v.shapes.end()) {
                    if (a.decoration == 65535 && shape_type(shape_it->second) == 8 &&
                        shape_it->second != terrain_shape({1, 1, 1, 1}) &&
                        shape_it->second != terrain_shape({2, 2, 2, 2}))
                        continue;
                    top = world_y + shape_top(shape_it->second, .5f, .5f) + .012f;
                }
                quad(mesh.decorations,
                     {{{{startx, top, startz}},
                       {{startx, top, startz + size}},
                       {{startx + size, top, startz + size}},
                       {{startx + size, top, startz}}}},
                     {1, 1, 1}, {0, 1, 0});
                RGB tint = a.painted ? unpack_rgb(a.tint) : c.grass_tint;
                int turn = int((hash >> 40) % 4);
                for (int i = 0; i < 4; i++) {
                    auto &vertex = mesh.decorations.vertices[mesh.decorations.vertices.size() - 4 + i];
                    int uv = (i + turn) % 4;
                    vertex.u = float(uv == 1 || uv == 2);
                    vertex.v = float(uv >= 2);
                    vertex.texture = float(o.top);
                    vertex.overlay = -1;
                    vertex.tr = tint.r;
                    vertex.tg = tint.g;
                    vertex.tb = tint.b;
                    vertex.preserve = a.painted ? a.preserve / 1000.f : c.preserve;
                    vertex.brightness = a.painted ? a.brightness / 1000.f : c.brightness;
                    vertex.painted = (o.tintable || a.painted) ? 1 : 0;
                    vertex.decal = 1;
                }
            }
}
std::vector<GrassInstance> grass_for_block(std::array<float, 3>, uint8_t, const Appearance &,
                                           const Column &, const Registry &, const Settings &,
                                           int) {
    return {};
}
static void surface_vegetation(MeshResult &, const Volume &, const Generator &) {
}
MeshResult build_vegetation(const TileKey &key, uint64_t version, const Generator &generator,
                            const Volume &volume) {
    auto start = std::chrono::steady_clock::now();
    MeshResult result;
    result.key = key;
    result.version = version;
    result.vegetation_only = true;
    surface_vegetation(result, volume, generator);
    result.milliseconds = elapsed(start);
    return result;
}
static void skirts(MeshResult &mesh, const Volume &v, const Registry &r) {
    if (mesh.key.level == 0)
        return;
    float size = float(mesh.key.size()), step = float(v.step), inset = .002f;
    for (int edge = 0; edge < 4; edge++)
        for (int i = 0; i < 32; i++) {
            int x = edge < 2 ? i : (edge == 2 ? 0 : 31), z = edge < 2 ? (edge == 0 ? 0 : 31) : i,
                y = v.height - 1;
            while (y > 0 && !r.get(v.at(x, y, z)).solid)
                y--;
            int p[] = {x, y, z};
            auto style = face_style(int(v.at(x, y, z)) + 1, edge < 2 ? 2 : 0, p, v, r);
            float top = v.min_y + (y + 1) * v.y_step, bottom = float(v.min_y);
            std::array<float, 3> n = edge == 0   ? std::array<float, 3>{0, 0, -1}
                                     : edge == 1 ? std::array<float, 3>{0, 0, 1}
                                     : edge == 2 ? std::array<float, 3>{-1, 0, 0}
                                                 : std::array<float, 3>{1, 0, 0};
            float ax = edge < 2 ? i * step : (edge == 2 ? inset : size - inset),
                  az = edge < 2 ? (edge == 0 ? inset : size - inset) : i * step,
                  bx = edge < 2 ? (i + 1) * step : ax, bz = edge < 2 ? az : (i + 1) * step;
            float top_a=top,top_b=top;
            auto shape=v.shapes.find(voxel_index(v,x,y,z));
            if(shape!=v.shapes.end()) {
                float base=v.min_y+y*v.y_step;
                top_a=base+shape_top(shape->second,ax/step-x,az/step-z)*v.y_step;
                top_b=base+shape_top(shape->second,bx/step-x,bz/step-z)*v.y_step;
            }
            quad(mesh.solid, {{{{ax, top_a, az}}, {{bx, top_b, bz}}, {{bx, bottom, bz}}, {{ax, bottom, az}}}},
                 r.get(v.at(x, y, z)).color, n);
            style_quad(mesh.solid, style, r);
            for(int j=0;j<4;j++) mesh.solid.vertices[mesh.solid.vertices.size()-4+j].wind=(j==0||j==3)?top_a:top_b;
        }
}
MeshResult build_tile(const TileKey &key, uint64_t version, const Generator &g,
                      std::shared_ptr<const EditMap> edits, std::shared_ptr<const AppearanceMap> surfaces,
                      bool include_vegetation) {
    auto start = std::chrono::steady_clock::now();
    MeshResult result;
    result.key = key;
    result.version = version;
    auto volume = make_volume(key, g, edits, surfaces, result.stages);
    auto mesh_start = std::chrono::steady_clock::now();
    greedy(result, *volume, g.blocks());
    partial_blocks(result, *volume, g.blocks());
    skirts(result, *volume, g.blocks());
    decorations(result, *volume, g);
    if (include_vegetation) {
        surface_vegetation(result, *volume, g);
        result.foliage_included = key.level == 0;
    }
    result.stages.mesh_ms = elapsed(mesh_start);
    if (key.level == 0)
        result.volume = std::move(volume);
    result.milliseconds = elapsed(start);
    return result;
}
} // namespace altaria
