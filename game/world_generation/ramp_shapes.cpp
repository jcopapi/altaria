#include "block_shapes.h"
#include <algorithm>
#include <cmath>
namespace altaria {
using P=std::array<float,3>;
RampSpec ramp_spec(uint8_t packed) {
    if(packed<32 && (packed&7)==3) return {true,RampScale::Full,RampCorner::Straight,(packed>>3)&3};
    if(packed>=128 && packed<188) {
        int group=(packed-128)/4;
        return {true,RampScale(group/5),RampCorner(group%5),(packed-128)%4};
    }
    return {};
}
uint8_t ramp_shape(RampScale scale,RampCorner corner,int turn) {
    turn&=3;
    if(scale==RampScale::Full && corner==RampCorner::Straight) return uint8_t(3+(turn<<3));
    return uint8_t(128+(int(scale)*5+int(corner))*4+turn);
}
const char *ramp_corner_name(RampCorner corner) {
    static const char *names[]={"straight","inner_left","inner_right","outer_left","outer_right"};
    return names[int(corner)];
}
uint8_t connect_ramp(uint8_t packed,const std::function<uint8_t(int,int)> &neighbors) {
    auto s=ramp_spec(packed); if(!s.valid)return packed;
    const int dx[]={1,0,-1,0},dz[]={0,1,0,-1};
    auto compatible=[&](uint8_t code) { auto n=ramp_spec(code); return n.valid && std::abs(n.rise()-s.rise())<.001f; };
    auto same=[&](int x,int z) { auto n=ramp_spec(neighbors(x,z)); return n.valid && n.turn==s.turn && std::abs(n.rise()-s.rise())<.001f; };
    auto front=neighbors(dx[s.turn],dz[s.turn]); auto f=ramp_spec(front);
    if(compatible(front) && (f.turn&1)!=(s.turn&1) && !same(-dx[f.turn],-dz[f.turn]))
        return ramp_shape(s.scale,(f.turn-s.turn+4)%4==3 ? RampCorner::OuterLeft : RampCorner::OuterRight,s.turn);
    auto back=neighbors(-dx[s.turn],-dz[s.turn]); auto b=ramp_spec(back);
    if(compatible(back) && (b.turn&1)!=(s.turn&1) && !same(dx[b.turn],dz[b.turn]))
        return ramp_shape(s.scale,(b.turn-s.turn+4)%4==3 ? RampCorner::InnerLeft : RampCorner::InnerRight,s.turn);
    return ramp_shape(s.scale,RampCorner::Straight,s.turn);
}
static P rotate(P p,int turns,bool normal=false) {
    for(int i=0;i<turns;i++) { float x=p[0]; p[0]=normal ? -p[2] : 1-p[2]; p[2]=x; }
    return p;
}
static float height(const RampSpec &s,float x,float z) {
    float value=x;
    if(s.corner==RampCorner::InnerLeft)value=std::max(x,1-z);
    if(s.corner==RampCorner::InnerRight)value=std::max(x,z);
    if(s.corner==RampCorner::OuterLeft)value=std::min(x,1-z);
    if(s.corner==RampCorner::OuterRight)value=std::min(x,z);
    return s.low()+s.rise()*value;
}
float ramp_top(uint8_t packed,float x,float z) {
    auto s=ramp_spec(packed); auto p=rotate({x,0,z},(4-s.turn)&3);
    return height(s,p[0],p[2]);
}
static std::array<P,4> top_points(const RampSpec &s) {
    return {{{0,height(s,0,0),0},{0,height(s,0,1),1},{1,height(s,1,1),1},{1,height(s,1,0),0}}};
}
static bool left(const RampSpec &s) {return s.corner==RampCorner::InnerLeft || s.corner==RampCorner::OuterLeft;}
std::vector<ShapeFace> ramp_faces(uint8_t packed) {
    auto s=ramp_spec(packed); auto p=top_points(s);
    P a{0,0,0},b{0,0,1},c{1,0,1},d{1,0,0};
    std::vector<ShapeFace> faces={{{a,b,p[1],p[0]},{-1,0,0}},{{d,p[3],p[2],c},{1,0,0}},
                                {{a,d,c,b},{0,-1,0}},{{a,p[0],p[3],d},{0,0,-1}},{{b,c,p[2],p[1]},{0,0,1}}};
    auto triangle=[&](int i,int j,int k) {
        P u,v,n;
        for(int a=0;a<3;a++) {u[a]=p[j][a]-p[i][a]; v[a]=p[k][a]-p[i][a];}
        n={u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
        float length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); for(auto &x:n)x/=length;
        faces.push_back({{p[i],p[j],p[k],p[k]},n});
    };
    if(left(s)) {triangle(0,1,3); triangle(1,2,3);} else {triangle(0,1,2);triangle(0,2,3);}
    for(auto &f:faces) {for(auto &p:f.points)p=rotate(p,s.turn); f.normal=rotate(f.normal,s.turn,true);}
    return faces;
}
std::vector<std::vector<P>> ramp_hulls(uint8_t packed) {
    auto s=ramp_spec(packed); auto p=top_points(s);
    std::array<std::array<int,3>,2> triangles=left(s) ? std::array<std::array<int,3>,2>{{{0,1,3},{1,2,3}}} : std::array<std::array<int,3>,2>{{{0,1,2},{0,2,3}}};
    std::vector<std::vector<P>> hulls;
    for(auto triangle:triangles) {
        float volume=0; std::vector<P> hull;
        for(int i:triangle) {hull.push_back(rotate(p[i],s.turn)); hull.push_back(rotate({p[i][0],0,p[i][2]},s.turn)); volume+=p[i][1];}
        if(volume>.00001f)hulls.push_back(std::move(hull));
    }
    return hulls;
}
bool ramp_intersects(uint8_t packed,P lo,P hi) {
    auto s=ramp_spec(packed); auto a=rotate(lo,(4-s.turn)&3),b=rotate(hi,(4-s.turn)&3);
    for(int i=0;i<3;i++) {lo[i]=std::max(0.f,std::min(a[i],b[i]));hi[i]=std::min(1.f,std::max(a[i],b[i])); if(lo[i]>=hi[i])return false;}
    float top=0; for(float x:{lo[0],hi[0]})for(float z:{lo[2],hi[2]})top=std::max(top,height(s,x,z));
    return lo[1]<top-.0001f;
}
}
