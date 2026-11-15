#include "world_data.h"
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <cmath>
#include <initializer_list>
#include <type_traits>
namespace altaria {
using namespace godot;
bool parse_caves(const Dictionary &d,caves::Config &c,String &error) {
    for(auto [key,target]:{std::pair{"enabled",&c.enabled},std::pair{"biome_variation",&c.biome_variation},
                           std::pair{"formation_shapes",&c.formation_shapes},
                           std::pair{"natural_entrances",&c.plan.natural_entrances}}) {
        if(d.has(key) && d[key].get_type()!=Variant::BOOL){error=String("Expected boolean cave setting: ")+key;return false;}
        *target=d.get(key,*target);
    }
    if(d.has("landmark_archetype")) {
        if(d["landmark_archetype"].get_type()!=Variant::STRING){error="Expected cave landmark archetype identifier";return false;}
        c.plan.landmark_archetype=String(d["landmark_archetype"]).utf8().get_data();
    }
    auto number=[&](const Dictionary &source,const char *key,double fallback,double lo,double hi,double &out) {
        Variant v=source.get(key,fallback);
        if ((v.get_type()!=Variant::INT && v.get_type()!=Variant::FLOAT) ||
            !std::isfinite(double(v)) || double(v)<lo || double(v)>hi) {error=String("Invalid cave setting: ")+key;return false;}
        out=double(v);return true;
    };
    double value;
#define NUM(key,target,lo,hi) if(!number(d,key,double(target),lo,hi,value))return false; \
    if constexpr(std::is_integral_v<decltype(target)>) {if(std::floor(value)!=value){error=String("Expected integer cave setting: ")+key;return false;}} \
    target=decltype(target)(value)
    NUM("region_size",c.plan.region_size,128,2048);
    NUM("roof_margin",c.plan.roof_margin,4,64);
    NUM("entrance_chance",c.plan.entrance_chance,0,1);
    NUM("branch_chance",c.plan.branch_chance,0,1);
    NUM("flooded_basin_chance",c.plan.flooded_basin_chance,0,1);
    NUM("isolated_chance",c.plan.isolated_chance,0,1);
    NUM("infernal_chance",c.plan.infernal_chance,0,1);
    NUM("dimension",c.dimension,0,1000000);
    NUM("cache_regions",c.cache_regions,16,128);
    NUM("cache_bytes",c.cache_bytes,1048576,134217728);
    NUM("roof_grid",c.roof_grid,4,32);
    NUM("max_lod",c.max_lod,0,2);
    NUM("detail_scale",c.detail_scale,4,128);
    NUM("detail_amplitude",c.detail_amplitude,0,.75);
    NUM("surface_influence_depth",c.depth.surface_influence_depth,32,1024);
    NUM("hydrology_basin_regions",c.hydrology.basin_regions,0,4);
    NUM("underground_lake_chance",c.hydrology.lake_chance,0,1);
    NUM("underground_lake_depth",c.hydrology.lake_depth,0,3);
    NUM("underground_lava_chance",c.hydrology.lava_chance,0,1);
    NUM("underground_lava_min_depth",c.hydrology.lava_min_depth,1,3);
    NUM("underground_lake_fill",c.hydrology.lake_fill,.1,.85);
    NUM("underground_lava_fill",c.hydrology.lava_fill,.1,.85);
#undef NUM
    if(d.has("noise")) {
        if(d["noise"].get_type()!=Variant::DICTIONARY){error="Expected cave noise object";return false;}
        Dictionary n=d["noise"];auto &o=c.noise;
        if(n.has("enabled")) {
            if(n["enabled"].get_type()!=Variant::BOOL){error="Expected boolean cave setting: noise.enabled";return false;}
            o.enabled=n["enabled"];
        }
        for(auto [key,target]:std::initializer_list<std::pair<const char*,float*>>{
                {"surface_margin",&o.surface_margin},{"surface_fade",&o.surface_fade},{"cheese_scale",&o.cheese_scale},
                {"cheese_threshold",&o.cheese_threshold},{"deep_threshold",&o.deep_threshold},{"tunnel_scale",&o.tunnel_scale},
                {"tunnel_width",&o.tunnel_width},{"squeeze_scale",&o.squeeze_scale},{"squeeze_width",&o.squeeze_width},
                {"fissure_scale",&o.fissure_scale},{"fissure_width",&o.fissure_width},{"region_scale",&o.region_scale},
                {"pillar_spacing",&o.pillar_spacing},{"hell_floor_amplitude",&o.hell_floor_amplitude},
                {"hell_ceiling_amplitude",&o.hell_ceiling_amplitude},{"hell_scale",&o.hell_scale},
                {"hell_pillar_spacing",&o.hell_pillar_spacing},{"transition_height",&o.transition_height},
                {"lava_sea_scale",&o.lava_sea_scale},{"lava_sea_threshold",&o.lava_sea_threshold},
                {"lava_sea_depth",&o.lava_sea_depth},{"lava_sea_height",&o.lava_sea_height},
                {"spike_chance",&o.spike_chance},{"hell_spike_chance",&o.hell_spike_chance},{"pillar_chance",&o.pillar_chance}}) {
            if(!number(n,key,*target,-100000,100000,value))return false;
            *target=float(value);
        }
        for(auto [key,target]:std::initializer_list<std::pair<const char*,int*>>{
                {"hell_floor",&o.hell_floor},{"hell_ceiling",&o.hell_ceiling},{"hell_lava",&o.hell_lava}}) {
            if(!number(n,key,*target,-8192,8192,value) || std::floor(value)!=value){error=String("Invalid cave setting: noise.")+key;return false;}
            *target=int(value);
        }
        if(n.has("lava_seas")) {
            if(n["lava_seas"].get_type()!=Variant::ARRAY || Array(n["lava_seas"]).size()>3){error="Expected up to three lava sea levels";return false;}
            Array a=n["lava_seas"];o.lava_seas.clear();
            for(int i=0;i<a.size();i++) {
                Variant v=a[i];
                if((v.get_type()!=Variant::INT && v.get_type()!=Variant::FLOAT) || std::floor(double(v))!=double(v)){error="Invalid lava sea level";return false;}
                o.lava_seas.push_back(int(v));
            }
        }
    }
    if(d.has("depths")) {
        if(d["depths"].get_type()!=Variant::ARRAY || Array(d["depths"]).size()!=4){error="Expected four cave depths";return false;}
        Array a=d["depths"];
        for(int i=0;i<4;i++) {
            Variant v=a[i];if((v.get_type()!=Variant::INT && v.get_type()!=Variant::FLOAT) || !std::isfinite(double(v))){error="Invalid cave depth";return false;}
            c.plan.depths[i]=double(v);
        }
    }
    if(d.has("archetypes")) {
        if(d["archetypes"].get_type()!=Variant::ARRAY){error="Expected cave archetypes array";return false;}
        Array rows=d["archetypes"];c.plan.archetypes.clear();
        for(int i=0;i<rows.size();i++) {
            if(rows[i].get_type()!=Variant::DICTIONARY){error="Expected cave archetype object";return false;}
            Dictionary row=rows[i];caves::Archetype a;
            if(!row.has("id") || row["id"].get_type()!=Variant::STRING){error="Expected cave archetype identifier";return false;}
            a.id=String(row["id"]).utf8().get_data();
            if(!number(row,"min_depth",0,0,3,value) || std::floor(value)!=value){error="Invalid archetype minimum depth";return false;}a.min_depth=value;
            if(!number(row,"max_depth",3,0,3,value) || std::floor(value)!=value){error="Invalid archetype maximum depth";return false;}a.max_depth=value;
            if(!number(row,"weight",1,.0001,1000000,value))return false;a.weight=value;
            for(const char *key:{"radius_min","radius_max"}) {
                if(!row.has(key) || row[key].get_type()!=Variant::ARRAY || Array(row[key]).size()!=3){error="Expected three cave radius components";return false;}
                Array components=row[key];
                for(int axis=0;axis<3;axis++) {
                    Variant v=components[axis];if((v.get_type()!=Variant::INT && v.get_type()!=Variant::FLOAT) || !std::isfinite(double(v))){error="Invalid cave radius";return false;}
                    (String(key)=="radius_min"?a.radius_min:a.radius_max)[axis]=double(v);
                }
            }
            c.plan.archetypes.push_back(std::move(a));
        }
    }
    if(d.has("depth_transition_widths")) {
        if(d["depth_transition_widths"].get_type()!=Variant::ARRAY || Array(d["depth_transition_widths"]).size()!=3){error="Expected three cave depth transition widths";return false;}
        Array values=d["depth_transition_widths"];
        for(int i=0;i<3;i++) {
            Variant v=values[i];if((v.get_type()!=Variant::INT && v.get_type()!=Variant::FLOAT) || !std::isfinite(double(v))){error="Invalid cave depth transition width";return false;}
            c.depth.transition_widths[i]=double(v);
        }
    }
    if(d.has("biomes")) {
        if(d["biomes"].get_type()!=Variant::DICTIONARY){error="Expected underground biomes object";return false;}
        if(!parse_cave_biomes(d["biomes"],c.biomes,error))return false;
    }
    if(d.has("exploration_formations")) {
        if(d["exploration_formations"].get_type()!=Variant::ARRAY){error="Expected exploration formation list";return false;}
        const Array ids=d["exploration_formations"];
        c.exploration_formations.clear();
        for(int i=0;i<ids.size();i++) {
            if(ids[i].get_type()!=Variant::STRING){error="Expected exploration formation identifier";return false;}
            c.exploration_formations.emplace_back(String(ids[i]).utf8().get_data());
        }
    }
    std::string validation;
    if(!caves::validate(c,validation)){error=validation.c_str();return false;}
    return true;
}
}
