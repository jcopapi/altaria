shader_type spatial;
render_mode diffuse_toon, specular_toon;
#include "res://game/world_generation/shaders/lod_opacity.gdshaderinc"
#include "res://game/world_generation/shaders/terrain_edges.gdshaderinc"
#include "res://game/world_generation/shaders/block_tint.gdshaderinc"
#include "res://game/world_generation/shaders/grass_noise.gdshaderinc"
uniform vec3 sky_fill = vec3(0.0);
uniform vec3 underworld_glow = vec3(0.0);
uniform float underworld_top = -8192.0;
uniform float underworld_lava = -8192.0;
uniform bool fullbright = false;
uniform bool local_textures = false;
uniform bool distance_limited = false;
uniform vec3 vegetation_center = vec3(0.0);
uniform float vegetation_limit = 64.0;
uniform float wind_time = 0.0;
uniform float wind_strength = 0.18;
uniform vec3 hit_box_min = vec3(0.0);
uniform vec3 hit_box_max = vec3(0.0);
uniform float hit_flash = 0.0;
uniform vec3 leaf_fade_center = vec3(0.0);
uniform float leaf_fade_opacity = 1.0;
instance uniform vec3 texture_anchor = vec3(0.0);
// Deliberately sample the stored sRGB texels: HSV tinting is done in the artist's
// color space, then converted to linear once before the lighting calculation.
uniform sampler2DArray block_textures : filter_nearest_mipmap, repeat_enable;
varying vec3 world_position;
varying vec3 texture_position;
varying vec3 texture_normal;
varying vec3 target_hsv;
varying vec4 base_brightness;
varying vec4 tint_flags;
varying vec2 layers;
varying float preserve;
varying float side_top;
varying vec2 leaf_uv;
varying vec2 tile_xz;
void vertex() {
    world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
    texture_position = local_textures ? VERTEX + texture_anchor : world_position;
    texture_normal = local_textures ? NORMAL : MODEL_NORMAL_MATRIX * NORMAL;
    target_hsv = rgb_to_hsv(COLOR.rgb);
    preserve = COLOR.a;
    base_brightness = CUSTOM0;
    tint_flags = vec4(CUSTOM2.x, CUSTOM2.y, CUSTOM1.x, CUSTOM1.y);
    layers = vec2(floor(CUSTOM2.z * 255.0 + 0.5), floor(CUSTOM2.w * 255.0 + 0.5) - 1.0);
    side_top=CUSTOM1.y+(local_textures?texture_anchor.y:0.0);
    leaf_uv = UV;
    tile_xz = VERTEX.xz;
    // A small visual overlap seals T-junctions between
    // greedy faces of unequal lengths without moving collision geometry.
    float mesh_flags = floor(CUSTOM1.x + 0.5);
    // The hardware rounds projected vertices to a pixel grid. A slightly
    // larger margin at distance keeps sub-pixel T-junctions covered in flight.
    float margin = clamp(distance(world_position, CAMERA_POSITION_WORLD) * 0.00036, 0.018, 0.20);
    bool solid = mod(mesh_flags, 4.0) < 1.5;
    if (mod(floor(mesh_flags / 8192.0), 2.0) > 0.5) {
        vec2 overlap = (UV - 0.5) * margin;
        if (abs(NORMAL.y) > 0.5) { VERTEX.xz += overlap.yx; }
        else if (abs(NORMAL.x) > 0.5) {
            VERTEX.z += overlap.y;
            if (solid) { VERTEX.y += overlap.x; }
        } else {
            VERTEX.x += overlap.x;
            if (solid) { VERTEX.y += overlap.y; }
        }
    } else if (mod(floor(mesh_flags / 16384.0), 2.0) > 0.5) {
        float overlap = (UV.x - 0.5) * margin;
        if (abs(NORMAL.x) > 0.5) { VERTEX.z += overlap; }
        else { VERTEX.x += overlap; }
        if (solid) { VERTEX.y += (UV.y - 0.5) * margin; }
    }
    if (mod(floor(CUSTOM1.x / 8.0), 2.0) > 0.5) {
        float breeze = sin(wind_time * 2.1 + dot(world_position.xz, vec2(0.38, 0.21)))
                     + 0.35 * sin(wind_time * 3.8 + world_position.z * 1.3);
        vec3 sway = vec3(0.91, 0.0, 0.40) * breeze * wind_strength;
        VERTEX += (inverse(MODEL_MATRIX) * vec4(sway, 0.0)).xyz;
    }
}
// Pixel-snapped Voronoi edges: glowing cracks in the underworld rock.
vec2 ember_hash(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}
float ember_veins(vec2 q) {
    vec2 cell = floor(q), local = fract(q);
    float first = 8.0, second = 8.0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++) {
            vec2 g = vec2(float(x), float(y));
            vec2 r = g + ember_hash(cell + g) - local;
            float d = dot(r, r);
            if (d < first) { second = first; first = d; } else if (d < second) { second = d; }
        }
    return 1.0 - smoothstep(0.0, 0.07, sqrt(second) - sqrt(first));
}
void fragment() {
    float flags = floor(tint_flags.z + 0.5);
    bool vegetation = mod(floor(flags / 32768.0), 2.0) > 0.5;
    if (vegetation ? !lod_pixel_visible(FRAGCOORD.xy) : lod_opacity < 0.0)
        discard;
    if(distance_limited){
        float weight=1.0-smoothstep(vegetation_limit-8.0,vegetation_limit,distance(world_position,vegetation_center));
        if(weight<=0.0||grass_hash(floor(world_position.xz*16.0)+vec2(floor(world_position.y*16.0)))>weight)discard;
    }
    vec2 tex_uv;
    if (abs(texture_normal.y) > 0.5) { tex_uv = texture_position.xz; }
    else if (abs(texture_normal.x) > 0.5) { tex_uv = vec2(texture_position.z, -texture_position.y+side_top); }
    else { tex_uv = vec2(texture_position.x, -texture_position.y+side_top); }
    // Retain texture scale and mipmaps at every LOD. Pixel size is controlled by
    // the scene SubViewport, avoiding coarse texel jumps at chunk boundaries.
    // Interpolation can place an encoded integer just below its boundary.
    // Decode the nearest integer before testing material and feature bits.
    // Seam-only faces (far heightfield borders) show exactly where the
    // ordinary border is replaced by a LOD seam.
    bool seam_only = mod(floor(flags / 2097152.0), 2.0) > 0.5;
    if (terrain_edge_replaced(flags, tile_xz) != seam_only)
        discard;
    bool leaf_card = mod(floor(flags / 32.0), 2.0) > 0.5;
    bool leaf = mod(floor(flags / 8.0), 2.0) > 0.5;
    bool face_uv = mod(floor(flags / 524288.0), 2.0) > 0.5;
    vec4 original = texture(block_textures, vec3(leaf_card || face_uv ? leaf_uv : tex_uv, layers.x));
    if ((leaf_card || mod(floor(flags / 4.0), 2.0) > 0.5) && original.a < 0.42) {
        discard;
    }
    if (leaf && !leaf_card) {
        original.rgb = mix(vec3(0.35, 0.53, 0.25), original.rgb, original.a);
    }
    if (mod(floor(flags / 16.0), 2.0) > 0.5) {
        vec3 average = textureLod(block_textures, vec3(tex_uv, layers.x), 4.0).rgb;
        vec3 detail = texture(block_textures, vec3(tex_uv, layers.x), 0.75).rgb;
        original.rgb = mix(average, detail, 0.48);
        original.rgb *= mix(1.0, grass_variation(texture_position.xz + texture_position.y * vec2(0.73, -0.41)), 0.40);
        float heat = smoothstep(45.0, 420.0, -world_position.y);
        original.rgb *= mix(vec3(1.0), vec3(1.16, .48, .42), heat);
    }
    float material_type = mod(flags, 4.0);
    original.rgb *= base_brightness.rgb;
    if (tint_flags.x > 0.5) { original = tint_block(original, target_hsv, preserve, base_brightness.a); }
    if (layers.y >= 0.0) {
        vec4 overlay = texture(block_textures, vec3(tex_uv, layers.y));
        if (tint_flags.y > 0.5 || tint_flags.x > 0.5) {
            overlay = tint_block(overlay, target_hsv, preserve, base_brightness.a);
        }
        if (tint_flags.y > 0.5) { overlay.rgb *= grass_variation(texture_position.xz); }
        original.rgb = mix(original.rgb, overlay.rgb, overlay.a);
    } else if (tint_flags.y > 0.5) {
        original.rgb *= grass_variation(texture_position.xz);
    }
    if (mod(floor(flags / 65536.0), 2.0) > 0.5) {
        // World-space patches stay on the canopy as tiles change LOD.
        // Fade each band once its cells become smaller than a screen pixel.
        float footprint = max(length(dFdx(tex_uv)), length(dFdy(tex_uv)));
        float patches = grass_noise(tex_uv * 1.15) - 0.5;
        float flecks = grass_hash(floor(tex_uv * 3.0)) - 0.5;
        original.rgb *= 1.0
            + patches * 0.36 * (1.0 - smoothstep(0.6, 1.5, footprint))
            + flecks * 0.13 * (1.0 - smoothstep(0.15, 0.45, footprint));
    }
    // Rock warms toward red and soot-dark as it nears the underworld.
    if (material_type < 1.5 && !vegetation && !leaf) {
        float depth_heat = smoothstep(-120.0, -440.0, world_position.y);
        original.rgb *= mix(vec3(1.0), vec3(1.10, 0.84, 0.78), depth_heat);
    }
    vec3 color = color_to_linear(original.rgb);
    float voxel_hit_flash = 0.0;
    if (hit_flash > 0.001 && !local_textures && !vegetation && !leaf) {
        vec3 p_hit = world_position - texture_normal * 0.003;
        if (all(greaterThanEqual(p_hit, hit_box_min)) && all(lessThanEqual(p_hit, hit_box_max))) {
            voxel_hit_flash = clamp(hit_flash, 0.0, 1.0);
            color = mix(color, vec3(1.0), voxel_hit_flash * 0.85);
        }
    }
    ALBEDO = fullbright ? vec3(0.0) : color;
    if (leaf_fade_opacity < 0.995 && !local_textures && (leaf || leaf_card)) {
        float dist_player = distance(world_position, leaf_fade_center);
        float zone = 1.0 - smoothstep(1.8, 4.4, dist_player);
        float near_eye = smoothstep(0.35, 1.35, dist_player);
        float target_op = mix(1.0, leaf_fade_opacity * mix(0.28, 1.0, near_eye), zone);
        if (target_op < 0.995) {
            vec3 wp = floor(world_position * 12.0 + 0.01);
            int bx = int(clamp(floor(mod(floor(FRAGCOORD.x) + wp.x + wp.z, 4.0)), 0.0, 3.0));
            int by = int(clamp(floor(mod(floor(FRAGCOORD.y) + wp.y + wp.z * 2.0, 4.0)), 0.0, 3.0));
            const float bayer[16] = float[16](
                0.0625, 0.5625, 0.1875, 0.6875,
                0.8125, 0.3125, 0.9375, 0.4375,
                0.2500, 0.7500, 0.1250, 0.6250,
                1.0000, 0.5000, 0.8750, 0.3750
            );
            if (bayer[by * 4 + bx] > target_op)
                discard;
        }
    }
    float sky = mod(floor(flags / 64.0), 16.0) / 15.0;
    // Daylit ground is lit as if every face pointed up: neither the sky nor
    // the sun shades block sides, so the surface reads as one rounded form
    // at every LOD. Faces in caves (no daylight) keep their real normals.
    // Trees, proxies and liquids keep their own shading.
    float daylit = (!leaf && !vegetation && material_type < 1.5) ? smoothstep(0.0, 0.25, sky) : 0.0;
    if (daylit > 0.0) {
        NORMAL = normalize(mix(NORMAL, (VIEW_MATRIX * vec4(0.0, 1.0, 0.0, 0.0)).xyz, daylit));
        if (abs(texture_normal.y) < 0.5) sky = mix(sky, 1.0, daylit);
    }
    sky *= 1.4 - 0.4 * sky;
    float facing_sky = mix(0.72 + 0.28 * max(0.0, texture_normal.y), 1.0, daylit);
    EMISSION = fullbright ? color : material_type > 2.5 ? color * 0.85 : color * sky_fill * sky * facing_sky;
    if (voxel_hit_flash > 0.0) {
        EMISSION = mix(EMISSION, vec3(1.0), voxel_hit_flash * 0.90);
    }
    if (!fullbright && material_type < 2.5 && world_position.y < underworld_top) {
        float below = smoothstep(underworld_top, underworld_top - 40.0, world_position.y);
        float near_lava = smoothstep(underworld_lava + 70.0, underworld_lava, world_position.y);
        // Rock that faces down toward the lava catches more of its light.
        float facing_lava = 0.65 + 0.35 * max(0.0, -texture_normal.y);
        EMISSION += color * underworld_glow * below * (0.45 + 0.55 * near_lava) * facing_lava;
        // Embers glow in the cracks of the roof and the rock closest to it.
        vec3 snapped = floor(texture_position * 8.0) / 8.0;
        float ceiling = smoothstep(0.3, 0.9, -texture_normal.y);
        // Thin cracks alias into stripes far away; let them fade with distance.
        float near = 1.0 - smoothstep(24.0, 60.0, distance(world_position, CAMERA_POSITION_WORLD));
        float veins = ember_veins(snapped.xz * 0.09 + snapped.y * 0.05);
        float pulse = 0.75 + 0.25 * sin(wind_time * 1.3 + snapped.x * 0.21 + snapped.z * 0.17);
        EMISSION += vec3(1.0, 0.34, 0.07) * veins * below * near * (0.08 + 0.9 * ceiling) * pulse;
    }
    // Reserved bands identify water boundaries and blacklisted vegetation in
    // the existing normal buffer; no extra camera or mask pass.
    bool no_outline = mod(floor(flags / 1048576.0), 2.0) > 0.5;
    ROUGHNESS = no_outline ? 1.0 : face_uv ? .885 : material_type > 2.5 ? .94 : material_type > 1.5 ? .68 : material_type > .5 ? .82 : .94;
    SPECULAR = 0.1;
}
