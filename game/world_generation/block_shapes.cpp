#include "block_shapes.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace altaria {
using P = std::array<float, 3>;
static const char *names[] = {"cube",   "slab", "vertical_slab", "ramp",
                              "stairs", "roof", "pillar",        "mini_pillar"};
static constexpr int HORIZONTAL_MINI_PILLAR = 188;
static constexpr int BRANCH_FORK = 190;
// Codes 192..254 were Terrain caps (removed); old saves read them as cubes.
static constexpr int LEGACY_TERRAIN = 192;
static constexpr int SHAPE_CODE_END = 255;
static uint8_t current(uint8_t packed) { return packed >= LEGACY_TERRAIN ? 0 : packed; }
uint8_t horizontal_mini_pillar(int axis) { return HORIZONTAL_MINI_PILLAR + (axis & 1); }
uint8_t horizontal_pillar(int axis) { return 48 + (axis & 1); }
static bool corner_shape(int code) { return (code >= 32 && code < 48) || (code >= 50 && code < 58) || (code >= 98 && code < 106); }
static bool roof_corner(int code) { return (code >= 40 && code < 48) || (code >= 98 && code < 106); }
static bool sideways_pillar(int code) { return code == 48 || code == 49; }
// Four straight forms and sixteen connected corner variants.
static bool inverted_stair(int code) { return code >= 58 && code < 78; }
static uint8_t upright_stair(uint8_t code) {
    if (code < 62) return uint8_t(4 | ((code - 58) << 3));
    if (code < 70) return code - 30;
    return code - 20;
}
uint8_t upside_down_stairs(uint8_t packed) {
    if (packed < 32) return 58 + shape_rotation(packed);
    if (packed < 40) return packed + 30;
    return packed + 20;
}
uint8_t connect_building_corner(uint8_t packed, const std::function<uint8_t(int,int)> &neighbors) {
    if (inverted_stair(packed)) {
        const auto joined = connect_building_corner(upright_stair(packed), [&](int x, int z) {
            const auto neighbor = neighbors(x, z);
            return inverted_stair(neighbor) ? upright_stair(neighbor) : uint8_t(255);
        });
        return upside_down_stairs(joined);
    }
    const int type = shape_type(packed);
    if (type != int(BlockShape::Stairs) && type != int(BlockShape::Roof))
        return packed;
    const int turn = shape_rotation(packed);
    constexpr int x[] = {1, 0, -1, 0}, z[] = {0, 1, 0, -1};
    for (int side : {1, -1}) {
        const auto neighbor = neighbors(side * x[turn], side * z[turn]);
        if (inverted_stair(neighbor) || shape_type(neighbor) != type)
            continue;
        const int difference = (shape_rotation(neighbor) - turn + 4) % 4;
        if (difference == 1 || difference == 3) {
            if (type == int(BlockShape::Roof))
                return (difference == 3 ? 98 : 40) + turn + (side < 0 ? 4 : 0);
            return 32 + turn + (side < 0 ? 4 : 0) + (difference == 3 ? 18 : 0);
        }
    }
    return packed;
}
uint8_t branch_fork(int axis) { return BRANCH_FORK + (axis & 1); }
static constexpr int BRANCH_JOINT = 113, BRANCH_JOINT_END = 127;
uint8_t branch_elbow_up(int direction) { return uint8_t(BRANCH_JOINT + (direction & 3)); }
uint8_t branch_tee_up(int axis) { return uint8_t(BRANCH_JOINT + 4 + (axis & 1)); }
uint8_t branch_stub(int direction) { return uint8_t(BRANCH_JOINT + 6 + (direction & 3)); }
uint8_t branch_elbow_down(int direction) { return uint8_t(BRANCH_JOINT + 10 + (direction & 3)); }
static bool joint_shape(int code) { return code >= BRANCH_JOINT && code < BRANCH_JOINT_END; }
static bool joint_trunk(int code) { return code >= BRANCH_JOINT + 6 && code < BRANCH_JOINT + 10; }
// Arms as bits: 0 -x, 1 +x, 2 -y, 3 +y, 4 -z, 5 +z.
static int joint_arms(int code) {
    static constexpr int horizontal[4] = {1, 2, 16, 32};
    const int k = code - BRANCH_JOINT;
    if (k < 4) return horizontal[k] | 8;
    if (k < 6) return (k == 4 ? 3 : 48) | 8;
    if (k < 10) return horizontal[k - 6];
    return horizontal[k - 10] | 4;
}
static bool branch_shape(int code) {
    return (code >= HORIZONTAL_MINI_PILLAR && code < LEGACY_TERRAIN) || joint_shape(code);
}

bool valid_shape_id(int shape) {
    // 8 and 192..254 are retired Terrain ids, still accepted so old saves load.
    return (shape>=0 && shape<=SHAPE_COUNT) || (shape>=32 && shape<=112) || joint_shape(shape) ||
           (shape>=128 && shape<SHAPE_CODE_END);
}
int shape_type(uint8_t packed) {
    packed = current(packed);
    if (inverted_stair(packed))
        return int(BlockShape::Stairs);
    if (corner_shape(packed))
        return int(roof_corner(packed) ? BlockShape::Roof : BlockShape::Stairs);
    if (sideways_pillar(packed))
        return int(BlockShape::Pillar);
    if (joint_shape(packed))
        return int(joint_trunk(packed) ? BlockShape::Pillar : BlockShape::MiniPillar);
    if (branch_shape(packed))
        return int(packed < BRANCH_FORK ? BlockShape::MiniPillar : BlockShape::Pillar);
    return ramp_spec(packed).valid ? 3 : packed<32 ? packed&7 : 0;
}
int shape_rotation(uint8_t packed) {
    packed = current(packed);
    if (inverted_stair(packed))
        return shape_rotation(upright_stair(packed));
    if (corner_shape(packed))
        return (packed - (packed >= 98 ? 98 : packed >= 50 ? 50 : 32)) & 3;
    const auto ramp = ramp_spec(packed);
    return ramp.valid ? ramp.turn : packed<32 ? (packed>>3)&3 : 0;
}
uint8_t pack_shape(int shape,int rotation) {
    if (shape >= 78 && shape < 98)
        return uint8_t(shape - (shape - 78) % 4 + ((shape - 78 + rotation) & 3));
    if (inverted_stair(shape))
        return upside_down_stairs(pack_shape(shape < 62 ? 4 : upright_stair(shape),
                                            rotation + (shape < 62 ? shape_rotation(shape) : 0)));
    if (corner_shape(shape))
        return uint8_t(shape - shape_rotation(shape) + ((shape_rotation(shape) + rotation) & 3));
    if (sideways_pillar(shape))
        return uint8_t(shape ^ (rotation & 1));
    if (shape == SHAPE_COUNT || shape >= LEGACY_TERRAIN)
        return 0;
    if (joint_shape(shape))
        return uint8_t(shape);
    if (branch_shape(shape))
        return uint8_t(shape ^ (rotation & 1));
    if(shape>=128 && shape<188)return uint8_t(128+(shape-128)/4*4+((shape-128+rotation)&3));
    return shape>=0 && shape<8 ? uint8_t(shape|((rotation&3)<<3)) : 0;
}
void unpack_shape(uint8_t packed,Appearance &a) {
    packed = current(packed);
    if (corner_shape(packed) || sideways_pillar(packed) || inverted_stair(packed) || ramp_spec(packed).inverted) {
        a.shape = packed;
        a.rotation = 0;
        return;
    }
    a.shape=branch_shape(packed) ? packed :
        packed>=128 && packed<188 ? uint8_t(128+(packed-128)/4*4) : shape_type(packed);
    a.rotation=shape_rotation(packed);
}
const char *shape_name(int shape) {
    if (corner_shape(shape) || sideways_pillar(shape) || inverted_stair(shape))
        return names[shape_type(uint8_t(shape))];
    if (branch_shape(shape))
        return names[shape_type(uint8_t(shape))];
    return (shape >= 78 && shape < 98) || (shape>=128 && shape<188) ? "ramp" : shape>=0 && shape<SHAPE_COUNT ? names[shape] : "cube";
}
int shape_id(const std::string &s) {for(int i=0;i<SHAPE_COUNT;i++)if(s==names[i])return i;return -1;}
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

template<class Emit>
static void branch_boxes(uint8_t packed, const Emit &emit) {
    if (joint_shape(packed)) {
        // Non-overlapping boxes so shared inner faces cull exactly.
        constexpr float lo = .375f, hi = .625f;
        const int arms = joint_arms(packed);
        const bool trunk = joint_trunk(packed);
        if (trunk)
            emit(P{.25f, 0, .25f}, P{.75f, 1, .75f});
        else
            emit(P{lo, lo, lo}, P{hi, hi, hi});
        const float inner_lo = trunk ? .25f : lo, inner_hi = trunk ? .75f : hi;
        for (int bit = 0; bit < 6; ++bit) {
            if (!(arms & (1 << bit)))
                continue;
            const int axis = bit / 2;
            P a{lo, lo, lo}, b{hi, hi, hi};
            if (bit & 1) { a[axis] = inner_hi; b[axis] = 1.f; }
            else { a[axis] = 0.f; b[axis] = inner_lo; }
            emit(a, b);
        }
        return;
    }
    if (packed >= BRANCH_FORK)
        emit(P{.25f, 0, .25f}, P{.75f, 1, .75f});
    P lo{.375f, .375f, .375f}, hi{.625f, .625f, .625f};
    const int axis = (packed & 1) ? 2 : 0;
    lo[axis] = 0;
    hi[axis] = 1;
    emit(lo, hi);
}
static bool inner_corner(uint8_t packed) {
    return ((packed - (packed >= 98 ? 98 : packed >= 50 ? 50 : 32)) & 4) != 0;
}
static float corner_height(uint8_t packed, float x, float z) {
    if (roof_corner(packed)) {
        const float across = packed >= 98 ? 1.f - z : z;
        const float rise = inner_corner(packed) ? std::max(x, across) : std::min(x, across);
        return .125f + .875f * rise;
    }
    const bool high_x = x >= .5f;
    const bool high_z = packed >= 50 ? z < .5f : z >= .5f;
    return (inner_corner(packed) ? high_x || high_z : high_x && high_z) ? 1.f : .5f;
}
static std::vector<ShapeFace> make_shape_faces(uint8_t packed) {
    std::vector<ShapeFace> f;
    if (inverted_stair(packed)) {
        f = make_shape_faces(upright_stair(packed));
        for (auto &face : f) {
            for (auto &point : face.points)
                point[1] = 1.f - point[1];
            std::swap(face.points[1], face.points[3]);
            face.normal[1] = -face.normal[1];
        }
        return f;
    }
    if(ramp_spec(packed).valid) return ramp_faces(packed);
    if (sideways_pillar(packed)) {
        if (packed == horizontal_pillar(0))
            box(f, 0, .25f, .25f, 1, .75f, .75f);
        else
            box(f, .25f, .25f, 0, .75f, .75f, 1);
        return f;
    }
    if (branch_shape(packed)) {
        branch_boxes(packed, [&](P lo, P hi) { box(f, lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]); });
        return f;
    }
    int shape = packed & 7;
    if (corner_shape(packed)) {
        if (roof_corner(packed)) {
            for (int z = 0; z < 2; ++z)
                for (int x = 0; x < 2; ++x) {
                    const float left = x * .5f, front = z * .5f;
                    P corners[] = {{left, 0, front}, {left, 0, front + .5f},
                                   {left + .5f, 0, front + .5f}, {left + .5f, 0, front}};
                    for (auto &point : corners)
                        point[1] = corner_height(packed, point[0], point[2]);
                    const float middle = corner_height(packed, left + .25f, front + .25f);
                    const int start = std::abs((corners[0][1] + corners[2][1]) * .5f - middle) < .001f ? 0 : 1;
                    for (int triangle = 0; triangle < 2; ++triangle) {
                        P top[] = {corners[start], corners[(start + triangle + 1) % 4],
                                   corners[(start + triangle + 2) % 4]};
                        P bottom[] = {top[0], top[1], top[2]};
                        for (auto &point : bottom)
                            point[1] -= .125f;
                        auto n = cross(sub(top[1], top[0]), sub(top[2], top[0]));
                        const float length = std::sqrt(dot(n, n));
                        for (auto &component : n)
                            component /= length;
                        f.push_back({{top[0], top[1], top[2], top[2]}, n});
                        f.push_back({{bottom[0], bottom[2], bottom[1], bottom[1]}, {0, -1, 0}});
                        for (int edge = 0; edge < 3; ++edge) {
                            const int next = (edge + 1) % 3;
                            const auto d = sub(bottom[next], bottom[edge]);
                            const float width = std::sqrt(dot(d, d));
                            f.push_back({{bottom[edge], bottom[next], top[next], top[edge]},
                                         {-d[2] / width, 0, d[0] / width}});
                        }
                        // Convex hull extraction groups faces in sets of six.
                        f.push_back({{bottom[0], bottom[0], bottom[0], bottom[0]}, {0, -1, 0}});
                    }
                }
        } else {
            for (int z = 0; z < 2; ++z)
                for (int x = 0; x < 2; ++x)
                    box(f, x * .5f, 0, z * .5f, (x + 1) * .5f,
                        corner_height(packed, x * .5f + .25f, z * .5f + .25f), (z + 1) * .5f);
        }
    } else if (shape == 1)
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
    } else if (shape == 5) {
        // A single sloped tile, with one eighth of a block of thickness.
        box(f, 0, 0, 0, 1, .125f, 1);
        for (auto &face : f)
            for (auto &point : face.points)
                point[1] += .875f * point[0];
        const float normal_y = 1.f / std::sqrt(1.f + .875f * .875f);
        f[2].normal = {.875f * normal_y, -normal_y, 0};
        f[3].normal = {-.875f * normal_y, normal_y, 0};
    } else
        box(f, 0, 0, 0, 1, 1, 1);
    int turn = shape_rotation(packed);
    for (auto &face : f) {
        for (auto &p : face.points)
            p = rotate(p, turn);
        face.normal = rotate(face.normal, turn, true);
    }
    return f;
}
const std::vector<ShapeFace> &shape_faces(uint8_t packed) {
    static const std::array<std::vector<ShapeFace>, SHAPE_CODE_END> cache = []() {
        std::array<std::vector<ShapeFace>, SHAPE_CODE_END> result;
        for (int i = 0; i < SHAPE_CODE_END; i++)
            result[i] = make_shape_faces(uint8_t(i));
        return result;
    }();
    return cache[current(packed)];
}
std::vector<std::vector<P>> shape_hulls(uint8_t packed) {
    packed = current(packed);
    if(ramp_spec(packed).valid) return ramp_hulls(packed);
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
    packed = current(packed);
    if (inverted_stair(packed))
        return 1.f;
    if(ramp_spec(packed).valid) return ramp_top(packed,x,z);
    if (branch_shape(packed)) {
        float top = 0;
        branch_boxes(packed, [&](P lo, P hi) {
            if (x >= lo[0] && x <= hi[0] && z >= lo[2] && z <= hi[2])
                top = std::max(top, hi[1]);
        });
        return top;
    }
    if (sideways_pillar(packed))
        return std::abs((packed == horizontal_pillar(0) ? z : x) - .5f) <= .25f ? .75f : 0.f;
    P p = rotate({x, 0, z}, (4 - shape_rotation(packed)) & 3);
    x = p[0];
    z = p[2];
    if (corner_shape(packed))
        return corner_height(packed, x, z);
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
        return .125f + .875f * x;
    case 6:
        return x >= .25f && x <= .75f && z >= .25f && z <= .75f ? 1 : 0;
    case 7:
        return x >= .375f && x <= .625f && z >= .375f && z <= .625f ? 1 : 0;
    default:
        return 1;
    }
}
bool shape_intersects(uint8_t packed, P lo, P hi) {
    packed = current(packed);
    if (inverted_stair(packed)) {
        const float bottom = 1.f - hi[1];
        hi[1] = 1.f - lo[1];
        lo[1] = bottom;
        return shape_intersects(upright_stair(packed), lo, hi);
    }
    if(ramp_spec(packed).valid) return ramp_intersects(packed,lo,hi);
    if (branch_shape(packed)) {
        bool hit = false;
        branch_boxes(packed, [&](P a, P b) {
            hit |= lo[0] < b[0] && hi[0] > a[0] && lo[1] < b[1] && hi[1] > a[1] && lo[2] < b[2] && hi[2] > a[2];
        });
        return hit;
    }
    // Transform the AABB back into canonical block space (quarter turns preserve AABBs).
    P a = rotate(lo, (4 - shape_rotation(packed)) & 3), b = rotate(hi, (4 - shape_rotation(packed)) & 3);
    for (int i = 0; i < 3; i++) {
        lo[i] = std::max(0.f, std::min(a[i], b[i]));
        hi[i] = std::min(1.f, std::max(a[i], b[i]));
        if (lo[i] >= hi[i])
            return false;
    }
    int s = packed & 7;
    float top = 1;
    if (sideways_pillar(packed)) {
        const int cross_axis = packed == horizontal_pillar(0) ? 2 : 0;
        return lo[cross_axis] < .75f && hi[cross_axis] > .25f && lo[1] < .75f && hi[1] > .25f;
    }
    if (roof_corner(packed) || s == 5) {
        const auto height = [&](float x, float z) {
            return roof_corner(packed) ? corner_height(packed, x, z) : .125f + .875f * x;
        };
        const bool reversed = roof_corner(packed) && packed >= 98;
        const float low = height(lo[0], reversed ? hi[2] : lo[2]) - .125f;
        const float high = height(hi[0], reversed ? lo[2] : hi[2]);
        return lo[1] < high - .0001f && hi[1] > low + .0001f;
    }
    if (corner_shape(packed))
        top = corner_height(packed, hi[0] - .0001f, packed >= 50 ? lo[2] : hi[2] - .0001f);
    else if (s == 1)
        top = .5f;
    else if (s == 2 && lo[0] >= .5f)
        return false;
    else if (s == 3)
        top = hi[0];
    else if (s == 4)
        top = hi[0] > .5f ? 1 : .5f;
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
        const auto &def = r.get(id);
        if (def.solid && !def.liquid && !def.water && !def.lava) {
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
