instance uniform float lod_opacity = 1.0;

// Complementary pixel coverage preserves opaque depth and normal buffers.
bool lod_pixel_visible(vec2 pixel) {
    if (lod_opacity >= 1.0)
        return true;
    float threshold = fract(52.9829189 * fract(dot(floor(pixel), vec2(.06711056, .00583715))));
    return lod_opacity >= 0.0 ? threshold < lod_opacity : threshold >= -lod_opacity - 1.0;
}
