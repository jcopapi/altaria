#pragma once
#include "voxel_core.h"
namespace altaria {
// Packed ordinary forms use 0..31, ramps 128..187 and branches 188..191.
// 192..254 held the retired Terrain caps and now read as cubes.
enum class BlockShape : uint8_t { Cube, Slab, VerticalSlab, Ramp, Stairs, Roof, Pillar, MiniPillar };
constexpr int SHAPE_COUNT = 8;
uint8_t horizontal_mini_pillar(int axis);
uint8_t horizontal_pillar(int axis);
uint8_t upside_down_stairs(uint8_t packed);
uint8_t connect_building_corner(uint8_t packed, const std::function<uint8_t(int,int)> &neighbors);
uint8_t branch_fork(int axis);
// Branch joints (mini-pillar arms through a small core). Directions: 0 -x, 1 +x, 2 -z, 3 +z.
uint8_t branch_elbow_up(int direction);   // arm from `direction`, continuing upward
uint8_t branch_elbow_down(int direction); // arm from `direction`, continuing downward
uint8_t branch_tee_up(int axis);          // through arm on axis (0 x, 1 z) with an upward shoot
uint8_t branch_stub(int direction);       // trunk pillar with a single arm toward `direction`
bool valid_shape_id(int shape);
uint8_t pack_shape(int shape, int rotation = 0);
int shape_type(uint8_t packed);
int shape_rotation(uint8_t packed);
void unpack_shape(uint8_t packed, Appearance &appearance);
enum class RampScale { Full, Half, Raised };
enum class RampCorner { Straight, InnerLeft, InnerRight, OuterLeft, OuterRight };
struct RampSpec {
    bool valid=false;
    RampScale scale=RampScale::Full;
    RampCorner corner=RampCorner::Straight;
    int turn=0;
    bool inverted=false;
    float low() const { return scale==RampScale::Raised ? .5f : 0.f; }
    float rise() const { return scale==RampScale::Full ? 1.f : .5f; }
};
RampSpec ramp_spec(uint8_t packed);
uint8_t upside_down_ramp(uint8_t packed);
uint8_t ramp_shape(RampScale scale, RampCorner corner, int turn=0);
const char *ramp_corner_name(RampCorner corner);
uint8_t connect_ramp(uint8_t packed, const std::function<uint8_t(int,int)> &neighbors);
const char *shape_name(int shape);
int shape_id(const std::string &name);
struct ShapeFace {
    std::array<std::array<float, 3>, 4> points;
    std::array<float, 3> normal;
};
std::vector<ShapeFace> ramp_faces(uint8_t packed);
std::vector<std::vector<std::array<float,3>>> ramp_hulls(uint8_t packed);
float ramp_top(uint8_t packed,float x,float z);
bool ramp_intersects(uint8_t packed,std::array<float,3> lo,std::array<float,3> hi);
const std::vector<ShapeFace> &shape_faces(uint8_t packed);
std::vector<std::vector<std::array<float, 3>>> shape_hulls(uint8_t packed);
float shape_top(uint8_t packed, float x, float z);
bool shape_intersects(uint8_t packed, std::array<float, 3> minimum, std::array<float, 3> maximum);
float shape_ray(uint8_t packed, std::array<float, 3> origin, std::array<float, 3> direction, float limit,
                std::array<float, 3> *normal = nullptr);
RayHit raycast_shapes(const std::array<float, 3> &origin, const std::array<float, 3> &direction, float reach,
                      const std::function<BlockId(int, int, int)> &query,
                      const std::function<uint8_t(int, int, int)> &shapes, const Registry &registry);
} // namespace altaria
