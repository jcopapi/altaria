uniform bool world_clouds = true;
uniform float cloud_base = 232.0;
uniform float cloud_top = 282.0;
uniform float cloud_dusk = 0.0;
uniform float cloud_bands = 4.0;
uniform vec3 cloud_lit_color : source_color = vec3(1.04, 1.0, 0.96);
uniform vec3 cloud_shadow_color : source_color = vec3(0.56, 0.55, 0.86);
uniform vec3 cloud_dusk_color : source_color = vec3(1.0, 0.60, 0.66);
uniform float cloud_coverage = 0.48;
uniform float cloud_cell = 12.0;
uniform float cloud_daylight = 1.0;
uniform float cloud_above_opacity = 0.4;
uniform vec2 cloud_drift;
uniform vec3 cloud_sun_direction = vec3(0.3, 0.8, -0.4);

// Classic video-game clouds: a grid of solid blocks with flat bottoms and
// stepped, puffy tops. A DDA walks the block grid so every face is crisp;
// faces are toon-shaded (bright top, banded sides, lavender underside).
float cloud_hash(vec2 p) {
    vec3 q = fract(p.xyx * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

float cloud_noise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    f *= f * (3.0 - 2.0 * f);
    return mix(mix(cloud_hash(cell), cloud_hash(cell + vec2(1, 0)), f.x),
               mix(cloud_hash(cell + vec2(0, 1)), cloud_hash(cell + vec2(1, 1)), f.x), f.y);
}

// Number of stacked blocks in a cloud column (0 = clear sky).
int cloud_column(vec2 c, int levels) {
    float n = cloud_noise(c * 0.085) * 0.7 + cloud_noise(c * 0.23 + 17.0) * 0.3;
    float threshold = 0.86 - cloud_coverage * 0.62;
    float h = (n - threshold) / max(0.05, 1.0 - threshold);
    return h <= 0.0 ? 0 : clamp(int(ceil(h * float(levels) * 1.7)), 1, levels);
}

vec3 render_world_clouds(vec3 background, vec3 eye, vec3 ray, float scene_depth, vec2 pixel) {
    const float max_range = 4200.0;
    float dy = abs(ray.y) < 0.0001 ? 0.0001 : ray.y;
    vec2 crossings = (vec2(cloud_base, cloud_top) - eye.y) / dy;
    float first = max(0.0, min(crossings.x, crossings.y));
    float last = min(min(scene_depth, max_range), max(crossings.x, crossings.y));
    if (last <= first) {
        return background;
    }
    float cell = max(cloud_cell, 2.0);
    int levels = max(1, int(floor((cloud_top - cloud_base) / cell)));
    vec3 origin = vec3(eye.x - cloud_drift.x, eye.y - cloud_base, eye.z - cloud_drift.y) / cell;
    vec3 start = origin + ray * (first / cell);
    ivec3 c = ivec3(floor(start));
    vec3 step_dir = sign(ray);
    vec3 delta = abs(vec3(1.0) / ray);
    float t = first / cell;
    vec3 next = t + (vec3(c) + max(step_dir, 0.0) - start) / ray;
    next = vec3(ray.x == 0.0 ? 1e30 : next.x, ray.y == 0.0 ? 1e30 : next.y, ray.z == 0.0 ? 1e30 : next.z);
    // Rays from outside enter through the deck's top or bottom plane.
    int axis = first > 0.0 ? 1 : -1;
    bool skip_first = first <= 0.0; // Never fill the screen when the camera is inside a block.
    for (int i = 0; i < 112; ++i) {
        if (t * cell > last) {
            break;
        }
        if (!skip_first && c.y >= 0 && c.y < levels && c.y < cloud_column(vec2(c.xz), levels) && axis >= 0) {
            float distance_to_hit = t * cell;
            vec3 normal = axis == 0 ? vec3(-step_dir.x, 0.0, 0.0)
                        : axis == 1 ? vec3(0.0, -step_dir.y, 0.0) : vec3(0.0, 0.0, -step_dir.z);
            // Toon face shading from the sun.
            float sun = dot(normal, normalize(cloud_sun_direction));
            float bands = max(2.0, cloud_bands);
            float lit = floor(clamp(sun * 0.5 + 0.5, 0.0, 0.999) * bands) / (bands - 1.0);
            vec3 top_color = mix(cloud_lit_color, cloud_dusk_color, cloud_dusk * 0.75);
            vec3 face = normal.y > 0.5 ? top_color
                      : normal.y < -0.5 ? cloud_shadow_color
                      : mix(cloud_shadow_color, top_color, 0.45 + 0.45 * lit);
            vec3 night = mix(vec3(0.03, 0.035, 0.09), vec3(0.10, 0.11, 0.24), normal.y * 0.5 + 0.5);
            vec3 color = mix(night, face, cloud_daylight);
            float alpha = 1.0 - smoothstep(max_range * 0.55, max_range, distance_to_hit);
            // From above, thin the deck so the world below stays visible.
            float above = smoothstep(cloud_top - cell * 0.5, cloud_top + cell * 1.5, eye.y);
            alpha *= mix(0.94, cloud_above_opacity, above);
            return mix(background, color, alpha);
        }
        skip_first = false;
        if (next.x < next.y && next.x < next.z) {
            t = next.x; next.x += delta.x; c.x += int(step_dir.x); axis = 0;
        } else if (next.y < next.z) {
            t = next.y; next.y += delta.y; c.y += int(step_dir.y); axis = 1;
        } else {
            t = next.z; next.z += delta.z; c.z += int(step_dir.z); axis = 2;
        }
        if ((c.y < 0 && step_dir.y <= 0.0) || (c.y >= levels && step_dir.y >= 0.0)) {
            break;
        }
    }
    return background;
}
