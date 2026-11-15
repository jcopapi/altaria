shader_type spatial;
render_mode diffuse_toon, specular_toon, cull_disabled, depth_draw_never, shadows_disabled;
#include "res://game/world_generation/shaders/lod_opacity.gdshaderinc"
#include "res://game/world_generation/shaders/terrain_edges.gdshaderinc"
#include "res://game/world_generation/shaders/grass_noise.gdshaderinc"

// Stylized water: depth absorption from glassy aqua to deep blue, soft moving
// ripples, sky reflection at grazing angles, faint caustics in the shallows
// and a thin foam line where water meets land. Seen from below, the surface
// shows a bright window toward the sky. Patterns snap to 1/8-block texels.
uniform sampler2D terrain_depth : hint_depth_texture, filter_nearest;
uniform vec3 sky_fill;
uniform bool fullbright = false;
uniform float wind_time = 0.0;
uniform vec3 shallow_color : source_color = vec3(0.30, 0.80, 0.82);
uniform vec3 mid_color : source_color = vec3(0.14, 0.60, 0.78);
uniform vec3 deep_color : source_color = vec3(0.10, 0.42, 0.66);
// Grass-style banded colour regions: water drifts between these two tints.
uniform vec3 band_tint_a : source_color = vec3(0.86, 1.06, 0.92);
uniform vec3 band_tint_b : source_color = vec3(0.92, 0.94, 1.10);
uniform float water_band_scale = 0.07;
uniform float water_band_strength = 0.75;
uniform vec3 abyss_color : source_color = vec3(0.05, 0.10, 0.30);
uniform vec3 foam_color : source_color = vec3(0.90, 0.97, 1.0);
uniform vec3 caustic_color : source_color = vec3(0.70, 1.0, 0.96);
uniform vec3 reflection_color : source_color = vec3(0.46, 0.68, 0.96);

varying vec3 world_position;
varying vec3 world_normal;
varying vec2 tile_xz;
varying float flags;

void vertex() {
    world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
    world_normal = normalize(MODEL_NORMAL_MATRIX * NORMAL);
    tile_xz = VERTEX.xz;
    flags = floor(CUSTOM1.x + 0.5);
}

vec2 water_hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}
// Thin bright lines along cell borders, like light refracted on a seabed.
float caustics(vec2 p, float t) {
    vec2 cell = floor(p), f = fract(p);
    float best = 8.0, second = 8.0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++) {
            vec2 o = vec2(float(x), float(y));
            vec2 h = water_hash2(cell + o);
            vec2 point = o + 0.5 + 0.42 * sin(t * 0.8 + 6.2831 * h);
            float d = length(point - f);
            if (d < best) { second = best; best = d; }
            else if (d < second) { second = d; }
        }
    return 1.0 - smoothstep(0.0, 0.09, second - best);
}

float water_noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = water_hash2(i).x, b = water_hash2(i + vec2(1, 0)).x;
    float c = water_hash2(i + vec2(0, 1)).x, d = water_hash2(i + vec2(1, 1)).x;
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
// Height of two drifting wave layers; its slope bends the surface normal.
float water_waves(vec2 p, float t) {
    return water_noise(p * 0.55 + vec2(t * 0.22, t * 0.13)) * 0.6
         + water_noise(p * 1.3 - vec2(t * 0.31, -t * 0.27)) * 0.4;
}

void fragment() {
    if (lod_opacity < 0.0) {
        discard;
    }
    float packed = floor(flags + 0.5);
    if (terrain_edge_replaced(packed, tile_xz)) {
        discard;
    }
    float raw_depth = textureLod(terrain_depth, SCREEN_UV, 0.0).r;
    vec4 bottom = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, raw_depth, 1.0);
    float thickness = raw_depth < 0.000001 ? 40.0 : max(0.0, length(bottom.xyz / bottom.w) - length(VERTEX));
    bool top = abs(world_normal.y) > 0.5;
    vec2 plane = top ? world_position.xz : vec2(world_position.x + world_position.z, world_position.y);
    vec2 p = (floor(plane * 8.0) + 0.5) / 8.0;
    float t = wind_time;
    float sky = mod(floor(packed / 64.0), 16.0) / 15.0;
    // Ripples, caustics and crests only up close: far away they shimmer into
    // pixel noise, so distant water settles into calm, clean colour.
    float near = 1.0 - smoothstep(14.0, 46.0, length(VERTEX));

    // Ripple normal from the wave height slope.
    vec3 n = world_normal;
    if (top && near > 0.001) {
        float h = water_waves(p, t);
        float hx = water_waves(p + vec2(0.25, 0.0), t) - h;
        float hz = water_waves(p + vec2(0.0, 0.25), t) - h;
        n = normalize(n + vec3(-hx, 0.0, -hz) * 1.6 * near);
    }
    NORMAL = normalize(mat3(VIEW_MATRIX) * (FRONT_FACING ? n : -n));
    float fresnel = pow(1.0 - clamp(abs(dot(NORMAL, VIEW)), 0.0, 1.0), 4.0);

    if (!FRONT_FACING && top) {
        // From below: a bright window toward the sky straight up, and the
        // surface turns to a dark mirror at grazing angles.
        float window = smoothstep(0.55, 0.85, abs(dot(NORMAL, VIEW)));
        float shimmer = water_waves(p * 1.7, t * 1.3);
        vec3 under = mix(mid_color * 0.75, shallow_color * 1.25 + vec3(0.10, 0.14, 0.12), window);
        under *= 0.9 + 0.25 * shimmer;
        ALBEDO = fullbright ? vec3(0.0) : under;
        EMISSION = fullbright ? under : under * (0.3 + 0.7 * sky) * (0.55 + 0.45 * window);
        ROUGHNESS = 0.2;
        SPECULAR = 0.0;
        ALPHA = mix(0.9, 0.5, window);
    } else {
        // Light absorption: colour deepens smoothly with the water it crosses.
        // Gentle absorption: water stays light and clear until it is genuinely deep.
        float depth01 = 1.0 - exp(-thickness * 0.055);
        float d = mix(depth01, floor(depth01 * 6.0) / 6.0, 0.25 * near);
        vec3 color = d < 0.35 ? mix(shallow_color, mid_color, d / 0.35)
                   : d < 0.8 ? mix(mid_color, deep_color, (d - 0.35) / 0.45)
                             : mix(deep_color, abyss_color, (d - 0.8) / 0.2);
        // Same stepped world noise as the grass: soft colour patches in 5 bands
        // (greener vs bluer, lighter vs darker), on 1/4-block texels.
        vec2 bp = floor(world_position.xz * 4.0) / 4.0 * water_band_scale;
        float bn = grass_noise(bp + vec2(31.0, 7.0)) * 0.65 + grass_noise(bp * 2.7 + vec2(3.0, 41.0)) * 0.35;
        float band = floor(bn * 5.0) / 4.0;
        vec3 tint = mix(band_tint_a, band_tint_b, band);
        float bright = 1.0 + (band - 0.5) * 0.10;
        color = mix(color, color * tint * bright, water_band_strength * (1.0 - d * 0.6));

        // Faint caustics that fade with depth.
        float shallow = 1.0 - smoothstep(0.3, 3.5, thickness);
        float c = near > 0.001 ? caustics(p * 0.6 + vec2(t * 0.05, -t * 0.03), t) * shallow * near : 0.0;
        color = mix(color, caustic_color, c * 0.22);

        // A thin, soft foam line at the shore, breathing with the waves.
        float wobble = water_noise(p * 0.9 + vec2(t * 0.35, -t * 0.25));
        float foam = top ? (1.0 - smoothstep(0.08, 0.32 + 0.22 * wobble, thickness)) * 0.75 : 0.0;
        color = mix(color, foam_color, foam);

        // Sky reflection at grazing angles; light crests catch a little more.
        float crest = top && near > 0.001 ? smoothstep(0.55, 0.85, water_waves(p, t)) * 0.12 * near : 0.0;
        vec3 reflected = reflection_color * (0.35 + 0.65 * sky);
        color = mix(color, reflected, clamp(fresnel * 0.45 + crest * sky, 0.0, 0.6));

        // Deep water turns to darkness: past ~14 m the bottom is never seen,
        // so the mesher can leave the deep seabed out entirely.
        float abyss = smoothstep(9.0, 15.0, thickness);
        color = mix(color, abyss_color * 0.45, abyss * 0.85);
        ALBEDO = fullbright ? vec3(0.0) : color;
        EMISSION = fullbright ? color : color * sky_fill * sky * 0.85 + caustic_color * c * 0.06 * sky;
        ROUGHNESS = 0.12;
        // Toon specular paints hard white dots on every ripple; the sky reflection
        // above gives the surface its shine instead.
        SPECULAR = 0.0;
        ALPHA = clamp(mix(0.42, 0.88, d) + fresnel * 0.2 + foam + abyss, 0.0, 1.0);
    }
}
