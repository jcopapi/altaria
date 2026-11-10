#include "world_data.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/project_settings.hpp>
namespace altaria {
using namespace godot;
Dictionary read_json(const String &path, String &error) {
    auto f = FileAccess::open(path, FileAccess::READ);
    if (f.is_null()) {
        error = "Cannot read " + path;
        return {};
    }
    Ref<JSON> json;
    json.instantiate();
    if (json->parse(f->get_as_text()) != OK || json->get_data().get_type() != Variant::DICTIONARY) {
        error = "Invalid JSON: " + path + " (" + json->get_error_message() + ")";
        return {};
    }
    return json->get_data();
}
Dictionary load_generation_data(String &error) {
    auto data = read_json("res://game/world_generation/data/generation.json", error);
    if (error.is_empty() && int(data.get("generator_version", 1)) >= 2)
        data["biomes"] = read_json("res://game/world_generation/data/biomes.json", error);
    if (error.is_empty())
        data["environment"] = read_json("res://game/world_generation/data/environment.json", error);
    return data;
}
Dictionary load_block_data(String &error) {
    auto data = read_json("res://game/world_generation/data/blocks.json", error);
    if (error.is_empty()) {
        auto overlays = read_json("res://game/world_generation/data/overlays.json", error);
        data["overlays"] = overlays.get("definitions", Array());
    }
    return data;
}
bool write_json_atomic(const String &path, const Dictionary &value, String &error) {
    auto absolute = ProjectSettings::get_singleton()->globalize_path(path).utf8();
    std::string target = absolute.get_data(), tmp = target + ".tmp";
    auto text = JSON::stringify(value, "  ").utf8();
    std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
    file.write(text.get_data(), text.length());
    file.flush();
    if (!file) {
        error = "Could not write world metadata";
        return false;
    }
    file.close();
    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        error = "Could not commit world metadata: " + String(ec.message().c_str());
        return false;
    }
    return true;
}
bool parse_environment(const Dictionary &d, EnvironmentSettings &settings, String &error) {
#define LOAD_ENV(field, low, high)                                                                           \
    {                                                                                                        \
        double v = d.get(#field, settings.field);                                                            \
        if (!std::isfinite(v) || v < low || v > high) {                                                      \
            error = "Invalid environment setting: " #field;                                                  \
            return false;                                                                                    \
        }                                                                                                    \
        settings.field = decltype(settings.field)(v);                                                        \
    }
    settings.outline_enabled = d.get("outline_enabled", true);
    settings.pixelation_enabled = d.get("pixelation_enabled", true);
    settings.godrays_enabled = d.get("godrays_enabled", true);
    settings.godrays_volumetric = d.get("godrays_volumetric", true);
    LOAD_ENV(godrays_density, 0, .01);
    LOAD_ENV(godrays_length, 16, 192);
    LOAD_ENV(godrays_light_energy, 0, 64);
    LOAD_ENV(outline_blacklist_distance, 4, 1024);
    LOAD_ENV(godrays_strength, 0, 2);
    LOAD_ENV(godrays_reach, .2, 1);
    LOAD_ENV(godrays_samples, 8, 32);
    LOAD_ENV(fog_transition, 0, 10);
    LOAD_ENV(outline_width, .5, 3);
    LOAD_ENV(outline_strength, 0, 1);
    LOAD_ENV(outline_highlight, 0, 1);
    LOAD_ENV(outline_distance_start, 0, 2048);
    LOAD_ENV(outline_distance_end, 0, 8192);
    LOAD_ENV(outline_min_depth_gap, 1, 64);
    LOAD_ENV(outline_min_feature_size, 2, 32);
    LOAD_ENV(outline_shoreline_strength, 0, 1);
    if(settings.outline_distance_end<settings.outline_distance_start) {error="Invalid outline distance fade";return false;}
    LOAD_ENV(render_pixel_size, 1, 4);
    LOAD_ENV(pixelation_max, 1, 16);
    LOAD_ENV(pixelation_start, 1, 2048);
    LOAD_ENV(pixelation_end, 64, 8192);
    LOAD_ENV(sky_biome_strength, 0, 1);
    if (settings.pixelation_end <= settings.pixelation_start) {
        error = "pixelation_end must exceed pixelation_start";
        return false;
    }
    LOAD_ENV(sun_energy, 0, 5);
    LOAD_ENV(moon_energy, 0, 2);
    LOAD_ENV(day_ambient, 0, 2);
    LOAD_ENV(night_ambient, 0, 2);
    LOAD_ENV(day_seconds, 60, 86400);
    LOAD_ENV(start_hour, 0, 24);
    LOAD_ENV(lapse_c_per_m, 0, .1);
    LOAD_ENV(rain_delta, -50, 50);
    LOAD_ENV(storm_delta, -50, 50);
    LOAD_ENV(wind_chill, 0, 20);
    LOAD_ENV(wind, 0, 3);
    LOAD_ENV(wind_strength, 0, 1);
    LOAD_ENV(grass_noise_scale, .001, 10);
    LOAD_ENV(grass_noise_strength, 0, 1);
    LOAD_ENV(cloud_coverage, 0, 1);
    LOAD_ENV(cloud_pixel_size, .001, .1);
    LOAD_ENV(max_bodies, 1, 128);
    LOAD_ENV(max_parts, 1, 256);
    LOAD_ENV(physics_distance, 32, 128);
    LOAD_ENV(shadow_distance, 32, 512);
#undef LOAD_ENV
    return true;
}
bool parse_settings(const Dictionary &d, Settings &s, String &error) {
#define LOAD(field)                                                                                          \
    do {                                                                                                     \
        if (d.has(#field)) {                                                                                 \
            if (d[#field].get_type() != Variant::INT && d[#field].get_type() != Variant::FLOAT) {            \
                error = "Expected numeric setting: " #field;                                                 \
                return false;                                                                                \
            }                                                                                                \
            double v = double(d[#field]);                                                                    \
            if (!std::isfinite(v) || std::abs(v) > 1000000) {                                                \
                error = "Setting out of numeric range: " #field;                                             \
                return false;                                                                                \
            }                                                                                                \
            s.field = decltype(s.field)(v);                                                                  \
        }                                                                                                    \
    } while (false)
    LOAD(generator_version);
    LOAD(min_y);
    LOAD(max_y);
    LOAD(sea_level);
    LOAD(base_height);
    LOAD(continental_amplitude);
    LOAD(mountain_amplitude);
    LOAD(continental_scale);
    LOAD(mountain_scale);
    LOAD(erosion_scale);
    LOAD(detail_scale);
    LOAD(detail_amplitude);
    LOAD(warp_scale);
    LOAD(warp_amplitude);
    LOAD(valley_depth);
    LOAD(valley_width);
    LOAD(cliff_strength);
    LOAD(terrace_strength);
    LOAD(plains_threshold);
    LOAD(snow_height);
    LOAD(density_scale);
    LOAD(terrace_height);
    LOAD(ridge_power);
    LOAD(octaves);
    LOAD(workers);
    LOAD(upload_budget);
    LOAD(max_pending);
    LOAD(near_distance);
    LOAD(middle_distance);
    LOAD(coarse_distance);
    LOAD(view_distance);
    LOAD(autosave_seconds);
    LOAD(distant_distance);
    LOAD(horizon_distance);
    LOAD(fog_start);
    LOAD(biome_scale);
    LOAD(biome_blend);
    LOAD(altitude_cooling);
    LOAD(cold_start_altitude);
    LOAD(snow_temperature);
    LOAD(mountain_region_scale);
    LOAD(mountain_threshold);
    LOAD(mountain_blend);
    LOAD(tree_spacing);
    LOAD(tree_lod_max);
    LOAD(grass_tufts);
    LOAD(grass_height);
    LOAD(grass_height_variation);
    LOAD(grass_tall_chance);
    LOAD(grass_tall_multiplier);
    LOAD(grass_render_distance);
    LOAD(grass_fade_distance);
    LOAD(bush_render_distance);
    LOAD(upload_time_budget_ms);
    s.bush_block = String(d.get("bush_block", String("altaria:leaves"))).utf8().get_data();
    s.vegetation = d.get("vegetation", false);
    s.surface_shapes = d.get("surface_shapes", false);
    // Retire both experimental generators, including snapshots in existing saves.
    if (bool(d.get("surface_ramps", false)) || bool(d.get("terrain_smoothing", false)))
        s.surface_shapes = true;
    if(s.upload_time_budget_ms<.25f || s.upload_time_budget_ms>8) {
        error="Invalid upload budget"; return false;
    }
    if (s.tree_lod_max < 0 || s.tree_lod_max > 2 || s.grass_tufts < 1 || s.grass_tufts > 16 ||
        s.tree_spacing < 8 || s.tree_spacing > 64 || s.grass_height < .1f || s.grass_height > 2 ||
        s.grass_height_variation < 0 || s.grass_height_variation > .9f || s.grass_tall_chance < 0 ||
        s.grass_tall_chance > 1 || s.grass_tall_multiplier < 1 || s.grass_tall_multiplier > 3 ||
        s.grass_render_distance < 8 || s.grass_render_distance > 96 || s.grass_fade_distance < 1 ||
        s.grass_fade_distance >= s.grass_render_distance || s.bush_render_distance < 8 ||
        s.bush_render_distance > 128 || s.bush_block.empty()) {
        error = "Invalid vegetation settings";
        return false;
    }
#undef LOAD
    if ((s.generator_version != 1 && s.generator_version != 2) || s.min_y % 16 || s.max_y % 16 ||
        s.min_y < -512 || s.max_y > 1024 || s.max_y - s.min_y < 64 || s.max_y - s.min_y > 768 ||
        s.sea_level <= s.min_y || s.sea_level >= s.max_y || s.octaves < 2 || s.octaves > 8 || s.workers < 1 ||
        s.workers > 8 || s.upload_budget < 1 || s.upload_budget > 8 || s.max_pending < 4 ||
        s.max_pending > 64 || s.near_distance < 32 || s.middle_distance <= s.near_distance ||
        s.coarse_distance <= s.middle_distance || s.view_distance < s.coarse_distance + 256 ||
        s.view_distance > 8192 || s.autosave_seconds < 2 || s.continental_scale <= 0 ||
        s.mountain_scale <= 0 || s.erosion_scale <= 0 || s.detail_scale <= 0 || s.warp_scale <= 0 ||
        s.density_scale <= 0 || s.terrace_height <= 0 || s.ridge_power < .1f || s.ridge_power > 8 ||
        s.valley_width <= 0 || s.plains_threshold <= .18f || s.plains_threshold > 1 || s.cliff_strength < 0 ||
        s.cliff_strength > 64 || s.terrace_strength < 0 || s.terrace_strength > 1 || s.warp_amplitude < 0 ||
        s.warp_amplitude > 512) {
        error = "Generation settings out of range. See game/world_generation/README.md.";
        return false;
    }
    if (s.generator_version >= 2 &&
        (s.distant_distance <= s.coarse_distance || s.horizon_distance <= s.distant_distance ||
         s.view_distance < s.horizon_distance + 1024 || s.fog_start <= s.coarse_distance ||
         s.fog_start >= s.view_distance * .94f || s.mountain_region_scale <= 0 || s.mountain_blend <= 0 ||
         s.mountain_threshold < 0 || s.mountain_threshold >= 1)) {
        error = "Invalid horizon or mountain-region settings";
        return false;
    }
    if (d.has("biomes")) {
        if (d["biomes"].get_type() != Variant::DICTIONARY) {
            error = "Expected biome settings object";
            return false;
        }
        Dictionary b = d["biomes"];
        s.testing_biomes = b.get("testing_layout", true);
        s.biome_scale = b.get("scale", 260);
        s.biome_blend = b.get("blend_width", 48);
        s.altitude_cooling = b.get("altitude_cooling", .004);
        s.cold_start_altitude = b.get("cold_start_altitude", 110);
        s.snow_temperature = b.get("snow_temperature", .22);
        if (!std::isfinite(s.biome_scale) || !std::isfinite(s.biome_blend) ||
            !std::isfinite(s.altitude_cooling) || !std::isfinite(s.cold_start_altitude) ||
            !std::isfinite(s.snow_temperature) || s.biome_scale < 64 || s.biome_scale > 20000 ||
            s.biome_blend < 1 || s.biome_blend > s.biome_scale || s.altitude_cooling < 0 ||
            s.altitude_cooling > .05 || s.snow_temperature < 0 || s.snow_temperature > 1) {
            error = "Invalid biome scale, blend or temperature setting";
            return false;
        }
        Array definitions = b.get("definitions", Array());
        if (definitions.size() < 5 || definitions.size() > 64) {
            error = "Define between 5 and 64 biomes";
            return false;
        }
        s.biomes.clear();
        std::unordered_map<std::string, bool> keys;
        for (int i = 0; i < definitions.size(); i++) {
            if (definitions[i].get_type() != Variant::DICTIONARY) {
                error = "Invalid biome object";
                return false;
            }
            Dictionary bd = definitions[i];
            BiomeDef out;
            out.key = String(bd.get("key", String())).utf8().get_data();
            out.name = String(bd.get("name", String())).utf8().get_data();
            Array color = bd.get("grass_tint", Array());
            if (out.key.empty() || keys.count(out.key) || color.size() != 3) {
                error = "Biome needs a unique key and RGB grass_tint";
                return false;
            }
            keys[out.key] = true;
            out.grass = {float(color[0]), float(color[1]), float(color[2])};
            out.temperature = bd.get("temperature", .6);
            out.humidity = bd.get("humidity", .5);
            out.terrain_multiplier = bd.get("terrain_multiplier", 1);
            out.mountain_multiplier = bd.get("mountain_multiplier", 1);
            out.pebbles = bd.get("pebble_density", .1);
            out.preserve_saturation = bd.get("preserve_saturation", .65);
            out.brightness = bd.get("brightness", 1.35);
            out.desert = bd.get("desert", false);
            out.snow = bd.get("snow", false);
            out.day_c = bd.get("day_temperature_c", out.snow ? -4 : out.desert ? 38 : 22);
            out.night_c = bd.get("night_temperature_c", out.snow ? -16 : out.desert ? 8 : 10);
            out.tree_density = bd.get("tree_density", out.desert ? 0 : .45);
            auto fog_defaults = default_biomes();
            for (const auto &candidate : fog_defaults)
                if (candidate.key == out.key) {
                    out.fog_day = candidate.fog_day;
                    out.fog_night = candidate.fog_night;
                    out.fog_start = candidate.fog_start;
                    out.fog_end = candidate.fog_end;
                }
            auto fog_color = [&](const char *name, RGB &color) {
                if (!bd.has(name))
                    return true;
                Array values = bd[name];
                if (values.size() != 3)
                    return false;
                for (int j = 0; j < 3; j++)
                    if (!std::isfinite(double(values[j])) || double(values[j]) < 0 || double(values[j]) > 1)
                        return false;
                color = {float(values[0]), float(values[1]), float(values[2])};
                return true;
            };
            out.fog_start = bd.get("fog_start", out.fog_start);
            out.fog_end = bd.get("fog_end", out.fog_end);
            if (!fog_color("fog_day", out.fog_day) || !fog_color("fog_night", out.fog_night) ||
                !std::isfinite(out.fog_start) || !std::isfinite(out.fog_end) || out.fog_start < 0 ||
                out.fog_end <= out.fog_start + 1 || out.fog_end > 20000) {
                error = "Invalid biome fog";
                return false;
            }
            out.bush_density = bd.get("bush_density", 0);
            out.grass_density = bd.get("grass_density", 0);
            if (!std::isfinite(out.day_c) || !std::isfinite(out.night_c) || out.day_c < -100 ||
                out.day_c > 100 || out.night_c < -100 || out.night_c > 100 ||
                !std::isfinite(out.tree_density) || out.tree_density < 0 || out.tree_density > 1 ||
                !std::isfinite(out.grass_density) || out.grass_density < 0 || out.grass_density > 1 ||
                !std::isfinite(out.bush_density) || out.bush_density < 0 || out.bush_density > .25f) {
                error = "Invalid biome climate or vegetation";
                return false;
            }
            float values[] = {out.grass.r,
                              out.grass.g,
                              out.grass.b,
                              out.temperature,
                              out.humidity,
                              out.terrain_multiplier,
                              out.mountain_multiplier,
                              out.pebbles,
                              out.preserve_saturation,
                              out.brightness};
            for (float v : values)
                if (!std::isfinite(v) || v < 0 || v > 4) {
                    error = "Invalid biome numeric value";
                    return false;
                }
            if (out.grass.r > 1 || out.grass.g > 1 || out.grass.b > 1 || out.temperature > 1 ||
                out.humidity > 1 || out.pebbles > 1 || out.preserve_saturation > 1 || out.brightness > 2) {
                error = "Biome color, temperature or tint out of range";
                return false;
            }
            s.biomes.push_back(out);
        }
        bool has_snow = false;
        for (auto &b : s.biomes)
            has_snow |= b.snow;
        if (!has_snow) {
            error = "At least one cold biome must have snow=true";
            return false;
        }
    }
    if (d.has("environment")) {
        if (d["environment"].get_type() != Variant::DICTIONARY) {
            error = "Expected environment object";
            return false;
        }
        EnvironmentSettings environment;
        if (!parse_environment(d["environment"], environment, error))
            return false;
    }
    return true;
}
bool parse_registry(const Dictionary &d, Registry &r, String &error) {
    if (d.get("blocks", Variant()).get_type() != Variant::ARRAY) {
        error = "Missing blocks array";
        return false;
    }
    Array blocks = d["blocks"];
    for (int i = 0; i < blocks.size(); i++) {
        if (blocks[i].get_type() != Variant::DICTIONARY) {
            error = "Expected block object";
            return false;
        }
        Dictionary b = blocks[i];
        int id = int(b.get("id", -1));
        Array color = b.get("color", Array());
        String key = b.get("key", String());
        if (id < 0 || id > 4095 || color.size() != 3 || !key.contains(":")) {
            error = "Invalid block definition";
            return false;
        }
        BlockDef def;
        def.id = BlockId(id);
        def.key = key.utf8().get_data();
        def.name = String(b.get("name", key)).utf8().get_data();
        def.name_pt = String(b.get("name_pt", String(def.name.c_str()))).utf8().get_data();
        def.color = {float(color[0]), float(color[1]), float(color[2])};
        def.texture_layer = b.get("texture_layer", 0);
        def.overlay = b.get("overlay", 0);
        def.transition_overlay = b.get("transition_overlay", 0);
        def.solid = b.get("solid", true);
        def.water = b.get("water", false);
        def.placeable = b.get("placeable", true);
        def.wood = b.get("wood", false);
        def.foliage = b.get("foliage", false);
        def.tintable = b.get("tintable", false);
        def.mass = b.get("mass", 20);
        def.thermal_delta = b.get("thermal_delta_c", 0);
        def.thermal_radius = b.get("thermal_radius", 0);
        if (!std::isfinite(def.thermal_delta) || std::abs(def.thermal_delta) > 100 ||
            !std::isfinite(def.thermal_radius) || def.thermal_radius < 0 || def.thermal_radius > 8 ||
            !std::isfinite(def.mass) || def.mass < .01f || def.mass > 1000) {
            error = "Invalid block thermal or mass value";
            return false;
        }
        if (!std::isfinite(def.color.r) || !std::isfinite(def.color.g) || !std::isfinite(def.color.b) ||
            def.color.r < 0 || def.color.r > 1 || def.color.g < 0 || def.color.g > 1 || def.color.b < 0 ||
            def.color.b > 1) {
            error = "Block color must be RGB values from 0 to 1";
            return false;
        }
        std::string e;
        if (!r.add(std::move(def), e)) {
            error = String(e.c_str());
            return false;
        }
    }
    Array blacklist = d.get("outline_distance_blacklist", Array());
    for(int i=0;i<blacklist.size();i++) {
        String key=blacklist[i];
        auto it=r.keys.find(key.utf8().get_data());
        if(it==r.keys.end()) { error="Unknown outline blacklist block: "+key; return false; }
        r.blocks[it->second].distant_outline=false;
    }
    Array textures = d.get("textures", Array());
    if (textures.is_empty())
        r.textures.push_back({"", "solid", {1, 1, 1}});
    for (int i = 0; i < textures.size(); i++) {
        if (textures[i].get_type() != Variant::DICTIONARY) {
            error = "Invalid texture slot";
            return false;
        }
        Dictionary t = textures[i];
        TextureDef out;
        out.path = String(t.get("path", String())).utf8().get_data();
        out.generator = String(t.get("generator", String("solid"))).utf8().get_data();
        Array color = t.get("color", Array());
        if (color.size() == 3)
            out.color = {float(color[0]), float(color[1]), float(color[2])};
        else
            out.color = {1, 1, 1};
        r.textures.push_back(out);
    }
    r.overlays.resize(1);
    r.overlays[0].id = 0;
    r.overlays[0].key = "altaria:none";
    r.overlay_keys["altaria:none"] = 0;
    Array overlays = d.get("overlays", Array());
    for (int i = 0; i < overlays.size(); i++) {
        if (overlays[i].get_type() != Variant::DICTIONARY) {
            error = "Invalid overlay object";
            return false;
        }
        Dictionary o = overlays[i];
        int id = o.get("id", -1);
        String key = o.get("key", String());
        if (id < 1 || id > 4095 || key.is_empty() || r.overlay_keys.count(key.utf8().get_data()) ||
            (id < int(r.overlays.size()) && !r.overlays[id].key.empty())) {
            error = "Duplicate overlay id/key";
            return false;
        }
        OverlayDef out;
        out.id = id;
        out.key = key.utf8().get_data();
        out.name = String(o.get("name", key)).utf8().get_data();
        out.top = o.get("top", -1);
        out.side = o.get("side", -1);
        out.bottom = o.get("bottom", -1);
        out.decal = String(o.get("kind", String("surface"))) == "decal";
        out.tintable = o.get("tintable", true);
        out.density = o.get("density", .1);
        for (int layer : {out.top, out.side, out.bottom})
            if (layer < -1 || layer >= int(r.textures.size())) {
                error = "Overlay references an invalid texture slot";
                return false;
            }
        if (id >= int(r.overlays.size()))
            r.overlays.resize(id + 1);
        r.overlay_keys[out.key] = id;
        r.overlays[id] = out;
    }
    for (auto &b : r.blocks)
        if (!b.key.empty() &&
            (b.texture_layer < 0 || b.texture_layer >= int(r.textures.size()) || b.overlay < 0 ||
             b.overlay >= int(r.overlays.size()) || (b.overlay > 0 && r.overlays[b.overlay].key.empty()) ||
             b.transition_overlay < 0 || b.transition_overlay >= int(r.overlays.size()) ||
             (b.transition_overlay > 0 && r.overlays[b.transition_overlay].key.empty()))) {
            error = "Block references unknown texture/overlay";
            return false;
        }
    Dictionary roles = d.get("roles", Dictionary());
#define ROLE(field)                                                                                          \
    do {                                                                                                     \
        String key = roles.get(#field, "altaria:" #field);                                                   \
        if (!r.keys.count(key.utf8().get_data())) {                                                          \
            error = "Missing terrain role: " #field;                                                         \
            return false;                                                                                    \
        }                                                                                                    \
        r.field = r.find(key.utf8().get_data());                                                             \
    } while (false)
    ROLE(air);
    ROLE(stone);
    ROLE(dirt);
    ROLE(grass);
    ROLE(sand);
    ROLE(water);
    ROLE(snow);
    ROLE(bedrock);
#undef ROLE
    if (r.air != 0 || r.get(r.air).solid || r.get(r.air).water || !r.get(r.water).water ||
        r.get(r.water).solid || !r.get(r.stone).solid || !r.get(r.bedrock).solid) {
        error = "Air must be id 0 and empty; water must be non-solid; stone/bedrock must be solid";
        return false;
    }
    return true;
}
} // namespace altaria
