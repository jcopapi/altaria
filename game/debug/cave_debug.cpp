#include "native/src/altaria_game.h"
#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/vector3i.hpp>
#include <cmath>
namespace godot {
Array AltariaGame::get_cave_formations() const {
    Array out;
    if(!world || !player || !world->generator()->underground_biomes())return out;
    auto p=player->get_position();if(!p.is_finite() || std::abs(p.x)>1000000 || std::abs(p.z)>1000000)return out;
    int span=world->settings().caves.plan.region_size;
    auto plan=world->cave_plan_at(altaria::caves::basin_of({int(std::floor(p.x)),int(std::floor(p.z))},span));
    if(!plan)return out;
    const auto &g=*world->generator();
    for(const auto &f:plan->formations) {
        Dictionary row;row["id"]=String::num_uint64(f.id);row["index"]=out.size();
        row["kind"]=String(altaria::caves::formation_name(f.kind));
        row["rule"]=String(g.underground_biomes()->formation_definition(f.rule).id.c_str());
        row["base"]=Vector3(f.base[0],f.base[1],f.base[2]);row["height"]=f.height;row["radius"]=f.radius;
        row["vertical"]=f.vertical;
        row["rock"]=int(f.rock);row["accent"]=int(f.accent);row["room"]=String::num_uint64(f.chamber);
        auto mask=g.underground()->resident_support(f.id);
        row["support_ready"]=bool(mask);
        if(mask) {
            row["supported_voxels"]=int64_t(mask->kept);row["pruned_voxels"]=int64_t(mask->removed);
            for(size_t i=0;i<mask->bits.size();i++)if(mask->bits[i]) {
                uint64_t word=mask->bits[i];size_t bit=0;while((word&1)==0){bit++;word>>=1;}
                size_t cell=i*64+bit;
                row["sample"]=Vector3i(mask->origin[0]+int(cell%mask->size[0]),
                    mask->origin[1]+int((cell/mask->size[0])%mask->size[1]),
                    mask->origin[2]+int(cell/(size_t(mask->size[0])*mask->size[1])));
                break;
            }
        }
        out.append(row);
    }
    return out;
}
Dictionary AltariaGame::get_cave_environment(Vector3 p) const {
    Dictionary out;
    if(!world || !world->generator()->underground_biomes() || !p.is_finite() || std::abs(p.x)>1000000 ||
       std::abs(p.z)>1000000 || p.y<world->settings().min_y || p.y>=world->settings().max_y)return out;
    const auto &g=*world->generator();
    int x=int(std::floor(p.x)),y=int(std::floor(p.y)),z=int(std::floor(p.z));
    auto column=g.column(x,z);
    const auto *hydro=world->cave_hydrology_at(x,z);
    bool loaded=world->voxel_ready(x,y,z);
    bool in_cave_fluid=loaded && world->cave_liquid_at(x,y,z);
    auto fluid=in_cave_fluid?(world->block_at(x,y,z)==g.blocks().water?altaria::caves::Fluid::water:altaria::caves::Fluid::lava):altaria::caves::Fluid::dry;
    bool wet=fluid==altaria::caves::Fluid::water;
    const auto &stage=*g.underground_biomes();auto env=stage.environment(column.underground,column.height,p.y,wet);
    float immersion=in_cave_fluid && hydro?std::max(0.f,float(hydro->top+1)-float(p.y)):0;
    if(wet) {
        env.pressure_kpa+=9.80665f*immersion;
        env.oxygen=0;env.visibility*=.55f;
    } else if(fluid==altaria::caves::Fluid::lava) {
        env.oxygen=0;env.visibility*=.4f;env.damage_per_second+=25;
    }
    Array weights;for(auto w:env.depth.weights)weights.append(w);
    out["depth"]=String(altaria::caves::depth_name(env.depth.primary));out["depth_weights"]=weights;
    out["below_surface"]=env.depth.below_surface;out["surface_height"]=column.height;
    out["surface_influence"]=env.depth.surface_influence;out["geothermal"]=env.depth.geothermal;
    out["biome"]=String(stage.definition(env.biomes.primary).id.c_str());
    out["secondary_biome"]=String(stage.definition(env.biomes.secondary).id.c_str());out["primary_weight"]=env.biomes.primary_weight;
    out["temperature_c"]=env.temperature_c;out["humidity"]=env.humidity;out["geology"]=column.underground.geology;
    out["frozen"]=env.frozen;out["sedimentary"]=env.sedimentary;out["rooted"]=env.rooted;
    out["pressure_kpa"]=env.pressure_kpa;out["oxygen_factor"]=env.oxygen;out["visibility_factor"]=env.visibility;
    out["environmental_damage_per_second"]=env.damage_per_second;
    out["loaded"]=loaded;out["fluid"]=String(altaria::caves::fluid_name(fluid));
    out["immersion_depth"]=immersion;
    if(wet)out["current"]=world->cave_river_at(p);
    if(hydro) {
        out["basin_source"]=String(altaria::caves::water_source_name(hydro->source));
        out["river_courses"]=hydro->rivers?int64_t(hydro->rivers->size()):0;
        if(hydro->fluid!=altaria::caves::Fluid::dry)out["fluid_surface_y"]=hydro->top+1;
    }
    out["rock"]=int(g.rock_material(column,x,y,z,wet));
    return out;
}
String AltariaGame::toggle_cave_map() {
    cave_map_enabled=!cave_map_enabled;
    if(cave_map_enabled) {
        if(!cave_map) {
            cave_map=memnew(MeshInstance3D);
            cave_map->set_name("CaveDebugMap");
            cave_map->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
            cave_map->set_ignore_occlusion_culling(true);
            stage->add_child(cave_map);
        }
        cave_map->show();
        update_cave_map();
        return "Cave map on. Depth: green / yellow / violet / red. Cyan: water; orange: lava; white: current chunk.";
    }
    if(cave_map){cave_map->hide();cave_map->set_mesh(Ref<Mesh>());}
    cave_map_plan.reset();cave_map_loaded=false;
    return "Cave map off.";
}
void AltariaGame::update_cave_map() {
    if(!world || !player || !cave_map)return;
    auto p=player->get_position();
    if(!p.is_finite() || std::abs(p.x)>1000000 || std::abs(p.z)>1000000)return;
    int x=int(std::floor(p.x)),z=int(std::floor(p.z)),span=world->settings().caves.plan.region_size;
    auto region=altaria::caves::basin_of({x,z},span),chunk=altaria::caves::basin_of({x,z},32);
    altaria::TileKey tile{chunk.x*32,chunk.z*32,0};
    auto plan=world->cave_plan_at(region);
    const auto *hydro=world->cave_hydrology_at(x,z);
    if(!plan) {
        if(cave_map->get_mesh().is_valid())cave_map->set_mesh(Ref<Mesh>());
        cave_map_plan.reset();return;
    }
    if(cave_map_plan.lock()==plan && cave_map_tile==tile && cave_map_loaded==bool(hydro))return;
    cave_map_plan=plan;cave_map_tile=tile;cave_map_loaded=bool(hydro);
    Ref<ImmediateMesh> mesh;mesh.instantiate();
    Ref<StandardMaterial3D> material;material.instantiate();
    material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR,true);
    material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST,true);
    material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG,true);
    mesh->surface_begin(Mesh::PRIMITIVE_LINES,material);
    auto line=[&](Vector3 a,Vector3 b,Color color){mesh->surface_set_color(color);mesh->surface_add_vertex(a);mesh->surface_add_vertex(b);};
    auto vector=[](const altaria::caves::Point &a){return Vector3(a[0]+.5f,a[1]+.5f,a[2]+.5f);};
    const std::array<Color,4> depths={Color(.3f,1,.35f),Color(1,.85f,.25f),Color(.8f,.55f,1),Color(1,.25f,.25f)};
    constexpr float tau=6.28318530718f;
    auto ring=[&](Vector3 center,Vector3 a,Vector3 b,Color color) {
        auto previous=center+a;
        for(int k=1;k<=24;k++) {
            float angle=tau*float(k)/24;
            Vector3 next=center+a*std::cos(angle)+b*std::sin(angle);
            line(previous,next,color);previous=next;
        }
    };
    std::vector<float> xs,zs;
    std::unordered_map<uint64_t,int> room_depth;
    const auto &network=plan->network;
    for(const auto &c:network.chambers){xs.push_back(c.center[0]);zs.push_back(c.center[2]);room_depth.emplace(c.id,c.depth);}
    for(const auto &c:network.external)room_depth.emplace(c.id,c.depth);
    auto g=world->generator();
    auto columns=g->columns(xs,zs);
    for(size_t i=0;i<network.chambers.size();i++) {
        const auto &room=network.chambers[i];Vector3 c=vector(room.center);
        auto r=room.radius;auto depth=depths[room.depth];
        ring(c,Vector3(r[0],0,0),Vector3(0,0,r[2]),depth);
        ring(c,Vector3(0,r[1],0),Vector3(r[0],0,0),depth);
        ring(c,Vector3(0,r[1],0),Vector3(0,0,r[2]),depth);
        bool wet=hydro && hydro->fluid==altaria::caves::Fluid::water && room.center[1]<=hydro->top;
        auto biomes=g->underground_biomes();
        auto env=biomes->environment(columns[i].underground,columns[i].height,room.center[1],wet);
        const auto &id=biomes->definition(env.biomes.primary).id;
        uint32_t hash=2166136261u;for(unsigned char k:id){hash^=k;hash*=16777619u;}
        Color tint=Color::from_hsv(float(hash%360)/360,.7f,1);
        for(Vector3 axis:{Vector3(2,0,0),Vector3(0,2,0),Vector3(0,0,2)})line(c-axis,c+axis,tint);
        if(hydro && hydro->fluid!=altaria::caves::Fluid::dry) {
            float plane=hydro->top+1.f,d=(plane-c.y)/r[1];
            if(std::abs(d)<1) {
                float f=std::sqrt(1-d*d);
                auto water=hydro->fluid==altaria::caves::Fluid::water?Color(.2f,.9f,1):Color(1,.45f,.1f);
                ring(Vector3(c.x,plane,c.z),Vector3(r[0]*f,0,0),Vector3(0,0,r[2]*f),water);
            }
        }
    }
    for(const auto &e:network.passages) {
        auto start=room_depth.find(e.a);
        Color color=e.entrance?Color(.85f,1,1):start!=room_depth.end()?depths[start->second]:Color(.7f,.7f,.7f);
        for(size_t i=1;i<e.path.size();i++)line(vector(e.path[i-1]),vector(e.path[i]),color);
    }
    auto y0=float(world->settings().min_y),y1=float(world->settings().max_y);
    for(int i=0;i<4;i++) {
        const int k=(i+1)%4;
        Vector3 a(tile.x+((i==1||i==2)?32:0),y0,tile.z+(i>=2?32:0));
        Vector3 b(tile.x+((k==1||k==2)?32:0),y0,tile.z+(k>=2?32:0));
        Vector3 height(0,y1-y0,0);
        line(a,b,Color(1,1,1));line(a+height,b+height,Color(1,1,1));line(a,a+height,Color(1,1,1));
    }
    mesh->surface_end();cave_map->set_mesh(mesh);
}
}
