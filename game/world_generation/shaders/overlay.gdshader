shader_type spatial;
render_mode cull_disabled, diffuse_toon;
#include "res://game/world_generation/shaders/lod_opacity.gdshaderinc"
#include "res://game/world_generation/shaders/block_tint.gdshaderinc"
uniform bool fullbright = false;
uniform sampler2DArray block_textures : filter_nearest_mipmap, repeat_disable;
varying vec3 target_hsv;
varying float preserve;
varying float brightness;
varying float layer;
varying float painted;
void vertex() {
    target_hsv = rgb_to_hsv(COLOR.rgb);
    preserve = COLOR.a;
    brightness = CUSTOM0.a;
    layer = floor(CUSTOM2.z * 255.0 + 0.5);
    painted = CUSTOM2.x;
}
void fragment() {
    if (lod_opacity < 0.0)
        discard;
    vec4 original = texture(block_textures, vec3(UV, layer));
    if (painted > 0.5) { original = tint_block(original, target_hsv, preserve, brightness); }
    vec3 color = color_to_linear(original.rgb);
    ALBEDO = fullbright ? vec3(0.0) : color;
    EMISSION = fullbright ? color : vec3(0.0);
    ALPHA = original.a;
    ALPHA_SCISSOR_THRESHOLD = 0.45;
    ROUGHNESS = 1.0;
    SPECULAR = 0.0;
}
