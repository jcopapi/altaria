// Adapted from the supplied canvas_item shader: same HSV replacement, saturation
// preservation and brightness rule. Inputs/outputs are sRGB; lighting conversion
// is performed once by the spatial material after all layers are composited.
vec3 rgb_to_hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
vec3 hsv_to_rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}
vec4 tint_block(vec4 original, vec3 target_hsv, float preserve_saturation, float brightness) {
    vec3 original_hsv = rgb_to_hsv(original.rgb);
    original_hsv.x = target_hsv.x;
    original_hsv.y = mix(target_hsv.y, original_hsv.y, preserve_saturation);
    original_hsv.z = clamp(original_hsv.z * target_hsv.z * brightness, 0.0, 1.0);
    return vec4(hsv_to_rgb(original_hsv), original.a);
}
vec3 color_to_linear(vec3 color) {
    return mix(color / 12.92, pow((color + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), color));
}
