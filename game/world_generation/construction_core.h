#pragma once
#include "block_shapes.h"
#include <unordered_set>
namespace altaria {
using Point = std::array<float, 3>;
struct BuiltBlock {
    uint64_t id = 0;
    IVec3 origin8;
    BlockId block = 0;
    Appearance appearance;
    uint8_t connected_shape=255;
    Point position() const {
        return {origin8.x / 8.f, origin8.y / 8.f, origin8.z / 8.f};
    }
    uint8_t shape() const {
        return connected_shape==255 ? pack_shape(appearance.shape, appearance.rotation) : connected_shape;
    }
};
std::pair<Point, Point> shape_bounds(uint8_t shape);
bool shapes_overlap(uint8_t a, Point origin_a, uint8_t b, Point origin_b);
IVec3 snap_placement(Point hit, Point normal, uint8_t shape, Point preferred);
class ConstructionStore {
    std::unordered_map<IVec3, std::vector<uint64_t>, CoordHash> cells;
    void index(const BuiltBlock &block, bool remove);

  public:
    std::function<std::pair<BlockId,uint8_t>(int,int,int)> terrain_lookup;
    std::unordered_set<uint64_t> shape_changes;
    std::unordered_map<uint64_t, BuiltBlock> blocks;
    uint64_t next_id = 1, revision = 0;
    std::vector<uint64_t> nearby(Point minimum, Point maximum) const;
    void put(BuiltBlock block);
    void erase(uint64_t id);
    uint8_t connection_shape(const BuiltBlock &block,const BuiltBlock *preview=nullptr) const;
    uint8_t neighbor_ramp(int x8,int z8,float plane,uint64_t exclude=0,const BuiltBlock *preview=nullptr) const;
    void refresh_connections(Point position);
    bool collides(Point minimum, Point maximum, const Registry &registry) const;
    bool can_place(const BuiltBlock &block, const Registry &registry, const Settings &settings,
                   const std::function<std::pair<BlockId, uint8_t>(int, int, int)> &terrain,
                   std::string &error) const;
};
} // namespace altaria
