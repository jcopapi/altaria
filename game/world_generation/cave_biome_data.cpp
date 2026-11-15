#include "world_data.h"
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <cmath>
namespace altaria {
using namespace godot;
bool parse_cave_biomes(const Dictionary &d,caves::BiomeSettings &s,String &error) {
    auto number=[&](const Dictionary &source,const char *key,float &out,float lo,float hi) {
        Variant v=source.get(key,out);
        if((v.get_type()!=Variant::INT && v.get_type()!=Variant::FLOAT) || !std::isfinite(double(v)) || double(v)<lo || double(v)>hi) {
            error=String("Invalid underground biome number: ")+String(key);return false;
        }
        out=double(v);return true;
    };
    auto text=[&](const Dictionary &source,const char *key,std::string &out) {
        Variant v=source.get(key,String(out.c_str()));
        if(v.get_type()!=Variant::STRING || String(v).is_empty()){error=String("Expected underground biome identifier: ")+String(key);return false;}
        out=String(v).utf8().get_data();return true;
    };
    auto array_numbers=[&](const Dictionary &source,const char *key,float *target,int count,float lo,float hi) {
        if(!source.has(key))return true;
        if(source[key].get_type()!=Variant::ARRAY || Array(source[key]).size()!=count){error=String("Invalid underground biome vector: ")+String(key);return false;}
        Array values=source[key];
        for(int i=0;i<count;i++) {
            Variant v=values[i];if((v.get_type()!=Variant::INT && v.get_type()!=Variant::FLOAT) || !std::isfinite(double(v)) || double(v)<lo || double(v)>hi){error="Invalid underground biome vector component";return false;}
            target[i]=double(v);
        }
        return true;
    };
    if(!number(d,"region_scale",s.region_scale,64,4096) || !number(d,"geology_scale",s.geology_scale,64,8192) ||
       !number(d,"transition_ratio",s.transition_ratio,.01,.5) || !text(d,"fallback",s.fallback) ||
       !text(d,"frozen_rock",s.frozen_rock) || !text(d,"desert_rock",s.desert_rock) || !text(d,"rooted_rock",s.rooted_rock))return false;
    if(d.has("resource_rules")) {
        if(d["resource_rules"].get_type()!=Variant::ARRAY || Array(d["resource_rules"]).size()>256) {
            error="Expected up to 256 underground resource rules";return false;
        }
        s.resources.clear();Array rules=d["resource_rules"];
        for(int i=0;i<rules.size();i++) {
            if(rules[i].get_type()!=Variant::DICTIONARY){error="Expected underground resource rule object";return false;}
            Dictionary row=rules[i];caves::ResourceRule r;
            if(!text(row,"id",r.id) || !text(row,"resource",r.resource) ||
               !number(row,"abundance",r.abundance,0,1000) || !number(row,"region_scale",r.region_scale,64,8192) ||
               !number(row,"regional_variation",r.regional_variation,0,1) ||
               !array_numbers(row,"depth_weights",r.depth_weights.data(),4,0,100))return false;
            for(auto pair:{std::pair{"geology",&r.geology},std::pair{"geothermal",&r.geothermal}}) {
                auto &range=*pair.second;float values[]={range.min,range.max};
                if(!array_numbers(row,pair.first,values,2,0,1) || !number(row,"range_feather",range.feather,.01,.5))return false;
                range.min=values[0];range.max=values[1];
            }
            if(row.has("requires_water") && row["requires_water"].get_type()!=Variant::BOOL) {
                error="Expected boolean resource requires_water";return false;
            }
            r.requires_water=row.get("requires_water",false);
            s.resources.push_back(std::move(r));
        }
    }
    if(d.has("formations")) {
        if(d["formations"].get_type()!=Variant::ARRAY || Array(d["formations"]).size()>64) {
            error="Expected up to 64 underground formation rules";return false;
        }
        s.formations.clear();Array rules=d["formations"];
        for(int i=0;i<rules.size();i++) {
            if(rules[i].get_type()!=Variant::DICTIONARY){error="Expected underground formation object";return false;}
            Dictionary row=rules[i];caves::FormationRule r;std::string kind,water="any";float count=r.count;
            if(!text(row,"id",r.id) || !text(row,"kind",kind) || !text(row,"rock",r.rock))return false;
            r.accent=r.rock;
            if(!text(row,"accent",r.accent) || !text(row,"water",water) ||
               (row.has("archetype") && !text(row,"archetype",r.archetype)) ||
               !number(row,"count",count,1,6) || !number(row,"chance",r.chance,0,1) ||
               !number(row,"min_frozen",r.min_frozen,0,1) || !number(row,"min_geothermal",r.min_geothermal,0,1) ||
               !number(row,"min_rooted",r.min_rooted,0,1) || !number(row,"min_sedimentary",r.min_sedimentary,0,1) ||
               !array_numbers(row,"radius",r.radius.data(),2,1,12) ||
               !array_numbers(row,"height",r.height.data(),2,2,40))return false;
            if(!caves::formation_kind(kind,r.kind) || std::floor(count)!=count) {
                error="Invalid formation kind or count";return false;
            }
            if (row.has("ceiling") && (r.kind != caves::FormationKind::root ||
                                       row["ceiling"].get_type() != Variant::BOOL)) {
                error = "Expected a boolean ceiling setting for a root formation";
                return false;
            }
            r.ceiling = row.get("ceiling", false);
            if(water=="dry")r.water=caves::FormationWater::dry;
            else if(water=="wet")r.water=caves::FormationWater::wet;
            else if(water!="any"){error="Expected formation water setting: any, dry or wet";return false;}
            r.count=int(count);s.formations.push_back(std::move(r));
        }
    }
    if(d.has("definitions")) {
        if(d["definitions"].get_type()!=Variant::ARRAY || Array(d["definitions"]).is_empty() || Array(d["definitions"]).size()>64){error="Expected 1..64 underground biome definitions";return false;}
        Array defs=d["definitions"];s.definitions.clear();
        for(int i=0;i<defs.size();i++) {
            if(defs[i].get_type()!=Variant::DICTIONARY){error="Expected underground biome object";return false;}
            Dictionary row=defs[i];caves::BiomeDefinition b;
            if(row.has("requires_water") && row["requires_water"].get_type()!=Variant::BOOL){error="Expected boolean underground biome requires_water";return false;}
            b.requires_water=row.get("requires_water",false);
            if(!row.has("id") || !text(row,"id",b.id) || !text(row,"name",b.name) || !text(row,"rock",b.rock) ||
               !array_numbers(row,"depth_weights",b.depths.data(),4,0,100) || !number(row,"weight",b.weight,.0001,1000) ||
               !number(row,"temperature_delta_c",b.temperature_delta,-100,100) || !number(row,"oxygen_factor",b.oxygen,0,1) ||
               !number(row,"visibility_factor",b.visibility,.01,1) || !number(row,"damage_per_second",b.environmental_damage,0,100))return false;
            const char *keys[]={"heat","humidity","geology","geothermal"};
            caves::ClimateRange *ranges[]={&b.heat,&b.humidity,&b.geology,&b.geothermal};
            for(int j=0;j<4;j++) {
                float values[]={ranges[j]->min,ranges[j]->max};
                if(!array_numbers(row,keys[j],values,2,0,1) || !number(row,"range_feather",ranges[j]->feather,.01,.5))return false;
                ranges[j]->min=values[0];ranges[j]->max=values[1];
            }
            const char *lists[]={"formations","entities","resource_rules"};
            std::vector<std::string> *outputs[]={&b.formations,&b.entities,&b.resource_rules};
            for(int j=0;j<3;j++)if(row.has(lists[j])) {
                if(row[lists[j]].get_type()!=Variant::ARRAY || Array(row[lists[j]]).size()>64){error="Invalid underground extension list";return false;}
                Array entries=row[lists[j]];
                for(int k=0;k<entries.size();k++) {
                    if(entries[k].get_type()!=Variant::STRING || String(entries[k]).is_empty()){error="Invalid underground extension identifier";return false;}
                    outputs[j]->push_back(String(entries[k]).utf8().get_data());
                }
            }
            s.definitions.push_back(std::move(b));
        }
    }
    std::string validation;if(!caves::valid_biomes(s,validation)){error=validation.c_str();return false;}
    return true;
}
}
