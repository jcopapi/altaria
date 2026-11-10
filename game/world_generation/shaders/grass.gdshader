shader_type spatial;
render_mode cull_disabled, diffuse_toon, world_vertex_coords;
#include "res://game/world_generation/shaders/block_tint.gdshaderinc"
#include "res://game/world_generation/shaders/grass_noise.gdshaderinc"
uniform bool fullbright=false;
uniform sampler2DArray block_textures : filter_nearest_mipmap,repeat_enable;
uniform float wind_time=0.0;
uniform float wind_strength=0.18;
uniform vec2 wind_direction=vec2(0.9,0.4);
uniform vec3 vegetation_center=vec3(0.0);
uniform float fade_start=38.0;
uniform float fade_end=48.0;
varying vec3 root_position;
varying vec3 target_hsv;
varying float preserve;
varying float brightness;
varying float seed;
varying float range_weight;
varying float top_layer;
varying float apply_tint;
void vertex() {
    // MultiMesh supplies each tuft's transform/color/custom values; one shared
    // eight-vertex mesh replaces thousands of duplicated blade meshes.
    root_position=MODEL_MATRIX[3].xyz;
    float root_distance=distance(root_position,vegetation_center);
    range_weight=1.0-smoothstep(fade_start,fade_end,root_distance);
    float tip=1.0-UV.y;
    seed=INSTANCE_CUSTOM.y;
    float breeze=sin(wind_time*2.1+dot(root_position.xz,vec2(0.38,0.21))+seed*.15)
                 +0.35*sin(wind_time*3.8+root_position.z*1.3);
    VERTEX.xz+=normalize(wind_direction)*breeze*wind_strength*tip*tip;
    // Fully distant tufts collapse before rasterization. The CPU also culls and
    // releases whole spatial batches outside the bounded area around the player.
    VERTEX=mix(root_position,VERTEX,step(0.00001,range_weight));
    target_hsv=rgb_to_hsv(COLOR.rgb);
    preserve=COLOR.a;
    brightness=INSTANCE_CUSTOM.x;
    top_layer=INSTANCE_CUSTOM.z;apply_tint=INSTANCE_CUSTOM.w;
}
void fragment() {
    if(range_weight<=0.0)discard;
    vec2 pixel=floor(UV*vec2(16.0,16.0))/vec2(16.0,16.0);
    float blade=floor(pixel.x*3.0);
    float blade_height=.66+.34*grass_hash(vec2(blade,seed*71.0));
    float blade_y=(1.0-pixel.y)/blade_height;
    float lane=fract(pixel.x*3.0+blade*.17);
    float silhouette=step(abs(lane-.5),.44-.35*blade_y)*step(blade_y,1.0);
    // Sample the exact top-overlay texel beneath this tuft, then use the same
    // HSV and world-noise calculation as the supporting block's surface.
    vec4 base=texture(block_textures,vec3(root_position.xz,top_layer));
    if(apply_tint>.5)base=tint_block(base,target_hsv,preserve,brightness);
    vec3 color=base.rgb*grass_variation(root_position.xz);
    float dither=grass_hash(floor(root_position.xz*16.0)+floor(UV*16.0));
    ALPHA=silhouette*step(dither,range_weight);
    ALPHA_SCISSOR_THRESHOLD=.5;
    vec3 linear=color_to_linear(color);
    ALBEDO=fullbright?vec3(0):linear;
    EMISSION=fullbright?linear:vec3(0);
    BACKLIGHT=vec3(0.0);
    ROUGHNESS=1.0;SPECULAR=0.0;
}
