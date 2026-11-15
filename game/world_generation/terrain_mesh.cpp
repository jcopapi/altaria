#include <array>
#include <unordered_set>
#include <climits>
#include <cstdlib>
#include "block_shapes.h"
#include "voxel_core.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <tuple>

namespace altaria {
// Material flags occupy the low six bits, sky exposure the next four,
// and the four optional boundary sides are encoded above them.
static constexpr float terrain_edge_code = 1024.f;
static constexpr float terrain_quad_overlap = 8192.f;
static constexpr float seam_quad_overlap = 16384.f;
// Water this deep is opaque (water.gdshader's abyss); coarse tiles skip what lies below.
static constexpr float deep_water_cutoff = 16.f;
static constexpr float vegetation_fade_flag = 32768.f;
static constexpr float canopy_noise_flag = 65536.f;
// 131072 and 262144 were the retired Terrain cap decal flags.
static constexpr float resource_face_uv = 524288.f;
static constexpr float resource_no_outline = 1048576.f;
// With a boundary side code: drawn only on segments a LOD seam replaces.
static constexpr float terrain_seam_only = 2097152.f;

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
    if(std::abs(facing)<1e-8f) {
        // A wedge may represent a triangular face as a quad with a repeated
        // corner. Use the non-degenerate second triangle to choose winding.
        for(int i=0;i<3;i++){a[i]=p[2][i]-p[0][i];b[i]=p[3][i]-p[0][i];}
        facing=(a[1]*b[2]-a[2]*b[1])*n[0]+(a[2]*b[0]-a[0]*b[2])*n[1]+
               (a[0]*b[1]-a[1]*b[0])*n[2];
    }
    const int cw[] = {0, 2, 1, 0, 3, 2}, ccw[] = {0, 1, 2, 0, 2, 3};
    auto order = facing > 0 ? cw : ccw;
    for (int i = 0; i < 6; i++)
        s.indices.push_back(base + order[i]);
}
static size_t voxel_index(const Volume &v, int x, int y, int z) {
    return ((z + 1) * (v.height + 2) + (y + 1)) * 34 + x + 1;
}
bool Volume::cave_liquid_at(int x,int y,int z) const {
    return !static_cave_liquid.empty() && x>=-1 && x<=32 && z>=-1 && z<=32 &&
           y>=0 && y<height && static_cave_liquid[voxel_index(*this,x,y,z)];
}
uint8_t Volume::sky_exposure(int x, int y, int z) const {
    if (x < -1 || x > 32 || z < -1 || z > 32 || y < -1 || y > height)
        return 0;
    if (y >= sky_floor[(z + 1) * 34 + x + 1])
        return 15;
    if (auto light = indirect_sky.find(voxel_index(*this, x, y, z)); light != indirect_sky.end())
        return light->second;
    return 0;
}

static bool inside_voxel_halo(int x, int z) {
    return x >= -1 && x <= 32 && z >= -1 && z <= 32;
}

// Only a tile with shadowed air on its border needs to sample daylight paths
// through its neighbours. The expanded field is temporary and never meshed.
struct SkylightHalo {
    static constexpr int pad = skylight_reach + 1;
    static constexpr int width = 32 + 2 * pad;
    int low = 0, high = 0;
    std::vector<int> floors;
    std::vector<uint8_t> opaque;

    static int column(int x, int z) { return (z + pad) * width + x + pad; }
    bool contains(int x, int z) const {
        return high > low && x >= -pad && x < 32 + pad && z >= -pad && z < 32 + pad;
    }
    bool blocked(int x, int y, int z) const {
        return y < low || y >= high || opaque[(y - low) * width * width + column(x, z)];
    }
};

static bool blocks_skylight(const BlockDef &block) {
    return block.solid && !block.foliage && !block.wood;
}
static size_t voxel_index(const Volume &v, int x, int y, int z);
// Dripstone and thin columns (Pillar, MiniPillar) let diffuse daylight past:
// treating them as cubes cast dark, square "shadows" around them.
static bool cell_blocks_skylight(const Volume &v, const Registry &r, int x, int y, int z) {
    if (!blocks_skylight(r.get(v.at(x, y, z))))
        return false;
    if (v.shapes.empty())
        return true;
    const auto shape = v.shapes.find(voxel_index(v, x, y, z));
    if (shape == v.shapes.end())
        return true;
    const int type = shape_type(shape->second);
    return type != int(BlockShape::Pillar) && type != int(BlockShape::MiniPillar);
}

static SkylightHalo skylight_halo(const Volume &v, const Generator &g, const TileKey &key, const EditMap &edits) {
    SkylightHalo halo;
    if (key.level != 0)
        return halo;
    const auto &r = g.blocks();
    const auto &s = g.settings();
    int bottom = v.height, top = -1;
    for (int z = -1; z <= 32; ++z)
        for (int x = -1; x <= 32; ++x) {
            if (x > -1 && x < 32 && z > -1 && z < 32)
                continue;
            const int column = (z + 1) * 34 + x + 1;
            const int near_surface = int(std::floor(v.columns[column].height)) - v.min_y - 2 * skylight_reach;
            for (int y = std::max(0, near_surface); y < std::min(v.height, int(v.sky_floor[column])); ++y) {
                if (!blocks_skylight(r.get(v.at(x, y, z)))) {
                    bottom = std::min(bottom, y);
                    top = std::max(top, y);
                }
            }
        }
    if (top < bottom)
        return halo;
    halo.low = std::max(0, bottom - skylight_reach);
    halo.high = std::min(v.height, top + skylight_reach + 1);
    const int area = halo.width * halo.width;
    halo.floors.resize(area, halo.low);
    halo.opaque.resize(area * (halo.high - halo.low));
    std::vector<int> edit_top(area, s.min_y);
    for (const auto &[p, id] : edits) {
        const int x = p.x - key.x, z = p.z - key.z;
        if (halo.contains(x, z) && !inside_voxel_halo(x, z) && blocks_skylight(r.get(id)))
            edit_top[halo.column(x, z)] = std::max(edit_top[halo.column(x, z)], p.y);
    }
    std::vector<float> xs, zs;
    for (int z = -halo.pad; z < 32 + halo.pad; ++z)
        for (int x = -halo.pad; x < 32 + halo.pad; ++x)
            if (!inside_voxel_halo(x, z)) {
                xs.push_back(float(key.x + x));
                zs.push_back(float(key.z + z));
            }
    const auto columns = g.columns(xs, zs);
    // Caves are sampled in bulk for the whole padded block: one graph query and
    // one noise fill, instead of a scalar world lookup per cell (which rebuilt
    // a cave query at every tile boundary and dominated tile generation).
    const int band_floor = v.min_y + halo.low, band_cells = halo.high - halo.low;
    int tallest = band_floor + band_cells;
    for (const auto &terrain : columns)
        tallest = std::max(tallest, int(std::ceil(terrain.height + s.cliff_strength * terrain.cliff)) + 3);
    caves::Query cave_query;
    if (g.underground())
        cave_query = g.underground()->query({{float(key.x - halo.pad), float(band_floor - 1), float(key.z - halo.pad)},
                                             {float(key.x + 32 + halo.pad), float(std::min(tallest, s.max_y)),
                                              float(key.z + 32 + halo.pad)}});
    std::vector<uint8_t> noise_cells;
    const auto *noise = g.noise_caves();
    if (noise) {
        std::vector<float> heights(size_t(area), 0.f);
        for (int z = -1; z <= 32; ++z)
            for (int x = -1; x <= 32; ++x)
                heights[size_t(halo.column(x, z))] = v.columns[(z + 1) * 34 + x + 1].height;
        for (size_t i = 0; i < columns.size(); ++i)
            heights[size_t(halo.column(int(xs[i]) - key.x, int(zs[i]) - key.z))] = columns[i].height;
        noise->fill(key.x - halo.pad, key.z - halo.pad, 1, halo.width, halo.width, band_floor, 1, band_cells,
                    heights.data(), noise_cells);
    }
    for (size_t i = 0; i < columns.size(); ++i) {
        const int wx = int(xs[i]), wz = int(zs[i]);
        const int index = halo.column(wx - key.x, wz - key.z);
        const auto &terrain = columns[i];
        const int natural_top = int(std::ceil(terrain.height + s.cliff_strength * terrain.cliff)) + 2;
        const int highest = std::min(s.max_y - 1, std::max(natural_top, edit_top[index]));
        const int cell_x = int(std::floor(wx / 32.0)), cell_z = int(std::floor(wz / 32.0));
        int bin_cell = INT_MIN;
        const std::vector<uint32_t> *bin = nullptr;
        // Open cave air lets daylight through. Dripstone, formations and cave
        // grass are ignored here: they are thin and do not change the fill.
        const auto carved = [&](int wy) {
            if (!cave_query.bins.empty()) {
                const int cell_y = int(std::floor(wy / 32.0));
                if (cell_y != bin_cell) {
                    bin_cell = cell_y;
                    const auto found = cave_query.bins.find({cell_x, cell_y, cell_z});
                    bin = found == cave_query.bins.end() ? nullptr : &found->second;
                }
                if (bin && g.underground()->sample(cave_query, wx, wy, wz, terrain.height, bin).carved)
                    return true;
            }
            if (!noise)
                return false;
            const int row = wy - band_floor;
            if (row >= 0 && row < band_cells)
                return noise_cells[size_t(index) * size_t(band_cells) + size_t(row)] != uint8_t(caves::NoiseCell::solid);
            return noise->at(wx, wy, wz, terrain.height) != caves::NoiseCell::solid;
        };
        const auto opaque_at = [&](int wy) {
            const auto edit = edits.find({wx, wy, wz});
            if (edit != edits.end())
                return blocks_skylight(r.get(edit->second));
            return wy <= natural_top && g.host_rock(terrain, wx, wy, wz) && !carved(wy);
        };
        for (int wy = highest; wy >= v.min_y + halo.high; --wy) {
            if (opaque_at(wy)) {
                halo.floors[index] = halo.high;
                break;
            }
        }
        for (int y = std::min(halo.high - 1, highest - v.min_y); y >= halo.low; --y) {
            const bool blocked = opaque_at(v.min_y + y);
            if (blocked)
                halo.floors[index] = std::max(halo.floors[index], y + 1);
            halo.opaque[(y - halo.low) * area + index] = blocked;
        }
    }
    return halo;
}

static void spread_skylight(Volume &v, const Generator &g, const TileKey &key, const EditMap &edits) {
    constexpr int daylight = 15;
    constexpr std::array<IVec3, 4> horizontal{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}};
    const auto &r = g.blocks();
    const auto halo = skylight_halo(v, g, key, edits);
    const auto inside = [&](int x, int z) { return inside_voxel_halo(x, z) || halo.contains(x, z); };
    const auto sky_floor = [&](int x, int z) {
        return inside_voxel_halo(x, z) ? int(v.sky_floor[(z + 1) * 34 + x + 1]) :
            halo.floors[halo.column(x, z)];
    };
    std::array<std::vector<IVec3>, daylight> frontier;
    std::unordered_map<IVec3, uint8_t, CoordHash> exterior_light;
    const auto admit = [&](int x, int y, int z, int light) {
        if (light <= 0 || y < 0 || y >= v.height || !inside(x, z) || y >= sky_floor(x, z))
            return;
        const bool local = inside_voxel_halo(x, z);
        if (local ? cell_blocks_skylight(v, r, x, y, z) : halo.blocked(x, y, z))
            return;
        auto &stored = local ? v.indirect_sky[voxel_index(v, x, y, z)] : exterior_light[{x, y, z}];
        if (stored >= light)
            return;
        stored = uint8_t(light);
        frontier[light].push_back({x, y, z});
    };
    // Only columns bordering direct skylight seed the flood. Falloff is per
    // metre so the reach of daylight stays similar across terrain resolutions.
    const int horizontal_loss = v.step;
    const int vertical_loss = v.y_step;
    if (horizontal_loss >= daylight)
        return;
    const int pad = halo.high > halo.low ? halo.pad : 1;
    for (int z = -pad; z < 32 + pad; ++z)
        for (int x = -pad; x < 32 + pad; ++x) {
            const bool local = inside_voxel_halo(x, z);
            const int top = std::min(local ? v.height : halo.high, sky_floor(x, z));
            int first_lit_neighbor = top;
            for (const auto &dir : horizontal)
                if (inside(x + dir.x, z + dir.z))
                    first_lit_neighbor = std::min(first_lit_neighbor, sky_floor(x + dir.x, z + dir.z));
            for (int y = std::max(first_lit_neighbor, local ? 0 : halo.low); y < top; ++y)
                admit(x, y, z, daylight - horizontal_loss);
        }
    for (int light = daylight - 1; light > 0; --light)
        for (const auto &p : frontier[light]) {
            const int current = inside_voxel_halo(p.x, p.z) ? v.sky_exposure(p.x, p.y, p.z) : exterior_light.at(p);
            if (current != light)
                continue;
            for (const auto &dir : horizontal)
                admit(p.x + dir.x, p.y, p.z + dir.z, light - horizontal_loss);
            admit(p.x, p.y + 1, p.z, light - vertical_loss);
            admit(p.x, p.y - 1, p.z, light - vertical_loss);
        }
}

static std::shared_ptr<Volume> make_volume(TileKey key, const Generator &g,
                                           std::shared_ptr<const EditMap> edits,
                                           std::shared_ptr<const AppearanceMap> surfaces,
                                           StageTimings &timings, VerticalWindow window = {}) {
    const auto &s = g.settings();
    const auto &r = g.blocks();
    int step = 1 << key.level, ystep = std::min(step, 4), ny = (s.max_y - s.min_y) / ystep;
    auto v = std::make_shared<Volume>();
    v->step = step;
    v->y_step = ystep;
    v->min_y = s.min_y;
    v->height = ny;
    v->water_surface = s.sea_level + 1;
    std::vector<float> x, z;
    for (int iz = -1; iz <= 32; iz++)
        for (int ix = -1; ix <= 32; ix++) {
            x.push_back(float(key.x + ix * step));
            z.push_back(float(key.z + iz * step));
        }
    v->columns = g.columns(x, z, &timings);
    const auto &cols = v->columns;
    const bool carve_caves=g.underground() && key.level<=s.caves.max_lod;
    const bool sliced=window.mode!=VerticalWindow::full;
    const bool subsurface=window.mode==VerticalWindow::underground;
    const bool compact_caves=carve_caves && !s.caves.biome_variation;
    int query_floor=s.min_y, query_ceiling=s.max_y;
    if (subsurface) {
        query_floor=window.floor-2*ystep;
        query_ceiling=window.ceiling+2*ystep;
    } else if (sliced) {
        float low=float(s.max_y);
        for (const auto &c:cols)
            low=std::min(low,c.height-s.cliff_strength*c.cliff-72);
        query_floor=std::min(window.floor,int(std::floor(low/ystep))*ystep);
    }
    query_floor=std::clamp(query_floor,s.min_y,s.max_y-ystep);
    query_ceiling=std::clamp(query_ceiling,query_floor+ystep,s.max_y);
    caves::Query cave_query;
    if(carve_caves) {
        auto start=std::chrono::steady_clock::now();
        cave_query=g.underground()->query({{float(key.x-step),float(query_floor),float(key.z-step)},
            {float(key.x+32*step),float(query_ceiling),float(key.z+32*step)}});
        timings.cave_plan_ms+=elapsed(start);
    }
    std::vector<TreePart> trees;
    if (!subsurface && key.level == 0)
        trees=g.trees(key.x-step,key.z-step,key.x+33*step,key.z+33*step);

    // Visual LODs with no edits, caves or trees can omit their hidden substrate.
    if(key.level>=3 && key.level>s.caves.max_lod && key.level>s.tree_lod_max && edits->empty() && surfaces->empty()) {
        float low=float(s.max_y),high=float(s.min_y);bool finite=true;
        for(const auto &c:cols) {
            float band=s.cliff_strength*c.cliff+1;
            float bottom=c.height-band;
            if(s.generator_version>=2 && c.arch_mask>=.18f) {
                float lintel=4.5f+3.5f*c.ridge,half_height=5.5f+7.5f*c.arch_mask;
                bottom=std::min(bottom,c.height-lintel-1.75f*half_height);
            }
            low=std::min(low,bottom-2*ystep);
            high=std::max(high,std::max(c.height+band,float(std::max(s.sea_level,c.water_top)))+2*ystep);
            finite&=std::isfinite(bottom) && std::isfinite(c.height) && std::isfinite(band);
        }
        if(finite && std::isfinite(low) && std::isfinite(high)) {
            int local_min=std::clamp(int(std::floor(low/ystep))*ystep,s.min_y,s.max_y-ystep);
            int local_max=std::clamp(int(std::ceil(high/ystep))*ystep,local_min+ystep,s.max_y);
            if(s.far_vertical_trim) {
                v->min_y=local_min;ny=(local_max-local_min)/ystep;v->height=ny;
                if(local_min>s.min_y)v->bottom_fill=r.stone;
            }
        }
    }
    if (s.preset == WorldPreset::Structure) {
        int low = s.max_y - 1, high = s.min_y;
        for (const auto &[p, id] : *edits) {
            low = std::min(low, p.y);
            high = std::max(high, p.y);
        }
        for (const auto &[p, appearance] : *surfaces) {
            low = std::min(low, p.y);
            high = std::max(high, p.y);
        }
        v->min_y = std::max(s.min_y, low - 1);
        v->height = ny = std::min(s.max_y, high + 2) - v->min_y;
    } else if (s.preset == WorldPreset::Plain) {
        // The substrate is known stone all the way to bedrock. Only the
        // surface band and the depth of authored edits require a mesh volume.
        int low = int(cols.front().height) - 8;
        int high = int(cols.front().height) + 4;
        for (const auto &[p, id] : *edits) {
            low = std::min(low, p.y - 1);
            high = std::max(high, p.y + 2);
        }
        v->min_y = std::max(s.min_y, int(std::floor(double(low) / ystep)) * ystep);
        const int ceiling = std::min(s.max_y, int(std::ceil(double(high) / ystep)) * ystep);
        v->height = ny = (ceiling - v->min_y) / ystep;
        v->bottom_fill = v->min_y > s.min_y ? r.stone : r.air;
    } else if (subsurface) {
        v->min_y=query_floor;
        v->height=ny=(query_ceiling-query_floor)/ystep;
    } else if(compact_caves || (sliced && key.level<=2)) {
        // A uniform stone substrate can be represented implicitly. Keep the
        // complete cave envelopes, surface material band, trees and edits.
        float low=float(s.max_y),high=float(s.min_y);
        for(const auto &c:cols) {
            const float band=s.cliff_strength*c.cliff;
            float bottom=c.height-band-8;
            if(s.generator_version>=2 && c.arch_mask>=.18f)
                bottom=std::min(bottom,c.height-(4.5f+3.5f*c.ridge)-1.75f*(5.5f+7.5f*c.arch_mask));
            low=std::min(low,bottom);
            high=std::max(high,std::max(c.height+band,float(std::max(s.sea_level,c.water_top))));
        }
        if(sliced)low=std::min(low,float(query_floor+2*ystep));
        for(const auto &ref:cave_query.primitives)low=std::min(low,ref.owner->primitives[ref.primitive].bounds.lo[1]);
        if(carve_caves && key.level==0 && g.noise_caves())low=std::min(low,float(g.noise_caves()->lowest()));
        for (const auto &part : trees) high = std::max(high, float(part.first.y));
        for (const auto &[p, id] : *edits) {
            if (p.x >= key.x - step && p.x < key.x + key.size() + step &&
                p.z >= key.z - step && p.z < key.z + key.size() + step) {
                low = std::min(low, float(p.y));
                high = std::max(high, float(p.y));
            }
        }
        for(const auto &[p,appearance]:*surfaces){low=std::min(low,float(p.y));high=std::max(high,float(p.y));}
        v->min_y=std::clamp(int(std::floor(low/ystep))*ystep-2*ystep,s.min_y,s.max_y-ystep);
        const int local_max=std::clamp(int(std::ceil(high/ystep))*ystep+3*ystep,v->min_y+ystep,s.max_y);
        if (sliced)
            v->min_y=std::max(v->min_y,query_floor);
        v->height=ny=(local_max-v->min_y)/ystep;
        if(v->min_y>s.min_y)v->bottom_fill=r.stone;
    }
    v->unknown_below=s.preset==WorldPreset::Default && sliced && key.level<=2 && v->min_y>s.min_y;
    v->unknown_above=subsurface && query_ceiling<s.max_y;
    if(v->unknown_below)v->bottom_fill=0;
    v->data.resize(34 * (ny + 2) * 34, r.air);
    auto clock = std::chrono::steady_clock::now();
    std::vector<float> dx, dy, dz, noise;
    std::vector<size_t> indices;
    std::vector<BlockId> base_rock(ny);
    for (int iy = 0; iy < ny; ++iy)
        base_rock[iy] = g.base_rock(v->min_y + iy * ystep);
    for (int iz = -1; iz <= 32; iz++)
        for (int ix = -1; ix <= 32; ix++) {
            const auto &c = cols[(iz + 1) * 34 + ix + 1];
            float band = s.cliff_strength * c.cliff + 1;
            int w_top = std::max(s.sea_level, c.water_top);
            for (int iy = 0; iy < ny; iy++) {
                int y = v->min_y + iy * ystep;
                size_t i = voxel_index(*v, ix, iy, iz);
                if (y == s.min_y) {
                    if (s.preset != WorldPreset::Structure)
                        v->data[i] = r.bedrock;
                    continue;
                }
                if (std::abs(y - c.height) <= band) {
                    dx.push_back(float(key.x + ix * step));
                    dy.push_back(float(y));
                    dz.push_back(float(key.z + iz * step));
                    indices.push_back(i);
                } else if (y < c.height)
                    v->data[i] = base_rock[iy];
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
                                  ? base_rock[(int(y) - v->min_y) / ystep]
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
                int top_iy = std::min(ny - 1, int(std::ceil((c.height - v->min_y) / ystep)));
                int bot_iy = std::max(1, int(std::floor((s.sea_level + 6 - v->min_y) / ystep)));
                for (int iy = bot_iy; iy <= top_iy; iy++) {
                    int wy = v->min_y + iy * ystep;
                    if (g.natural_arch_carved(c, wx, wy, wz))
                        v->data[voxel_index(*v, ix, iy, iz)] = wy <= w_top ? r.water : r.air;
                }
            }
    }
    timings.terrain_ms += elapsed(clock);
    std::vector<uint8_t> cave_void;
    std::vector<std::array<int,3>> cave_grass;
    if (carve_caves) {
        auto start=std::chrono::steady_clock::now();
        const auto &query=cave_query;
        v->cave_basin_span=s.caves.hydrology.basin_regions*s.caves.plan.region_size;
        if(key.level==0) {
            int span=s.caves.plan.region_size;
            for(int rz=int(std::floor(double(key.z)/span));rz<=int(std::floor(double(key.z+31)/span));rz++)
                for(int rx=int(std::floor(double(key.x)/span));rx<=int(std::floor(double(key.x+31)/span));rx++) {
                    bool retained=false;
                    for(const auto &plan:query.owners)if(plan->network.region==caves::Region{rx,rz}) {
                        v->cave_plans.push_back(plan);retained=true;break;
                    }
                    if(!retained) {
                        auto plan=g.underground()->region({rx,rz});
                        if(plan && plan->network.valid)v->cave_plans.push_back(std::move(plan));
                    }
                }
        }
        std::array<uint8_t,34*34> hydro_index{};
        if(v->cave_basin_span && !query.primitives.empty())
            for(int iz=-1;iz<=32;iz++)for(int ix=-1;ix<=32;ix++) {
                auto basin=g.underground()->basin_at(key.x+ix*step,key.z+iz*step);
                auto it=std::find_if(v->cave_basins.begin(),v->cave_basins.end(),[&](const auto &p){return p.basin==basin;});
                size_t i=it-v->cave_basins.begin();
                if(it==v->cave_basins.end())v->cave_basins.push_back(g.underground()->hydrology(basin));
                hydro_index[(iz+1)*34+ix+1]=uint8_t(i+1);
            }
        if(key.level==0)for(const auto &ref:query.primitives) {
            const auto &primitive=ref.owner->primitives[ref.primitive];
            if(!primitive.passage)continue;
            const auto &path=ref.owner->network.passages[primitive.index];
            auto basin=caves::basin_of(ref.owner->network.region,s.caves.hydrology.basin_regions);
            for(const auto &h:v->cave_basins)if(h.basin==basin && h.rivers && primitive.bounds.lo[1]<=h.top) {
                auto match=std::lower_bound(h.rivers->begin(),h.rivers->end(),path.id,
                    [](const caves::RiverCourse &r,uint64_t id){return r.passage<id;});
                if(match==h.rivers->end() || match->passage!=path.id)break;
                auto a=path.path[primitive.segment-1],b=path.path[primitive.segment];
                if(match->reverse)std::swap(a,b);
                v->cave_rivers.push_back({a,b,path.radius});
                break;
            }
        }
        timings.cave_plan_ms+=elapsed(start);start=std::chrono::steady_clock::now();
        if(!query.primitives.empty())cave_void.resize(v->data.size(),0);
        // Carve logical solids before materials/meshing; the basin, not the
        // surface sea level or a chunk-local decision, determines liquid fill.
        // The immutable query and generator are the same as scalar sampling.
        if(!query.bins.empty())for(int iz=-1;iz<=32;iz++)for(int ix=-1;ix<=32;ix++) {
            const auto &c=cols[(iz+1)*34+ix+1];
            auto h=hydro_index[(iz+1)*34+ix+1];
            const caves::HydroPlan *hydro=h?&v->cave_basins[h-1]:nullptr;
            const int wx=key.x+ix*step,wz=key.z+iz*step;
            auto horizontal=caves::basin_of({wx,wz},32);
            for(int iy=v->min_y==s.min_y?1:0;iy<ny;) {
                const int ycell=int(std::floor((v->min_y+iy*ystep)/32.0));
                const int end=std::min(ny,((ycell+1)*32-v->min_y+ystep-1)/ystep);
                caves::Cell cell{horizontal.x,ycell,horizontal.z};
                auto bin=query.bins.find(cell);
                if(bin==query.bins.end()){iy=end;continue;}
                auto details=query.formations.empty()?query.formations.end():query.formations.find(cell);
                for(;iy<end;iy++) {
                    auto i=voxel_index(*v,ix,iy,iz);
                    int wy=v->min_y+iy*ystep;
                    if(r.get(v->data[i]).solid && g.underground()->sample(query,wx,wy,wz,c.height,&bin->second).carved) {
                        auto fluid=hydro?hydro->at(wy):caves::Fluid::dry;
                        BlockId plant = r.air;
                        if(details!=query.formations.end()) {
                            auto dripstone = caves::DripstoneSection::none;
                            auto decoration=caves::sample_formation(query,{float(wx),float(wy),float(wz)},
                                fluid==caves::Fluid::water,fluid==caves::Fluid::lava,hydro?hydro->top:-8192,
                                &bin->second,&details->second,s.caves.formation_shapes ? &dripstone : nullptr);
                            if (r.get(decoration).root_plant) {
                                if (step == 1 && fluid == caves::Fluid::dry) {
                                    plant = decoration;
                                    v->has_root_plants = true;
                                }
                            } else if (decoration) {
                                v->data[i] = decoration;
                                cave_void[i] = 2;
                                if (dripstone != caves::DripstoneSection::none)
                                    v->shapes[i] = uint8_t(dripstone == caves::DripstoneSection::tip ?
                                                           BlockShape::MiniPillar : BlockShape::Pillar);
                                continue;
                            }
                        }
                        v->data[i]=fluid==caves::Fluid::water?r.water:fluid==caves::Fluid::lava?r.lava:plant;
                        if(fluid!=caves::Fluid::dry) {
                            if(v->static_cave_liquid.empty())v->static_cave_liquid.resize(v->data.size(),false);
                            v->static_cave_liquid[i]=true;
                        }
                        cave_void[i]=1;
                        if (iy > 0 && wy - ystep > s.min_y) {
                            const auto below = voxel_index(*v, ix, iy - 1, iz);
                            if (!cave_void[below] && r.get(v->data[below]).solid) {
                                v->data[below] = g.rock_material(c, wx, wy - ystep, wz, fluid == caves::Fluid::water);
                                if (step == 1 && fluid == caves::Fluid::dry && g.rock_grass_at(c, wx, wy, wz, true))
                                    cave_grass.push_back({ix, iy, iz});
                            }
                        }
                    }
                }
            }
        }
        // Noise caves never reach the surface (surface_margin), so the coarse
        // tiles seen from afar skip them: nothing of them could be visible.
        if(const auto *noise=key.level==0?g.noise_caves():nullptr) {
            std::array<float,34*34> heights;
            for(size_t i=0;i<heights.size();++i)heights[i]=cols[i].height;
            std::vector<uint8_t> cells;
            noise->fill(key.x-step,key.z-step,step,34,34,v->min_y,ystep,ny,heights.data(),cells);
            for(int iz=-1;iz<=32;iz++)for(int ix=-1;ix<=32;ix++) {
                const auto &c=cols[(iz+1)*34+ix+1];
                const int wx=key.x+ix*step,wz=key.z+iz*step;
                const uint8_t *column=cells.data()+size_t((iz+1)*34+ix+1)*ny;
                for(int iy=v->min_y==s.min_y?1:0;iy<ny;iy++) {
                    const auto cell=caves::NoiseCell(column[iy]);
                    if(cell==caves::NoiseCell::solid)continue;
                    const auto i=voxel_index(*v,ix,iy,iz);
                    // A planned chamber crossing a lava sea or the underworld fills
                    // with the same lava instead of leaving a dry pocket in it.
                    if(cell==caves::NoiseCell::lava && !cave_void.empty() && cave_void[i]==1 && v->data[i]==r.air) {
                        v->data[i]=r.lava;
                        if(v->static_cave_liquid.empty())v->static_cave_liquid.resize(v->data.size(),false);
                        v->static_cave_liquid[i]=true;
                        continue;
                    }
                    if(!r.get(v->data[i]).solid || (!cave_void.empty() && cave_void[i]))continue;
                    if(cave_void.empty())cave_void.resize(v->data.size(),0);
                    const int wy=v->min_y+iy*ystep;
                    const bool lava=cell==caves::NoiseCell::lava;
                    v->data[i]=lava?r.lava:r.air;
                    if(lava) {
                        if(v->static_cave_liquid.empty())v->static_cave_liquid.resize(v->data.size(),false);
                        v->static_cave_liquid[i]=true;
                    }
                    cave_void[i]=1;
                    if(iy>0 && wy-ystep>s.min_y) {
                        const auto below=voxel_index(*v,ix,iy-1,iz);
                        if(!cave_void[below] && r.get(v->data[below]).solid) {
                            v->data[below]=g.rock_material(c,wx,wy-ystep,wz);
                            if(step==1 && !lava && g.rock_grass_at(c,wx,wy,wz,true))
                                cave_grass.push_back({ix,iy,iz});
                        }
                    }
                }
            }
        }
        timings.cave_field_ms=elapsed(start);
    }
    clock = std::chrono::steady_clock::now();
    for (int iz = -1; iz <= 32; iz++)
        for (int ix = -1; ix <= 32; ix++) {
            const auto &c = v->columns[(iz + 1) * 34 + ix + 1];
            int first = v->min_y==s.min_y?1:0;
            if(g.underground() && !s.caves.biome_variation) {
                // Deep solid cells already contain the base rock for their elevation.
                const float transition=c.height-s.cliff_strength*c.cliff-8;
                first=std::max(first,int(std::ceil((transition-v->min_y)/ystep)));
            }
            for (int iy = first; iy < ny; iy++) {
                size_t i = voxel_index(*v, ix, iy, iz);
                if (!r.get(v->data[i]).solid)
                    continue;
                if(!cave_void.empty() && cave_void[i]>=2)continue;
                int wx = key.x + ix * step, wy = v->min_y + iy * ystep, wz = key.z + iz * step;
                if (!cave_void.empty() && (cave_void[voxel_index(*v, ix, iy + 1, iz)] & 1))
                    continue;
                if (c.arch_mask >= .18f && g.natural_arch_carved(c, wx, wy + ystep, wz)) {
                    v->data[i] = g.rock_material(c,wx,wy,wz);
                    continue;
                }
                bool exposed = (iy+1<ny || !v->unknown_above) && !r.get(v->at(ix, iy + 1, iz)).solid;
                v->data[i] = g.material(c,wx,wy,wz,exposed);
            }
        }
    const auto grow_grass = [&](int x, int y, int z, BlockId legacy) {
        const auto i = voxel_index(*v, x, y, z);
        v->data[i] = r.valid(r.grass) ? r.grass : legacy;
        if (v->data[i] == r.grass) {
            auto &a = v->surfaces[i];
            const auto &column = cols[(z + 1) * 34 + x + 1];
            a.painted = true;
            a.tint = pack_rgb(grass_on_support(column.grass_tint, r.get(v->at(x, y - 1, z)).color));
            a.preserve = uint16_t(column.preserve * 1000);
            a.brightness = uint16_t(column.brightness * 1000);
        }
    };
    for (const auto &[x, y, z] : cave_grass) {
        if (y + 2 >= ny)
            continue;
        const auto head = voxel_index(*v, x, y + 1, z);
        const auto clearance = voxel_index(*v, x, y + 2, z);
        if (cave_void[head] == 1 && cave_void[clearance] == 1 &&
            v->data[head] == r.air && v->data[clearance] == r.air)
            grow_grass(x, y, z, r.cave_grass ? r.cave_grass : r.rock_grass);
    }
    // Dripstone and thin columns, grown per vertical run of open cave air with
    // the same rule as Generator::cave_spike.
    if (step == 1 && !cave_void.empty() && g.noise_caves()) {
        const auto *noise = g.noise_caves();
        auto open = [&](int x, int y, int z) {
            const auto i = voxel_index(*v, x, y, z);
            const auto &def = r.get(v->data[i]);
            // Cave grass is a floor cover in open air, as in Generator::cave_probe.
            return cave_void[i] == 1 && (!def.solid || def.grass_on_rock || v->data[i] == r.grass) &&
                !def.liquid && !def.water;
        };
        auto rock = [&](int x, int y, int z) {
            const auto i = voxel_index(*v, x, y, z);
            return cave_void[i] == 0 && r.get(v->data[i]).solid;
        };
        for (int iz = -1; iz <= 32; ++iz)
            for (int ix = -1; ix <= 32; ++ix) {
                const auto &c = cols[(iz + 1) * 34 + ix + 1];
                const int wx = key.x + ix, wz = key.z + iz;
                for (int a = 0; a < ny;) {
                    if (!open(ix, a, iz)) {
                        ++a;
                        continue;
                    }
                    int b = a;
                    while (b + 1 < ny && open(ix, b + 1, iz))
                        ++b;
                    const int floor = a > 0 && rock(ix, a - 1, iz) ? v->min_y + a - 1 : INT_MIN;
                    const int roof = b + 1 < ny && rock(ix, b + 1, iz) ? v->min_y + b + 1 : INT_MAX;
                    const auto spikes = noise->spikes(wx, wz, floor, roof, std::min(b - a + 1, caves::spike_scan));
                    auto place = [&](int iy, BlockShape form) {
                        const auto i = voxel_index(*v, ix, iy, iz);
                        v->data[i] = g.rock_material(c, wx, v->min_y + iy, wz);
                        v->shapes[i] = pack_shape(int(form));
                        v->surfaces.erase(i);
                        cave_void[i] = 3;
                    };
                    if (spikes.pillar) {
                        for (int iy = a; iy <= b; ++iy)
                            place(iy, BlockShape::Pillar);
                    } else {
                        for (int k = 1; k <= spikes.down; ++k)
                            place(b + 1 - k, k == spikes.down ? BlockShape::MiniPillar : BlockShape::Pillar);
                        for (int k = 1; k <= spikes.up; ++k)
                            place(a - 1 + k, k == spikes.up ? BlockShape::MiniPillar : BlockShape::Pillar);
                    }
                    a = b + 1;
                }
            }
    }
    if (step == 1 && !subsurface && r.rock_grass) {
        for (int z = -1; z <= 32; ++z)
            for (int x = -1; x <= 32; ++x) {
                const auto &c = cols[(z + 1) * 34 + x + 1];
                if (c.surface != r.stone || c.mountain <= .25f)
                    continue;
                const float band = s.cliff_strength * c.cliff;
                const int lo = std::max(1, int(std::floor(c.height - band)) - v->min_y);
                const int hi = std::min(ny - 1, int(std::ceil(c.height + band)) + 1 - v->min_y);
                for (int y = lo; y <= hi; ++y) {
                    const auto i = voxel_index(*v, x, y, z);
                    if (v->data[i] == r.air && v->at(x, y - 1, z) != r.rock_grass && v->at(x, y - 1, z) != r.grass &&
                        r.get(v->at(x, y - 1, z)).solid &&
                        (cave_void.empty() || (!cave_void[i] && !cave_void[voxel_index(*v,x,y-1,z)])) &&
                        g.rock_grass_at(c, key.x+x, v->min_y+y, key.z+z, false))
                        grow_grass(x, y, z, r.rock_grass);
                }
            }
    }
    // Vegetation is a separate, deterministic pass, before player modifications.
    for (const auto &part : trees) {
        int ix = int(std::floor(float(part.first.x - key.x) / step));
        int iz = int(std::floor(float(part.first.z - key.z) / step));
        if(part.first.y<v->min_y || part.first.y>=v->min_y+ny*ystep)continue;
        int iy = (part.first.y - v->min_y) / ystep;
        if (ix >= -1 && ix <= 32 && iz >= -1 && iz <= 32 && iy >= 0 && iy < ny) {
            auto i = voxel_index(*v, ix, iy, iz);
            if (v->data[i] != r.air && !r.get(v->data[i]).foliage)
                continue;
            v->data[i] = part.second;
            if (part.shape)
                v->shapes[i] = part.shape;
            if (part.appearance.painted)
                v->surfaces[i] = part.appearance;
            else
                v->surfaces.erase(i);
        }
    }
    // Slabs on one-block terrain steps (surface_shapes), so the player walks up
    // gentle ground instead of jumping. Placements are collected first and
    // applied afterwards: a slab must never count as the next step up.
    std::vector<std::pair<size_t, BlockId>> step_slabs;
    // Slabs that only touch a rise diagonally (outer corners). On diagonal hillsides
    // these fill whole terraces and pile onto the slab below; they are filtered after.
    std::vector<std::array<int, 3>> corner_slabs;
    std::unordered_set<size_t> slab_cells;
    // Cells one past the halo come from the bare terrain, so both tiles that
    // share an edge make the same choice there.
    // Ring of 4 x 34 columns just outside the halo, generated in one batch on first use.
    std::vector<Column> ring;
    auto outside_cell = [&](int ix, int iy, int iz) -> BlockId {
        if (ix >= -1 && ix <= 32 && iz >= -1 && iz <= 32)
            return v->at(ix, iy, iz);
        if (ix < -2 || ix > 33 || iz < -2 || iz > 33)
            return r.air;
        if (ring.empty()) {
            std::vector<float> rx, rz;
            for (int side = 0; side < 4; ++side)
                for (int i = -2; i <= 33; ++i) {
                    rx.push_back(float(key.x + (side == 0 ? -2 : side == 1 ? 33 : i)));
                    rz.push_back(float(key.z + (side == 2 ? -2 : side == 3 ? 33 : i)));
                }
            ring = g.columns(rx, rz, nullptr, false);
        }
        const int side = ix == -2 ? 0 : ix == 33 ? 1 : iz == -2 ? 2 : 3;
        const int along = side < 2 ? iz : ix;
        return g.terrain_sample(ring[size_t(side * 36 + along + 2)], key.x + ix, v->min_y + iy, key.z + iz, false);
    };
    // Pass 1: half-height caps from the smooth height (block, slab, block on gentle
    // slopes). They must exist before step slabs are chosen, or a step slab lands
    // on a terrace that is about to become a slab itself and the two pile up.
    auto surface_cap = [&](int ix, int iy, int iz) -> bool {
        if (ix >= -1 && ix <= 32 && iz >= -1 && iz <= 32) {
            auto it = v->shapes.find(voxel_index(*v, ix, iy, iz));
            return it != v->shapes.end() && shape_type(it->second) == int(BlockShape::Slab);
        }
        if (ix < -2 || ix > 33 || iz < -2 || iz > 33 || outside_cell(ix, iy, iz) == r.air)
            return false;
        const int side = ix == -2 ? 0 : ix == 33 ? 1 : iz == -2 ? 2 : 3;
        const int along = side < 2 ? iz : ix;
        const auto &col = ring[size_t(side * 36 + along + 2)];
        return outside_cell(ix, iy, iz) != r.air && !r.get(outside_cell(ix, iy + 1, iz)).solid &&
               g.surface_shape(key.x + ix, v->min_y + iy, key.z + iz, col) != 0;
    };
    if (s.surface_shapes && key.level == 0) {
        for (int iz = -1; iz <= 32; iz++)
            for (int ix = -1; ix <= 32; ix++) {
                const auto &c = cols[(iz + 1) * 34 + ix + 1];
                const int iy = int(std::floor(c.height)) - v->min_y;
                if (iy < 1 || iy > ny - 3 || v->min_y + iy < s.sea_level)
                    continue;
                if (v->at(ix, iy + 1, iz) != r.air)
                    continue;
                const BlockId floor_id = v->at(ix, iy, iz);
                const auto &def = r.get(floor_id);
                if (floor_id == r.air || floor_id == r.bedrock || !def.solid || def.wood || def.foliage)
                    continue;
                const auto &headroom = r.get(v->at(ix, iy + 2, iz));
                if (headroom.liquid || headroom.water)
                    continue;
                const size_t floor_idx = voxel_index(*v, ix, iy, iz);
                if (v->shapes.count(floor_idx))
                    continue;
                bool edge = false;
                for (int k = 0; k < 4; ++k) {
                    const int nx = ix + (k == 0 ? 1 : k == 1 ? -1 : 0), nz = iz + (k == 2 ? 1 : k == 3 ? -1 : 0);
                    // Only a real drop (2+ blocks) is a cliff edge; a 1-block drop is
                    // exactly where the half-height cap forms the stair.
                    edge |= !r.get(outside_cell(nx, iy, nz)).solid && !r.get(outside_cell(nx, iy - 1, nz)).solid;
                }
                if (edge)
                    continue;
                if (const auto shape = g.surface_shape(key.x + ix, v->min_y + iy, key.z + iz, c))
                    v->shapes[floor_idx] = shape;
            }
    }
    // Pass 2: a full slab step in front of every remaining one-block rise.
    if (s.surface_shapes && key.level == 0) {
        for (int iz = -1; iz <= 32; iz++)
            for (int ix = -1; ix <= 32; ix++) {
                const auto &c = cols[(iz + 1) * 34 + ix + 1];
                int top_scan = std::min(ny - 2, int(std::ceil(c.height + s.cliff_strength * c.cliff - v->min_y)) + 1);
                int bot_scan = std::max(1, int(std::floor(c.height - s.cliff_strength * c.cliff - v->min_y)) - 1);
                bool checked_surface_shape = false;
                for (int iy = ny - 3; iy >= 1; iy--) {
                    if (v->at(ix, iy + 1, iz) != r.air)
                        continue;
                    const auto &headroom = r.get(v->at(ix, iy + 2, iz));
                    if (headroom.liquid || headroom.water)
                        continue;
                    const BlockId floor_id = v->at(ix, iy, iz);
                    if (floor_id == r.air || floor_id == r.bedrock)
                        continue;
                    const auto &def = r.get(floor_id);
                    if (!def.solid || def.wood || def.foliage)
                        continue;
                    const size_t floor_idx = voxel_index(*v, ix, iy, iz);
                    if (v->shapes.count(floor_idx))
                        continue;
                    // Every raised block/wall (cardinal or diagonal corner, surface or cave)
                    // surrounds adjacent walkable floors with a half-block slab transition.
                    bool rising = false, rising_cardinal = false, edge = false;
                    for (int dz = -1; dz <= 1; ++dz)
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dz == 0)
                                continue;
                            const int nx = ix + dx, nz = iz + dz;
                            const BlockId step_id = outside_cell(nx, iy + 1, nz);
                            const auto &step = r.get(step_id);
                            // A neighbour capped with a slab is only half a block higher: no step needed.
                            const bool neighbor_is_slab = surface_cap(nx, iy + 1, nz);
                            // Only a rise of exactly one block becomes a stair step: a slab
                            // against a taller wall is just clutter the player still can't climb.
                            const auto &above_step = r.get(outside_cell(nx, iy + 2, nz));
                            const bool one_high = !above_step.solid || above_step.wood || above_step.foliage;
                            const bool rise = step.solid && !step.wood && !step.foliage && step_id != r.bedrock &&
                                              !neighbor_is_slab && one_high;
                            rising |= rise;
                            if (dx == 0 || dz == 0)
                                rising_cardinal |= rise;
                            if (dx == 0 || dz == 0)
                                edge |= !r.get(outside_cell(nx, iy, nz)).solid;
                        }
                    if (rising) {
                        step_slabs.emplace_back(voxel_index(*v, ix, iy + 1, iz), floor_id);
                        slab_cells.insert(voxel_index(*v, ix, iy + 1, iz));
                        if (!rising_cardinal)
                            corner_slabs.push_back({ix, iy + 1, iz});
                    }
                    if (iy >= bot_scan && iy <= top_scan)
                        checked_surface_shape = true;
                }
            }
    }
    // A corner slab beside a slab one level lower would make a 1-block step between
    // two slabs; without it the terrace stays a full block: block, slab, block.
    std::unordered_set<size_t> dropped;
    for (const auto &c : corner_slabs) {
        static const int card[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto &d : card) {
            const int nx = c[0] + d[0], nz = c[2] + d[1];
            if (nx < -1 || nx > 32 || nz < -1 || nz > 32 || c[1] - 1 < 0)
                continue;
            if (slab_cells.count(voxel_index(*v, nx, c[1] - 1, nz))) {
                dropped.insert(voxel_index(*v, c[0], c[1], c[2]));
                break;
            }
        }
    }
    for (const auto &[index, block] : step_slabs) {
        if (dropped.count(index))
            continue;
        v->data[index] = block;
        v->shapes[index] = pack_shape(int(BlockShape::Slab));
    }
    // Cave rock stays blocky; a lone one-block column of rock between open air
    // becomes a slender pillar.
    if (key.level == 0 && !cave_void.empty()) {
        auto open = [&](size_t i) {
            const auto &def = r.get(v->data[i]);
            return (cave_void[i] & 1) && !def.solid && !def.liquid && !def.water;
        };
        for (int iz = 0; iz < 32; ++iz)
            for (int ix = 0; ix < 32; ++ix)
                for (int iy = 1; iy + 1 < ny; ++iy) {
                    const auto i = voxel_index(*v, ix, iy, iz);
                    if (cave_void[i] || v->shapes.count(i))
                        continue;
                    const auto &def = r.get(v->data[i]);
                    if (!def.solid || def.wood || def.foliage || v->data[i] == r.bedrock)
                        continue;
                    if (open(voxel_index(*v, ix - 1, iy, iz)) && open(voxel_index(*v, ix + 1, iy, iz)) &&
                        open(voxel_index(*v, ix, iy, iz - 1)) && open(voxel_index(*v, ix, iy, iz + 1)))
                        v->shapes[i] = pack_shape(int(BlockShape::Pillar));
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
        if (key.level > 0)
            break;
        auto p = e.first;
        if(p.y<v->min_y || p.y>=v->min_y+ny*ystep)continue;
        int ix = int(std::floor(float(p.x - key.x) / step)), iz = int(std::floor(float(p.z - key.z) / step)),
            iy = (p.y - v->min_y) / ystep;
        if (ix >= -1 && ix <= 32 && iz >= -1 && iz <= 32 && iy >= 0 && iy < ny) {
            auto i = voxel_index(*v, ix, iy, iz);
            v->data[i] = e.second;
            if(!v->static_cave_liquid.empty())v->static_cave_liquid[i]=false;
            v->shapes.erase(i); // Placed materials start as ordinary cubes.
            v->surfaces.erase(i);
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
        if (key.level > 0)
            break;
        auto p = e.first;
        if(p.y<v->min_y || p.y>=v->min_y+ny*ystep)continue;
        int ix = int(std::floor(float(p.x - key.x) / step)), iz = int(std::floor(float(p.z - key.z) / step)),
            iy = (p.y - v->min_y) / ystep;
        if (ix >= -1 && ix <= 32 && iz >= -1 && iz <= 32 && iy >= 0 && iy < ny) {
            auto i = voxel_index(*v, ix, iy, iz);
            auto style = e.second;
            auto generated = v->surfaces.find(i);
            if (!style.painted && generated != v->surfaces.end() && generated->second.painted) {
                style.painted = true;
                style.tint = generated->second.tint;
                style.preserve = generated->second.preserve;
                style.brightness = generated->second.brightness;
            }
            v->surfaces[i] = style;
            if (key.level == 0 && e.second.shape != 255) {
                if (e.second.shape)
                    v->shapes[i] = pack_shape(e.second.shape, e.second.rotation);
                else
                    v->shapes.erase(i);
            }
        }
    }
    for (auto it = v->shapes.begin(); it != v->shapes.end();)
        if (it->second == 0)
            it = v->shapes.erase(it);
        else
            ++it;
    // Resolve authored world ramps after edits.
    std::vector<std::pair<size_t,uint8_t>> connected;
    for(auto &[index,code]:v->shapes) {
        auto style=v->surfaces.find(index);if(style!=v->surfaces.end() && style->second.derived_ramp)continue;
        int ix=int(index%34)-1,iy=int(index/34%(ny+2))-1,iz=int(index/(34*(ny+2)))-1;
        if(ix<0 || ix>=32 || iz<0 || iz>=32)continue;
        const int type = shape_type(code);
        if (type == int(BlockShape::Stairs) || type == int(BlockShape::Roof)) {
            const auto corner = connect_building_corner(code, [&](int dx, int dz) -> uint8_t {
                const auto it = v->shapes.find(voxel_index(*v, ix + dx, iy, iz + dz));
                return it == v->shapes.end() ? 255 : it->second;
            });
            connected.push_back({index, corner});
            continue;
        }
        auto spec=ramp_spec(code);if(!spec.valid)continue;
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
    // Leaves and branches block direct rays, while diffuse sky light still
    // reaches the ground beneath them. Rock and solid roofs occlude the sky.
    for (int z = -1; z <= 32; ++z)
        for (int x = -1; x <= 32; ++x) {
            const int column_index = (z + 1) * 34 + x + 1;
            auto &floor = v->sky_floor[column_index];
            const auto &column = cols[column_index];
            if (v->unknown_above && v->min_y + ny * ystep <= column.height + s.cliff_strength * column.cliff) {
                floor = int16_t(ny + 1);
                continue;
            }
            for (int y = ny - 1; y >= 0; --y) {
                if (cell_blocks_skylight(*v, r, x, y, z)) {
                    floor = int16_t(y + 1);
                    break;
                }
            }
        }
    spread_skylight(*v, g, key, *edits);
    timings.surface_ms += elapsed(clock);
    return v;
}
struct Face {
    int signed_block = 0;
    uint32_t tint = 0xffffff;
    uint16_t preserve = 650, brightness = 1350;
    int overlay_layer = -1;
    bool painted = false, tintable = true;
    int liquid_ceiling=8192;
    uint8_t sky = 0;
    bool operator==(const Face &f) const {
        return signed_block == f.signed_block && tint == f.tint && preserve == f.preserve &&
               brightness == f.brightness && overlay_layer == f.overlay_layer && painted == f.painted &&
               tintable == f.tintable && liquid_ceiling == f.liquid_ceiling && sky == f.sky;
    }
};
static uint8_t surface_light(const Volume &v, int x, int y, int z) {
    return v.sky_exposure(x, y, z);
}
static uint8_t neighbor_sky_light(const Volume &v, int x, int y, int z) {
    uint8_t sky = surface_light(v, x, y, z);
    if (sky < 15 && !v.shapes.empty() && x >= -1 && x <= 32 && z >= -1 && z <= 32 && y >= 0 && y + 1 < v.height &&
        v.shapes.count(voxel_index(v, x, y, z))) {
        sky = std::max(sky, surface_light(v, x, y + 1, z));
    }
    return sky;
}
// Daylit sides are lit like tops in the shader (no ambient side shadow), so
// any exposure past the shader's ramp is stored as full daylight. Faces then
// share one light value and greedy meshing merges them; caves keep theirs.
static uint8_t side_light(uint8_t sky) {
    return sky >= 4 ? 15 : sky;
}
static Appearance appearance(const Volume &v, int x, int y, int z) {
    auto it = v.surfaces.find(voxel_index(v, x, y, z));
    return it == v.surfaces.end() ? Appearance() : it->second;
}
static Face face_style(int signed_id, int axis, const int *p, const Volume &v, const Registry &r,
                       int origin_x=0,int origin_z=0) {
    BlockId id = BlockId(std::abs(signed_id) - 1);
    const auto &b = r.get(id);
    const auto &c = v.columns[(p[2] + 1) * 34 + p[0] + 1];
    auto a = appearance(v, p[0], p[1], p[2]);
    Face out;
    out.signed_block = signed_id;
    int air[] = {p[0], p[1], p[2]};
    air[axis] += signed_id > 0 ? 1 : -1;
    out.sky = axis == 1 ? surface_light(v, air[0], air[1], air[2])
                        : side_light(neighbor_sky_light(v, air[0], air[1], air[2]));
    if(v.cave_liquid_at(p[0],p[1],p[2])) {
        auto basin=caves::basin_of({origin_x+p[0]*v.step,origin_z+p[2]*v.step},v.cave_basin_span);
        for(const auto &h:v.cave_basins)if(h.basin==basin){out.liquid_ceiling=h.top+1;break;}
    } else if(b.water && v.min_y + p[1] * v.y_step < v.water_surface)out.liquid_ceiling=v.water_surface;
    const bool grass_top = b.grass_on_rock;
    out.painted = a.painted || b.foliage || b.tintable || grass_top;
    out.tint = a.painted ? a.tint : pack_rgb(b.grass_on_rock || b.foliage ? b.color : c.grass_tint);
    out.preserve = a.painted || b.foliage ? a.preserve : uint16_t(c.preserve * 1000);
    out.brightness = a.painted ? a.brightness : b.foliage ? 1300 : uint16_t(c.brightness * 1000);
    out.tintable = b.tintable || grass_top;
    int overlay = a.overlay == 65535 ? b.overlay : a.overlay;
    if (overlay == 0 && a.overlay == 65535 && axis != 1 && p[1] + 1 < v.height) {
        BlockId above_id = v.at(p[0], p[1] + 1, p[2]);
        const auto &above = r.get(above_id);
        if (above_id != id && above.solid && above.transition_overlay > 0) {
            overlay = above.transition_overlay;
            auto above_a = appearance(v, p[0], p[1] + 1, p[2]);
            if (!out.painted && (above_a.painted || above.grass_on_rock)) {
                out.tint = above_a.painted ? above_a.tint : pack_rgb(above.color);
                out.preserve = above_a.painted ? above_a.preserve : out.preserve;
                out.brightness = above_a.painted ? above_a.brightness : out.brightness;
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
    } else if (v.step > 1 && !a.painted) {
        // Distant tiles: quantize the per-column colour so neighbouring faces
        // share a style and the greedy mesher can merge them.
        const uint32_t mask = v.step >= 8 ? 0xf0f0f0u : 0xf8f8f8u;
        out.tint = (out.tint & mask) | ((~mask >> 1) & 0x7f7f7fu);
        const int bucket = v.step >= 8 ? 60 : 30;
        out.preserve = uint16_t(out.preserve / bucket * bucket + bucket / 2);
        out.brightness = uint16_t(out.brightness / bucket * bucket + bucket / 2);
    }
    return out;
}
static void style_quad(Surface &s, const Face &face, const Registry &r) {
    auto &b = r.get(BlockId(std::abs(face.signed_block) - 1));
    RGB tint = unpack_rgb(face.tint);
    const float ny = s.vertices.back().ny;
    const int layer = b.texture_end_layer >= 0 && (b.grass_on_rock ? ny > .45f : std::abs(ny) > .5f)
                        ? b.texture_end_layer : b.texture_layer;
    for (size_t i = s.vertices.size() - 4; i < s.vertices.size(); i++) {
        auto &v = s.vertices[i];
        v.tr = tint.r;
        v.tg = tint.g;
        v.tb = tint.b;
        v.preserve = face.preserve / 1000.f;
        v.brightness = face.brightness / 1000.f;
        v.texture = float(layer);
        v.overlay = float(face.overlay_layer);
        v.painted = face.painted ? 1.f : 0.f;
        v.overlay_tint = face.tintable ? 1.f : 0.f;
        v.decal = b.lava ? 3.f : b.water ? 2.f : !b.distant_outline ? 1.f : 0.f;
        // The low two bits retain the material kind used by the outline pass.
        if (b.alpha_cutout && !b.foliage)
            v.decal += 4.f;
        if (b.foliage)
            v.decal += 8.f;
        if (b.wood || b.foliage)
            v.decal += vegetation_fade_flag;
        if (b.id == r.stone || b.rock_variation)
            v.decal += 16.f;
        v.decal += 64.f * face.sky;
        if (layer > 0)
            v.r = v.g = v.b = 1;
    }
}
static void append_leaf_fringe(Surface &out, std::array<float, 3> position, BlockId id,
                               const Appearance &a, const Registry &r, uint8_t sky) {
    const auto &leaf = r.get(id);
    Face style;
    style.signed_block = int(id) + 1;
    style.tint = a.painted ? a.tint : pack_rgb(leaf.color);
    style.painted = true;
    style.preserve = a.preserve;
    style.brightness = a.painted ? a.brightness : 1300;
    style.tintable = false;
    style.sky = sky;

    // Overlapping cards break up the outline of the opaque faces that cast shadows.
    const float x = position[0] + .5f, y = position[1], z = position[2] + .5f;
    const uint64_t variation = CoordHash{}({int(x * 8), int(y * 8), int(z * 8)});
    const float yaw = .18f + float(variation & 255) / 255.f;
    const float spread = .82f + float((variation >> 8) & 255) * (.24f / 255.f);
    // Three planes 60 degrees apart cover the cell like four did, with 25% fewer triangles.
    for (int plane = 0; plane < 3; ++plane) {
        const float angle = yaw + plane * 1.04719755f;
        const float dx = std::cos(angle), dz = std::sin(angle);
        const float lean = (plane % 2 == 0 ? 1.f : -1.f) *
                           (.30f + .30f * float((variation >> (16 + plane * 4)) & 15) / 15.f);
        const float rise = plane == 2 ? .52f : .66f;
        const float ox = -dz * lean, oz = dx * lean;
        const std::array<std::array<float, 3>, 4> points = {{
            {x - dx * spread + ox, y + .54f + rise, z - dz * spread + oz},
            {x + dx * spread + ox, y + .54f + rise, z + dz * spread + oz},
            {x + dx * spread - ox, y + .54f - rise, z + dz * spread - oz},
            {x - dx * spread - ox, y + .54f - rise, z - dz * spread - oz}
        }};
        const float length = std::hypot(lean, rise);
        // One face per card: the leaf material draws both sides (cull_disabled).
        for (float side : {1.f}) {
            quad(out, points, leaf.color, {-dz * rise * side / length, -lean * side / length, dx * rise * side / length});
            style_quad(out, style, r);
            for (size_t i = out.vertices.size() - 4; i < out.vertices.size(); ++i)
                out.vertices[i].decal += 32.f;
        }
    }
}
template<class T,class Visible,class Emit>
static void rectangles(std::vector<T> &mask,int du,int dw,Visible visible,Emit emit) {
    for(int j=0;j<dw;j++)for(int i=0;i<du;) {
        T style=mask[j*du+i];
        if(!visible(style)){i++;continue;}
        int width=1,height=1;
        while(i+width<du && mask[j*du+i+width]==style)width++;
        while(j+height<dw) {
            int k=0;while(k<width && mask[(j+height)*du+i+k]==style)k++;
            if(k!=width)break;
            height++;
        }
        emit(style,i,j,width,height);
        for(int y=0;y<height;y++)for(int x=0;x<width;x++)mask[(j+y)*du+i+x]=T();
        i+=width;
    }
}
static void greedy(MeshResult &mesh, const Volume &v, const Registry &r,bool collider=false,bool world_floor=false) {
    int dims[] = {32, v.height, 32};
    float scale[] = {float(v.step), float(v.y_step), float(v.step)};
    std::vector<Face> mask;
    std::vector<int8_t> collision_mask;
    std::vector<size_t> exposed_leaves;
    std::vector<bool> shaped;
    if(!v.shapes.empty()) {
        shaped.resize(v.data.size(),false);
        for(const auto &[index,shape]:v.shapes)shaped[index]=true;
    }
    // Layers made of a single material (deep rock, open sky) hold no faces
    // between their own cells. Most of a tall near volume is such layers.
    // Growing the vertex buffer by doubling copied it several times per tile.
    if (mesh.solid.vertices.capacity() < 16384) {
        mesh.solid.vertices.reserve(16384);
        mesh.solid.indices.reserve(24576);
    }
    std::vector<int> uniform(size_t(v.height + 2), -1);
    for (int y = -1; y <= v.height; ++y) {
        const BlockId first = v.at(-1, y, -1);
        bool same = true;
        for (int z = -1; z <= 32 && same; ++z)
            for (int x = -1; x <= 32; ++x)
                if (v.at(x, y, z) != first || (!shaped.empty() && shaped[voxel_index(v, x, y, z)])) {
                    same = false;
                    break;
                }
        if (same)
            uniform[size_t(y + 1)] = int(first);
    }
    const auto uniform_layer = [&](int y) { return y >= -1 && y <= v.height ? uniform[size_t(y + 1)] : -1; };
    for (int d = 0; d < 3; d++) {
        int u = (d + 1) % 3, w = (d + 2) % 3, du = dims[u], dw = dims[w];
        mask.resize(du * dw);
        if(collider)collision_mask.resize(du*dw);
        int p[3] = {};
        for (int plane = -1; plane < dims[d]; plane++) {
            if(d==1 && ((v.unknown_below && plane==-1) ||
                       (v.unknown_above && plane==v.height-1)))continue;
            if(world_floor && d==1 && plane==-1) {
                if(collider)mesh.collision_rects.push_back({{0,int16_t(v.min_y),0},32,32,1,false});
                continue;
            }
            p[d] = plane;
            bool any_collision=false;
            if (d == 1 && uniform_layer(plane) >= 0 && uniform_layer(plane) == uniform_layer(plane + 1))
                continue; // Same material on both sides of the whole plane: nothing to draw.
            for (int j = 0; j < dw; j++)
                for (int i = 0; i < du; i++) {
                    p[u] = i;
                    p[w] = j;
                    if (d != 1 && uniform_layer(p[1]) >= 0) {
                        mask[j * du + i] = Face();
                        if (collider)
                            collision_mask[j * du + i] = 0;
                        continue;
                    }
                    BlockId a = v.at(p[0], p[1], p[2]);
                    p[d]++;
                    BlockId b = v.at(p[0], p[1], p[2]);
                    p[d]--;
                    const bool shape_a = !shaped.empty() && shaped[voxel_index(v,p[0],p[1],p[2])];
                    p[d]++;
                    const bool shape_b = !shaped.empty() && shaped[voxel_index(v,p[0],p[1],p[2])];
                    p[d]--;
                    if (shape_a)
                        a = r.air;
                    if (shape_b)
                        b = r.air;
                    if(d==1 && shape_b && !shape_a) {
                        auto packed=v.shapes.at(voxel_index(v,p[0],p[1]+1,p[2]));
                        int type=shape_type(packed);
                        // Full-footprint base hides the cube below.
                        if(type==1 || type==3 || type==4 || type==5)
                            b=v.at(p[0],p[1]+1,p[2]);
                    }
                    const auto &ba = r.get(a);
                    const auto &bb = r.get(b);
                    int value = 0;
                    // A cutout canopy casts a solid shadow, but its cards do
                    // not cover the ground or walls behind it.
                    const bool exposes_a = ba.solid && (!bb.solid || (!ba.foliage && bb.foliage));
                    const bool exposes_b = bb.solid && (!ba.solid || (!bb.foliage && ba.foliage));
                    if (plane >= 0 && a != r.air && (exposes_a || ((ba.water || ba.liquid || ba.lava) && b == r.air)))
                        value = int(a) + 1;
                    else if (plane + 1 < dims[d] && b != r.air &&
                             (exposes_b || ((bb.water || bb.liquid || bb.lava) && a == r.air)))
                        value = -(int(b) + 1);
                    if (value) {
                        if (value < 0)
                            p[d]++;
                        if (r.get(BlockId(std::abs(value) - 1)).foliage && v.step == 1)
                            exposed_leaves.push_back(voxel_index(v, p[0], p[1], p[2]));
                        // Distant seabed deeper than the water's opaque depth is never
                        // seen (the water shader turns to darkness there): skip it.
                        if (v.step > 1 && p[0] >= -1 && p[0] <= 32 && p[2] >= -1 && p[2] <= 32) {
                            const auto &column = v.columns[(p[2] + 1) * 34 + p[0] + 1];
                            const float level = float(std::max(v.water_surface - 1, column.water_top));
                            const float top = float(v.min_y + (p[1] + 1) * v.y_step);
                            const auto &exposed = r.get(BlockId(std::abs(value) - 1));
                            if (!(exposed.water || exposed.liquid || exposed.lava) &&
                                column.height < level - deep_water_cutoff && top < level - deep_water_cutoff) {
                                if (value < 0)
                                    p[d]--;
                                mask[j * du + i] = Face();
                                continue;
                            }
                        }
                        mask[j * du + i] = face_style(value, d, p, v, r, mesh.key.x, mesh.key.z);
                        if (value < 0)
                            p[d]--;
                    } else
                        mask[j * du + i] = Face();
                    if(collider) {
                        const auto &def=value>0?ba:bb;
                        int8_t sign=value && !(def.water || def.liquid || def.lava)?(value>0?1:-1):0;
                        collision_mask[j*du+i]=sign;any_collision|=sign!=0;
                    }
                }
            auto points_at=[&](int i,int j,int width,int height) {
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
                    for(auto &pt:points)pt[1]+=v.min_y;
                    return points;
            };
            rectangles(mask,du,dw,[](const Face &style){return style.signed_block!=0;},
                [&](const Face &style,int i,int j,int width,int height) {
                    auto points=points_at(i,j,width,height);
                    for(auto &pt:points)pt[1]=std::min(pt[1],float(style.liquid_ceiling));
                    BlockId id = BlockId(std::abs(style.signed_block) - 1);
                    std::array<float, 3> normal{};
                    normal[d] = style.signed_block > 0 ? 1.f : -1.f;
                    const auto &def=r.get(id);
                    auto &surface = (def.water || def.liquid || def.lava) ? mesh.water :
                                    def.foliage && v.step == 1 ? mesh.leaf_shadows : mesh.solid;
                    quad(surface, points, r.get(id).color, normal);
                    style_quad(surface, style, r);
                    if (def.wood && v.step == 1) {
                        quad(mesh.sky_shadows, points, r.get(id).color, normal);
                        style_quad(mesh.sky_shadows, style, r);
                    }
                    if (!def.wood && !def.foliage) {
                        const bool boundary = d != 1 && (plane == -1 || plane == dims[d] - 1);
                        const int edge = (d == 2 ? 0 : 2) + int(plane >= 0);
                        const float flags = terrain_quad_overlap + (boundary ? terrain_edge_code * (edge + 1) : 0.f);
                        for (size_t i = surface.vertices.size() - 4; i < surface.vertices.size(); ++i)
                            surface.vertices[i].decal += flags;
                    }
            });
            if(any_collision)rectangles(collision_mask,du,dw,[](int8_t sign){return sign!=0;},
                [&](int8_t sign,int i,int j,int width,int height) {
                    // Same oriented face coverage as rendering; textures and
                    // paint are irrelevant. Queue compact integer rectangles;
                    // Jolt receives triangle vertices only when uploading.
                    CollisionRect rect;
                    rect.corner[d]=int16_t((plane+1)*scale[d]);
                    rect.corner[u]=int16_t(i*scale[u]);rect.corner[w]=int16_t(j*scale[w]);
                    rect.corner[1]+=v.min_y;
                    rect.width=uint16_t(width*scale[u]);rect.height=uint16_t(height*scale[w]);
                    rect.axis=uint8_t(d);rect.positive=sign>0;
                    mesh.collision_rects.push_back(rect);
            });
        }
    }
    std::sort(exposed_leaves.begin(), exposed_leaves.end());
    exposed_leaves.erase(std::unique(exposed_leaves.begin(), exposed_leaves.end()), exposed_leaves.end());
    for (size_t index : exposed_leaves) {
        const int x = int(index % 34) - 1;
        const int y = int(index / 34 % (v.height + 2)) - 1;
        const int z = int(index / 34 / (v.height + 2)) - 1;
        append_leaf_fringe(mesh.leaf_cards, {float(x), float(v.min_y + y), float(z)}, v.data[index],
                            appearance(v, x, y, z), r, v.sky_exposure(x, y, z));
    }
}

// Far tiles without caves, trees or edits only ever show their top surface.
// Mesh them as a heightfield: one sloped quad (2 triangles) per column whose
// corners average the four neighbouring column tops, so tiles meet exactly,
// plus short skirts on the tile border to hide cracks against coarser rings.
static void far_heightfield_mesh(MeshResult &mesh, const Volume &v, const Registry &r) {
    constexpr int N = 34; // columns -1..32
    std::array<float, N * N> ground{};
    std::array<int, N * N> top_cell{};
    std::array<int, N * N> liquid_cell{};
    const float base = float(v.min_y), ys = float(v.y_step), xs = float(v.step);
    for (int z = -1; z <= 32; ++z)
        for (int x = -1; x <= 32; ++x) {
            const int c = (z + 1) * N + x + 1;
            int y = v.height - 1, liquid = -1;
            for (; y >= 0; --y) {
                const BlockId id = v.at(x, y, z);
                if (id == r.air) continue;
                const auto &b = r.get(id);
                if (b.water || b.liquid || b.lava) { if (liquid < 0) liquid = y; continue; }
                if (b.solid) break;
            }
            top_cell[c] = y;
            liquid_cell[c] = liquid;
            ground[c] = y >= 0 ? base + (y + 1) * ys : (v.bottom_fill ? base : base - ys);
            // A seabed deeper than the water's opaque depth is never seen: a
            // flat floor there keeps the far mesh simple.
            if (liquid >= 0) {
                const float surface = base + (liquid + 1) * ys;
                ground[c] = std::max(ground[c], surface - deep_water_cutoff);
            }
        }
    const auto corner = [&](int cx, int cz) { // corner between columns cx-1..cx, cz-1..cz
        float sum = 0;
        for (int dz = -1; dz <= 0; ++dz)
            for (int dx = -1; dx <= 0; ++dx)
                sum += ground[(cz + dz + 1) * N + cx + dx + 1];
        return sum * .25f;
    };
    std::array<float, 33 * 33> h{};
    for (int z = 0; z <= 32; ++z)
        for (int x = 0; x <= 32; ++x)
            h[z * 33 + x] = corner(x, z);
    // Where a finer ring meets this tile, its LOD seam is built from column
    // tops (collect_terrain_edges). The averaged corners reach into the
    // neighbour's columns and leave the border above or below that seam, so
    // border columns carry a second, column-top surface that draws only on
    // seam segments; the averaged one keeps same-level neighbours seamless.
    const auto column_top = [&](int cx, int cz) { return ground[(cz + 1) * N + cx + 1]; };
    const auto edge_flag = [](int cx, int cz) {
        const int edge = cz == 0 ? 0 : cz == 31 ? 1 : cx == 0 ? 2 : cx == 31 ? 3 : -1;
        return edge < 0 ? 0.f : terrain_edge_code * float(edge + 1);
    };
    const auto flag_last = [&](Surface &surface, int count, float flags) {
        for (size_t i = surface.vertices.size() - count; i < surface.vertices.size(); ++i)
            surface.vertices[i].decal += flags;
    };
    const float skirt = 2.f * std::max(ys, xs);
    std::vector<Face> water(32 * 32);
    std::array<int, 32 * 32> water_level{};
    for (int z = 0; z < 32; ++z)
        for (int x = 0; x < 32; ++x) {
            const int c = (z + 1) * N + x + 1;
            const float h00 = h[z * 33 + x], h10 = h[z * 33 + x + 1], h11 = h[(z + 1) * 33 + x + 1],
                        h01 = h[(z + 1) * 33 + x];
            const float x0 = x * xs, x1 = (x + 1) * xs, z0 = z * xs, z1 = (z + 1) * xs;
            if (top_cell[c] >= 0) {
                int p[3] = {x, top_cell[c], z};
                const Face style = face_style(int(v.at(x, top_cell[c], z)) + 1, 1, p, v, r, mesh.key.x, mesh.key.z);
                // Lit like the near caps: a flat up normal, so slopes keep the colour of
                // the grass on them instead of shading like broken blocks.
                const BlockId id = BlockId(std::abs(style.signed_block) - 1);
                const float border = edge_flag(x, z);
                // Corner order matches greedy top faces (U along z, V along x):
                // the shader's overlap widens quads in that frame, and the old
                // x-first order shrank them, opening dotted cracks.
                quad(mesh.solid, {{{x0, h00, z0}, {x0, h01, z1}, {x1, h11, z1}, {x1, h10, z0}}}, r.get(id).color,
                     {0, 1, 0});
                style_quad(mesh.solid, style, r);
                flag_last(mesh.solid, 4, terrain_quad_overlap + border);
                // Skirts hang below the border edges; they face outward. Seam
                // segments replace them along with the averaged border.
                const auto skirt_quad = [&](std::array<float, 3> a, std::array<float, 3> b, std::array<float, 3> n) {
                    quad(mesh.solid, {{a, b, {b[0], b[1] - skirt, b[2]}, {a[0], a[1] - skirt, a[2]}}},
                         r.get(id).color, n);
                    style_quad(mesh.solid, style, r);
                    flag_last(mesh.solid, 4, border);
                };
                if (x == 0) skirt_quad({x0, h00, z0}, {x0, h01, z1}, {-1, 0, 0});
                if (x == 31) skirt_quad({x1, h10, z0}, {x1, h11, z1}, {1, 0, 0});
                if (z == 0) skirt_quad({x0, h00, z0}, {x1, h10, z0}, {0, 0, -1});
                if (z == 31) skirt_quad({x0, h01, z1}, {x1, h11, z1}, {0, 0, 1});
                if (border > 0.f) {
                    const float top = column_top(x, z);
                    const auto pin = [&](int vx, int vz, float averaged) {
                        return vx == 0 || vx == 32 || vz == 0 || vz == 32 ? top : averaged;
                    };
                    const std::array<std::array<float, 3>, 4> pinned{{{x0, pin(x, z, h00), z0},
                        {x0, pin(x, z + 1, h01), z1}, {x1, pin(x + 1, z + 1, h11), z1}, {x1, pin(x + 1, z, h10), z0}}};
                    quad(mesh.solid, pinned, r.get(id).color, {0, 1, 0});
                    style_quad(mesh.solid, style, r);
                    flag_last(mesh.solid, 4, terrain_quad_overlap + border + terrain_seam_only);
                    // Close the step to each in-tile neighbour along the border.
                    const auto riser = [&](std::array<float, 3> foot, std::array<float, 3> mine,
                                           std::array<float, 3> theirs, std::array<float, 3> n) {
                        if (mine[1] <= theirs[1] + 1e-4f)
                            return; // The higher column owns the riser.
                        for (float sign : {1.f, -1.f}) {
                            quad(mesh.solid, {{foot, mine, theirs, theirs}}, r.get(id).color,
                                 {n[0] * sign, n[1] * sign, n[2] * sign});
                            style_quad(mesh.solid, style, r);
                            flag_last(mesh.solid, 4, border + terrain_seam_only);
                        }
                    };
                    if (z == 0 || z == 31) {
                        const int ez = z == 0 ? 0 : 32, iz = z == 0 ? 1 : 31;
                        for (int dx : {-1, 1}) {
                            const int nx = x + dx;
                            if (nx < 0 || nx > 31)
                                continue;
                            const int vx = dx > 0 ? x + 1 : x;
                            riser({vx * xs, h[iz * 33 + vx], iz * xs}, {vx * xs, top, ez * xs},
                                  {vx * xs, column_top(nx, z), ez * xs}, {1, 0, 0});
                        }
                    }
                    if (x == 0 || x == 31) {
                        const int ex = x == 0 ? 0 : 32, ix = x == 0 ? 1 : 31;
                        for (int dz : {-1, 1}) {
                            const int nz = z + dz;
                            if (nz < 0 || nz > 31)
                                continue;
                            const int vz = dz > 0 ? z + 1 : z;
                            riser({ix * xs, h[vz * 33 + ix], vz * xs}, {ex * xs, top, vz * xs},
                                  {ex * xs, column_top(x, nz), vz * xs}, {0, 0, 1});
                        }
                    }
                }
            }
            if (liquid_cell[c] >= 0) {
                int p[3] = {x, liquid_cell[c], z};
                water[z * 32 + x] = face_style(int(v.at(x, liquid_cell[c], z)) + 1, 1, p, v, r, mesh.key.x, mesh.key.z);
                water_level[z * 32 + x] = liquid_cell[c];
            }
        }
    // Liquid surfaces stay flat, so equal neighbours merge into one quad.
    for (int i = 0; i < 32 * 32; ++i)
        if (water[i].signed_block)
            water[i].liquid_ceiling = std::min(water[i].liquid_ceiling, int(base + (water_level[i] + 1) * ys));
    rectangles(water, 32, 32, [](const Face &f) { return f.signed_block != 0; },
        [&](const Face &style, int i, int j, int width, int height) {
            const float y = float(style.liquid_ceiling);
            const float x0 = i * xs, x1 = (i + width) * xs, z0 = j * xs, z1 = (j + height) * xs;
            const BlockId id = BlockId(std::abs(style.signed_block) - 1);
            quad(mesh.water, {{{x0, y, z0}, {x1, y, z0}, {x1, y, z1}, {x0, y, z1}}}, r.get(id).color, {0, 1, 0});
            style_quad(mesh.water, style, r);
        });
}
static void cave_ceiling_shadows(MeshResult &mesh, const Volume &v, const Registry &r) {
    if (!v.unknown_above)
        return;
    // The streamed volume can end inside solid rock. Close that cut only in
    // the directional shadow pass, leaving genuine shafts open to daylight.
    std::vector<uint8_t> solid(32 * 32);
    for (int z = 0; z < 32; ++z)
        for (int x = 0; x < 32; ++x) {
            const auto id = v.at(x, v.height - 1, z);
            solid[z * 32 + x] = r.get(id).solid && !v.shapes.count(voxel_index(v, x, v.height - 1, z));
        }
    const float top = float(v.min_y + v.height * v.y_step);
    rectangles(solid, 32, 32, [](uint8_t cell) { return cell != 0; },
        [&](uint8_t, int x, int z, int width, int depth) {
            const float x0 = float(x * v.step), x1 = float((x + width) * v.step);
            const float z0 = float(z * v.step), z1 = float((z + depth) * v.step);
            quad(mesh.sky_shadows, {{{x0, top, z0}, {x0, top, z1}, {x1, top, z1}, {x1, top, z0}}},
                 {1, 1, 1}, {0, 1, 0});
        });
}
void append_block_surface(Surface &out, std::array<float, 3> position, uint8_t shape, BlockId id,
                          const Appearance &a, const Column &c, const Registry &r, int transition_overlay,
                          bool foliage_cards) {
    auto &b = r.get(id);
    const uint8_t sky = position[1] + 1.f >= c.height ? 15 : 0;
    if (b.foliage && foliage_cards && !shape) {
        append_leaf_fringe(out, position, id, a, r, sky);
        return;
    }
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
        // The two prisms of a branch fork overlap only part of each face.
        // Keep those faces so the exposed sections of the trunk and arms draw.
        const bool fork = shape == branch_fork(0) || shape == branch_fork(1) ||
                          (shape >= branch_stub(0) && shape <= branch_stub(3));
        if (!fork && shape_intersects(shape, lo, hi))
            continue; // Remove internal faces between stair/roof hulls.
        auto points = face.points;
        for (auto &p : points)
            for (int i = 0; i < 3; i++)
                p[i] += position[i];
        quad(out, points, b.color, face.normal);
        Face style;
        style.signed_block = int(id) + 1;
        style.sky = sky;
        const bool grass_top = b.grass_on_rock;
        style.painted = a.painted || b.foliage || b.tintable || grass_top;
        style.tint = a.painted ? a.tint : pack_rgb(b.grass_on_rock || b.foliage ? b.color : c.grass_tint);
        style.preserve = a.painted || b.foliage ? a.preserve : uint16_t(c.preserve * 1000);
        style.brightness = a.painted ? a.brightness : b.foliage ? 1300 : uint16_t(c.brightness * 1000);
        style.tintable = b.tintable || grass_top;
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
            if (b.wood && b.texture_end_layer >= 0) {
                if (shape == horizontal_mini_pillar(0))
                    v.texture = float(std::abs(v.nx) > .5f ? b.texture_end_layer : b.texture_layer);
                else if (shape == horizontal_mini_pillar(1))
                    v.texture = float(std::abs(v.nz) > .5f ? b.texture_end_layer : b.texture_layer);
            }
        }
    }

}
static void partial_blocks(MeshResult &mesh, const Volume &v, const Registry &r) {
    // Shape records are sparse, even in tall chunks. Keep the previous face
    // order so equivalent tile rebuilds produce identical buffers.
    std::vector<std::tuple<int,int,int,uint8_t>> blocks;
    blocks.reserve(v.shapes.size());
    for(const auto &[index,shape]:v.shapes) {
        const int x=int(index%34)-1;
        const int y=int(index/34%(v.height+2))-1;
        const int z=int(index/34/(v.height+2))-1;
        if(x>=0 && x<32 && z>=0 && z<32 && y>=0 && y<v.height && r.get(v.at(x,y,z)).solid)
            blocks.emplace_back(z,x,y,shape);
    }
    std::sort(blocks.begin(),blocks.end());
    Surface block;
    for(const auto &[z,x,y,shape]:blocks) {
        const auto &c = v.columns[(z + 1) * 34 + x + 1];
        auto a = appearance(v, x, y, z);
        BlockId id = v.at(x, y, z);
        int transition_overlay = 0;
        if (y + 1 < v.height) {
            BlockId above_id = v.at(x, y + 1, z);
            const auto &above = r.get(above_id);
            if (above_id != id && above.solid && above.transition_overlay > 0)
                transition_overlay = above.transition_overlay;
        }
        block.vertices.clear();
        block.indices.clear();
        const auto &definition = r.get(id);
        append_block_surface(block, {float(x), float(v.min_y + y*v.y_step), float(z)}, shape,
                             id, a, c, r, transition_overlay, true);
        // Keep only exposed faces on cell boundaries. Inset and sloped faces remain visible.
        for (size_t f = 0; f < block.vertices.size(); f += 4) {
            auto &n = block.vertices[f];
            int axis = std::abs(n.nx) > .99f   ? 0
                       : std::abs(n.ny) > .99f ? 1
                       : std::abs(n.nz) > .99f ? 2
                                               : -1;
            bool covered = false;
            int seam_edge = -1;
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
                if (on_boundary && axis != 1 && shape_type(shape) == int(BlockShape::Slab) &&
                    !r.get(id).wood && !r.get(id).foliage &&
                    ((normal < 0 && (axis == 0 ? x : z) == 0) || (normal > 0 && (axis == 0 ? x : z) == 31)))
                    seam_edge = (axis == 2 ? 0 : 2) + int(normal > 0);
                int q[3] = {x, y, z};
                q[axis] += normal > 0 ? 1 : -1;
                if (on_boundary && axis==1 &&
                    ((q[1]<0 && v.unknown_below) || (q[1]>=v.height && v.unknown_above)))
                    covered=true;
                const auto &neighbor = r.get(v.at(q[0], q[1], q[2]));
                if (on_boundary && neighbor.solid && (!neighbor.foliage || r.get(id).foliage)) {
                    auto q_it = v.shapes.find(voxel_index(v, q[0], q[1], q[2]));
                    if (q_it == v.shapes.end())
                        covered = true;
                }
            }
            if (covered)
                continue;
            int base = int(mesh.solid.vertices.size());
            int sample[] = {x, y, z};
            if (axis >= 0)
                sample[axis] += (axis == 0 ? n.nx : axis == 1 ? n.ny : n.nz) > 0 ? 1 : -1;
            else if (n.ny > 0)
                sample[1]++;
            const float face_flags = (definition.wood || definition.foliage ? vegetation_fade_flag : 0.f) +
                64.f * (axis == 1 ? surface_light(v, sample[0], sample[1], sample[2])
                              : side_light(neighbor_sky_light(v, sample[0], sample[1], sample[2]))) +
                terrain_edge_code * (seam_edge + 1);
            for (int i = 0; i < 4; i++)
            {
                auto vertex=block.vertices[f+i];
                vertex.x*=v.step; vertex.z*=v.step;
                float bottom=float(v.min_y+y*v.y_step);
                vertex.y=bottom+(vertex.y-bottom)*v.y_step;
                vertex.wind*=v.y_step;
                vertex.decal = std::fmod(vertex.decal, 64.f) + face_flags;
                if (shape_type(shape) == int(BlockShape::Slab) && !r.get(id).wood && !r.get(id).foliage) {
                    const float local_x = vertex.x / v.step - x, local_z = vertex.z / v.step - z;
                    const float local_y = (vertex.y - bottom) / (.5f * v.y_step);
                    vertex.u = axis == 0 ? local_y : axis == 2 ? local_x : local_z;
                    vertex.v = axis == 0 ? local_z : axis == 2 ? local_y : local_x;
                    vertex.decal += terrain_quad_overlap;
                }
                float nx=vertex.nx/v.step, ny=vertex.ny/v.y_step, nz=vertex.nz/v.step;
                float length=std::sqrt(nx*nx+ny*ny+nz*nz);
                vertex.nx=nx/length; vertex.ny=ny/length; vertex.nz=nz/length;
                mesh.solid.vertices.push_back(vertex);
                if (definition.wood && v.step == 1)
                    mesh.sky_shadows.vertices.push_back(vertex);
            }
            const size_t start = (f / 4) * 6;
            const int shadow_base = definition.wood && v.step == 1 ? int(mesh.sky_shadows.vertices.size()) - 4 : 0;
            for (size_t i = start; i < start + 6; i++) {
                mesh.solid.indices.push_back(base + block.indices[i] - int(f));
                if (definition.wood && v.step == 1)
                    mesh.sky_shadows.indices.push_back(shadow_base + block.indices[i] - int(f));
            }
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
    const bool physical_pebbles = std::any_of(g.settings().resources.begin(), g.settings().resources.end(),
        [](const ResourceRule &rule) { return rule.block == "altaria:pebble" && rule.chance > 0.f; });
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
                    if (!g.settings().generate_resource_deposits || physical_pebbles ||
                        float(hash & 65535) / 65535 > c.pebbles)
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
static void root_cards(MeshResult &mesh, const Volume &v, const Generator &g) {
    if (!v.has_root_plants)
        return;
    const auto &registry = g.blocks();
    for (int z = 0; z < 32; ++z)
        for (int x = 0; x < 32; ++x)
            for (int y = 0; y < v.height; ++y) {
                const auto block = v.at(x, y, z);
                if (block == registry.air || !registry.get(block).root_plant)
                    continue;
                const int world_y = v.min_y + y;
                const auto &support = registry.get(v.at(x, y + 1, z));
                if (!support.solid || support.wood || support.foliage)
                    continue;
                const float base = float(world_y + 1);
                const uint64_t hash = decoration_hash(mesh.key.x + x, world_y, mesh.key.z + z, g.seed_value());
                const float angle = float(hash & 255) * .02454f;
                const float height = .75f + .24f * float((hash >> 8) & 255) / 255.f;
                const float end = base - height;
                const std::array<float, 3> glow{x + .5f, base - .32f, z + .5f};
                if (mesh.root_lights.size() < 8 && std::none_of(mesh.root_lights.begin(), mesh.root_lights.end(),
                    [&](const auto &other) {
                        const float dx = glow[0] - other[0], dy = glow[1] - other[1], dz = glow[2] - other[2];
                        return dx * dx + dy * dy + dz * dz < 9.f;
                    }))
                    mesh.root_lights.push_back(glow);
                Face style;
                style.signed_block = int(block) + 1;
                style.sky = v.sky_exposure(x, y, z);
                style.brightness = 1000;
                // Two crossed, double-sided cards show the complete root sprite.
                for (float turn : {angle, angle + 1.5707963f}) {
                    const float dx = .46f * std::cos(turn), dz = .46f * std::sin(turn);
                    const float cx = x + .5f, cz = z + .5f;
                    std::array<std::array<float, 3>, 4> points{{
                        {cx - dx, end, cz - dz}, {cx + dx, end, cz + dz},
                        {cx + dx, base, cz + dz}, {cx - dx, base, cz - dz}}};
                    std::array<float, 3> normal{-std::sin(turn), 0.f, std::cos(turn)};
                    for (int side = 0; side < 2; ++side) {
                        quad(mesh.solid, points, registry.get(block).color, normal);
                        style_quad(mesh.solid, style, registry);
                        for (size_t vertex = mesh.solid.vertices.size() - 4; vertex < mesh.solid.vertices.size(); ++vertex) {
                            auto &sprite = mesh.solid.vertices[vertex];
                            // The opaque edge of the texture must touch the support.
                            const bool attached = sprite.y == base;
                            sprite.v = attached ? 0.f : 1.f;
                            sprite.u = 1.f - sprite.u;
                            sprite.decal += resource_face_uv + resource_no_outline;
                        }
                        std::swap(points[0], points[1]);
                        std::swap(points[2], points[3]);
                        normal[0] = -normal[0];
                        normal[2] = -normal[2];
                    }
                }
            }
}
static void resource_pieces(MeshResult &mesh, Volume &v, const Generator &g, const EditMap &edits,
                            const ResourceDepletions *depleted) {
    if (mesh.key.level != 0 || g.settings().preset != WorldPreset::Default || g.settings().resources.empty())
        return;
    const auto &registry = g.blocks();
    struct Deposit {
        const ResourceRule *rule;
        BlockId block;
        std::vector<BlockId> hosts;
    };
    std::vector<Deposit> deposits;
    float min_depth = std::numeric_limits<float>::infinity();
    float max_depth = -min_depth;
    for (const auto &rule : g.settings().resources) {
        Deposit deposit{&rule, registry.find(rule.block), {}};
        for (const auto &host : rule.hosts)
            deposit.hosts.push_back(registry.find(host));
        if (deposit.block != registry.air) {
            deposits.push_back(std::move(deposit));
            min_depth = std::min(min_depth, rule.min_depth);
            max_depth = std::max(max_depth, rule.max_depth);
        }
    }
    if (deposits.empty())
        return;
    const std::array<IVec3, 6> faces{{{0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}};
    for (int z = 0; z < 32; ++z)
        for (int x = 0; x < 32; ++x) {
            const auto &column = v.columns[(z + 1) * 34 + x + 1];
            const int first = int(std::clamp(std::ceil(column.height - max_depth - v.min_y), 0.f, float(v.height)));
            const int last = int(std::clamp(std::floor(column.height - min_depth - v.min_y), -1.f, float(v.height - 1)));
            for (int y = first; y <= last; ++y) {
                const BlockId host = v.at(x, y, z);
                if (host == registry.air || !registry.get(host).solid)
                    continue;
                const IVec3 support{mesh.key.x + x, v.min_y + y, mesh.key.z + z};
                const float depth = column.height - support.y;
                for (size_t i = 0; i < deposits.size(); ++i) {
                    const auto &deposit = deposits[i];
                    const auto &rule = *deposit.rule;
                    const bool modeled = !registry.get(deposit.block).deposit_model.empty();
                    if (depth < rule.min_depth || depth > rule.max_depth ||
                        std::find(deposit.hosts.begin(), deposit.hosts.end(), host) == deposit.hosts.end())
                        continue;
                    const uint64_t hash = decoration_hash(support.x, support.y, support.z,
                        int32_t(uint32_t(g.seed_value()) + uint32_t(i) * 17389u + 8011u));
                    if (float(hash & 65535) / 65536.f >= rule.chance)
                        continue;
                    const auto shape = v.shapes.find(voxel_index(v, x, y, z));
                    std::array<int, 6> exposed{};
                    int count = 0;
                    for (int side = 0; side < (rule.all_faces ? int(faces.size()) : 1); ++side) {
                        const auto &face = faces[side];
                        const int adjacent_y = y + face.y;
                        if ((adjacent_y < 0 && v.unknown_below) || (adjacent_y >= v.height && v.unknown_above) ||
                            (side != 0 && shape != v.shapes.end()) ||
                            v.at(x + face.x, adjacent_y, z + face.z) != registry.air)
                            continue;
                        exposed[count++] = side;
                    }
                    if (!count || edits.count(support))
                        continue;
                    const int top_count = exposed[0] == 0 ? 1 : 0;
                    if (count > top_count)
                        std::rotate(exposed.begin() + top_count,
                                    exposed.begin() + top_count + (hash >> 56) % (count - top_count),
                                    exposed.begin() + count);
                    const int used_faces = std::min(count, 4);
                    const int budget = std::max(rule.pieces, used_faces);
                    for (int i_face = 0; i_face < used_faces; ++i_face) {
                        const int side = exposed[i_face];
                        const auto face = faces[side];
                        const std::array<float, 3> outward{float(face.x), float(face.y), float(face.z)};
                        const std::array<float, 3> tangent = face.x ? std::array<float, 3>{0, 0, 1}
                                                                    : std::array<float, 3>{1, 0, 0};
                        const std::array<float, 3> across{
                            tangent[1] * outward[2] - tangent[2] * outward[1],
                            tangent[2] * outward[0] - tangent[0] * outward[2],
                            tangent[0] * outward[1] - tangent[1] * outward[0]};
                        uint32_t mask = 0;
                        if (depleted) {
                            const auto found = depleted->find(support);
                            if (found != depleted->end())
                                mask = found->second;
                        }
                        Face style;
                        style.signed_block = int(deposit.block) + 1;
                        style.sky = v.sky_exposure(x + face.x, y + face.y, z + face.z);
                        style.painted = pack_rgb(rule.tint) != 0xffffff;
                        style.tint = pack_rgb(rule.tint);
                        style.preserve = 180;
                        style.brightness = 1000;
                        const bool irregular = rule.irregular || rule.all_faces;
                        const int grid = irregular ? 2 : 3;
                        const int cells = grid * grid;
                        const int stride = irregular ? 1 : 4;
                        const int face_pieces = budget / used_faces + int(i_face < budget % used_faces);
                        for (int slot = 0; slot < face_pieces; ++slot) {
                            const int slot_id = side * 4 + slot;
                            if (mask & (1u << slot_id))
                                continue;
                            const int cell = (int((hash >> 16) % cells) + slot * stride) % cells;
                            float u = (float(cell % grid) + .5f) / grid - .5f;
                            float w = (float(cell / grid) + .5f) / grid - .5f;
                            const uint64_t detail = hash * (0xd6e8feb86659fd93ULL + uint64_t(slot_id) * 2);
                            const auto sample = [&](int shift) { return float((detail >> shift) & 255) / 255.f; };
                            if (irregular) {
                                const float spread = slot == 0 ? .36f : .88f;
                                u = u * spread + .26f * (sample(0) - .5f);
                                w = w * spread + .26f * (sample(8) - .5f);
                            }
                            if (rule.all_faces && face_pieces == 1) {
                                u = .08f * (sample(0) - .5f);
                                w = .08f * (sample(8) - .5f);
                            }
                            const float random_size = float((hash >> (24 + 4 * slot)) & 15) / 15.f;
                            const float variation = irregular ? .28f : .20f;
                            const float size = rule.size * (1.f - variation * (1.f - random_size));
                            std::array<float, 3> dimensions{size, size, size};
                            if (irregular) {
                                const float length = slot == 0 ? rule.size * (1.05f + .18f * sample(16)) : size;
                                dimensions = {length, length * (.58f + .37f * sample(24)),
                                              length * (.56f + .39f * sample(32))};
                            }
                            const float yaw = float((hash >> (32 + 8 * slot)) & 255) * (6.2831853f / 256.f);
                            const float tilt = irregular && !modeled ? (random_size - .5f) * .58f : 0.f;
                            const float cs = std::cos(yaw), sn = std::sin(yaw);
                            const float ct = std::cos(tilt), st = std::sin(tilt);
                            std::array<std::array<float, 3>, 3> axes{};
                            std::array<float, 3> position{x + .5f, support.y + .5f, z + .5f};
                            for (int axis = 0; axis < 3; ++axis) {
                                const float turned = sn * tangent[axis] + cs * across[axis];
                                axes[0][axis] = cs * tangent[axis] - sn * across[axis];
                                axes[1][axis] = ct * outward[axis] + st * turned;
                                axes[2][axis] = ct * turned - st * outward[axis];
                                position[axis] += u * tangent[axis] + w * across[axis] +
                                                  (.5f + dimensions[1] * (.5f - rule.embed)) * outward[axis];
                            }
                            // The rotated footprint must fit on its own face even beside a solid cell.
                            for (int axis = 0; axis < 3; ++axis) {
                                if (outward[axis] != 0.f)
                                    continue;
                                float half_extent = 0.f;
                                for (int local_axis = 0; local_axis < 3; ++local_axis)
                                    half_extent += .5f * dimensions[local_axis] * std::abs(axes[local_axis][axis]);
                                const float center = axis == 0 ? x + .5f : axis == 1 ? support.y + .5f : z + .5f;
                                const float travel = std::max(0.f, .49f - half_extent);
                                position[axis] = std::clamp(position[axis], center - travel, center + travel);
                            }
                            if (shape != v.shapes.end()) {
                                float top = 1.f;
                                for (float dx : {-dimensions[0] / 2, dimensions[0] / 2})
                                    for (float dz : {-dimensions[2] / 2, dimensions[2] / 2})
                                        top = std::min(top, shape_top(shape->second,
                                            position[0] - x + axes[0][0] * dx + axes[2][0] * dz,
                                            position[2] - z + axes[0][2] * dx + axes[2][2] * dz));
                                if (top <= .01f)
                                    continue;
                                position[1] += top - 1.f;
                            }
                            v.resources.push_back({support, position, dimensions, axes, face, deposit.block, uint8_t(slot_id)});
                            if (modeled)
                                continue;
                            for (const auto &cube_face : shape_faces(0)) {
                                auto points = cube_face.points;
                                std::array<float, 3> normal{};
                                for (int axis = 0; axis < 3; ++axis) {
                                    normal[axis] = axes[0][axis] * cube_face.normal[0] +
                                                   axes[1][axis] * cube_face.normal[1] +
                                                   axes[2][axis] * cube_face.normal[2];
                                    for (size_t corner = 0; corner < points.size(); ++corner) {
                                        const auto &local = cube_face.points[corner];
                                        points[corner][axis] = position[axis] +
                                            axes[0][axis] * dimensions[0] * (local[0] - .5f) +
                                            axes[1][axis] * dimensions[1] * (local[1] - .5f) +
                                            axes[2][axis] * dimensions[2] * (local[2] - .5f);
                                    }
                                }
                                quad(mesh.solid, points, registry.get(deposit.block).color, normal);
                                style_quad(mesh.solid, style, registry);
                                for (size_t vertex = mesh.solid.vertices.size() - 4; vertex < mesh.solid.vertices.size(); ++vertex)
                                    mesh.solid.vertices[vertex].decal += resource_face_uv +
                                        (rule.outline ? 0.f : resource_no_outline);
                            }
                        }
                    }
                    break;
                }
            }
        }
}
std::optional<GrassInstance> grass_for_block(std::array<float, 3> origin, uint8_t shape,
                                             const Appearance &appearance, const Column &column,
                                             const BlockDef &block, const Settings &settings, int seed, int tuft) {
    const uint64_t h = decoration_hash(int(std::floor(origin[0])), int(std::floor(origin[1])),
                                      int(std::floor(origin[2])), seed ^ 0x67726173 ^ (tuft * 0x19e37));
    const auto variation = [&](int shift) { return float((h >> shift) & 255) / 255.f; };
    const int grid = int(std::ceil(std::sqrt(float(settings.grass_tufts))));
    const int rows = (settings.grass_tufts + grid - 1) / grid;
    float x = (tuft % grid + .27f + variation(0) * .46f) / grid;
    float z = (tuft / grid + .27f + variation(8) * .46f) / rows;
    float height = shape ? shape_top(shape, x, z) : 1.f;
    if (height <= 0 && tuft == 0) {
        x = z = .5f;
        height = shape_top(shape, x, z);
    }
    if (height <= 0)
        return std::nullopt;

    GrassInstance inst;
    inst.x = origin[0] + x;
    inst.y = origin[1] + height;
    inst.z = origin[2] + z;
    inst.width = .62f + variation(24) * .21f;
    inst.height = settings.grass_height * (1.f + settings.grass_height_variation * (2.f * variation(32) - 1.f));
    if (variation(48) < settings.grass_tall_chance)
        inst.height *= settings.grass_tall_multiplier;
    inst.angle = variation(16) * 6.2831853f;
    inst.phase = variation(40) * 10.f;
    inst.tint = appearance.painted ? unpack_rgb(appearance.tint) :
                unpack_rgb(pack_rgb(block.grass_on_rock ? block.color : column.grass_tint));
    inst.preserve = (appearance.painted ? appearance.preserve : uint16_t(column.preserve * 1000)) / 1000.f;
    inst.brightness = (appearance.painted ? appearance.brightness : uint16_t(column.brightness * 1000)) / 1000.f;
    inst.top_layer = float(block.texture_layer);
    inst.apply_tint = appearance.painted || block.tintable || block.grass_on_rock;
    inst.sky_exposure = origin[1] + 1.f >= column.height ? 15 : 0;
    return inst;
}

static void surface_vegetation(MeshResult &mesh, const Volume &v, const Generator &g) {
    if (mesh.key.level != 0)
        return;
    const auto &r = g.blocks();
    std::unordered_map<IVec3, std::vector<const ResourcePiece *>, CoordHash> ground_resources;
    for (const auto &piece : v.resources)
        if (piece.exposed_face.y == 1)
            ground_resources[piece.support].push_back(&piece);
    const auto edge_weight = [](float position) {
        const float t = std::clamp((position - .52f) / .44f, 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    };
    // Two passes: the first two thirds of each block's tufts come first, so a
    // distant tile can draw only that prefix and keep an even, thinner lawn.
    const int core = std::max(1, g.settings().grass_tufts * 2 / 3);
    for (int pass = 0; pass < 2; ++pass) {
    const bool core_pass = pass == 0;
    if (core_pass == false)
        mesh.grass_core = int(mesh.grass.size());
    for (int z = 0; z < 32; ++z) {
        for (int x = 0; x < 32; ++x) {
            const auto &column = v.columns[(z + 1) * 34 + x + 1];
            for (int y = 0; y < v.height; ++y) {
                const BlockId id = v.at(x, y, z);
                if (id != r.grass && !r.get(id).grass_on_rock)
                    continue;
                if ((y + 1 == v.height && v.unknown_above) || v.at(x, y + 1, z) != r.air)
                    continue;
                const auto shape = v.shapes.find(voxel_index(v, x, y, z));
                const auto a = appearance(v, x, y, z);
                const uint8_t sky = v.sky_exposure(x, y + 1, z);
                const auto &settings = g.settings();
                const auto deposit = ground_resources.find({mesh.key.x + x, v.min_y + y, mesh.key.z + z});
                const auto neighboring_grass = [&](int nx, int nz) {
                    const auto block = v.at(nx, y, nz);
                    return block == r.grass || r.get(block).grass_on_rock;
                };
                const bool west = !neighboring_grass(x - 1, z), east = !neighboring_grass(x + 1, z);
                const bool north = !neighboring_grass(x, z - 1), south = !neighboring_grass(x, z + 1);
                for (int i = core_pass ? 0 : core; i < (core_pass ? core : settings.grass_tufts); ++i) {
                    auto tuft = grass_for_block({float(mesh.key.x + x), float(v.min_y + y), float(mesh.key.z + z)},
                        shape == v.shapes.end() ? 0 : shape->second, a, column, r.get(id), settings, g.seed_value(), i);
                    if (tuft) {
                        tuft->sky_exposure = sky;
                        tuft->x -= mesh.key.x;
                        tuft->z -= mesh.key.z;
                        const float u = tuft->x - x, w = tuft->z - z;
                        tuft->lean_x = .28f * (float(east) * edge_weight(u) -
                                               float(west) * edge_weight(1.f - u));
                        tuft->lean_z = .28f * (float(south) * edge_weight(w) -
                                               float(north) * edge_weight(1.f - w));
                        if (deposit != ground_resources.end() &&
                            std::any_of(deposit->second.begin(), deposit->second.end(), [&](const ResourcePiece *piece) {
                                const float radius = .5f * std::hypot(piece->dimensions[0], piece->dimensions[2]) +
                                                     .5f * tuft->width + .13f +
                                                     std::hypot(tuft->lean_x, tuft->lean_z);
                                const float dx = tuft->x - piece->position[0], dz = tuft->z - piece->position[2];
                                return dx * dx + dz * dz < radius * radius;
                            }))
                            continue;
                        mesh.grass.push_back(*tuft);
                    }
                }
            }
        }
    }
    }
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
static void collect_terrain_edges(MeshResult &mesh, const Volume &v, const Generator &g) {
    const auto &r = g.blocks();
    Surface scratch;
    scratch.vertices.resize(4);
    for (int edge = 0; edge < 4; ++edge)
        for (int i = 0; i < 32; ++i) {
            const int x = edge < 2 ? i : (edge == 2 ? 0 : 31);
            const int z = edge < 2 ? (edge == 0 ? 0 : 31) : i;
            auto &profile = mesh.edges[edge][i];
            profile.floor = float(v.unknown_below ? v.min_y : g.settings().min_y);
            Face previous;
            const auto append = [&](BlockId id, int y, float bottom, float top) {
                if (top <= bottom)
                    return;
                const auto &block = r.get(id);
                const int cell[] = {x, y, z};
                const auto style = face_style((edge & 1 ? 1 : -1) * (int(id) + 1), edge < 2 ? 2 : 0,
                                              cell, v, r, mesh.key.x, mesh.key.z);
                top = std::min(top, float(style.liquid_ceiling));
                if (top <= bottom)
                    return;
                if (!profile.spans.empty() && profile.spans.back().top == bottom && style == previous) {
                    profile.spans.back().top = top;
                    return;
                }
                auto &vertex = scratch.vertices.back();
                vertex = {};
                vertex.r = block.color.r;
                vertex.g = block.color.g;
                vertex.b = block.color.b;
                style_quad(scratch, style, r);
                profile.spans.push_back({bottom, top, block.solid, vertex, {}});
                previous = style;
            };
            if (v.bottom_fill && profile.floor < v.min_y)
                append(v.bottom_fill, 0, profile.floor, float(v.min_y));
            for (int y = 0; y < v.height; ++y) {
                const auto id = v.at(x, y, z);
                const auto &block = r.get(id);
                if ((!block.solid && !block.water && !block.liquid && !block.lava) || block.wood || block.foliage)
                    continue;
                const auto shape = v.shapes.find(voxel_index(v, x, y, z));
                const float bottom = float(v.min_y + y * v.y_step);
                if (shape != v.shapes.end() && shape_type(shape->second) != int(BlockShape::Slab))
                    continue; // The exact authored shape retains its own boundary faces.
                const float top = bottom + (shape == v.shapes.end() ? 1.f : .5f) * v.y_step;
                // The deep opaque substrate normally spans most of the column.
                // Reuse the last stone style until an opening, paint or surface.
                const bool deep_stone = id == r.stone && v.surfaces.empty() &&
                    y + 1 < v.sky_floor[(z + 1) * 34 + x + 1] - 8 &&
                    r.get(v.at(x + (edge == 2 ? -1 : edge == 3 ? 1 : 0), y,
                               z + (edge == 0 ? -1 : edge == 1 ? 1 : 0))).solid &&
                    v.at(x, y + 1, z) == id && !profile.spans.empty() && previous.sky == 0 &&
                    std::abs(previous.signed_block) == int(id) + 1 && previous.overlay_layer < 0 &&
                    profile.spans.back().top == bottom;
                if (deep_stone)
                    profile.spans.back().top = top;
                else
                    append(id, y, bottom, top);
            }
        }
}

void append_terrain_seam(Surface &surface, int axis, float boundary, float from, float to,
                         const TerrainEdgeColumn &first, const TerrainEdgeColumn &second) {
    const float floor = std::max(first.floor, second.floor);
    using EdgeHeight = std::array<float, 2>;
    const auto midpoint = [](EdgeHeight height) { return (height[0] + height[1]) * .5f; };
    const auto top_at = [](const TerrainEdgeSpan &span, float at) {
        return span.top + (span.end_top.value_or(span.top) - span.top) * at;
    };
    const auto emit = [&](const TerrainEdgeSpan &span, EdgeHeight bottom, EdgeHeight top, float direction,
                          float segment_from, float segment_to) {
        if (midpoint(bottom) >= midpoint(top))
            return;
        std::array<float, 3> normal{};
        normal[axis] = direction;
        const int along = 2 - axis;
        std::array<std::array<float, 3>, 4> points{};
        for (int i = 0; i < 4; ++i) {
            points[i][axis] = boundary;
            const int end = int(i == 1 || i == 2);
            points[i][along] = end ? segment_to : segment_from;
            points[i][1] = i < 2 ? std::max(top[end], bottom[end]) : bottom[end];
        }
        quad(surface, points, {span.material.r, span.material.g, span.material.b}, normal);
        for (size_t i = surface.vertices.size() - 4; i < surface.vertices.size(); ++i) {
            auto &v = surface.vertices[i];
            const auto position = v;
            v = span.material;
            v.x = position.x; v.y = position.y; v.z = position.z;
            v.nx = normal[0]; v.ny = normal[1]; v.nz = normal[2];
            v.u = position.u;
            v.v = 1.f - position.v;
            v.decal += seam_quad_overlap;
        }
    };
    std::vector<float> cuts;
    const auto cross = [&](EdgeHeight a, EdgeHeight b, float begin, float end_at) {
        const float start = a[0] - b[0], end = a[1] - b[1];
        if (start * end < 0.f)
            cuts.push_back(begin + (end_at - begin) * start / (start - end));
    };
    const auto find_cuts = [&](const TerrainEdgeColumn &a, const TerrainEdgeColumn &b, float begin, float end) {
        const auto upper = [&](const TerrainEdgeSpan &span) {
            return EdgeHeight{top_at(span, begin), top_at(span, end)};
        };
        for (const auto &span : a.spans) {
            if (!span.end_top)
                continue;
            const auto line = upper(span);
            if (line[0] == line[1])
                continue;
            cross(line, {floor, floor}, begin, end);
            for (const auto &other : b.spans) {
                cross(line, {other.bottom, other.bottom}, begin, end);
                cross(line, upper(other), begin, end);
            }
        }
    };
    find_cuts(first, second, 0.f, 1.f);
    find_cuts(second, first, 0.f, 1.f);
    const auto stitch = [&](const TerrainEdgeColumn &a, const TerrainEdgeColumn &b, float direction,
                            float begin, float end) {
        const auto clip_top = [&](const TerrainEdgeSpan &span) {
            return EdgeHeight{top_at(span, begin), top_at(span, end)};
        };
        const float segment_from = from + (to - from) * begin;
        const float segment_to = from + (to - from) * end;
        size_t next = 0;
        for (const auto &span : a.spans) {
            const float bottom = std::max(floor, span.bottom);
            EdgeHeight start{bottom, bottom};
            const EdgeHeight end_top = clip_top(span);
            while (next < b.spans.size() && midpoint(clip_top(b.spans[next])) <= midpoint(start))
                ++next;
            for (size_t i = next; i < b.spans.size() && b.spans[i].bottom < midpoint(end_top); ++i) {
                const auto &other = b.spans[i];
                if (span.solid && !other.solid)
                    continue;
                emit(span, start, {other.bottom, other.bottom}, direction, segment_from, segment_to);
                const auto other_top = clip_top(other);
                if (midpoint(other_top) > midpoint(start))
                    start = other_top;
            }
            emit(span, start, end_top, direction, segment_from, segment_to);
        }
    };
    if (cuts.empty()) {
        stitch(first, second, 1.f, 0.f, 1.f);
        stitch(second, first, -1.f, 0.f, 1.f);
        return;
    }
    cuts.push_back(0.f);
    cuts.push_back(1.f);
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    for (size_t i = 1; i < cuts.size(); ++i) {
        stitch(first, second, 1.f, cuts[i - 1], cuts[i]);
        stitch(second, first, -1.f, cuts[i - 1], cuts[i]);
    }
}

// A crown box relative to the tree: x/z from the trunk centre, y from the
// bottom of the first trunk block (base y + 1).
struct CrownBox { float x0, x1, y0, y1, z0, z1; };
// Stacked boxes keep the silhouette: Spire tiers into a cone, other trees
// split upper and lower foliage (side branches, lean); shrubs are one mass.
static int crown_bands(TreeForm form, bool far) {
    if (form == TreeForm::Spire)
        return far ? 2 : 3;
    return form == TreeForm::Shrub || far ? 1 : 2;
}
// Boxes whose second moments match the leaf cells (slightly widened so the
// face-on silhouette matches a round crown), clamped to the real bounds.
// Stacked bands span the full height, so a spire keeps its tip; a single
// box fits its height like its width.
static std::vector<CrownBox> crown_boxes(const std::vector<IVec3> &cells, int bands) {
    std::vector<CrownBox> out;
    if (cells.empty())
        return out;
    constexpr float widen = 1.12f;
    const auto fit = [&](float sum, float squares, float n, float lo, float hi, float &a, float &b) {
        const float mean = sum / n, var = std::max(0.f, squares / n - mean * mean);
        const float half = std::sqrt(3.f * var + .25f) * widen;
        a = std::max(lo, mean - half);
        b = std::min(hi, mean + half);
    };
    float sy = 0, syy = 0, ylo = 1e9f, yhi = -1e9f;
    for (const auto &c : cells) {
        const float y = float(c.y) - .5f;
        sy += y; syy += y * y;
        ylo = std::min(ylo, y - .5f); yhi = std::max(yhi, y + .5f);
    }
    float y0 = ylo, y1 = yhi;
    if (bands == 1)
        fit(sy, syy, float(cells.size()), ylo, yhi, y0, y1);
    for (int band = 0; band < bands; ++band) {
        const float b0 = y0 + (y1 - y0) * float(band) / float(bands);
        const float b1 = y0 + (y1 - y0) * float(band + 1) / float(bands);
        float n = 0, sx = 0, sxx = 0, sz = 0, szz = 0;
        float xlo = 1e9f, xhi = -1e9f, zlo = 1e9f, zhi = -1e9f;
        for (const auto &c : cells) {
            const float y = float(c.y) - .5f;
            // Outer bands also take the sparse cells beyond the fitted extent.
            if ((y < b0 && band > 0) || (y >= b1 && band < bands - 1))
                continue;
            const float x = float(c.x), z = float(c.z);
            n += 1; sx += x; sxx += x * x; sz += z; szz += z * z;
            xlo = std::min(xlo, x - .5f); xhi = std::max(xhi, x + .5f);
            zlo = std::min(zlo, z - .5f); zhi = std::max(zhi, z + .5f);
        }
        if (n == 0)
            continue;
        CrownBox box{0, 0, b0, b1, 0, 0};
        fit(sx, sxx, n, xlo, xhi, box.x0, box.x1);
        fit(sz, szz, n, zlo, zhi, box.z0, box.z1);
        out.push_back(box);
    }
    return out;
}
// Leaf cells of a tree relative to its base, minus leaves the player removed.
static std::vector<IVec3> leaf_cells(const std::vector<TreePart> &parts, const TreeSite &tree, BlockId leaves,
                                     const EditMap &edits) {
    std::vector<IVec3> cells;
    for (const auto &part : parts) {
        if (part.second != leaves)
            continue;
        const auto edit = edits.find(part.first);
        if (edit != edits.end() && edit->second != leaves)
            continue;
        cells.push_back({part.first.x - tree.base.x, part.first.y - tree.base.y, part.first.z - tree.base.z});
    }
    return cells;
}
// Far tiles hold thousands of trees, too many to grow each one. They use the
// mean crown of real trees of the same form and size, centred on the trunk.
static std::vector<CrownBox> typical_crown(const Generator &g, const TreeSite &tree, BlockId leaves) {
    static std::mutex lock;
    static std::map<std::tuple<int, int, int>, std::vector<CrownBox>> cache;
    const auto key = std::make_tuple(int(tree.form), tree.height, tree.radius);
    std::lock_guard<std::mutex> guard(lock);
    const auto found = cache.find(key);
    if (found != cache.end())
        return found->second;
    const int bands = crown_bands(tree.form, true);
    std::vector<CrownBox> sum(size_t(bands), CrownBox{0, 0, 0, 0, 0, 0});
    std::vector<int> count(size_t(bands), 0);
    constexpr int samples = 8;
    for (int i = 0; i < samples; ++i) {
        TreeSite sample = tree;
        sample.base = {0, 0, 0};
        sample.branch_axis = i & 1;
        sample.pattern = (0x9e3779b97f4a7c15ULL * uint64_t(i + 1)) ^ (uint64_t(i) << 32);
        const auto boxes = crown_boxes(leaf_cells(g.tree_parts(sample), sample, leaves, {}), bands);
        if (int(boxes.size()) != bands)
            continue;
        for (int b = 0; b < bands; ++b) {
            const auto &box = boxes[size_t(b)];
            const float hx = (box.x1 - box.x0) * .5f, hz = (box.z1 - box.z0) * .5f;
            // Branch sides are random, so the mean crown is round about the trunk.
            const float half = (hx + hz) * .5f;
            auto &s = sum[size_t(b)];
            s.x0 -= half; s.x1 += half; s.z0 -= half; s.z1 += half;
            s.y0 += box.y0; s.y1 += box.y1;
            ++count[size_t(b)];
        }
    }
    std::vector<CrownBox> out;
    for (int b = 0; b < bands; ++b) {
        if (!count[size_t(b)])
            continue;
        const float n = float(count[size_t(b)]);
        auto box = sum[size_t(b)];
        box.x0 /= n; box.x1 /= n; box.y0 /= n; box.y1 /= n; box.z0 /= n; box.z1 /= n;
        out.push_back(box);
    }
    cache.emplace(key, out);
    return out;
}

// Distant stand-ins for trees: a trunk stick plus a few boxes fitted to the
// tree's real foliage, so the swap with the voxel tree keeps its outline.
static void tree_proxies(MeshResult &mesh, const Volume &v, const Generator &g,
                         const EditMap &edits, const AppearanceMap &surfaces) {
    const auto &r = g.blocks();
    const auto &key = mesh.key;
    const auto wood = r.find("altaria:wood");
    const bool draw_trunks = key.level <= g.settings().tree_lod_max;
    auto sites = g.tree_sites(key.x, key.z, key.x + key.size(), key.z + key.size(), false);
    // Shrubs are a pixel or less past the LOD 3 ring; skip them there.
    if (key.level < 4) {
        const auto shrubs = g.shrub_sites(key.x, key.z, key.x + key.size(), key.z + key.size(), false);
        sites.insert(sites.end(), shrubs.begin(), shrubs.end());
    }
    for (const auto &tree : sites) {
        const auto [x, y, z] = tree.base;
        const bool shrub = tree.form == TreeForm::Shrub;
        const auto leaves = r.find(shrub ? g.settings().bush_block : "altaria:leaves");
        auto canopy = tree.canopy;
        // Near tiles fit each tree's own crown (lean included); far tiles
        // use the typical crown unless edits force the real parts anyway.
        const bool exact = draw_trunks || !edits.empty() || !surfaces.empty();
        std::vector<CrownBox> crown;
        if (exact) {
            const auto parts = g.tree_parts(tree);
            for (const auto &part : parts) {
                if (part.second != leaves)
                    continue;
                const auto edit = edits.find(part.first);
                if (edit != edits.end() && edit->second != leaves)
                    continue;
                const auto paint = surfaces.find(part.first);
                if (paint != surfaces.end() && paint->second.painted) {
                    canopy = paint->second;
                    break;
                }
            }
            crown = crown_boxes(leaf_cells(parts, tree, leaves, edits), crown_bands(tree.form, !draw_trunks));
        } else {
            crown = typical_crown(g, tree, leaves);
        }
        if (crown.empty())
            continue;
        // Past the LOD 3 ring a crown covers a pixel or less. Keep a share of
        // the trees and widen them, so a forest keeps its cover and colour
        // with a fraction of the triangles.
        const int keep = exact ? 1 : key.level >= 5 ? 4 : key.level == 4 ? 2 : 1;
        if (keep > 1) {
            uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(z) * 19349663u;
            h ^= h >> 15;
            h *= 2246822519u;
            h ^= h >> 13;
            if (h % uint32_t(keep) != 0)
                continue;
            const float widen = keep == 4 ? 2.f : 1.414f;
            for (auto &box : crown) {
                box.x0 *= widen; box.x1 *= widen;
                box.z0 *= widen; box.z1 *= widen;
            }
        }
        const int ix = (x - key.x) / v.step, iz = (z - key.z) / v.step;
        const auto &column = v.columns[(iz + 1) * 34 + ix + 1];
        int ground = v.min_y + int(std::floor((column.height - v.min_y) / v.y_step)) * v.y_step + v.y_step;
        const int start = std::min(v.height - 1, int(std::ceil((column.height +
                                    g.settings().cliff_strength * column.cliff - v.min_y) / v.y_step)));
        for (int iy = start; iy >= 0 && iy >= start - 8; --iy) {
            const auto &block = r.get(v.at(ix, iy, iz));
            if (block.solid || block.water) {
                ground = v.min_y + (iy + 1) * v.y_step;
                break;
            }
        }
        const float cx = x - key.x + .5f, cz = z - key.z + .5f;
        // Real trees stand on y + 1; coarse terrain may sit a little off it.
        const float base = ground + std::clamp(float(y + 1 - ground), -2.f, 2.f);
        if (shrub)
            crown.front().y0 = std::min(crown.front().y0, float(ground) - .08f - base);
        const float canopy_bottom = base + crown.front().y0;
        if (draw_trunks && !shrub) {
            const float bottom = std::min(float(ground) - .05f, canopy_bottom - 2.f);
            for (const auto &face : shape_faces(uint8_t(BlockShape::Pillar))) {
                if (face.normal[1] > 0)
                    continue;
                auto points = face.points;
                for (auto &p : points) {
                    p[0] += cx - .5f;
                    p[1] = bottom + p[1] * (canopy_bottom - bottom);
                    p[2] += cz - .5f;
                }
                quad(mesh.solid, points, r.get(wood).color, face.normal);
                Face style;
                style.signed_block = int(wood) + 1;
                style.tintable = false;
                style.sky = 15;
                style_quad(mesh.solid, style, r);
            }
        }
        // Shade the crown exactly like the leaf blocks it stands in for: the
        // leaf texture through the canopy's tint, saturation and brightness.
        // A raw tint colour came out darker and flatter than the real leaves.
        Face leaf_style;
        leaf_style.signed_block = int(leaves) + 1;
        leaf_style.tint = canopy.tint;
        leaf_style.painted = true;
        leaf_style.preserve = canopy.preserve;
        leaf_style.brightness = canopy.brightness;
        leaf_style.sky = 15;
        for (size_t b = 0; b < crown.size(); ++b) {
            const auto &box = crown[b];
            for (const auto &face : shape_faces(0)) {
                // Far crowns are only ever seen from the side or above; an
                // upper tier's underside is hidden when it rests inside the one below.
                const bool covered = b > 0 && box.x0 >= crown[b - 1].x0 && box.x1 <= crown[b - 1].x1 &&
                                     box.z0 >= crown[b - 1].z0 && box.z1 <= crown[b - 1].z1;
                if ((key.level >= 3 || covered) && face.normal[1] < 0)
                    continue;
                auto points = face.points;
                for (auto &p : points) {
                    p[0] = cx + box.x0 + p[0] * (box.x1 - box.x0);
                    p[1] = base + box.y0 + p[1] * (box.y1 - box.y0);
                    p[2] = cz + box.z0 + p[2] * (box.z1 - box.z0);
                }
                quad(mesh.solid, points, r.get(leaves).color, face.normal);
                style_quad(mesh.solid, leaf_style, r);
                for (size_t i = mesh.solid.vertices.size() - 4; i < mesh.solid.vertices.size(); ++i)
                    mesh.solid.vertices[i].decal += canopy_noise_flag;
            }
        }
    }
}

// Group lava surfaces into 8 m cells; each cell becomes one warm point light
// above the pool so caves glow around it.
static void lava_light_anchors(MeshResult &result) {
    struct Cell { double x = 0, y = 0, z = 0, area = 0; };
    std::map<std::pair<int, int>, Cell> cells;
    const auto &w = result.water.vertices;
    for (size_t i = 0; i + 3 < w.size(); i += 4) {
        if ((int(w[i].decal) & 3) != 3 || w[i].ny < .5f)
            continue;
        const float x0 = std::min({w[i].x, w[i + 1].x, w[i + 2].x, w[i + 3].x});
        const float x1 = std::max({w[i].x, w[i + 1].x, w[i + 2].x, w[i + 3].x});
        const float z0 = std::min({w[i].z, w[i + 1].z, w[i + 2].z, w[i + 3].z});
        const float z1 = std::max({w[i].z, w[i + 1].z, w[i + 2].z, w[i + 3].z});
        // Split large greedy quads so a wide lake still lights evenly.
        for (float cz = z0; cz < z1; cz += 8.f)
            for (float cx = x0; cx < x1; cx += 8.f) {
                const float ex = std::min(x1, cx + 8.f), ez = std::min(z1, cz + 8.f);
                const float area = (ex - cx) * (ez - cz);
                const float mx = (cx + ex) * .5f, mz = (cz + ez) * .5f;
                auto &c = cells[{int(std::floor(mx / 8.f)), int(std::floor(mz / 8.f))}];
                c.x += mx * area; c.z += mz * area; c.y += w[i].y * area; c.area += area;
            }
    }
    for (const auto &[key, c] : cells)
        if (c.area >= 2.0 && result.lava_lights.size() < 12)
            result.lava_lights.push_back({float(c.x / c.area), float(c.y / c.area), float(c.z / c.area),
                                          float(c.area)});
}
MeshResult build_tile(const TileKey &key, uint64_t version, const Generator &g,
                      std::shared_ptr<const EditMap> edits, std::shared_ptr<const AppearanceMap> surfaces,
                      bool include_vegetation, VerticalWindow window,
                      std::shared_ptr<const ResourceDepletions> depleted) {
    auto start = std::chrono::steady_clock::now();
    MeshResult result;
    result.key = key;
    result.version = version;
    result.window = window;
    if (g.settings().preset == WorldPreset::Structure &&
        (key.level > 0 || (edits->empty() && surfaces->empty()))) {
        result.volume_min_y = g.settings().min_y;
        if (key.level == 0) {
            result.volume = std::make_shared<Volume>();
            result.volume->min_y = result.volume_min_y;
        }
        result.milliseconds = elapsed(start);
        return result;
    }
    auto volume = make_volume(key, g, edits, surfaces, result.stages, window);
    result.volume_min_y=volume->min_y;result.volume_height=volume->height;result.voxel_bytes=volume->data.size()*sizeof(BlockId);
    auto mesh_start = std::chrono::steady_clock::now();
    const int world_floor=g.settings().min_y;
    const auto &settings = g.settings();
    const bool flat_far = settings.far_heightfield_lod > 0 && key.level >= settings.far_heightfield_lod &&
                          key.level > settings.caves.max_lod && key.level > settings.tree_lod_max &&
                          volume->shapes.empty() && volume->cave_plans.empty() && edits->empty() && surfaces->empty();
    if (flat_far)
        far_heightfield_mesh(result, *volume, g.blocks());
    else
        greedy(result,*volume,g.blocks(),key.level==0,volume->min_y==world_floor);
    cave_ceiling_shadows(result, *volume, g.blocks());
    if(key.level==0 && volume->bottom_fill)
        result.collision_rects.push_back({{0,int16_t(world_floor),0},32,32,1,false});
    const size_t partial_begin=result.solid.indices.size();
    partial_blocks(result, *volume, g.blocks());
    if(key.level==0)for(size_t i=partial_begin;i<result.solid.indices.size();i++) {
        const auto &p=result.solid.vertices[result.solid.indices[i]];
        result.partial_collision_faces.push_back({p.x,p.y,p.z});
    }
    resource_pieces(result, *volume, g, *edits, depleted.get());
    root_cards(result, *volume, g);
    if (window.mode!=VerticalWindow::underground) {
        collect_terrain_edges(result, *volume, g);
        decorations(result, *volume, g);
        if (key.level > 0)
            tree_proxies(result, *volume, g, *edits, *surfaces);
    }
    if (include_vegetation) {
        surface_vegetation(result, *volume, g);
        result.foliage_included = key.level == 0;
    }
    if (key.level == 0)
        lava_light_anchors(result);
    result.stages.mesh_ms = elapsed(mesh_start);
    if (key.level == 0) {
        volume->compress();
        result.voxel_bytes = volume->voxel_bytes();
        result.volume = std::move(volume);
    }
    result.milliseconds = elapsed(start);
    return result;
}
} // namespace altaria
