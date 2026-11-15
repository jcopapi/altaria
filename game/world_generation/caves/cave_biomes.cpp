#include "cave_biomes.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
namespace altaria::caves {
static uint64_t hash(uint64_t x) {x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);}
static uint64_t text_hash(const std::string &s) {uint64_t h=1469598103934665603ULL;for(unsigned char c:s){h^=c;h*=1099511628211ULL;}return hash(h);}
static float unit(uint64_t x) {return float(hash(x)>>40)/16777216.f;}
static float smooth(float a,float b,float v) {float t=std::clamp((v-a)/(b-a),0.f,1.f);return t*t*(3-2*t);}
static float noise(float x,float z,uint64_t salt) {
    int ix=int(std::floor(x)),iz=int(std::floor(z));float tx=x-ix,tz=z-iz;
    tx=tx*tx*(3-2*tx);tz=tz*tz*(3-2*tz);
    auto at=[&](int dx,int dz){return unit(hash(salt^uint32_t(ix+dx))^uint32_t(iz+dz));};
    return (at(0,0)*(1-tx)+at(1,0)*tx)*(1-tz)+(at(0,1)*(1-tx)+at(1,1)*tx)*tz;
}
static float fit(ClimateRange r,float v) {
    return smooth(r.min-r.feather,r.min,v)*(1-smooth(r.max,r.max+r.feather,v));
}
static bool valid_range(ClimateRange r) {
    return std::isfinite(r.min) && std::isfinite(r.max) && std::isfinite(r.feather) &&
           r.min>=0 && r.max<=1 && r.min<=r.max && r.feather>=.01f && r.feather<=.5f;
}
static bool resource_id(const std::string &id) {
    auto colon=id.find(':');
    if(id.size()>128 || colon==std::string::npos || colon==0 || colon==id.size()-1 || id.find(':',colon+1)!=std::string::npos)return false;
    return std::all_of(id.begin(),id.end(),[](char c){return (c>='a' && c<='z') || (c>='0' && c<='9') ||
                                                                  c=='_' || c=='-' || c=='.' || c==':' || c=='/';});
}
bool valid_biomes(const BiomeSettings &s,std::string &error) {
    auto fail=[&]{error="Invalid underground biome definition or climate setting";return false;};
    if(!std::isfinite(s.region_scale) || s.region_scale<64 || s.region_scale>4096 ||
       !std::isfinite(s.geology_scale) || s.geology_scale<64 || s.geology_scale>8192 ||
       !std::isfinite(s.transition_ratio) || s.transition_ratio<.01f || s.transition_ratio>.5f ||
       s.definitions.empty() || s.definitions.size()>64 || s.resources.size()>256 || s.formations.size()>64 ||
       s.frozen_rock.empty() || s.desert_rock.empty() || s.rooted_rock.empty())return fail();
    std::unordered_set<std::string> formation_ids;
    for(const auto &f:s.formations)if(!valid_formation(f) || !formation_ids.insert(f.id).second) {
        error="Invalid underground formation definition: "+f.id;return false;
    }
    std::unordered_set<std::string> resource_ids;
    for(const auto &r:s.resources) {
        if(!resource_id(r.id) || !resource_id(r.resource) || !resource_ids.insert(r.id).second ||
           !std::isfinite(r.abundance) || r.abundance<0 || r.abundance>1000 ||
           !std::isfinite(r.region_scale) || r.region_scale<64 || r.region_scale>8192 ||
           !std::isfinite(r.regional_variation) || r.regional_variation<0 || r.regional_variation>1 ||
           !valid_range(r.geology) || !valid_range(r.geothermal)) {
            error="Invalid underground resource abundance rule";return false;
        }
        for(float d:r.depth_weights)if(!std::isfinite(d) || d<0 || d>100) {
            error="Invalid underground resource depth weight";return false;
        }
    }
    std::unordered_set<std::string> ids;bool fallback=false;
    for(const auto &b:s.definitions) {
        if(b.id.empty() || !ids.insert(b.id).second || b.rock.empty() || !std::isfinite(b.weight) || b.weight<=0 || b.weight>1000)return fail();
        bool allowed=false;for(float d:b.depths){if(!std::isfinite(d) || d<0 || d>100)return fail();allowed|=d>0;}
        if(!allowed)return fail();
        for(auto r:{b.heat,b.humidity,b.geology,b.geothermal})if(!valid_range(r))return fail();
        if(!std::isfinite(b.temperature_delta) || std::abs(b.temperature_delta)>100 || !std::isfinite(b.oxygen) || b.oxygen<0 || b.oxygen>1 ||
           !std::isfinite(b.visibility) || b.visibility<.01f || b.visibility>1 || !std::isfinite(b.environmental_damage) || b.environmental_damage<0 || b.environmental_damage>100)return fail();
        for(const auto *v:{&b.formations,&b.entities,&b.resource_rules}) {
            if(v->size()>64)return fail();
            for(const auto &id:*v)if(id.empty())return fail();
        }
        std::unordered_set<std::string> linked;
        for(const auto &id:b.resource_rules)if(!resource_ids.count(id) || !linked.insert(id).second) {
            error="Unknown or duplicate underground resource rule: "+id;return false;
        }
        linked.clear();
        for(const auto &id:b.formations)if(!formation_ids.count(id) || !linked.insert(id).second) {
            error="Unknown or duplicate underground formation: "+id;return false;
        }
        if(b.id==s.fallback) {
            if(b.requires_water)return fail();
            fallback=true;for(float d:b.depths)if(d<=0)return fail();
            for(auto r:{b.heat,b.humidity,b.geology,b.geothermal})if(r.min!=0 || r.max!=1)return fail();
        }
    }
    return fallback ? true : fail();
}
BiomeStage::BiomeStage(BiomeSettings settings,DepthSettings depths,std::array<float,4> t,uint64_t s,uint64_t dimension,
                       const Resolver &resolve,const Resolver &resolve_plant)
    :cfg(std::move(settings)),depth_cfg(depths),targets(t),seed(hash(hash(s)^dimension)) {
    if(!valid_biomes(cfg,failure))return;
    if(!valid_depth(depth_cfg,targets) || !resolve){failure="Invalid underground depth settings or material resolver";return;}
    auto get=[&](const std::string &key,uint16_t &id) {
        if(resolve(key,id))return true;
        failure="Underground rock must reference an existing solid rock block: "+key;return false;
    };
    if(!get(cfg.frozen_rock,frozen) || !get(cfg.desert_rock,desert) || !get(cfg.rooted_rock,rooted))return;
    for(const auto &b:cfg.definitions) {uint16_t id;if(!get(b.rock,id))return;rocks.push_back(id);salts.push_back(text_hash(b.id));}
    std::unordered_map<std::string,uint16_t> resource_indices;
    for(uint16_t i=0;i<cfg.resources.size();i++) {
        resource_indices.emplace(cfg.resources[i].id,i);resource_salts.push_back(text_hash(cfg.resources[i].id));
    }
    biome_resources.resize(cfg.definitions.size());
    for(size_t i=0;i<cfg.definitions.size();i++)for(const auto &id:cfg.definitions[i].resource_rules)
        biome_resources[i].push_back(resource_indices.at(id));
    std::unordered_map<std::string,uint16_t> formation_indices;
    for(uint16_t i=0;i<cfg.formations.size();i++) {
        const auto &f=cfg.formations[i];std::array<uint16_t,2> materials;
        if (f.kind == FormationKind::root && resolve_plant) {
            if (!resolve_plant(f.rock, materials[0]) || !resolve_plant(f.accent, materials[1])) {
                failure = "Root formation must reference a root plant: " + f.rock;
                return;
            }
        } else if(!get(f.rock,materials[0]) || !get(f.accent,materials[1]))return;
        formation_indices.emplace(f.id,i);formation_rocks.push_back(materials);
    }
    biome_formations.resize(cfg.definitions.size());
    for(size_t i=0;i<cfg.definitions.size();i++)for(const auto &id:cfg.definitions[i].formations)
        biome_formations[i].push_back(formation_indices.at(id));
    configured=true;
}
BiomeColumn BiomeStage::column(float x,float z,SurfaceClimate surface,float thermal) const {
    auto finite=[](float &v,float fallback){v=std::isfinite(v)?std::clamp(v,0.f,1.f):fallback;};
    finite(surface.heat,.6f);finite(surface.humidity,.5f);finite(surface.frozen,0);finite(surface.desert,0);finite(surface.lush,0);finite(surface.volcanic,0);
    BiomeColumn c;c.surface=surface;c.geothermal=std::isfinite(thermal)?std::clamp(thermal,0.f,1.f):0;
    if(!configured || !std::isfinite(x) || !std::isfinite(z) || std::abs(x)>1000000 || std::abs(z)>1000000)return c;
    c.heat=noise(x/cfg.geology_scale,z/cfg.geology_scale,seed^0x48454154);
    c.humidity=noise(x/cfg.geology_scale,z/cfg.geology_scale,seed^0x574554);
    c.geology=noise(x/cfg.geology_scale,z/cfg.geology_scale,seed^0x524f434b);
    for(size_t i=0;i<cfg.definitions.size();i++) {
        const auto &b=cfg.definitions[i];float bias=.05f+1.5f*noise(x/cfg.region_scale,z/cfg.region_scale,seed^salts[i]);
        c.spatial_weights[i]=b.weight*fit(b.geology,c.geology)*fit(b.geothermal,c.geothermal)*bias;
    }
    return c;
}
CaveEnvironment BiomeStage::environment(const BiomeColumn &c,float roof,float y,bool water) const {
    CaveEnvironment out;if(!configured || !std::isfinite(roof) || !std::isfinite(y))return out;
    out.depth=classify_depth_validated(roof,y,targets,depth_cfg,c.geothermal);
    float influence=out.depth.surface_influence;
    float heat=std::clamp(c.heat*.65f+c.geothermal*.45f,0.f,1.f);
    heat=std::clamp(heat*(1-influence)+(c.surface.heat+c.surface.volcanic*.35f)*influence,0.f,1.f);
    float humidity=c.humidity*(1-influence)+c.surface.humidity*influence;
    if(water)humidity=std::max(humidity,.85f);
    float first=-1,second=-1,total=0,temperature=0,oxygen=0,visibility=0,damage=0;uint16_t a=0,b=0;
    // Score the actual point's climate, including intermediate surface influence.
    // Cached endpoint winners would erase biomes with narrow intermediate ranges.
    // Environmental properties include every weight, even at triple junctions.
    for(uint16_t i=0;i<cfg.definitions.size();i++) {
        if(c.spatial_weights[i]<=0)continue;
        const auto &def=cfg.definitions[i];float affinity=0;
        if(def.requires_water && !water)continue;
        for(int j=0;j<4;j++)affinity+=def.depths[j]*out.depth.weights[j];
        float score=c.spatial_weights[i]*affinity*fit(def.heat,heat)*fit(def.humidity,humidity);
        total+=score;temperature+=score*def.temperature_delta;oxygen+=score*def.oxygen;
        visibility+=score*def.visibility;damage+=score*def.environmental_damage;
        if(score>first){second=first;b=a;first=score;a=i;}else if(score>second){second=score;b=i;}
    }
    float blend=first>0 && second>0 ? .5f*smooth(1-cfg.transition_ratio,1,second/first) : 0;
    out.biomes={a,b,1-blend};
    out.temperature_c=(8+c.heat*14+c.geothermal*48)*(1-influence)+(c.surface.heat*44-12)*influence;
    out.humidity=humidity;
    out.sedimentary=influence*c.surface.desert;
    out.rooted=influence*c.surface.lush*out.humidity;
    if(total>0) {
        out.temperature_c+=temperature/total;out.oxygen=oxygen/total;
        out.visibility=visibility/total;out.damage_per_second=damage/total;
    }
    out.frozen=influence*c.surface.frozen*(1-smooth(25,45,out.temperature_c));
    return out;
}
uint16_t BiomeStage::material(const BiomeColumn &c,float roof,int x,int y,int z,bool water) const {
    if(!configured)return 0;
    auto env=environment(c,roof,float(y),water);
    uint64_t h=hash(hash(hash(seed^uint32_t(int(std::floor(x/2.0))))^uint32_t(int(std::floor(y/2.0))))^uint32_t(int(std::floor(z/2.0))));
    float choice=unit(h);uint16_t index=choice<env.biomes.primary_weight?env.biomes.primary:env.biomes.secondary;
    float modifier=unit(h^0x53555246414345);
    if(modifier<env.frozen*.85f)return frozen;
    if(modifier<env.sedimentary*.9f)return desert;
    if(modifier<env.rooted*.65f)return rooted;
    return rocks[index];
}
float BiomeStage::resource_abundance(std::string_view resource,const BiomeColumn &c,float roof,
                                    float x,float y,float z,bool water) const {
    if(!configured || cfg.resources.empty() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
       !std::isfinite(roof) || std::abs(x)>1000000 || std::abs(z)>1000000)return 0;
    auto env=environment(c,roof,y,water);
    auto evaluate=[&](uint16_t biome) {
        float total=0;
        for(auto i:biome_resources[biome]) {
            const auto &rule=cfg.resources[i];
            if(resource!=rule.resource || (rule.requires_water && !water))continue;
            float depth=0;
            for(size_t j=0;j<4;j++)depth+=env.depth.weights[j]*rule.depth_weights[j];
            float patch=noise(x/rule.region_scale,z/rule.region_scale,seed^resource_salts[i])*2-1;
            total+=rule.abundance*depth*fit(rule.geology,c.geology)*fit(rule.geothermal,c.geothermal)*
                   (1+patch*rule.regional_variation);
        }
        return total;
    };
    auto b=env.biomes;
    return evaluate(b.primary)*b.primary_weight+
           (b.primary_weight<1?evaluate(b.secondary)*(1-b.primary_weight):0);
}
void BiomeStage::plan_formations(const Chamber &room,const BiomeColumn &column,float roof,std::vector<Formation> &out) const {
    if(!configured || cfg.formations.empty())return;
    auto env=environment(column,roof,room.center[1]);
    std::array<bool,64> selected{};
    for(auto i:biome_formations[env.biomes.primary])selected[i]=true;
    if(env.biomes.primary_weight<.85f)for(auto i:biome_formations[env.biomes.secondary])selected[i]=true;
    std::vector<Formation> local;
    for(size_t i=0;i<cfg.formations.size();i++) {
        const auto &f=cfg.formations[i];
        if(!selected[i] && f.archetype.empty() && f.min_frozen==0 && f.min_rooted==0 && f.min_sedimentary==0)continue;
        if((!f.archetype.empty() && f.archetype!=room.archetype) ||
           env.frozen<f.min_frozen || env.depth.geothermal<f.min_geothermal ||
           env.rooted<f.min_rooted || env.sedimentary<f.min_sedimentary)continue;
        for(int n=0;n<f.count;n++) {
            auto detail=plan_formation(room,f,n,formation_rocks[i][0],formation_rocks[i][1]);
            detail.rule=uint16_t(i);
            if(detail.rock)local.push_back(detail);
        }
    }
    std::sort(local.begin(),local.end(),[](const Formation &a,const Formation &b){return a.id<b.id;});
    out.insert(out.end(),local.begin(),local.begin()+std::min(size_t(12),local.size()));
}
}
