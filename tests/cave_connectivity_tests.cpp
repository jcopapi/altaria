// Independent six-neighbor voxel flood-fill gate, including mixed archetypes.
// Uniform +0.75 shrink bounds the runtime's smaller procedural detail.
#include "game/world_generation/caves/cave_field.h"
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
using namespace altaria::caves;
using Air=std::function<bool(int,int,int)>;
struct Lattice {int x0,y0,z0,nx,ny,nz;std::vector<unsigned char> state;std::vector<uint32_t> queue;
 explicit Lattice(Bounds b){x0=int(std::floor(b.lo[0]))-1;y0=int(std::floor(b.lo[1]))-1;z0=int(std::floor(b.lo[2]))-1;nx=int(std::ceil(b.hi[0]))-x0+2;ny=int(std::ceil(b.hi[1]))-y0+2;nz=int(std::ceil(b.hi[2]))-z0+2;size_t n=size_t(nx)*ny*nz;if(n>100000000){std::cerr<<"BOUNDS_TOO_LARGE "<<n<<'\n';std::exit(2);}state.resize(n);}
 uint32_t id(int x,int y,int z)const{return uint32_t((z-z0)*ny+y-y0)*nx+x-x0;}
 bool inside(int x,int y,int z)const{return x>=x0&&x<x0+nx&&y>=y0&&y<y0+ny&&z>=z0&&z<z0+nz;}
 bool push(int x,int y,int z,const Air&air){if(!inside(x,y,z))return false;auto i=id(x,y,z);if(state[i])return state[i]==2;bool open=air(x,y,z);state[i]=open?2:1;if(open)queue.push_back(i);return open;}
 bool flood(Point from,Point to,const Air&air,bool full=false){int sx=int(std::lround(from[0])),sy=int(std::lround(from[1])),sz=int(std::lround(from[2]));int tx=int(std::lround(to[0])),ty=int(std::lround(to[1])),tz=int(std::lround(to[2]));if(!inside(tx,ty,tz)||!push(sx,sy,sz,air)||!air(tx,ty,tz))return false;auto goal=id(tx,ty,tz);for(size_t n=0;n<queue.size();n++){auto i=queue[n];if(!full&&i==goal)return true;int x=x0+i%nx,y=y0+(i/nx)%ny,z=z0+i/(nx*ny);push(x-1,y,z,air);push(x+1,y,z,air);push(x,y-1,z,air);push(x,y+1,z,air);push(x,y,z-1,air);push(x,y,z+1,air);}return state[goal]==2;}
};
static Query query(Network n){auto p=std::make_shared<IndexedNetwork>(std::move(n));Query q;q.owners.push_back(p);for(uint32_t i=0;i<p->primitives.size();i++)q.primitives.push_back({p.get(),i});q.bins=p->bins;return q;}
static Bounds expanded(Bounds b){for(int i=0;i<3;i++){b.lo[i]-=1;b.hi[i]+=1;}return b;}
static bool edge_test(const Passage&p,const PlanSettings&cfg,const std::function<float(float,float)>&roof,float detail,size_t&visited){Network n;n.valid=true;n.passages={p};auto q=query(std::move(n));Lattice grid(expanded(bounds(p)));Air air=[&](int x,int y,int z){return sample(q,{float(x),float(y),float(z)},roof(x,z),cfg.floor,cfg.roof_margin,detail).carved;};bool ok=grid.flood(p.from,p.to,air);visited+=grid.queue.size();return ok;}
static bool chamber_test(const Chamber&c,const PlanSettings&cfg,const std::function<float(float,float)>&roof,float detail,size_t&count,size_t&visited){Network n;n.valid=true;n.chambers={c};auto q=query(std::move(n));Lattice grid(expanded(bounds(c)));Air air=[&](int x,int y,int z){return sample(q,{float(x),float(y),float(z)},roof(x,z),cfg.floor,cfg.roof_margin,detail).carved;};bool ok=grid.flood(c.center,c.center,air,true);count=0;for(int z=grid.z0;z<grid.z0+grid.nz;z++)for(int y=grid.y0;y<grid.y0+grid.ny;y++)for(int x=grid.x0;x<grid.x0+grid.nx;x++)count+=air(x,y,z);visited=grid.queue.size();return ok&&count==visited;}
int main(){PlanSettings cfg;cfg.ceiling=512;cfg.infernal_chance=1;cfg.entrance_chance=1;cfg.isolated_chance=1;
std::vector<std::pair<std::string,std::function<float(float,float)>>> profiles={
 {"smooth_hills",[](float x,float z){return 110.f+35*std::sin(x/150.f)+25*std::cos(z/180.f);}},
 {"mountain_valleys",[](float x,float z){return 130.f+110*std::sin(x/67.f)*std::cos(z/83.f);}},
 {"steep_smooth",[](float x,float z){return 150.f+130*std::sin(x/31.f)*std::cos(z/39.f);}}};
size_t total=0,canonical=0,entrances=0,shafts=0,visited=0,rooms=0,missing=0;std::unordered_set<std::string> archetypes;
for(auto&profile:profiles){std::unordered_set<uint64_t> seen;Planner p(1337,0,cfg,profile.second);for(int rz=-1;rz<=1;rz++)for(int rx=-1;rx<=1;rx++){auto n=p.region({rx,rz});if(!n.valid){std::cout<<"INVALID_NETWORK "<<profile.first<<" "<<rx<<","<<rz<<'\n';return 1;}std::unordered_set<uint64_t> local;for(auto&c:n.chambers)local.insert(c.id);
for(auto&e:n.passages)if(seen.insert(e.id).second){bool cross=!e.entrance&&(!local.count(e.a)||!local.count(e.b));if(cross){bool shared=false;for(auto r:{Region{rx-1,rz},Region{rx+1,rz},Region{rx,rz-1},Region{rx,rz+1}}){auto peer=p.region(r);for(auto&other:peer.passages)if(other.id==e.id){if(e.path!=other.path||e.radius!=other.radius){std::cout<<"PORTAL_GEOMETRY_MISMATCH\n";return 1;}shared=true;}}if(!shared){missing++;std::cout<<"PORTAL_NOT_SHARED "<<profile.first<<" region="<<rx<<","<<rz<<" id="<<e.id<<" a="<<e.a<<" b="<<e.b<<" from="<<e.from[0]<<","<<e.from[1]<<","<<e.from[2]<<" to="<<e.to[0]<<","<<e.to[1]<<","<<e.to[2]<<std::endl;}canonical++;}
for(float detail:{0.f,.75f}) {
    if(!edge_test(e,cfg,profile.second,detail,visited)) {
        std::cout<<"PASSAGE_DISCONNECTED profile="<<profile.first<<" seed=1337 region="<<rx<<","<<rz<<" id="<<e.id<<" radius="<<e.radius<<" entrance="<<e.entrance<<" vertical="<<e.vertical<<" detail="<<detail<<" from="<<e.from[0]<<","<<e.from[1]<<","<<e.from[2]<<" to="<<e.to[0]<<","<<e.to[1]<<","<<e.to[2]<<'\n';
        return 1;
    }
}
total++;entrances+=e.entrance;shafts+=e.vertical;}
for(auto&c:n.chambers)if(archetypes.insert(profile.first+":"+c.archetype).second){size_t carved=0,reached=0;if(!chamber_test(c,cfg,profile.second,.75f,carved,reached)){std::cout<<"CHAMBER_DISCONNECTED profile="<<profile.first<<" id="<<c.id<<" type="<<c.archetype<<" carved="<<carved<<" reached="<<reached<<'\n';return 1;}rooms++;}
}std::cout<<"PROFILE_PASS "<<profile.first<<" cumulative_edges="<<total<<" visited="<<visited<<std::endl;}
if(missing){std::cout<<"CONNECTIVITY_FAIL missing_portals="<<missing<<std::endl;return 1;}
std::cout<<"CONNECTIVITY_PASS edges="<<total<<" canonical_portals="<<canonical<<" entrances="<<entrances<<" shafts="<<shafts<<" complete_chambers="<<rooms<<" voxel_visits="<<visited<<" detail_zero_and_max_shrink\n";
return 0;
}
