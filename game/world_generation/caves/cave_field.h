#pragma once
#include "cave_plan.h"
#include "cave_formations.h"
#include <memory>
#include <mutex>
#include <unordered_map>
namespace altaria::caves {
struct Cell {int x,y,z;bool operator==(Cell b) const {return x==b.x && y==b.y && z==b.z;}};
struct CellHash {size_t operator()(Cell c) const;};
struct Primitive {bool passage;uint32_t index;Bounds bounds;uint32_t segment=0;};
struct IndexedNetwork {
    Network network;
    std::vector<Primitive> primitives;
    std::unordered_map<Cell,std::vector<uint32_t>,CellHash> bins;
    std::vector<uint32_t> large;
    std::vector<Formation> formations;
    size_t bytes=0;
    explicit IndexedNetwork(Network plan,std::vector<Formation> formations={});
};
struct Query {
    struct Ref {const IndexedNetwork *owner;uint32_t primitive;};
    std::vector<std::shared_ptr<const IndexedNetwork>> owners;
    std::vector<Ref> primitives;
    std::unordered_map<Cell,std::vector<uint32_t>,CellHash> bins;
    std::unordered_map<Cell,std::vector<const Formation*>,CellHash> formations;
    std::unordered_map<uint64_t,std::shared_ptr<const FormationMask>> support;
};
struct CacheStats {
    size_t regions,bytes,hits,misses;
    size_t roof_regions=0,roof_bytes=0,pinned_regions=0,support_entries=0,support_bytes=0;
};
// Only get/query build cold plans; use resident() for main-thread probes and
// prewarm cold regions on native workers. No lock spans planning or carving.
class PlanCache {
    struct Entry {std::shared_ptr<const IndexedNetwork> plan;uint64_t used;};
    std::shared_ptr<const Planner> planner;
    std::function<std::vector<Formation>(const Network &)> formation_planner;
    size_t max_regions,max_bytes,bytes=0,hits=0,misses=0;uint64_t clock=0;
    std::unordered_map<Cell,Entry,CellHash> entries;
    std::unordered_map<Cell,std::weak_ptr<const IndexedNetwork>,CellHash> live;
    mutable std::mutex mutex;
public:
    PlanCache(std::shared_ptr<const Planner> planner,size_t regions=16,size_t bytes=32*1024*1024,
              std::function<std::vector<Formation>(const Network &)> formation_planner={});
    std::shared_ptr<const IndexedNetwork> get(Region region);
    std::shared_ptr<const IndexedNetwork> resident(Region region) const;
    Query query(Bounds bounds);
    CacheStats stats() const;
};
struct FieldSample {bool carved=false;uint64_t owner=0;int depth=-1;float distance=1e6f;};
float passage_distance(const Passage &passage,Point p);
// Workers may supply the immutable candidate bin for p's 32 m cell.
FieldSample sample(const Query &query,Point p,float host_roof,float floor,float roof_margin,
                   float geological_detail=0,const std::vector<uint32_t> *prepared_bin=nullptr);
enum class DripstoneSection { none, base, tip };
uint16_t sample_formation(const Query &query,Point p,bool water,bool lava,
                          int liquid_top=-8192,
                          const std::vector<uint32_t> *prepared_bin=nullptr,
                          const std::vector<const Formation*> *prepared_formations=nullptr,
                          DripstoneSection *dripstone=nullptr);
bool formation_clearance(const Query &query,Point p,const std::vector<uint32_t> *prepared_bin=nullptr);
}
