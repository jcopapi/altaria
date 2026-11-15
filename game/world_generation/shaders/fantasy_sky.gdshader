shader_type sky;
// Stylized fantasy sky: banded gradient, soft dusk glow, toon sun with halo,
// moon and twinkling stars. Palette comes from environment.json.
uniform bool surface_streamed = true;
uniform float daylight : hint_range(0.0,1.0)=1.0;
uniform vec3 sun_direction=vec3(0.3,0.7,-0.6);
uniform float cloud_time=0.0;
uniform vec3 horizon_color : source_color=vec3(0.62,0.86,1.0);
uniform vec3 zenith_color : source_color=vec3(0.16,0.46,0.95);
uniform vec3 dusk_glow : source_color=vec3(1.0,0.42,0.52);
uniform vec3 sun_tint : source_color=vec3(1.0,0.93,0.78);
uniform float sun_halo=0.6;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p){vec2 a=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);return mix(mix(hash(a),hash(a+vec2(1,0)),f.x),mix(hash(a+vec2(0,1)),hash(a+vec2(1,1)),f.x),f.y);}
void sky(){
    vec3 dir=normalize(EYEDIR);
    float height=clamp(dir.y,0.0,1.0);
    float night=1.0-daylight;
    // Soft-stepped gradient: a few gentle bands read as painted, not photographic.
    float t=pow(height,0.55);
    float banded=mix(t,floor(t*6.0+0.5)/6.0,0.25);
    vec3 color=mix(horizon_color,zenith_color,banded);
    vec3 sun=normalize(sun_direction);
    float sun_dot=max(0.0,dot(dir,sun));
    float dusk=1.0-smoothstep(0.0,0.35,abs(sun.y));
    // Wide dusk band around the horizon, strongest toward the sun.
    float horizon_band=1.0-smoothstep(0.0,0.42,height);
    float toward=0.35+0.65*pow(sun_dot,3.0);
    color=mix(color,dusk_glow,dusk*horizon_band*toward*0.85);
    color+=dusk_glow*pow(sun_dot,10.0)*dusk*0.35;
    // Toon sun: crisp disc, a pale ring and a broad halo.
    float disc=smoothstep(0.9990,0.9993,sun_dot);
    float ring=smoothstep(0.9955,0.9962,sun_dot)*(1.0-smoothstep(0.9970,0.9978,sun_dot));
    float halo=pow(sun_dot,48.0)*sun_halo;
    color+=sun_tint*(disc*3.0+ring*0.45+halo*0.55)*max(daylight,dusk*0.8);
    // Moon and stars.
    vec3 moon=-sun;
    float moon_dot=max(0.0,dot(dir,moon));
    float moon_disc=smoothstep(0.99925,0.9994,moon_dot);
    float lunar_detail=0.72+0.28*noise(floor(dir.xz*1600.0));
    color+=vec3(0.70,0.78,1.0)*(moon_disc*lunar_detail+pow(moon_dot,90.0)*0.22)*night;
    vec2 star_grid=floor(dir.xz/(abs(dir.y)+0.4)*650.0);
    float star_seed=hash(star_grid);
    float stars=step(0.9955,star_seed)*night*smoothstep(0.02,0.18,height);
    vec3 star_color=mix(vec3(0.75,0.82,1.0),vec3(1.0,0.80,0.95),step(0.998,star_seed));
    color+=star_color*stars*(0.55+0.45*sin(hash(star_grid+3.0)*20.0+cloud_time*0.8));
    if(dir.y<0.0)color=mix(horizon_color,horizon_color*0.35+vec3(0.03,0.02,0.07),min(-dir.y*1.5,1.0));
    // An unloaded end of a deep passage has no exterior sky behind it.
    COLOR = surface_streamed ? color : vec3(0.0);
}
