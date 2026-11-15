shader_type spatial;
render_mode unshaded;
#include "res://game/world_generation/shaders/lod_opacity.gdshaderinc"
#include "res://game/world_generation/shaders/terrain_edges.gdshaderinc"

// Stylized molten lava: dark crust plates drifting over glowing cracks,
// pulsing hot spots and downward streaks on lavafalls. Pattern is snapped to
// 1/8-block texels so it matches the pixel-art world.
uniform bool fullbright = false;
uniform float wind_time = 0.0;
uniform vec3 crust_color : source_color = vec3(0.16, 0.035, 0.03);
uniform vec3 ember_color : source_color = vec3(0.95, 0.20, 0.03);
uniform vec3 molten_color : source_color = vec3(1.0, 0.56, 0.08);
uniform vec3 core_color : source_color = vec3(1.0, 0.80, 0.30);
uniform float glow = 1.35;

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

vec2 lava_hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}
float lava_noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = lava_hash2(i).x, b = lava_hash2(i + vec2(1, 0)).x;
    float c = lava_hash2(i + vec2(0, 1)).x, d = lava_hash2(i + vec2(1, 1)).x;
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
// Distance to the nearest cell border: 0 on cracks, larger inside a plate.
float crust_cells(vec2 p, float t) {
    vec2 cell = floor(p), f = fract(p);
    float best = 8.0, second = 8.0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++) {
            vec2 o = vec2(float(x), float(y));
            vec2 h = lava_hash2(cell + o);
            vec2 point = o + 0.5 + 0.38 * sin(t * 0.35 + 6.2831 * h);
            float d = length(point - f);
            if (d < best) { second = best; best = d; }
            else if (d < second) { second = d; }
        }
    return second - best;
}

void fragment() {
    if (lod_opacity < 0.0) {
        discard;
    }
    float packed = floor(flags + 0.5);
    if (terrain_edge_replaced(packed, tile_xz)) {
        discard;
    }
    bool top = abs(world_normal.y) > 0.5;
    vec2 plane = top ? world_position.xz
                     : vec2(dot(world_position.xz, vec2(-world_normal.z, world_normal.x)), world_position.y);
    // Pixel-art texels.
    vec2 p = (floor(plane * 8.0) + 0.5) / 8.0;
    float t = wind_time;
    vec2 flow = top ? vec2(t * 0.07, t * 0.045) : vec2(0.0, t * 0.9);
    float warp = lava_noise(p * 0.45 + t * 0.05) * 1.6;
    float cells = crust_cells(p * 0.55 + flow * 0.55 + warp, t);
    float heat = lava_noise(p * 0.23 - flow * 0.3 + 3.0);
    float pulse = 0.5 + 0.5 * sin(t * 1.3 + heat * 9.0);
    // Cracks glow; plate interiors cool to crust. Hot regions melt the crust.
    float crack = 1.0 - smoothstep(0.03, 0.22 + 0.18 * heat, cells);
    float melt = smoothstep(0.72, 0.98, heat + 0.12 * pulse) * 0.7;
    float hot = clamp(max(crack, melt), 0.0, 1.0);
    if (!top) {
        // Lavafalls: bright streaks running down.
        float streak = lava_noise(vec2(p.x * 2.2, p.y * 0.35 + t * 1.4));
        hot = clamp(max(hot * 0.6, smoothstep(0.35, 0.8, streak)), 0.0, 1.0);
    }
    // Toon steps of temperature.
    float band = floor(hot * 4.0 + 0.5) / 4.0;
    vec3 color = band < 0.13 ? crust_color
               : band < 0.38 ? ember_color
               : band < 0.63 ? mix(ember_color, molten_color, 0.6)
               : band < 0.88 ? molten_color : core_color;
    // Sparks of crust over molten areas keep it from looking flat.
    float speck = step(0.93, lava_hash2(floor(p * 4.0) + floor(t * 2.0)).x) * (1.0 - band);
    color = mix(color, crust_color * 1.6, speck * 0.5);
    float deep_heat = smoothstep(110.0, 480.0, -world_position.y);
    color *= mix(vec3(1.0), vec3(1.14, .34, .38), deep_heat);
    // Unshaded with a capped brightness: lights and bloom never wash it out to white.
    float intensity = mix(0.55, glow, band * band) * (0.92 + 0.08 * pulse);
    ALBEDO = fullbright ? color : min(color * intensity, vec3(1.6, 1.15, 0.6));
}
