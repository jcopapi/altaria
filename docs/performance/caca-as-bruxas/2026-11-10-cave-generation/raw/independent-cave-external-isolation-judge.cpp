#include "game/world_generation/caves/cave_field.h"
#include <FastNoise/FastNoise.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <unordered_set>
using namespace altaria::caves;
static Query make_query(const Network&n){auto owner=std::make_shared<IndexedNetwork>(n);Query q;q.owners.push_back(owner);for(uint32_t i=0;i<owner->primitives.size();i++)q.primitives.push_back({owner.get(),i});q.bins=owner->bins;return q;}
static Bounds expand(Bounds b,float n){for(int a=0;a<3;a++){b.lo[a]-=n;b.hi[a]+=n;}return b;}
struct Profile{std::string name;int basin=0;std::function<float(float,float)>roof;};
int main(){std::cout<<std::unitbuf;std::vector<Profile> profiles={{"mixed_flat",0,[](float,float){return 110.f;}},{"flat_dry",0,[](float,float){return 110.f;}},{"smooth_dry",0,[](float x,float z){return 110.f+35*std::sin(x/150.f)+25*std::cos(z/180.f);}},{"flat_basins",3,[](float,float){return 110.f;}}};size_t isolated=0,broad=0,voxels=0;
for(auto&profile:profiles)for(int seed=0;seed<120;seed++){
 PlanSettings cfg;cfg.floor=-448;cfg.ceiling=320;cfg.basin_regions=profile.basin;cfg.isolated_chance=1;cfg.entrance_chance=1;cfg.infernal_chance=1;
 if(profile.name=="mixed_flat")cfg.archetypes={{"mod:small",0,3,{6,5,6},{6,5,6},4},{"mod:giant",1,1,{100,25,100},{100,25,100},2},{"mod:tall",1,1,{6,100,6},{6,100,6},8}};
 Planner planner(seed,0,cfg,profile.roof);std::map<std::pair<int,int>,Network> nets;
 auto get=[&](int x,int z)->const Network&{auto key=std::make_pair(x,z);auto it=nets.find(key);if(it==nets.end())it=nets.emplace(key,planner.region({x,z})).first;return it->second;};
 auto node=FastNoise::New<FastNoise::Simplex>();node->SetScale(13.f);
 auto eval=[&](const Query&q,Point p){auto s=sample(q,p,profile.roof(p[0],p[2]),cfg.floor,cfg.roof_margin);if(std::abs(s.distance)<=.65f)s=sample(q,p,profile.roof(p[0],p[2]),cfg.floor,cfg.roof_margin,node->GenSingle3D(p[0],p[1],p[2],seed)*.65f);return s;};
 for(int rz=-3;rz<=3;rz++)for(int rx=-3;rx<=3;rx++){const auto&own=get(rx,rz);for(auto&c:own.chambers)if(c.isolated){isolated++;auto cb=expand(bounds(c),.75f);Network single;single.valid=true;single.chambers={c};auto cq=make_query(single);
 for(int dz=-1;dz<=1;dz++)for(int dx=-1;dx<=1;dx++)if(dx||dz){int nx=rx+dx,nz=rz+dz;auto other=get(nx,nz);std::unordered_set<uint64_t> local_ids;for(auto&p:own.passages)local_ids.insert(p.id);other.passages.erase(std::remove_if(other.passages.begin(),other.passages.end(),[&](const Passage&p){return local_ids.count(p.id);}),other.passages.end());std::vector<Bounds> intersections;auto nearby=expand(cb,1.f);
 for(auto&r:other.chambers)if(expand(bounds(r),.75f).intersects(nearby))intersections.push_back(expand(bounds(r),1.75f));
 for(auto&p:other.passages)if(expand(bounds(p),.75f).intersects(nearby))intersections.push_back(expand(bounds(p),1.75f));
 if(intersections.empty())continue;broad++;auto nq=make_query(other);
 int lo[3],hi[3],size[3];size_t count=1;for(int a=0;a<3;a++){lo[a]=int(std::ceil(cb.lo[a]));hi[a]=int(std::floor(cb.hi[a]));size[a]=hi[a]-lo[a]+1;count*=size[a];}std::vector<unsigned char> candidates(count);
 for(auto b:intersections){int low[3],high[3];for(int a=0;a<3;a++){low[a]=std::max(lo[a],int(std::ceil(b.lo[a])));high[a]=std::min(hi[a],int(std::floor(b.hi[a])));}for(int z=low[2];z<=high[2];z++)for(int y=low[1];y<=high[1];y++)for(int x=low[0];x<=high[0];x++)candidates[(size_t(z-lo[2])*size[1]+y-lo[1])*size[0]+x-lo[0]]=1;}
 for(int z=lo[2];z<=hi[2];z++)for(int y=lo[1];y<=hi[1];y++)for(int x=lo[0];x<=hi[0];x++){if(!candidates[(size_t(z-lo[2])*size[1]+y-lo[1])*size[0]+x-lo[0]])continue;voxels++;Point a{float(x),float(y),float(z)};auto ca=eval(cq,a);if(!ca.carved)continue;
 for(int dir=-1;dir<6;dir++){Point b=a;if(dir>=0)b[dir/2]+=dir%2?1:-1;auto ob=eval(nq,b);if(!ob.carved)continue;std::string kind="unknown";for(auto&r:other.chambers)if(r.id==ob.owner)kind=r.isolated?"isolated_room":"room";for(auto&p:other.passages)if(p.id==ob.owner)kind=p.entrance?"entrance":"passage";
 std::cout<<"EXTERNAL_ISOLATION_FAILURE profile="<<profile.name<<" seed="<<seed<<" own_region="<<rx<<","<<rz<<" isolated_id="<<c.id<<" archetype="<<c.archetype<<" center="<<c.center[0]<<","<<c.center[1]<<","<<c.center[2]<<" radius="<<c.radius[0]<<","<<c.radius[1]<<","<<c.radius[2]<<" neighbor_region="<<nx<<","<<nz<<" neighbor_id="<<ob.owner<<" kind="<<kind<<" relation="<<(dir<0?"voxel_overlap":"six_neighbor_contact")<<" a="<<a[0]<<","<<a[1]<<","<<a[2]<<" b="<<b[0]<<","<<b[1]<<","<<b[2]<<" distances="<<ca.distance<<","<<ob.distance<<" scanned_isolated="<<isolated<<" broadphase="<<broad<<" voxel_tests="<<voxels<<std::endl;return 1;}
 }
 }
 }}
 if(seed%10==9)std::cout<<"ISOLATION_PROGRESS profile="<<profile.name<<" seed="<<seed<<" isolated="<<isolated<<" broadphase="<<broad<<" voxel_tests="<<voxels<<std::endl;
}
std::cout<<"ISOLATION_PASS tested_seeds=120 profiles=3 regions_per_seed=49 isolated="<<isolated<<" broadphase="<<broad<<" voxel_tests="<<voxels<<" actual_FastNoise_detail_and_cardinal_contacts\n";
}
