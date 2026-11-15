#include "cave_plan.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
namespace altaria::caves {
static uint64_t mix(uint64_t x) {x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);}
static float unit(uint64_t x) {return float((mix(x)>>40)&0xffffff)/16777216.f;}
const char *entrance_kind_name(EntranceKind kind) {
    switch(kind) {
        case EntranceKind::mouth:return "mouth";
        case EntranceKind::ravine:return "ravine";
        default:return "shaft";
    }
}
std::vector<Archetype> default_archetypes() {
    return {{"normal",0,3,{9,5,8},{25,13,23},4},{"giant",1,3,{48,22,40},{80,37,72},1.5f},
            {"rift",0,3,{8,16,28},{16,32,50},1},{"collapse",0,2,{18,8,16},{35,18,30},1},
            {"geode",1,3,{10,9,10},{20,18,20},.5f},{"crystal",1,3,{15,12,15},{30,24,30},.5f}};
}
bool Bounds::intersects(Bounds b) const {for(int a=0;a<3;a++)if(hi[a]<b.lo[a] || lo[a]>b.hi[a])return false;return true;}
Bounds bounds(const Chamber &c) {Bounds b;for(int a=0;a<3;a++){b.lo[a]=c.center[a]-c.radius[a];b.hi[a]=c.center[a]+c.radius[a];}return b;}
Bounds bounds(const Passage &p) {
    Bounds b{p.from,p.from};for(auto q:p.path)for(int a=0;a<3;a++){b.lo[a]=std::min(b.lo[a],q[a]);b.hi[a]=std::max(b.hi[a],q[a]);}
    for(int a=0;a<3;a++){b.lo[a]-=p.radius;b.hi[a]+=p.radius;}return b;
}
Region basin_of(Region r,int span) {
    if(span<=0)return r;
    auto divide=[span](int x){return x/span-(x%span<0);};
    return {divide(r.x),divide(r.z)};
}
Planner::Planner(uint64_t s,uint64_t d,PlanSettings c,std::function<float(float,float)> roof,
                 std::function<float(float,float)> opening,std::function<bool(const Passage&)> wet):seed(s),dimension(d),settings(std::move(c)),
    surface(std::move(roof)),opening_surface(opening?std::move(opening):surface),wet_opening(std::move(wet)) {
    if(settings.region_size<128 || settings.region_size>2048 || settings.basin_regions<0 || settings.basin_regions>4 || !surface || !std::isfinite(settings.floor) || settings.floor < -8192 || !std::isfinite(settings.ceiling) || settings.ceiling>8192 || settings.floor>=settings.ceiling || settings.roof_margin<4 || !std::isfinite(settings.roof_margin) || settings.archetypes.empty() || settings.archetypes.size()>128)valid_config=false;
    float previous=0;for(auto depth:settings.depths){if(!std::isfinite(depth) || depth<=previous)valid_config=false;previous=depth;}
    for(float chance:{settings.entrance_chance,settings.isolated_chance,settings.infernal_chance,settings.flooded_basin_chance,
                      settings.branch_chance})if(!std::isfinite(chance) || chance<0 || chance>1)valid_config=false;
    std::unordered_set<std::string> ids;
    for(auto &a:settings.archetypes) {
        if(a.id.empty() || !ids.insert(a.id).second || a.min_depth<0 || a.max_depth>3 || a.min_depth>a.max_depth || !std::isfinite(a.weight) || a.weight<=0 || a.weight>1000000)valid_config=false;
        for(int axis=0;axis<3;axis++)if(!std::isfinite(a.radius_min[axis]) || !std::isfinite(a.radius_max[axis]) || a.radius_min[axis]<2 || a.radius_max[axis]<a.radius_min[axis] || a.radius_max[axis]>settings.region_size*.4f)valid_config=false;
    }
    if(!settings.landmark_archetype.empty() && std::none_of(settings.archetypes.begin(),settings.archetypes.end(),[&](const Archetype &a){
        return a.id==settings.landmark_archetype && a.min_depth<=2 && a.max_depth>=2;
    }))valid_config=false;
}
float Planner::basin_clamp(float p,int region_axis,float margin) const {
    if(settings.basin_regions==0)return p;
    int span=settings.basin_regions*settings.region_size;
    int origin=basin_of({region_axis,0},settings.basin_regions).x*span;
    return std::clamp(p,origin+margin,origin+span-margin);
}
uint64_t Planner::key(Region r,int level,int slot) const {
    uint64_t h=mix(seed^0x43415645504c414eULL);h=mix(h^dimension);h=mix(h^uint32_t(r.x));h=mix(h^uint32_t(r.z));h=mix(h^uint64_t(level));return mix(h^uint64_t(slot));
}
float Planner::geothermal(float x,float z) const {
    if(!valid_config || !std::isfinite(x) || !std::isfinite(z) || std::abs(x)>999000 || std::abs(z)>999000)return 0;
    float px=x/settings.region_size-.5f,pz=z/settings.region_size-.5f;
    int ix=int(std::floor(px)),iz=int(std::floor(pz));float tx=px-ix,tz=pz-iz;
    tx=tx*tx*(3-2*tx);tz=tz*tz*(3-2*tz);
    auto hot=[&](int dx,int dz){return unit(key({ix+dx,iz+dz},3,99))<settings.infernal_chance?1.f:0.f;};
    float a=hot(0,0)*(1-tx)+hot(1,0)*tx,b=hot(0,1)*(1-tx)+hot(1,1)*tx;
    return a*(1-tz)+b*tz;
}
Chamber Planner::chamber(Region r,int level,int slot) const {
    Chamber c;c.id=key(r,level,slot);c.depth=level;c.isolated=slot==3;
    if(level==3 && unit(key(r,3,99))>=settings.infernal_chance)return {};
    float span=settings.region_size;auto placement=slot==0?key(r,0,0):c.id;
    c.center[0]=(float(r.x)+.5f)*span+(unit(placement+1)-.5f)*span*.55f;
    c.center[2]=(float(r.z)+.5f)*span+(unit(placement+2)-.5f)*span*.55f;
    bool landmark_routes=!settings.landmark_archetype.empty();
    if(landmark_routes && slot==0 && level>0) {
        const float heading=unit(placement+12)*6.2831853f+level*2.3999632f;
        const float offset=span*(.13f+.08f*unit(c.id+13));
        c.center[0]+=std::cos(heading)*offset;
        c.center[2]+=std::sin(heading)*offset;
    }
    float total=0;for(auto &a:settings.archetypes)if(level>=a.min_depth && level<=a.max_depth)total+=a.weight;
    if(total<=0)return {};
    float selection=unit(c.id+3)*total;const Archetype *chosen=nullptr;
    for(auto &a:settings.archetypes)if(level>=a.min_depth && level<=a.max_depth){chosen=&a;selection-=a.weight;if(selection<=0)break;}
    if(landmark_routes && slot==0 && (level==2 || (level==1 && unit(c.id+14)<.5f)))
        for(const auto &a:settings.archetypes)
            if(a.id==settings.landmark_archetype && level>=a.min_depth && level<=a.max_depth){chosen=&a;break;}
    c.archetype=chosen->id;
    c.rugged=landmark_routes && c.archetype==settings.landmark_archetype;
    for(int axis=0;axis<3;axis++)c.radius[axis]=chosen->radius_min[axis]+unit(c.id+4+axis)*(chosen->radius_max[axis]-chosen->radius_min[axis]);
    if(settings.basin_regions>0) {
        // Keep the complete opening and curving route inside the catchment.
        for(int axis:{0,2}) {
            float radius=c.radius[axis];
            if(slot==0)for(const auto &a:settings.archetypes)radius=std::max(radius,a.radius_max[axis]);
            const float reserve=landmark_routes && slot==0 ? (settings.natural_entrances?66.f:57.f) : 29.f;
            c.center[axis]=basin_clamp(c.center[axis],axis==0?r.x:r.z,std::max(reserve,radius)+5.f);
        }
    }
    float roof=std::numeric_limits<float>::infinity();
    // Conservative rectangular envelope. Final carving also checks the actual
    // host at every voxel, since a sampled atlas cannot prove an arbitrary callback.
    int nx=int(std::ceil(c.radius[0]*2/4)),nz=int(std::ceil(c.radius[2]*2/4));
    for(int z=0;z<=nz;z++)for(int x=0;x<=nx;x++) {
        float h=surface(c.center[0]-c.radius[0]+2*c.radius[0]*x/nx,c.center[2]-c.radius[2]+2*c.radius[2]*z/nz);
        if(!std::isfinite(h) || h>settings.ceiling){c.invalid_surface=true;c.radius={};return c;}roof=std::min(roof,h);
    }
    c.center[1]=roof-settings.depths[level];
    // Never clamp different categories onto the same floor. Missing host rock
    // means this chamber simply does not exist in this region.
    if(c.center[1]-c.radius[1]<settings.floor+4 || c.center[1]+c.radius[1]>roof-settings.roof_margin)return {};
    return c;
}
bool Planner::connect(const Chamber &a,const Chamber &b,bool shaft,Passage &p) const {
    if(a.radius[0]<=0 || b.radius[0]<=0)return false;
    p.a=std::min(a.id,b.id);p.b=std::max(a.id,b.id);p.id=mix(p.a^mix(p.b));
    p.from=a.id==p.a?a.center:b.center;p.to=a.id==p.a?b.center:a.center;
    p.vertical=shaft && std::abs(p.from[0]-p.to[0])<.001f && std::abs(p.from[2]-p.to[2])<.001f;
    p.radius=std::min(3+unit(p.id+3)*4,std::min(a.radius[1],b.radius[1])*.7f);
    const bool winding=shaft && !settings.landmark_archetype.empty();
    if(winding)p.radius=std::min(4.5f+unit(p.id+3)*3,std::min(a.radius[1],b.radius[1])*.85f);
    for(int axis=0;axis<3;axis++)p.control[axis]=(p.from[axis]+p.to[axis])*.5f;
    if(!p.vertical){p.control[0]+=(unit(p.id+1)-.5f)*18;p.control[2]+=(unit(p.id+2)-.5f)*18;}
    float length=0;for(int axis=0;axis<3;axis++){float d=p.to[axis]-p.from[axis];length+=d*d;}
    int count=std::max(2,int(std::ceil(std::sqrt(length)/2)));
    const float heading=winding?unit(p.id+7)*6.2831853f:0.f;
    const float bend=winding?settings.region_size*(.10f+.06f*unit(p.id+8)):0.f;
    if(winding)count+=int(std::ceil(bend));
    for(int i=0;i<=count;i++) {
        float t=float(i)/count;Point q;
        for(int axis=0;axis<3;axis++)q[axis]=(1-t)*(1-t)*p.from[axis]+2*(1-t)*t*p.control[axis]+t*t*p.to[axis];
        if(p.vertical){q[0]=p.from[0];q[2]=p.from[2];}
        if(winding && i>0 && i<count) {
            const float wave=std::sin(t*6.2831853f)*bend;
            q[0]=basin_clamp(q[0]+std::cos(heading)*wave,int(std::floor(a.center[0]/settings.region_size)),p.radius+3);
            q[2]=basin_clamp(q[2]+std::sin(heading)*wave,int(std::floor(a.center[2]/settings.region_size)),p.radius+3);
        }
        float roof=std::numeric_limits<float>::infinity();
        for(float z:{-1.f,0.f,1.f})for(float x:{-1.f,0.f,1.f}) {
            float h=surface(q[0]+x*p.radius,q[2]+z*p.radius);if(!std::isfinite(h) || h>settings.ceiling){p.invalid_surface=true;return false;}roof=std::min(roof,h);
        }
        float safe=roof-settings.roof_margin-p.radius;
        if((i==0 || i==count) && q[1]>safe)return false;
        q[1]=std::min(q[1],safe);
        if(q[1]-p.radius<settings.floor+4)return false;
        p.path.push_back(q);
    }
    return true;
}
void Planner::add_branch(Region r,const Chamber &room,Network &network) const {
    if(room.radius[0]<=0)return;
    const uint64_t id=key(r,room.depth,521);
    if(unit(id)>=settings.branch_chance)return;
    const float span=settings.region_size;
    for(int attempt=0;attempt<5;attempt++) {
        const float heading=6.2831853f*unit(id+1)+attempt*2.3999632f;
        const float distance=std::max(room.radius[0],room.radius[2])+16.f+12.f*unit(id+2);
        Chamber pocket;
        pocket.id=id;pocket.depth=room.depth;pocket.archetype="alcove";
        pocket.radius={4.f+unit(id+3)*4.f,3.4f,4.f+unit(id+4)*4.f};
        pocket.center={room.center[0]+distance*std::cos(heading),room.center[1]+8.f*(unit(id+5)-.5f),
                       room.center[2]+distance*std::sin(heading)};
        auto end=bounds(pocket);
        if(end.lo[0]<r.x*span+6 || end.hi[0]>(r.x+1)*span-6 ||
           end.lo[2]<r.z*span+6 || end.hi[2]>(r.z+1)*span-6)continue;
        for(int i=0;i<3;i++){end.lo[i]-=3;end.hi[i]+=3;}
        const auto overlaps_end=[&](const auto &other){return end.intersects(bounds(other));};
        if(std::any_of(network.chambers.begin(),network.chambers.end(),overlaps_end) ||
           std::any_of(network.external.begin(),network.external.end(),overlaps_end) ||
           std::any_of(network.passages.begin(),network.passages.end(),overlaps_end))continue;
        Passage branch;
        if(!connect(room,pocket,false,branch))continue;
        branch.dead_end=true;
        network.chambers.push_back(std::move(pocket));
        network.passages.push_back(std::move(branch));
        return;
    }
}
Planner::EntranceResult Planner::add_entrance(Region r,const Chamber &hub,Network &network,int attempt,bool dry) const {
    const uint64_t id=key(r,0,504);
    const uint64_t terminal=key(r,0,503);
    EntranceKind kind=static_cast<EntranceKind>(int(unit(id+1)*3));
    if (settings.natural_entrances) {
        const float roll = unit(id + 1);
        kind = roll < .84f ? EntranceKind::mouth : roll < .92f ? EntranceKind::ravine : EntranceKind::shaft;
    }
    const float heading=unit(id+2)*6.2831853f+attempt*2.3999632f;
    float dx=std::cos(heading),dz=std::sin(heading);
    Point lip=hub.center;
    const float offset = kind == EntranceKind::shaft ? 15.f :
                         settings.natural_entrances && kind == EntranceKind::mouth ? 48.f : 27.f;
    lip[0]+=dx*offset;
    lip[2]+=dz*offset;
    bool hillside = false;
    if (kind == EntranceKind::mouth) {
        // Prefer an outward-facing slope. A shallow cut works where the
        // terrain lacks a steep enough face to conceal a tunnel entrance.
        float best = -std::numeric_limits<float>::infinity();
        const int candidates = settings.natural_entrances ? 16 : 8;
        for (int i = 0; i < candidates; ++i) {
            const float angle = heading + i * .7853982f;
            const float ax = std::cos(angle), az = std::sin(angle);
            const float reach = offset + (i >= 8 ? 10.f : 0.f);
            const float x = hub.center[0] + ax * reach, z = hub.center[2] + az * reach;
            const float outside = settings.natural_entrances ? surface(x + 7 * ax, z + 7 * az) : surface(x, z);
            const float inside = settings.natural_entrances ? surface(x - 8 * ax, z - 8 * az) :
                                                              surface(x - 14 * ax, z - 14 * az);
            const float rise = inside - outside;
            if (!std::isfinite(rise))
                return EntranceResult::invalid;
            const bool lateral = rise >= 7.f;
            const float score = settings.natural_entrances ? (lateral ? 30.f : 0.f) + outside * .12f - std::abs(rise - 8.f)
                                                            : rise;
            if (score > best) {
                best = score;
                lip[0] = x; lip[2] = z; dx = ax; dz = az;
                hillside = lateral;
            }
        }
        if (!settings.natural_entrances && best < 2.f)
            kind = EntranceKind::shaft;
    }
    const float radius=kind==EntranceKind::mouth?5.f:kind==EntranceKind::ravine?5.75f:3.75f;
    float height=opening_surface(lip[0],lip[2]);
    if(!std::isfinite(height) || height>settings.ceiling)return EntranceResult::invalid;
    lip[1]=height+2;

    Chamber anchor;
    anchor.id=key(r,0,505);
    anchor.radius={radius,radius+2,radius};
    anchor.center=lip;
    if (kind == EntranceKind::mouth) {
        const float run = settings.natural_entrances ? 34.f : 14.f;
        anchor.center[0] -= dx * run;
        anchor.center[2] -= dz * run;
    }
    if(kind==EntranceKind::ravine){anchor.center[0]-=dx*9;anchor.center[2]-=dz*9;}
    if(kind==EntranceKind::shaft && settings.natural_entrances){anchor.center[0]-=dx*9;anchor.center[2]-=dz*9;}
    float roof=std::numeric_limits<float>::infinity();
    for(float oz:{-radius,0.f,radius})for(float ox:{-radius,0.f,radius}) {
        float sample=surface(anchor.center[0]+ox,anchor.center[2]+oz);
        if(!std::isfinite(sample) || sample>settings.ceiling)return EntranceResult::invalid;
        roof=std::min(roof,sample);
    }
    anchor.center[1]=roof-settings.roof_margin-radius-1;
    if (kind == EntranceKind::mouth && settings.natural_entrances)
        anchor.center[1] = std::min(anchor.center[1], height - 6.f);
    Passage opening;
    opening.id=id;opening.a=anchor.id;opening.b=terminal;
    opening.from=anchor.center;opening.to=lip;opening.radius=radius;opening.entrance=true;
    opening.path.push_back(anchor.center);
    if(kind==EntranceKind::mouth) {
        if(settings.natural_entrances) {
            Point exterior{lip[0] + dx * 7.f, 0.f, lip[2] + dz * 7.f};
            const float floor = opening_surface(exterior[0], exterior[2]);
            if (!std::isfinite(floor) || floor > settings.ceiling)
                return EntranceResult::invalid;
            exterior[1] = floor + 1.5f;
            Point inside{lip[0] - dx * 8.f, exterior[1] - (hillside ? 1.f : 5.f), lip[2] - dz * 8.f};
            anchor.center[1] = std::min(anchor.center[1], inside[1] - 2.f);
            if (inside[1] - anchor.center[1] > 17.f)
                return EntranceResult::unavailable;
            opening.from = anchor.center;
            opening.path.front() = anchor.center;
            opening.path.push_back(inside);
            lip = exterior;
            opening.to = lip;
        } else {
            opening.path.push_back({lip[0]-dx*5,height-3,lip[2]-dz*5});
            opening.path.push_back({lip[0]+dx*4,height-3,lip[2]+dz*4});
        }
    } else if(kind==EntranceKind::ravine) {
        for(int i=0;i<=4;i++) {
            const float offset=(i-1)*6.f;
            const float bend=settings.natural_entrances?2.f*std::sin(float(i)*1.1f):0.f;
            Point cut{lip[0]+dx*offset-dz*bend,0,lip[2]+dz*offset+dx*bend};
            float top=opening_surface(cut[0],cut[2]);
            if(!std::isfinite(top) || top>settings.ceiling)return EntranceResult::invalid;
            cut[1]=top-2.5f;
            opening.path.push_back(cut);
        }
        lip=opening.path.back();lip[1]+=4.5f;
        opening.to=lip;
    } else if(settings.natural_entrances) {
        opening.path.push_back({lip[0]-dx*3.f+dz*1.6f,height-3.5f,lip[2]-dz*3.f-dx*1.6f});
    }
    opening.path.push_back(lip);
    opening.control=opening.path[opening.path.size()/2];
    if(dry && wet_opening(opening))return EntranceResult::unavailable;
    Passage approach;
    if(!connect(hub,anchor,false,approach))
        return approach.invalid_surface?EntranceResult::invalid:EntranceResult::unavailable;
    network.passages.push_back(std::move(approach));
    network.passages.push_back(std::move(opening));
    network.junctions.push_back({anchor.id,anchor.center});
    network.entrances.push_back({terminal,lip,kind});
    return EntranceResult::added;
}
Network Planner::region_impl(Region r,bool include_isolated) const {
    if(!valid_config)return {};
    int limit=1000000/settings.region_size-2;
    if(std::abs(int64_t(r.x))>limit || std::abs(int64_t(r.z))>limit)return {};
    Network out;out.region=r;
    std::array<std::array<Chamber,3>,4> rooms{};
    bool invalid_surface=false;
    auto add=[&](const Chamber &a,const Chamber &b,bool vertical){
        Passage p;
        if(!connect(a,b,vertical,p)){invalid_surface|=p.invalid_surface;return false;}
        out.passages.push_back(std::move(p));return true;
    };
    Chamber previous;
    bool linked_from_surface=false,linked_to_landmark=false;
    for(int depth=0;depth<4;depth++) {
        for(int slot=0;slot<3;slot++) {
            auto c=chamber(r,depth,slot);if(c.invalid_surface)return {};
            rooms[depth][slot]=c;
        }
        auto &hub=rooms[depth][0];if(hub.radius[0]<=0)continue;
        for(const auto &c:rooms[depth])if(c.radius[0]>0)out.chambers.push_back(c);
        add(hub,rooms[depth][1],false);add(rooms[depth][1],rooms[depth][2],false);
        if(depth==0)linked_from_surface=true;
        if(previous.radius[0]>0) {
            const bool linked=add(previous,hub,true);
            linked_from_surface &= linked;
        }
        if(depth==2)linked_to_landmark=linked_from_surface;
        previous=hub;
        if(unit(hub.id+11)>.5f)add(hub,rooms[depth][2],false);
        for(auto neighbor:{Region{r.x-1,r.z},Region{r.x+1,r.z},Region{r.x,r.z-1},Region{r.x,r.z+1}}) {
            if(settings.basin_regions>0 && !(basin_of(r,settings.basin_regions)==basin_of(neighbor,settings.basin_regions)))continue;
            auto other=chamber(neighbor,depth,0);if(other.invalid_surface)return {};
            if(other.radius[0]>0){out.external.push_back(other);add(hub,other,false);}
        }
    }
    // A hub's existence must be canonical in its own region and every neighbor.
    // Keep all valid hubs; terrain can omit links between subnetworks. Only side
    // rooms unreachable from those hubs are pruned. An arbitrary first side room
    // must never erase a deeper network still referenced by neighboring regions.
    if(!out.chambers.empty()) {
        std::unordered_set<uint64_t> reached;
        for(const auto &level:rooms)if(level[0].radius[0]>0)reached.insert(level[0].id);
        for(size_t i=0;i<out.chambers.size();i++)for(auto &p:out.passages){if(reached.count(p.a))reached.insert(p.b);if(reached.count(p.b))reached.insert(p.a);}
        std::unordered_set<uint64_t> dropped;
        for(const auto &c:out.chambers)if(!reached.count(c.id))dropped.insert(c.id);
        out.chambers.erase(std::remove_if(out.chambers.begin(),out.chambers.end(),[&](const Chamber &c){return dropped.count(c.id);}),out.chambers.end());
        out.passages.erase(std::remove_if(out.passages.begin(),out.passages.end(),[&](const Passage &p){return dropped.count(p.a) || dropped.count(p.b);}),out.passages.end());
    }
    if(rooms[0][0].radius[0]>0 && unit(key(r,0,502))<settings.entrance_chance) {
        Region basin=basin_of(r,settings.basin_regions);
        bool dry=settings.basin_regions>0 && wet_opening && unit(key(basin,0,506))>=settings.flooded_basin_chance;
        if(!settings.landmark_archetype.empty()) {
            if(linked_to_landmark)for(int attempt=0;attempt<(settings.natural_entrances?4:dry?3:1);attempt++) {
                auto result=add_entrance(r,rooms[0][0],out,attempt,dry);
                if(result==EntranceResult::invalid)return {};
                if(result==EntranceResult::added)break;
            }
        } else {
            auto hub=rooms[0][0];Point end=hub.center;end[0]+=20;
            float roof=opening_surface(end[0],end[2]);if(!std::isfinite(roof) || roof>settings.ceiling)return {};
            end[1]=roof+2;uint64_t terminal=key(r,0,503);
            Passage p;p.a=hub.id;p.b=terminal;p.id=key(r,0,504);p.from=hub.center;p.to=end;p.control=hub.center;p.control[0]=end[0];p.control[1]=hub.center[1];p.radius=3;p.entrance=true;
            for(int i=0;i<=32;i++){float t=i/32.f;Point q;for(int axis=0;axis<3;axis++)q[axis]=(1-t)*(1-t)*p.from[axis]+2*(1-t)*t*p.control[axis]+t*t*p.to[axis];p.path.push_back(q);}
            if(!dry || !wet_opening(p)) {
                out.passages.push_back(std::move(p));out.entrances.push_back({terminal,end});
            }
        }
    }
    for(int depth=0;depth<3 && settings.branch_chance>0;depth++) {
        const auto side=std::find_if(out.chambers.begin(),out.chambers.end(),[&](const Chamber &c){return c.id==rooms[depth][1].id;});
        const Chamber origin=side==out.chambers.end()?rooms[depth][0]:*side;
        add_branch(r,origin,out);
    }
    if(include_isolated && unit(key(r,0,501))<settings.isolated_chance) {
        auto c=chamber(r,1,3);if(c.invalid_surface)return {};
        bool clear=c.radius[0]>0;
        auto envelope=bounds(c);
        // Two .75 m detail expansions plus a cardinal voxel contact can join
        // disjoint raw bounds. Three metres conservatively exclude that case.
        for(int axis=0;axis<3;axis++){envelope.lo[axis]-=3;envelope.hi[axis]+=3;}
        auto conflict=[&](const Network &network) {
            for(const auto &other:network.chambers)if(envelope.intersects(bounds(other)))return true;
            for(const auto &p:network.passages)if(envelope.intersects(bounds(p)))return true;
            return false;
        };
        clear=clear && !conflict(out);
        // Normal neighbouring geometry may reach across a region border,
        // including side rooms absent from this region's canonical portals.
        for(int dz=-1;clear && dz<=1;dz++)for(int dx=-1;clear && dx<=1;dx++) {
            if(dx==0 && dz==0)continue;
            Region neighbor{r.x+dx,r.z+dz};
            auto normal=region_impl(neighbor,false);
            if(normal.valid && conflict(normal)){clear=false;break;}
            if(unit(key(neighbor,0,501))<settings.isolated_chance) {
                auto pocket=chamber(neighbor,1,3);
                // Canonical priority prevents two isolated candidates from
                // joining each other, without recursive mutual planning.
                if(pocket.radius[0]>0 && pocket.id<c.id && envelope.intersects(bounds(pocket)))clear=false;
            }
        }
        if(clear)out.chambers.push_back(c);
    }
    if(invalid_surface)return {};
    out.valid=true;return out;
}
Network Planner::region(Region r) const {return region_impl(r,true);}
}
