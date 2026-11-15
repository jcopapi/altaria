shader_type spatial;
render_mode cull_disabled, diffuse_toon, specular_toon, world_vertex_coords;
#include "res://game/world_generation/shaders/lod_opacity.gdshaderinc"
#include "res://game/world_generation/shaders/block_tint.gdshaderinc"
#include "res://game/world_generation/shaders/grass_noise.gdshaderinc"

uniform vec3 sky_fill = vec3(0.0);
uniform bool fullbright = false;
uniform sampler2DArray block_textures : filter_nearest_mipmap, repeat_enable;
uniform sampler2D grass_texture : source_color, filter_nearest, repeat_disable;
uniform float wind_time = 0.0;
uniform float wind_strength = 0.18;
uniform vec2 wind_direction = vec2(0.9, 0.4);
uniform vec3 vegetation_center = vec3(0.0);
uniform float fade_start = 38.0;
uniform float fade_end = 48.0;
uniform vec3 hit_box_min = vec3(0.0);
uniform vec3 hit_box_max = vec3(-1.0);
uniform float hit_flash = 0.0;
uniform vec3 hit_root_shift = vec3(0.0);
uniform vec3 hit_blade_scale = vec3(1.0);

// SimpleGrassTextured interactive globals
global uniform vec3 sgt_player_position;
global uniform vec3 sgt_player_mov;
global uniform sampler2D sgt_normal_displacement;
global uniform sampler2D sgt_motion_texture;
global uniform vec3 sgt_wind_direction;
global uniform float sgt_wind_strength;
global uniform vec3 sgt_wind_movement;
global uniform sampler2D sgt_wind_pattern;

varying vec3 root_position;
varying vec3 target_hsv;
varying float preserve;
varying float brightness;
varying float seed;
varying float range_weight;
varying float top_layer;
varying float apply_tint;
varying float sky_exposure;
varying float hit_active;
varying vec2 grass_uv;
varying vec2 ground_uv;

float get_motion_value(sampler2D tex, vec2 uv) {
    vec4 color = textureLod(tex, uv, 0.0);
    return round(color.r * 65280.0) + round(color.g * 255.0);
}

void vertex() {
    root_position = MODEL_MATRIX[3].xyz;
    float root_distance = distance(root_position, vegetation_center);
    range_weight = 1.0 - smoothstep(fade_start, fade_end, root_distance);
    float tip = 1.0 - UV.y;
    seed = INSTANCE_CUSTOM.y;
    grass_uv = UV;
    ground_uv = VERTEX.xz;

    // SimpleGrassTextured wind simulation
    float text_wind = texture(sgt_wind_pattern, (root_position.xz * 0.005) - sgt_wind_movement.xz).r;
    float lev = tip * tip;
    VERTEX.x += text_wind * lev * sgt_wind_strength * sgt_wind_direction.x * 2.0;
    VERTEX.z += text_wind * lev * sgt_wind_strength * sgt_wind_direction.z * 2.0;
    VERTEX.x += sin(seed + sgt_wind_movement.y) * lev * 0.1;
    VERTEX.z += cos(seed + sgt_wind_movement.y) * lev * 0.1;

    // SimpleGrassTextured interactive player displacement
    float node_x = root_position.x / 50.0;
    float node_z = root_position.z / 50.0;
    vec2 uv_disp = vec2(clamp(node_x - (sgt_player_position.x - 0.5), 0.0, 1.0),
                        clamp(node_z - (sgt_player_position.z - 0.5), 0.0, 1.0));
    vec3 norm_mov = textureLod(sgt_normal_displacement, uv_disp - sgt_player_mov.xz, 0.0).rgb;
    float time = get_motion_value(sgt_motion_texture, uv_disp) / 65535.0;
    norm_mov.xy = (norm_mov.xy * 2.0 - 1.0) * 3.0 * lev;
    VERTEX.xz += norm_mov.xy * (sin(time * time * 15.0) / 2.0) * (1.0 - time);
    VERTEX.xz += norm_mov.xy * 0.5;
    VERTEX.y -= tip * norm_mov.b * 0.3;

    // Base procedural breeze
    float breeze = sin(wind_time * 2.1 + dot(root_position.xz, vec2(0.38, 0.21)) + seed * 0.15)
                 + 0.35 * sin(wind_time * 3.8 + root_position.z * 1.3);
    VERTEX.xz += normalize(wind_direction) * breeze * wind_strength * tip;

    hit_active = (all(greaterThanEqual(root_position, hit_box_min)) &&
                  all(lessThanEqual(root_position, hit_box_max))) ? 1.0 : 0.0;
    if (hit_active > 0.5) {
        VERTEX = root_position + (VERTEX - root_position) * hit_blade_scale + hit_root_shift;
    }

    // Distant culling collapse
    VERTEX = mix(root_position, VERTEX, step(0.00001, range_weight));
    target_hsv = rgb_to_hsv(COLOR.rgb);
    preserve = COLOR.a;
    brightness = INSTANCE_CUSTOM.x;
    top_layer = INSTANCE_CUSTOM.z;
    float packed = floor(INSTANCE_CUSTOM.w + 0.5);
    apply_tint = mod(packed, 2.0);
    sky_exposure = floor(packed / 2.0) / 15.0;
}

void fragment() {
    if (!lod_pixel_visible(FRAGCOORD.xy))
        discard;
    if (range_weight <= 0.0) discard;

    vec4 grass_tex = texture(grass_texture, grass_uv);
    if (grass_tex.a < 0.1) discard;

    // Project onto the same world-space texture and color noise as the support.
    // The blade sprite contributes its silhouette only.
    vec4 base = texture(block_textures, vec3(ground_uv, top_layer));
    if (apply_tint > 0.5) base = tint_block(base, target_hsv, preserve, brightness);
    vec3 color = base.rgb * grass_variation(ground_uv);

    float dither = grass_hash(floor(root_position.xz * 16.0) + floor(UV * 8.0));
    if (dither > range_weight) discard;

    // Both sides of a blade use the supporting top face's lighting.
    NORMAL = normalize((VIEW_MATRIX * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
    vec3 linear = color_to_linear(color);
    ALBEDO = fullbright ? vec3(0) : linear;
    float sky = sky_exposure * (1.4 - 0.4 * sky_exposure);
    EMISSION = fullbright ? linear : linear * sky_fill * sky;
    if (hit_active > 0.5 && hit_flash > 0.001) {
        float flash = clamp(hit_flash, 0.0, 1.0);
        ALBEDO = mix(ALBEDO, vec3(1.0), flash);
        EMISSION = mix(EMISSION, vec3(1.0), flash);
    }
    BACKLIGHT = vec3(0.0);
    ROUGHNESS = 1.0;
    SPECULAR = 0.1;
}
