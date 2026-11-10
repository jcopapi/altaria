// 3D world-space interpretation of electrick's CC0 noise grass color bands.
uniform float grass_noise_scale = 0.12;
uniform float grass_noise_strength = 0.22;
float grass_hash(vec2 p) { return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }
float grass_noise(vec2 p) {
    vec2 a=floor(p),f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(grass_hash(a),grass_hash(a+vec2(1,0)),f.x),mix(grass_hash(a+vec2(0,1)),grass_hash(a+vec2(1,1)),f.x),f.y);
}
float grass_variation(vec2 world) {
    vec2 p=floor(world*4.0)/4.0*grass_noise_scale;
    float n=grass_noise(p)*0.65+grass_noise(p*2.7+vec2(17.0,9.0))*0.35;
    return 1.0+(floor(n*5.0)/4.0-0.5)*grass_noise_strength;
}
