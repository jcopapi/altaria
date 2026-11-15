#include "cave_formations.h"
#include <algorithm>
#include <cmath>
namespace altaria::caves {
static uint64_t mix(uint64_t x) {x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);}
static float unit(uint64_t x){return float(mix(x)>>40)/16777216.f;}
static constexpr const char *names[]={"stalactite","stalagmite","pillar","rubble","crystal","geode","fossil","root","fungus","vent","ice_sheet"};
bool formation_kind(const std::string &name,FormationKind &out) {
    for(size_t i=0;i<std::size(names);i++)if(name==names[i]){out=FormationKind(i);return true;}
    return false;
}
const char *formation_name(FormationKind kind) {auto i=size_t(kind);return i<std::size(names)?names[i]:"unknown";}
bool valid_formation(const FormationRule &f) {
    auto normalized=[](float x){return std::isfinite(x) && x>=0 && x<=1;};
    auto interval=[](std::array<float,2> r,float lo,float hi) {
        return std::isfinite(r[0]) && std::isfinite(r[1]) && r[0]>=lo && r[1]<=hi && r[1]>=r[0];
    };
    return !f.id.empty() && !f.rock.empty() && !f.accent.empty() &&
           f.kind<=FormationKind::ice_sheet && f.water<=FormationWater::wet && f.count>=1 && f.count<=6 &&
           normalized(f.chance) && normalized(f.min_frozen) && normalized(f.min_geothermal) &&
           normalized(f.min_rooted) && normalized(f.min_sedimentary) &&
           interval(f.radius,1,12) && interval(f.height,2,40);
}
Formation plan_formation(const Chamber &room,const FormationRule &rule,int index,uint16_t rock,uint16_t accent) {
    Formation f;
    if(index<0 || index>=rule.count || !rock || room.radius[1]<4)return f;
    uint64_t name=1469598103934665603ULL;for(unsigned char c:rule.id){name^=c;name*=1099511628211ULL;}
    auto k=mix(mix(room.id)^mix(name)^uint64_t(index));
    if(unit(k+1)>=rule.chance)return f;
    constexpr float tau=6.28318530718f;
    float theta=tau*unit(k+2),radial=.50f+.24f*unit(k+3);
    f.id=k;f.chamber=room.id;f.kind=rule.kind;f.water=rule.water;f.rock=rock;f.accent=accent?accent:rock;
    f.base={room.center[0]+room.radius[0]*radial*std::cos(theta),room.center[1],
            room.center[2]+room.radius[2]*radial*std::sin(theta)};
    float half_height=room.radius[1]*std::sqrt(1-radial*radial);
    f.vertical=rule.kind==FormationKind::stalactite || (rule.kind==FormationKind::root && rule.ceiling) ? -1:1;
    f.base[1]-=f.vertical*(half_height+2.f);
    float yaw=theta+unit(k+4)*1.5f;
    f.cos_yaw=std::cos(yaw);f.sin_yaw=std::sin(yaw);
    f.radius=std::min(rule.radius[0]+unit(k+5)*(rule.radius[1]-rule.radius[0]),
                      .28f*std::min(room.radius[0],room.radius[2]));
    if(f.radius<.9f)return {};
    f.height=std::min(rule.height[0]+unit(k+6)*(rule.height[1]-rule.height[0]),half_height+2.f);
    if(f.kind==FormationKind::pillar)f.height=half_height*2.f+4.f;
    float y1=f.base[1]+f.vertical*f.height,extent=f.radius*1.45f;
    f.box={{f.base[0]-extent,std::min(y1,f.base[1]),f.base[2]-extent},
           {f.base[0]+extent,std::max(y1,f.base[1]),f.base[2]+extent}};
    if(f.kind==FormationKind::ice_sheet) {
        f.box.lo[1]=room.center[1]-half_height+1;
        f.box.hi[1]=room.center[1]+half_height-2;
    }
    return f;
}
uint16_t formation_voxel(const Formation &f,Point p,bool wet,bool lava,int top) {
    if(!f.rock || ((wet || lava) && f.water==FormationWater::dry) || (!wet && f.water==FormationWater::wet))return 0;
    for(int a=0;a<3;a++)if(p[a]<f.box.lo[a] || p[a]>f.box.hi[a])return 0;
    float h=(p[1]-f.base[1])*f.vertical/f.height,dx=(p[0]-f.base[0])/f.radius,dz=(p[2]-f.base[2])/f.radius;
    float x=dx*f.cos_yaw+dz*f.sin_yaw,z=dz*f.cos_yaw-dx*f.sin_yaw;
    float d=x*x+z*z;
    switch(f.kind) {
    case FormationKind::stalactite: {
        const float dx = std::abs(p[0] - std::round(f.base[0]));
        const float dz = std::abs(p[2] - std::round(f.base[2]));
        const float spacing = std::max(1.f, std::floor(f.radius * .54f));
        const bool center = dx == 0 && dz == 0;
        const bool satellite = f.radius >= 2.f && ((dx == spacing && dz == 0) || (dz == spacing && dx == 0));
        return (center || (satellite && h < .82f)) ? (h > .62f ? f.accent : f.rock) : 0;
    }
    case FormationKind::stalagmite: {
        float taper=1-h*.95f;
        return (x-h*.16f)*(x-h*.16f)+z*z<taper*taper?(h>.7f?f.accent:f.rock):0;
    }
    case FormationKind::pillar: {
        float taper=.65f+.35f*(2*h-1)*(2*h-1);
        x-=std::sin(h*6.283185f)*.16f;
        return x*x+z*z<taper*taper?f.rock:0;
    }
    case FormationKind::rubble: {
        bool mound=d+h*h<1 || (x+.57f)*(x+.57f)+(z-.22f)*(z-.22f)+h*h*2<.43f ||
                   (x-.64f)*(x-.64f)+(z+.19f)*(z+.19f)+h*h*3<.31f;
        return mound?(x+z>h && d>.08f?f.accent:f.rock):0;
    }
    case FormationKind::crystal: {
        bool main=std::abs(x-.45f*h)+std::abs(z)<.56f*(1-.68f*h);
        bool shard=h<.68f && std::abs(x+.4f+.55f*h)+std::abs(z-.2f)<.36f*(1-h);
        return (main || shard)?(h>.29f?f.accent:f.rock):0;
    }
    case FormationKind::geode: {
        float spherical=d+(h-.43f)*(h-.43f)*3.2f;
        if(spherical>.97f || (h>.33f && (x>.22f || spherical<.38f)))return 0;
        return spherical<.74f?f.accent:f.rock;
    }
    case FormationKind::fossil: {
        float rib=std::abs(std::sin(x*10.f));
        bool spine=std::abs(x)<1 && std::abs(z)<.20f && h<.77f;
        bool bones=std::abs(x)<.82f && std::abs(z)<.92f && rib<.65f &&
                   std::abs(h-(.6f-.44f*std::abs(z)))<.29f;
        return (spine || bones)?f.accent:0;
    }
    case FormationKind::root: {
        x-=h*h*.65f;
        float taper=.51f*(1-.85f*h);
        bool stem=x*x+z*z<taper*taper;
        bool branch=h<.78f && std::abs(z-.78f*h)+std::abs(x+.60f*h)<taper*1.3f;
        return stem||branch?f.rock:0;
    }
    case FormationKind::fungus: {
        // A .7 m stem can miss every voxel if the center lies between four
        // sample points. Keep at least one continuous support under the cap.
        float stem=std::max(.25f,.95f/f.radius);
        if(h>.52f) {
            float cap=(h-.57f)*2.1f;
            if(d+cap*cap<1 && h<.99f)return f.accent;
        }
        return h<.69f && d<stem*stem?f.rock:0;
    }
    case FormationKind::vent: {
        float taper=.85f-.43f*h;
        if(d>taper*taper || (h>.61f && d<.085f))return 0;
        return h>.63f?f.accent:f.rock;
    }
    case FormationKind::ice_sheet:
        return wet && p[1]==top && d<.9f+.1f*std::sin(x*4+z*3)?f.rock:0;
    }
    return 0;
}
bool FormationMask::contains(Point p) const {
    int c[3];
    for(int a=0;a<3;a++){c[a]=int(p[a])-origin[a];if(c[a]<0 || c[a]>=size[a])return false;}
    size_t i=(size_t(c[2])*size[1]+c[1])*size[0]+c[0];
    return i/64<bits.size() && (bits[i/64]&(uint64_t(1)<<(i%64)))!=0;
}
FormationMask support_formation(const Formation &f,const std::function<FormationCell(int,int,int)> &sample) {
    FormationMask out;
    if(!f.rock || !sample)return out;
    size_t count=1;
    for(int a=0;a<3;a++) {
        if(!std::isfinite(f.box.lo[a]) || !std::isfinite(f.box.hi[a]) ||
           std::abs(f.box.lo[a])>1000000 || std::abs(f.box.hi[a])>1000000)return {};
        out.origin[a]=int(std::ceil(f.box.lo[a]));
        out.size[a]=int(std::floor(f.box.hi[a]))-out.origin[a]+1;
        if(out.size[a]<=0 || out.size[a]>(a==1?max_vertical_span:256))return {};
        count*=size_t(out.size[a]);
    }
    if(count>2*1024*1024)return {};
    out.bits.resize((count+63)/64,0);
    std::vector<uint8_t> candidate(count,0);
    std::vector<size_t> wave;
    const int sx=out.size[0],sy=out.size[1],sz=out.size[2];
    const size_t padded_size=size_t(sx+2)*(sy+2)*(sz+2);
    std::vector<FormationCell> cells(padded_size);
    std::vector<bool> known(padded_size,false);
    auto at=[&](int x,int y,int z)->const FormationCell& {
        size_t i=(size_t(z+1)*(sy+2)+y+1)*(sx+2)+x+1;
        if(!known[i]){cells[i]=sample(x+out.origin[0],y+out.origin[1],z+out.origin[2]);known[i]=true;}
        return cells[i];
    };
    constexpr int directions[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    size_t generated=0;
    for(int z=0;z<sz;z++)for(int y=0;y<sy;y++)for(int x=0;x<sx;x++) {
        Point p{float(x+out.origin[0]),float(y+out.origin[1]),float(z+out.origin[2])};
        // Most of the bounding box is empty. The cheap geometry test avoids
        // terrain/noise queries there; wet-only shapes are tested as wet first.
        if(!formation_voxel(f,p,f.water==FormationWater::wet || f.kind==FormationKind::ice_sheet,false,int(p[1])))continue;
        const auto &c=at(x,y,z);
        if(!c.rock || !c.carved || !c.allowed || !formation_voxel(f,p,c.water,c.lava,c.liquid_top))continue;
        size_t index=(size_t(z)*sy+y)*sx+x;candidate[index]=1;generated++;
        bool anchored=false;
        for(auto &d:directions) {
            if (f.kind == FormationKind::stalactite && d[1] != 1)
                continue;
            const auto &n=at(x+d[0],y+d[1],z+d[2]);
            if((n.rock && !n.carved) || (f.kind==FormationKind::ice_sheet && d[1]==-1 && n.water)) {
                anchored=true;break;
            }
        }
        if(anchored){candidate[index]=2;wave.push_back(index);}
    }
    for(size_t cursor=0;cursor<wave.size();cursor++) {
        size_t i=wave[cursor];int x=int(i%sx),y=int((i/sx)%sy),z=int(i/(size_t(sx)*sy));
        out.bits[i/64]|=uint64_t(1)<<(i%64);
        for(auto &d:directions) {
            if (f.kind == FormationKind::stalactite && d[1] != -1)
                continue;
            int nx=x+d[0],ny=y+d[1],nz=z+d[2];
            if(nx<0 || nx>=sx || ny<0 || ny>=sy || nz<0 || nz>=sz)continue;
            size_t next=(size_t(nz)*sy+ny)*sx+nx;
            if(candidate[next]==1){candidate[next]=2;wave.push_back(next);}
        }
    }
    out.kept=wave.size();out.removed=generated-out.kept;
    if(!out.kept)out.bits.clear();
    return out;
}
}
