shader_type sky;
// Procedural gradient / moon / stars inspired by REDEMO_H747's night sky.
// Clouds use quantized planar coordinates to preserve a pixel-art silhouette.
uniform float daylight : hint_range(0.0,1.0)=1.0;
uniform vec3 sun_direction=vec3(0.3,0.7,-0.6);
uniform float cloud_time=0.0;
uniform float cloud_coverage=0.48;
uniform float cloud_pixel_size=0.008;
uniform vec3 horizon_color : source_color=vec3(0.66,0.75,0.83);
uniform vec3 zenith_color : source_color=vec3(0.22,0.43,0.70);
uniform float biome_strength = .85;
float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p){vec2 a=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);return mix(mix(hash(a),hash(a+vec2(1,0)),f.x),mix(hash(a+vec2(0,1)),hash(a+vec2(1,1)),f.x),f.y);}
float clouds(vec2 p){return noise(p)*0.58+noise(p*2.03+7.0)*0.28+noise(p*4.01-5.0)*0.14;}
void sky(){
    vec3 dir=normalize(EYEDIR);
    float height=clamp(dir.y,0.0,1.0);
    float night=1.0-daylight;
    vec3 color=mix(horizon_color,zenith_color,pow(height,0.65));
    vec3 sun=normalize(sun_direction);
    float sun_dot=max(0.0,dot(dir,sun));
    float dusk=1.0-smoothstep(0.0,0.35,abs(sun.y));
    color+=vec3(0.42,0.16,0.10)*pow(sun_dot,8.0)*dusk*(1.0-height);
    vec3 moon=-sun;
    float moon_dot=max(0.0,dot(dir,moon));
    float moon_disc=smoothstep(0.99925,0.9994,moon_dot);
    float lunar_detail=0.72+0.28*noise(floor(dir.xz*1600.0));
    color+=vec3(0.63,0.74,1.0)*(moon_disc*lunar_detail+pow(moon_dot,120.0)*0.16)*night;
    vec2 star_grid=floor(dir.xz/(abs(dir.y)+0.4)*650.0);
    float stars=step(0.996,hash(star_grid))*night*smoothstep(0.02,0.18,height);
    color+=vec3(0.70,0.81,1.0)*stars*(0.6+0.4*sin(hash(star_grid+3.0)*20.0+cloud_time*0.5));
    vec2 projected=dir.xz/max(dir.y,0.035)*0.22;
    projected=floor(projected/cloud_pixel_size)*cloud_pixel_size;
    vec2 flow=projected+vec2(cloud_time*0.002,cloud_time*0.0006);
    float cloud=clouds(flow*3.2);
    float mask=step(1.0-cloud_coverage,cloud)*smoothstep(0.015,0.10,height);
    float shade=floor(clamp((cloud-(1.0-cloud_coverage))*5.0,0.0,1.0)*3.0)/3.0;
    vec3 cloud_color=mix(vec3(0.065,0.075,0.16),mix(vec3(0.67,0.73,0.85),vec3(1.0,0.96,0.88),shade),daylight);
    cloud_color=mix(cloud_color,cloud_color*(.5+.5*normalize(max(horizon_color,vec3(.001)))) ,biome_strength*.45);
    color=mix(color,cloud_color,mask*0.96);
    // Sun transmission and warm silver lining remain visible behind thin
    // pixel clouds, providing a bright source for the atmospheric shafts.
    color+=vec3(1.0,.83,.55)*smoothstep(.9995,.9998,sun_dot)*daylight*3.0*(1.0-mask*.87);
    color+=vec3(.55,.23,.07)*pow(sun_dot,28.0)*dusk*(1.0-height)*(.5+mask*.5);
    if(dir.y<0.0)color=mix(horizon_color,vec3(0.06,0.07,0.10),min(-dir.y,1.0));
    COLOR=color;
}
