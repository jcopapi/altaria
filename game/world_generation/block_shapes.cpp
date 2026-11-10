#include "block_shapes.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace altaria {
using P = std::array<float, 3>;
static const char *names[] = {"cube",   "slab", "vertical_slab", "ramp",
                              "stairs", "roof", "pillar",        "mini_pillar", "terrain"};
bool valid_shape_id(int shape) {
    return (shape >= 0 && shape <= 8) || (shape >= 32 && shape <= 112) || (shape >= 128 && shape < 188);
}
int shape_type(uint8_t packed) {
    if (packed >= 128 && packed < 188)
        return 3;
    if ((packed >= 32 && packed <= 112) || packed == 8)
        return 8;
    return packed & 7;
}
int shape_rotation(uint8_t packed) { return packed >= 128 ? (packed-128)%4 : packed >= 32 ? 0 : (packed >> 3) & 3; }
uint8_t terrain_shape(std::array<int, 4> heights) {
    int code = 0, power = 1;
    for (int h : heights) { code += std::clamp(h, 0, 2) * power; power *= 3; }
    return uint8_t(32 + code);
}
std::array<float, 4> terrain_heights(uint8_t packed) {
    if (packed == 8)
        return {0.f, .5f, .5f, 0.f};
    int code = std::clamp(int(packed) - 32, 0, 80);
    std::array<float, 4> h;
    for (auto &v : h) { v = (code % 3) * .5f; code /= 3; }
    return h;
}
TerrainSpec terrain_spec(uint8_t packed) {
    if (packed == 8)
        return {true, {0, 1, 1, 0}};
    if (packed < 32 || packed > 112)
        return {};
    TerrainSpec spec{true, {}};
    int code = int(packed) - 32;
    for (int &v : spec.h) {
        v = code % 3;
        code /= 3;
    }
    return spec;
}
uint8_t connect_terrain(const std::function<bool(int, int, int)> &solid) {
    if (solid(0, 1, 0))
        return terrain_shape({2, 2, 2, 2});
    std::array<int, 4> h{};
    const int sx_table[4] = {-1, -1, 1, 1};
    const int sz_table[4] = {-1, 1, 1, -1};
    for (int c = 0; c < 4; c++) {
        int sx = sx_table[c], sz = sz_table[c];
        bool up_x = solid(sx, 1, 0), up_z = solid(0, 1, sz), up_d = solid(sx, 1, sz);
        bool same_x = solid(sx, 0, 0), same_z = solid(0, 0, sz), same_d = solid(sx, 0, sz);
        bool opp_x = solid(-sx, 0, 0) || solid(-sx, 1, 0);
        bool opp_z = solid(0, 0, -sz) || solid(0, 1, -sz);
        bool down_x = !same_x && !up_x && solid(sx, -1, 0);
        bool down_z = !same_z && !up_z && solid(0, -1, sz);
        bool down_d = !same_d && !up_d && solid(sx, -1, sz);
        if (up_x || up_z || up_d || (same_x && same_z && same_d)) {
            h[c] = 2;
        } else if (same_x && same_z) {
            h[c] = (down_d || solid(-sx, 1, 0) || solid(0, 1, -sz)) ? 0 : 1;
        } else if (same_x && !same_z) {
            h[c] = ((down_z && opp_z) || solid(0, 1, -sz) || solid(sx, 1, -sz)) ? 0 : 1;
        } else if (!same_x && same_z) {
            h[c] = ((down_x && opp_x) || solid(-sx, 1, 0) || solid(-sx, 1, sz)) ? 0 : 1;
        } else {
            h[c] = 0;
        }
    }
    if (h[0] == 0 && h[1] == 0 && h[2] == 0 && h[3] == 0)
        return terrain_shape({1, 1, 1, 1});
    return terrain_shape(h);
}
uint8_t pack_shape(int shape, int rotation) {
    if (shape == 255) return 0;
    if(shape>=128 && shape<188) return uint8_t(128+(shape-128)/4*4+((shape-128+rotation)&3));
    if (shape < 8) return uint8_t(shape | ((rotation & 3) << 3));
    auto h = shape == 8 ? std::array<float,4>{0,.5f,.5f,0} : terrain_heights(uint8_t(shape));
    std::array<int,4> rotated;
    for (int i=0;i<4;i++) rotated[(i + 4 - (rotation & 3)) % 4] = int(std::round(h[i]*2));
    return terrain_shape(rotated);
}
void unpack_shape(uint8_t packed, Appearance &a) {
    a.shape=packed>=128 ? uint8_t(128+(packed-128)/4*4) : packed>=32 ? packed : shape_type(packed);
    a.rotation=shape_rotation(packed);
}
const char *shape_name(int shape) {
    if (shape >= 128 && shape < 188)
        return "ramp";
    if (shape == 8 || (shape >= 32 && shape <= 112))
        return "terrain";
    return names[shape & 7];
}
int shape_id(const std::string &s) {
    for (int i = 0; i < SHAPE_COUNT; i++)
        if (s == names[i])
            return i;
    return -1;
}
static P rotate(P p, int turn, bool normal = false) {
    for (int i = 0; i < turn; i++) {
        float x = p[0];
        p[0] = normal ? -p[2] : 1 - p[2];
        p[2] = x;
    }
    return p;
}
static P sub(P a, P b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
static P cross(P a, P b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
static float dot(P a, P b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
static void box(std::vector<ShapeFace> &f, float x0, float y0, float z0, float x1, float y1, float z1) {
    f.push_back({{{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}}, {-1, 0, 0}});
    f.push_back({{{{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}}, {1, 0, 0}});
    f.push_back({{{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}, {0, -1, 0}});
    f.push_back({{{{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}}}, {0, 1, 0}});
    f.push_back({{{{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}}}, {0, 0, -1}});
    f.push_back({{{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}}, {0, 0, 1}});
}
static bool terrain_split_bd(const std::array<float, 4> &h) {
    return std::abs(h[0] - h[2]) > std::abs(h[1] - h[3]);
}
static std::vector<ShapeFace> make_shape_faces(uint8_t packed) {
    std::vector<ShapeFace> f;
    if(packed>=128 && packed<188) return ramp_faces(packed);
    if ((packed >= 32 && packed <= 112) || packed == 8) {
        auto h = terrain_heights(packed);
        box(f,0,0,0,1,1,1);
        for(auto &face:f) for(auto &p:face.points) if(p[1]>.5f) {
            int corner=p[0]<.5f ? (p[2]<.5f?0:1) : (p[2]<.5f?3:2);
            p[1]=h[corner];
        }
        P a{0,h[0],0}, b{0,h[1],1}, c{1,h[2],1}, d{1,h[3],0};
        auto triangle=[&](P p,P q,P r) {
            P n=cross(sub(q,p),sub(r,p)); float length=std::sqrt(dot(n,n));
            for(auto &v:n) v/=std::max(.00001f,length);
            return ShapeFace{{p,q,r,r},n};
        };
        if (terrain_split_bd(h)) {
            f[3] = triangle(a, b, d);
            f.push_back(triangle(b, c, d));
        } else {
            f[3] = triangle(a, b, c);
            f.push_back(triangle(a, c, d));
        }
        return f;
    }
    int shape = packed & 7;
    if (shape == 1)
        box(f, 0, 0, 0, 1, .5f, 1);
    else if (shape == 2)
        box(f, 0, 0, 0, .5f, 1, 1);
    else if (shape == 6)
        box(f, .25f, 0, .25f, .75f, 1, .75f);
    else if (shape == 7)
        box(f, .375f, 0, .375f, .625f, 1, .625f);
    else if (shape == 4) {
        box(f, 0, 0, 0, .5f, .5f, 1);
        box(f, .5f, 0, 0, 1, 1, 1);
    } else if (shape == 3 || shape == 5) {
        // A wedge or two half-width wedges, with exact sloped caps.
        int count = shape == 5 ? 2 : 1;
        for (int k = 0; k < count; k++) {
            float x0 = k * .5f, x1 = shape == 5 ? x0 + .5f : 1.f;
            float h0 = shape == 3 ? 0.f : (k == 0 ? .5f : 1.f), h1 = shape == 3 ? 1.f : (k == 0 ? 1.f : .5f);
            box(f, x0, 0, 0, x1, 1, 1);
            size_t start = f.size() - 6;
            for (size_t i = start; i < f.size(); i++)
                for (auto &p : f[i].points)
                    if (p[1] > .5f)
                        p[1] = std::abs(p[0] - x0) < .001f ? h0 : h1;
            P n = {-(h1 - h0) / (x1 - x0), 1, 0};
            float len = std::sqrt(dot(n, n));
            for (auto &a : n)
                a /= len;
            f[start + 3].normal = n;
        }
    } else
        box(f, 0, 0, 0, 1, 1, 1);
    int turn = (packed >> 3) & 3;
    for (auto &face : f) {
        for (auto &p : face.points)
            p = rotate(p, turn);
        face.normal = rotate(face.normal, turn, true);
    }
    return f;
}
const std::vector<ShapeFace> &shape_faces(uint8_t packed) {
    static const std::array<std::vector<ShapeFace>, 188> cache = []() {
        std::array<std::vector<ShapeFace>, 188> result;
        for (int i = 0; i < 188; i++)
            result[i] = make_shape_faces(uint8_t(i));
        return result;
    }();
    return cache[packed < 188 ? packed : 0];
}
std::vector<std::vector<P>> shape_hulls(uint8_t packed) {
    if(packed>=128 && packed<188) return ramp_hulls(packed);
    if((packed>=32 && packed<=112) || packed == 8) {
        auto h=terrain_heights(packed);
        P points[4]={{0,h[0],0},{0,h[1],1},{1,h[2],1},{1,h[3],0}};
        std::vector<std::vector<P>> hulls;
        auto tris = terrain_split_bd(h)
                        ? std::array<std::array<int, 3>, 2>{{{0, 1, 3}, {1, 2, 3}}}
                        : std::array<std::array<int, 3>, 2>{{{0, 1, 2}, {0, 2, 3}}};
        for(auto indices: tris) {
            std::vector<P> hull; float volume=0;
            for(int i:indices) { hull.push_back(points[i]); hull.push_back({points[i][0],0,points[i][2]}); volume+=h[i]; }
            if(volume>.0001f) hulls.push_back(std::move(hull));
        }
        return hulls;
    }
    const auto &faces = shape_faces(packed);
    std::vector<std::vector<P>> out;
    for (size_t i = 0; i < faces.size(); i += 6) {
        std::vector<P> vertices;
        for (size_t j = i; j < std::min(i + 6, faces.size()); j++)
            for (auto &p : faces[j].points) {
                bool exists = false;
                for (auto &v : vertices)
                    if (v == p)
                        exists = true;
                if (!exists)
                    vertices.push_back(p);
            }
        out.push_back(vertices);
    }
    return out;
}
float shape_top(uint8_t packed, float x, float z) {
    if(packed>=128 && packed<188) return ramp_top(packed,x,z);
    if((packed>=32 && packed<=112) || packed == 8) {
        auto h=terrain_heights(packed);
        if (terrain_split_bd(h)) {
            return x + z <= 1.f
                       ? h[0] * (1.f - x - z) + h[1] * z + h[3] * x
                       : h[1] * (1.f - x) + h[2] * (x + z - 1.f) + h[3] * (1.f - z);
        }
        return z>=x ? h[0]*(1-z)+h[1]*(z-x)+h[2]*x
                    : h[0]*(1-x)+h[2]*z+h[3]*(x-z);
    }
    P p = rotate({x, 0, z}, (4 - ((packed >> 3) & 3)) & 3);
    x = p[0];
    z = p[2];
    switch (packed & 7) {
    case 1:
        return .5f;
    case 2:
        return x <= .5f ? 1.f : 0.f;
    case 3:
        return x;
    case 4:
        return x < .5f ? .5f : 1.f;
    case 5:
        return 1 - std::abs(x - .5f);
    case 6:
        return x >= .25f && x <= .75f && z >= .25f && z <= .75f ? 1 : 0;
    case 7:
        return x >= .375f && x <= .625f && z >= .375f && z <= .625f ? 1 : 0;
    default:
        return 1;
    }
}
bool shape_intersects(uint8_t packed, P lo, P hi) {
    if(packed>=128 && packed<188) return ramp_intersects(packed,lo,hi);
    if((packed>=32 && packed<=112) || packed == 8) {
        for(int i=0;i<3;i++) { lo[i]=std::max(lo[i],0.f); hi[i]=std::min(hi[i],1.f); if(lo[i]>=hi[i])return false; }
        float top=0;
        for(float x:{lo[0],hi[0]}) for(float z:{lo[2],hi[2]}) top=std::max(top,shape_top(packed,x,z));
        auto h = terrain_heights(packed);
        if (terrain_split_bd(h)) {
            float low = std::max(lo[0], 1.f - hi[2]), high = std::min(hi[0], 1.f - lo[2]);
            if (low <= high) {
                top = std::max(top, shape_top(packed, low, 1.f - low));
                top = std::max(top, shape_top(packed, high, 1.f - high));
            }
        } else {
            float low=std::max(lo[0],lo[2]), high=std::min(hi[0],hi[2]);
            if(low<=high) { top=std::max(top,shape_top(packed,low,low)); top=std::max(top,shape_top(packed,high,high)); }
        }
        return lo[1]<top-.0001f;
    }
    // Transform the AABB back into canonical block space (quarter turns preserve AABBs).
    P a = rotate(lo, (4 - ((packed >> 3) & 3)) & 3), b = rotate(hi, (4 - ((packed >> 3) & 3)) & 3);
    for (int i = 0; i < 3; i++) {
        lo[i] = std::max(0.f, std::min(a[i], b[i]));
        hi[i] = std::min(1.f, std::max(a[i], b[i]));
        if (lo[i] >= hi[i])
            return false;
    }
    int s = packed & 7;
    float top = 1;
    if (s == 1)
        top = .5f;
    else if (s == 2 && lo[0] >= .5f)
        return false;
    else if (s == 3)
        top = hi[0];
    else if (s == 4)
        top = hi[0] > .5f ? 1 : .5f;
    else if (s == 5)
        top = 1 - std::abs(std::clamp(.5f, lo[0], hi[0]) - .5f);
    else if (s == 6 || s == 7) {
        float inset = s == 6 ? .25f : .375f;
        if (hi[0] <= inset || lo[0] >= 1 - inset || hi[2] <= inset || lo[2] >= 1 - inset)
            return false;
    }
    return lo[1] < top - .0001f;
}
float shape_ray(uint8_t packed, P origin, P direction, float limit, P *normal) {
    float best = limit + 1;
    const auto &faces = shape_faces(packed);
    for (auto &face : faces)
        for (int t = 0; t < 2; t++) {
            P a = face.points[0], b = face.points[t ? 2 : 1], c = face.points[t ? 3 : 2];
            P e1 = sub(b, a), e2 = sub(c, a), h = cross(direction, e2);
            float det = dot(e1, h);
            if (std::abs(det) < 1e-7f)
                continue;
            float inv = 1 / det;
            P delta = sub(origin, a);
            float u = dot(delta, h) * inv;
            if (u < 0 || u > 1)
                continue;
            P q = cross(delta, e1);
            float v = dot(direction, q) * inv;
            if (v < 0 || u + v > 1)
                continue;
            float d = dot(e2, q) * inv;
            if (d >= -1e-5f && d < best) {
                best = std::max(0.f, d);
                if (normal)
                    *normal = face.normal;
            }
        }
    return best <= limit ? best : -1;
}
RayHit raycast_shapes(const P &origin, const P &dir, float reach,
                      const std::function<BlockId(int, int, int)> &query,
                      const std::function<uint8_t(int, int, int)> &shapes, const Registry &r) {
    // DDA restricts exact polygon tests to crossed cells, avoiding fixed-step ray misses.
    IVec3 cell{int(std::floor(origin[0])), int(std::floor(origin[1])), int(std::floor(origin[2]))};
    float next[3], delta[3];
    int step[3];
    int *p[] = {&cell.x, &cell.y, &cell.z};
    for (int i = 0; i < 3; i++) {
        step[i] = dir[i] >= 0 ? 1 : -1;
        delta[i] = std::abs(dir[i]) < 1e-9f ? 1e30f : std::abs(1 / dir[i]);
        next[i] = std::abs(dir[i]) < 1e-9f ? 1e30f : ((*p[i] + (step[i] > 0 ? 1 : 0)) - origin[i]) / dir[i];
    }
    float traversed = 0;
    for (int n = 0; n < 16384 && traversed <= reach; n++) {
        auto id = query(cell.x, cell.y, cell.z);
        if (r.get(id).solid || r.get(id).water) {
            P normal;
            float hit =
                shape_ray(shapes(cell.x, cell.y, cell.z),
                          {origin[0] - cell.x, origin[1] - cell.y, origin[2] - cell.z}, dir, reach, &normal);
            if (hit >= traversed - .001f) {
                IVec3 previous = cell;
                int axis = 0;
                for (int i = 1; i < 3; i++)
                    if (std::abs(normal[i]) > std::abs(normal[axis]))
                        axis = i;
                if (axis == 0)
                    previous.x += normal[0] > 0 ? 1 : -1;
                else if (axis == 1)
                    previous.y += normal[1] > 0 ? 1 : -1;
                else
                    previous.z += normal[2] > 0 ? 1 : -1;
                return {true, cell, previous, hit, id};
            }
        }
        int axis = next[0] < next[1] ? (next[0] < next[2] ? 0 : 2) : (next[1] < next[2] ? 1 : 2);
        traversed = next[axis];
        *p[axis] += step[axis];
        next[axis] += delta[axis];
    }
    return {};
}
} // namespace altaria
