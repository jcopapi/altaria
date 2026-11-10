shader_type spatial;
render_mode diffuse_toon, specular_toon;
#include "res://game/world_generation/shaders/block_tint.gdshaderinc"
#include "res://game/world_generation/shaders/grass_noise.gdshaderinc"
uniform bool fullbright = false;
uniform bool local_textures = false;
uniform bool distance_limited = false;
uniform vec3 vegetation_center = vec3(0.0);
uniform float vegetation_limit = 64.0;
instance uniform float terrain_lod = 0.0;
instance uniform vec3 texture_anchor = vec3(0.0);
uniform float horizon_start = 4500.0;
uniform float horizon_end = 5775.0;
uniform vec4 horizon_color : source_color = vec4(0.63, 0.74, 0.82, 1.0);
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
void vertex() {
    world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
    texture_position = local_textures ? VERTEX + texture_anchor : world_position;
    texture_normal = local_textures ? NORMAL : MODEL_NORMAL_MATRIX * NORMAL;
    target_hsv = rgb_to_hsv(COLOR.rgb);
    preserve = COLOR.a;
    base_brightness = CUSTOM0;
    tint_flags = CUSTOM1;
    layers = UV2;
    side_top=CUSTOM1.w+(local_textures?texture_anchor.y:0.0);
}
void fragment() {
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
    vec4 original = texture(block_textures, vec3(tex_uv, layers.x));
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
    vec3 color = color_to_linear(original.rgb);
    ALBEDO = fullbright ? vec3(0.0) : color;
    EMISSION = fullbright ? color : vec3(0.0);
    // Radial fog also covers the edges of the frustum before terrain ends.
    FOG = vec4(horizon_color.rgb, smoothstep(horizon_start, horizon_end, distance(world_position, CAMERA_POSITION_WORLD)));
    // Reserved bands identify water boundaries and blacklisted vegetation in
    // the existing normal buffer; no extra camera or mask pass.
    ROUGHNESS = tint_flags.z > 1.5 ? .68 : tint_flags.z > .5 ? .82 : .94;
    SPECULAR = 0.1;
}
