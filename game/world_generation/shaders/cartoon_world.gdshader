shader_type spatial;
render_mode unshaded, fog_disabled, cull_disabled, depth_draw_never, depth_test_disabled, shadows_disabled;
#include "res://game/world_generation/shaders/world_clouds.gdshaderinc"
uniform sampler2D screen_tex : hint_screen_texture, filter_nearest, repeat_disable;
uniform sampler2D depth_tex : hint_depth_texture, filter_nearest, repeat_disable;
uniform sampler2D normals_tex : hint_normal_roughness_texture, filter_nearest, repeat_disable;
uniform bool outlines = true;
uniform bool silhouette_outlines = true;
// 0..1: shimmering hot air in the underworld, stronger with distance.
uniform float heat_haze = 0.0;
// Camera inside a liquid: 0 dry, 1 fully under water, and the same for lava.
uniform float underwater = 0.0;
uniform float in_lava = 0.0;
uniform bool pixelation = true;
uniform float outline_width = 1.0;
uniform float outline_strength = .9;
uniform float outline_highlight = .3;
uniform float outline_distance_start = 0.0;
uniform float outline_distance_end = 0.0;
uniform float outline_min_depth_gap = 4.0;
uniform float outline_min_feature_size = 3.0;
uniform float outline_shoreline_strength = .65;
uniform float pixelation_start = 48.0;
uniform float pixelation_end = 2400.0;
uniform float pixelation_max = 2.0;
uniform float fog_start = 1800.0;
uniform float fog_end = 5600.0;
uniform vec3 key_light_direction = vec3(.3, .8, -.4);
uniform float outline_blacklist_distance = 48.0;
uniform bool godrays = true;
uniform vec2 sun_screen = vec2(.5,.3);
uniform float godrays_visibility = 0.0;
uniform float godrays_strength = .3;
uniform float godrays_reach = .85;
uniform int godrays_samples = 16;
uniform vec3 godrays_color : source_color = vec3(1.0,.87,.65);
uniform bool debug_outline_mask = false;
// Split-tone grade: cool, saturated shadows and warm highlights.
uniform vec3 grade_shadow_tint : source_color = vec3(0.46, 0.36, 0.78);
uniform vec3 grade_highlight_tint : source_color = vec3(1.0, 0.90, 0.72);
uniform float grade_strength = 0.35;
vec3 view_position(vec2 uv, mat4 projection_inverse) {
    float raw = textureLod(depth_tex, uv, 0.0).r;
    if (raw < .0000001) return vec3(0.0, 0.0, -1000000.0);
    vec4 p = projection_inverse * vec4(uv * 2.0 - 1.0, raw, 1.0);
    return p.xyz / p.w;
}
vec3 normal_at(vec2 uv) {
    return normalize(textureLod(normals_tex, uv, 0.0).rgb * 2.0 - 1.0);
}
float outline_allowed(float roughness,float distance_to_camera) {
    float excluded=step(.79,roughness)*(1.0-step(.855,roughness));
    return 1.0-excluded*smoothstep(outline_blacklist_distance,outline_blacklist_distance+16.0,distance_to_camera);
}
float water_mask(float roughness) {
    return step(.61, roughness) * (1.0 - step(.75, roughness));
}
float resource_mask(float roughness) {
    return step(.874, roughness) * (1.0 - step(.895, roughness));
}
vec2 safe_uv(vec2 uv) {
    vec2 size = vec2(textureSize(depth_tex,0));
    return clamp(uv, .5 / size, 1.0 - .5 / size);
}
float feature_support(vec2 uv, vec3 p, vec3 n, mat4 projection_inverse) {
    vec3 q = view_position(safe_uv(uv), projection_inverse);
    // A wider footprint rejects single cubes and small protrusions. This is
    // deliberately independent of face normals and texture/color variations.
    float tolerance = max(outline_min_feature_size * 1.5, -p.z * .012);
    float close_depth = 1.0 - smoothstep(tolerance, tolerance * 1.5, abs(q.z - p.z));
    // Grazing terrain/shore planes have large perspective depth changes even
    // across a small footprint. Accept continuous planes as well as close faces.
    float same_plane = (1.0-smoothstep(.35,.85,abs(dot(n,q-p))))
        * (1.0-smoothstep(tolerance*10.0,tolerance*20.0,length(q-p)));
    return max(close_depth,same_plane);
}
float water_at(vec2 uv) {
    return water_mask(textureLod(normals_tex, safe_uv(uv), 0.0).a);
}
void vertex() { POSITION = vec4(VERTEX.xy, 1.0, 1.0); }
void fragment() {
    vec2 uv = SCREEN_UV;
    vec3 p = view_position(uv, INV_PROJECTION_MATRIX);
    float d = length(p);
    vec2 color_uv = uv;
    if (heat_haze > .001 && d < 100000.0) {
        float far = smoothstep(6.0, 70.0, d);
        vec2 wobble = vec2(sin(uv.y * 92.0 + TIME * 3.1) + sin(uv.y * 41.0 - TIME * 1.7),
                           cos(uv.x * 77.0 + TIME * 2.4));
        color_uv = safe_uv(uv + wobble * .0011 * heat_haze * far);
    }
    float submerged = max(underwater, in_lava);
    if (submerged > .001) {
        // The view wavers through the liquid: slow swells under water, a fast
        // boil inside lava.
        float speed = mix(1.0, 3.2, in_lava / max(submerged, .001));
        vec2 sway = vec2(sin(uv.y * 23.0 + TIME * 1.7 * speed) + .5 * sin(uv.y * 57.0 - TIME * 2.3 * speed),
                         cos(uv.x * 19.0 + TIME * 1.3 * speed));
        color_uv = safe_uv(color_uv + sway * (.0028 * underwater + .0045 * in_lava));
    }
    vec3 color = textureLod(screen_tex, color_uv, 0.0).rgb;
    float outline_fade = outline_distance_end > outline_distance_start
        ? smoothstep(outline_distance_start, outline_distance_end, d)
        : step(outline_distance_start, d);
    if (d < 100000.0) {
        vec4 normal_data = textureLod(normals_tex,uv,0.0);
        vec3 n = normalize(normal_data.rgb*2.0-1.0);
        // The whole scene already renders to a low-resolution SubViewport.
        // Additional distant coarsening is small, gradual, and only samples the
        // same surface. Preserve silhouettes and fog instead of a large mosaic.
        float far_weight = smoothstep(pixelation_start, pixelation_end, d);
        if (pixelation && pixelation_max > 1.0 && far_weight > .001) {
            float grid = max(2.0, floor(pixelation_max + .5));
            vec2 snapped = (floor(uv * VIEWPORT_SIZE / grid) + .5) * grid / VIEWPORT_SIZE;
            vec3 sample_position = view_position(snapped, INV_PROJECTION_MATRIX);
            if (abs(sample_position.z - p.z) < max(.2, -p.z * .025)
                    && dot(n, normal_at(snapped)) > .85) {
                color = mix(color, textureLod(screen_tex, snapped, 0.0).rgb, far_weight * .7);
            }
        }
        if (outlines && silhouette_outlines && d < fog_end && resource_mask(normal_data.a) > .5) {
            vec2 pixel = outline_width / VIEWPORT_SIZE;
            float interior = resource_mask(textureLod(normals_tex, safe_uv(uv + vec2(pixel.x, 0.0)), 0.0).a)
                           * resource_mask(textureLod(normals_tex, safe_uv(uv - vec2(pixel.x, 0.0)), 0.0).a)
                           * resource_mask(textureLod(normals_tex, safe_uv(uv + vec2(0.0, pixel.y)), 0.0).a)
                           * resource_mask(textureLod(normals_tex, safe_uv(uv - vec2(0.0, pixel.y)), 0.0).a);
            color *= mix(1.0, .24, (1.0 - interior) * outline_strength * (1.0 - smoothstep(fog_start, fog_end, d)));
        }
        if (outlines && d < fog_end && outline_fade>.001 && normal_data.a<.985 && outline_allowed(normal_data.a,d)>.001) {
            vec3 world_normal = normalize((INV_VIEW_MATRIX * vec4(n, 0.0)).xyz);
            float facing_light = max(0.0, dot(world_normal, normalize(key_light_direction)));
            float edge = 0.0;
            float shore_edge = 0.0;
            float meters_per_pixel = max(.0001, abs(2.0 * p.z * INV_PROJECTION_MATRIX[1][1]) / VIEWPORT_SIZE.y);
            float footprint = clamp(outline_min_feature_size / meters_per_pixel, 1.5, 24.0);
            float here_water = water_mask(normal_data.a);
            for (int i = 0; i < 4; i++) {
                vec2 direction = i == 0 ? vec2(1, 0) : i == 1 ? vec2(-1, 0)
                                  : i == 2 ? vec2(0, 1) : vec2(0, -1);
                vec2 neighbor = safe_uv(uv + direction * outline_width / VIEWPORT_SIZE);
                vec3 q = view_position(neighbor, INV_PROJECTION_MATRIX);
                float other_roughness = textureLod(normals_tex,neighbor,0.0).a;
                float permitted = outline_allowed(other_roughness,length(q));
                float gap = q.z < -100000.0 ? 1.0 : smoothstep(
                    max(outline_min_depth_gap, -p.z * .018),
                    max(outline_min_depth_gap * 1.6, -p.z * .035), p.z - q.z);
                // Perspective depth gradients along a beach/water plane and
                // half-block steps are not silhouettes of a landform.
                if (q.z > -100000.0) {
                    float plane_threshold = max(outline_min_depth_gap, -p.z*.012);
                    gap *= smoothstep(plane_threshold*.5, plane_threshold, abs(dot(n,q-p)));
                }
                float shoreline = (1.0 - here_water) * water_mask(other_roughness)
                    * (1.0 - smoothstep(outline_min_feature_size * 3.0, outline_min_feature_size * 5.0, abs(p.z-q.z)));
                if (max(gap, shoreline) * permitted > .001) {
                    vec2 inward = -direction * footprint / VIEWPORT_SIZE;
                    vec2 tangent = vec2(-direction.y, direction.x) * footprint / VIEWPORT_SIZE;
                    float support = feature_support(uv+inward,p,n,INV_PROJECTION_MATRIX)
                        + feature_support(uv+tangent,p,n,INV_PROJECTION_MATRIX)
                        + feature_support(uv-tangent,p,n,INV_PROJECTION_MATRIX);
                    float broad_surface = smoothstep(1.5, 2.5, support);
                    if (silhouette_outlines)
                        edge = max(edge, gap * broad_surface * permitted);
                    if (shoreline > .001) {
                        // Trace the land/water region boundary, not noisy sand
                        // texels or a one-block puddle. Samples reuse the depth
                        // / roughness buffer already needed by the post effect.
                        float water_extent = water_at(uv-inward)
                            + water_at(uv-inward+tangent*.5)
                            + water_at(uv-inward-tangent*.5);
                        shore_edge = max(shore_edge, shoreline * broad_surface
                            * smoothstep(1.5,2.5,water_extent));
                    }
                }
            }
            float visibility = outline_fade * (1.0 - smoothstep(fog_start, fog_end, d));
            // Cutout grass and decorations never acquire their own contour.
            float detail_weight = (1.0-step(.985,normal_data.a))*outline_allowed(normal_data.a,d);
            float contour = max(edge, shore_edge*outline_shoreline_strength);
            float ink = contour * outline_strength * visibility * detail_weight;
            color = mix(color, color * .35, ink);
            // Relative highlight avoids adding bright seams to dark night scenes.
            float highlight = edge * facing_light * outline_highlight * visibility * detail_weight;
            color = mix(color, min(color * 1.15, vec3(1.0)), highlight);
        }
    }
    {
        float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
        float lit = smoothstep(0.04, 0.55, luma);
        // Multiply-style tint keeps value; dark areas gain hue instead of going grey.
        vec3 shadow_grade = color * mix(vec3(1.0), grade_shadow_tint * 1.6, 1.0 - lit);
        vec3 graded = mix(shadow_grade, shadow_grade * grade_highlight_tint * 1.08, lit * lit);
        color = mix(color, graded, grade_strength);
    }
    // Screen-space ray loop replaced by Compositor-based LensEffects (Lens Flares & God Rays)
    if (world_clouds) {
        vec4 near_point = INV_PROJECTION_MATRIX * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
        vec3 ray = normalize(mat3(INV_VIEW_MATRIX) * (near_point.xyz / near_point.w));
        color = render_world_clouds(color, CAMERA_POSITION_WORLD, ray, d, FRAGCOORD.xy);
    }
    if (submerged > .001) {
        float reach = d < 100000.0 ? d : 400.0;
        // Water absorbs red first: near things keep some colour, distance fades
        // to a blue-green murk, light ripples across the view from above.
        if (underwater > .001) {
            float murk = 1.0 - exp(-reach * .075);
            vec3 tinted = color * vec3(.55, .86, .95);
            vec3 water_fog = vec3(.03, .19, .27);
            float ripple = .5 + .5 * sin(uv.x * 31.0 + uv.y * 17.0 + TIME * 2.2) * sin(uv.y * 23.0 - TIME * 1.6);
            vec3 under = mix(tinted, water_fog, murk) * (.92 + .16 * ripple * (1.0 - murk));
            color = mix(color, under, underwater);
        }
        // Lava: almost nothing is visible past a few blocks, all of it glowing.
        if (in_lava > .001) {
            float murk = 1.0 - exp(-reach * .6);
            // Swirling bright veins in a deep orange glow.
            float swirl = sin(uv.x * 9.0 + TIME * 1.9 + sin(uv.y * 7.0 - TIME * 1.3) * 2.0)
                        * sin(uv.y * 8.0 - TIME * 1.5 + sin(uv.x * 6.0 + TIME) * 2.0);
            vec3 glow = mix(vec3(.62, .13, .02), vec3(1.0, .52, .10), smoothstep(.2, .9, swirl));
            vec3 hot = mix(color * vec3(1.3, .55, .22), glow, murk);
            float flicker = .92 + .08 * sin(TIME * 7.0 + uv.x * 13.0) * sin(TIME * 5.3 + uv.y * 11.0);
            color = mix(color, hot * flicker, in_lava);
        }
        vec2 centered = uv - .5;
        float vignette = 1.0 - smoothstep(.25, .75, length(centered * vec2(1.25, 1.0)));
        color *= mix(1.0, .5 + .5 * vignette, submerged);
    }
    ALBEDO = color;
    if(debug_outline_mask) {
        float rough=textureLod(normals_tex,uv,0.0).a;
        ALBEDO=vec3(outline_allowed(rough,d),rough,outline_fade);
    }
}
