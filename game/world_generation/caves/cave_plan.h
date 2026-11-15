#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace altaria::caves {
inline constexpr int max_vertical_span = 1536;
using Point=std::array<float,3>;
struct Region {int x=0,z=0;bool operator==(Region b) const {return x==b.x && z==b.z;}};
struct Chamber {
    uint64_t id=0;Point center{},radius{};int depth=0;bool isolated=false;
    std::string archetype;bool invalid_surface=false,rugged=false;
};
struct Passage {
    uint64_t id=0,a=0,b=0;Point from{},control{},to{};float radius=0;bool vertical=false,entrance=false,invalid_surface=false;
    std::vector<Point> path;
    bool dead_end=false;
};
enum class EntranceKind { shaft, mouth, ravine };
struct Terminal {uint64_t id;Point position;EntranceKind kind=EntranceKind::shaft;};
struct Junction {uint64_t id;Point position;};
const char *entrance_kind_name(EntranceKind kind);
struct Network {
    bool valid=false;Region region;std::vector<Chamber> chambers,external;
    std::vector<Passage> passages;std::vector<Terminal> entrances;
    std::vector<Junction> junctions;
};
struct Archetype {
    std::string id;int min_depth=0,max_depth=3;
    Point radius_min{9,5,8},radius_max{25,13,23};float weight=1;
};
std::vector<Archetype> default_archetypes();
struct PlanSettings {
    int region_size=256;
    int basin_regions=0; // 0 keeps the unpartitioned graph for dry worlds.
    float floor=-512,ceiling=1024,roof_margin=10,entrance_chance=.45f,isolated_chance=.18f,infernal_chance=.45f;
    std::array<float,4> depths{24,90,190,340};
    std::vector<Archetype> archetypes=default_archetypes();
    std::string landmark_archetype;
    float flooded_basin_chance=1.f;
    float branch_chance=0.f;
    bool natural_entrances=false;
};
struct Bounds {Point lo,hi;bool intersects(Bounds b) const;};
Bounds bounds(const Chamber &c);
Bounds bounds(const Passage &p);
Region basin_of(Region region,int span);
class Planner {
    uint64_t seed,dimension;
    bool valid_config=true;
    PlanSettings settings;
    std::function<float(float,float)> surface,opening_surface;
    std::function<bool(const Passage&)> wet_opening;
    enum class EntranceResult { added, unavailable, invalid };
    uint64_t key(Region region,int level,int slot) const;
    Chamber chamber(Region region,int level,int slot) const;
    bool connect(const Chamber &a,const Chamber &b,bool shaft,Passage &out) const;
    void add_branch(Region region,const Chamber &room,Network &network) const;
    EntranceResult add_entrance(Region region,const Chamber &hub,Network &network,int attempt,bool dry) const;
    float basin_clamp(float p,int region_axis,float margin) const;
    Network region_impl(Region region,bool include_isolated) const;
public:
    Planner(uint64_t seed,uint64_t dimension,PlanSettings settings,std::function<float(float,float)> surface,
            std::function<float(float,float)> opening_surface={},std::function<bool(const Passage&)> wet_opening={});
    Network region(Region region) const;
    int region_size() const {return settings.region_size;}
    bool valid() const {return valid_config;}
    float geothermal(float x,float z) const;
    float floor() const {return settings.floor;}
    float roof_margin() const {return settings.roof_margin;}
};
}
