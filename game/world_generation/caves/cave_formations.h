#pragma once
#include "cave_plan.h"
namespace altaria::caves {
enum class FormationKind : uint8_t {
    stalactite, stalagmite, pillar, rubble, crystal, geode, fossil, root, fungus, vent, ice_sheet
};
enum class FormationWater : uint8_t { any, dry, wet };
struct FormationRule {
    std::string id,rock="$stone",accent="$stone",archetype;
    FormationKind kind=FormationKind::stalagmite;
    FormationWater water=FormationWater::any;
    std::array<float,2> radius{1.5f,3.f},height{3.f,8.f};
    float chance=1,min_frozen=0,min_geothermal=0,min_rooted=0,min_sedimentary=0;
    int count=1;
    bool ceiling=false;
};
struct Formation {
    uint64_t id=0,chamber=0;
    uint16_t rule=0;
    Bounds box{};
    Point base{};
    FormationKind kind=FormationKind::stalagmite;
    FormationWater water=FormationWater::any;
    float radius=0,height=0,cos_yaw=1,sin_yaw=0;
    uint16_t rock=0,accent=0;
    int vertical=1;
};
struct FormationCell {
    bool rock=false,carved=false,allowed=false,water=false,lava=false;
    int liquid_top=-8192;
};
struct FormationMask {
    std::array<int,3> origin{},size{};
    std::vector<uint64_t> bits;
    size_t kept=0,removed=0;
    bool contains(Point p) const;
    size_t bytes() const {return sizeof(*this)+bits.capacity()*sizeof(uint64_t);}
};
// Worker-only support pass. Unioned pieces are retained only if a cardinal
// path reaches uncarved host rock; an ice sheet may also rest on water.
FormationMask support_formation(const Formation &f,const std::function<FormationCell(int,int,int)> &sample);
bool valid_formation(const FormationRule &rule);
bool formation_kind(const std::string &name,FormationKind &out);
const char *formation_name(FormationKind kind);
Formation plan_formation(const Chamber &room,const FormationRule &rule,int index,uint16_t rock,uint16_t accent);
uint16_t formation_voxel(const Formation &formation,Point position,bool water,bool lava=false,int liquid_top=-8192);
}
