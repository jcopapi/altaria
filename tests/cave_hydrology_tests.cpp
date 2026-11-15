#include "game/world_generation/terrain_mesh.cpp"
#include <cassert>
#include <iostream>
#include <map>
#include <thread>
using namespace altaria;
using namespace altaria::caves;

static Registry blocks() {
    Registry r;std::string error;
    const char *keys[]={"air","stone","dirt","grass","sand","water","snow","bedrock","lava"};
    for(int i=0;i<9;i++) {
        BlockDef b;b.id=i;b.key=keys[i];b.solid=i!=0 && i!=5 && i!=8;
        b.water=i==5;b.lava=i==8;b.liquid=b.water||b.lava;
        assert(r.add(b,error));
    }
    r.lava=8;return r;
}
static Settings settings() {
    Settings cfg;cfg.min_y=-448;cfg.vegetation=false;cfg.caves.enabled=true;
    cfg.caves.hydrology={3,1,0,.52f,.28f};
    cfg.caves.plan.entrance_chance=0;cfg.caves.plan.infernal_chance=1;
    return cfg;
}
static void solve_fixture() {
    assert((basin_of({-1,-3},3)==Region{-1,-1}));
    assert((basin_of({-4,3},3)==Region{-2,1}));
    HydroSettings s{3,1,0,.52f,.28f};
    Network n;n.valid=true;n.region={-1,-1};
    n.chambers.push_back({25,{0,-82,0},{10,10,10},1,false,"normal"});
    n.chambers.push_back({26,{0,-350,0},{10,12,10},3,false,"normal"});
    n.passages.resize(2);n.passages[0].entrance=true;n.passages[0].id=1;
    n.passages[1].entrance=true;n.passages[1].id=2;
    auto dry=[](const Passage&){return Outlet{WaterSource::none,60};};
    auto plan=plan_hydrology({-1,-1},1337,0,s,42,{&n},dry);
    assert(plan.source==WaterSource::lake && plan.top==-82 && plan.at(-82)==Fluid::water && plan.at(-81)==Fluid::dry);
    // Give the lake a graded outlet to another room. With the same water
    // plane and roof above it, this is a river instead of a drowned tunnel.
    n.chambers.push_back({27,{28,-84,0},{10,10,10},0,false,"normal"});
    Passage river;river.id=3;river.a=25;river.b=27;river.radius=4;
    river.from={0,-82,0};river.to={28,-84,0};river.path={river.from,{7,-82.5,0},{14,-83,0},{21,-83.5,0},river.to};
    n.passages.push_back(river);
    auto flowing=plan_hydrology({-1,-1},1337,0,s,42,{&n,&n},dry);
    assert(flowing.rivers && flowing.rivers->size()==1 && flowing.rivers->front().passage==3 && !flowing.rivers->front().reverse);
    auto mixed=[](const Passage &p){return p.id==1?Outlet{WaterSource::none,60}:Outlet{WaterSource::ocean,42};};
    plan=plan_hydrology({-1,-1},1337,0,s,42,{&n},mixed);
    assert(plan.source==WaterSource::ocean && plan.top==42);
    s.lake_chance=0;s.lava_chance=1;
    plan=plan_hydrology({-1,-1},1337,0,s,42,{&n},dry);
    assert(plan.fluid==Fluid::lava && plan.source_room==26 && plan.top<-350);
}
static void verify_partition(const Generator &g) {
    size_t portals=0;
    const int width=3*g.settings().caves.plan.region_size;
    for(auto region:{Region{-4,-3},Region{-3,-3},Region{-1,-1},Region{0,0},Region{2,2},Region{3,3}}) {
        auto basin=basin_of(region,3);auto plan=g.underground()->region(region);assert(plan && plan->network.valid);
        for(const auto &p:plan->primitives) {
            assert(p.bounds.lo[0]>basin.x*width+2 && p.bounds.hi[0]<(basin.x+1)*width-2);
            assert(p.bounds.lo[2]>basin.z*width+2 && p.bounds.hi[2]<(basin.z+1)*width-2);
        }
        for(const auto &c:plan->network.external) {
            assert((basin_of({int(std::floor(c.center[0]/width)),int(std::floor(c.center[2]/width))},1)==basin));
            portals++;
        }
    }
    assert(portals>0);
}
static void verify_pool(bool lava) {
    auto cfg=settings();auto r=blocks();std::string error;
    if(lava){cfg.caves.hydrology.lake_chance=0;cfg.caves.hydrology.lava_chance=1;}
    Generator g(cfg,r,1337);assert(g.valid_generation(error));verify_partition(g);
    Region basin{-1,0};
    auto plan=g.underground()->hydrology(basin);
    assert(g.underground()->hydrology({INT32_MAX,INT32_MIN}).fluid==Fluid::dry);
    assert(plan.fluid==(lava?Fluid::lava:Fluid::water));
    std::array<HydroPlan,6> parallel;std::vector<std::thread> workers;
    for(size_t i=0;i<parallel.size();i++)workers.emplace_back([&,i]{parallel[i]=g.underground()->hydrology({0,-1});});
    for(auto &w:workers)w.join();
    for(const auto &p:parallel)assert(p.top==parallel[0].top && p.fluid==parallel[0].fluid && p.source_room==parallel[0].source_room);
    Chamber room;
    size_t fully_submerged=0,dry_rooms=0;
    for(int z=0;z<3;z++)for(int x=-3;x<0;x++)
        for(const auto &c:g.underground()->region({x,z})->network.chambers) {
            if(c.id==plan.source_room)room=c;
            if(!lava && !c.isolated) {
                auto center=g.terrain_sample(int(std::lround(c.center[0])),int(std::lround(c.center[1])),int(std::lround(c.center[2])));
                if(c.center[1]+c.radius[1]<plan.top-2){assert(center==r.water);fully_submerged++;}
                if(c.center[1]-c.radius[1]>plan.top+2){assert(center==r.air);dry_rooms++;}
            }
        }
    if(!lava)assert(fully_submerged>0 && dry_rooms>0);
    assert(room.id==plan.source_room && plan.top<room.center[1]+room.radius[1]-3);
    BlockId liquid=lava?r.lava:r.water;
    int wx=int(std::lround(room.center[0])),wz=int(std::lround(room.center[2]));
    assert(g.sample(wx,plan.top,wz)==liquid);
    assert(g.sample(wx,plan.top+1,wz)==r.air);
    for(int lod:{0,1,2}) {
        int step=1<<lod,size=32*step;
        TileKey key{int(std::floor(double(wx)/size))*size,int(std::floor(double(wz)/size))*size,lod};
        StageTimings timings;auto empty=std::make_shared<EditMap>();
        auto surfaces=std::make_shared<AppearanceMap>();
        auto v=make_volume(key,g,empty,surfaces,timings);
        int x=(wx-key.x)/step,z=(wz-key.z)/step,y=(plan.top-cfg.min_y)/step;
        assert(v->at(x,y,z)==liquid && v->cave_liquid_at(x,y,z) && v->at(x,y+1,z)==r.air);
        assert(v->at(x,y,z)==g.sample(key.x+x*step,cfg.min_y+y*step,key.z+z*step));
        MeshResult result;result.key=key;greedy(result,*v,r);
        bool plane=false;
        for(auto &p:result.water.vertices)if(p.y==plan.top+1 && p.ny>0)plane=true;
        assert(plane);
        if(lava) {
            for(auto &p:result.water.vertices)if((int(p.decal) & 3)==3)assert(p.y<=plan.top+1);
            for(auto &p:result.solid.vertices)assert((int(p.decal) & 3)!=3);
        }
        for(auto offset:{IVec3{-size,0,0},IVec3{size,0,0},IVec3{0,0,-size},IVec3{0,0,size}}) {
            auto other=make_volume({key.x+offset.x,key.z+offset.z,lod},g,empty,surfaces,timings);
            for(int edge=0;edge<32;edge++)for(int iy=1;iy<v->height;iy++) {
                int ax=offset.x<0?-1:offset.x>0?32:edge,az=offset.z<0?-1:offset.z>0?32:edge;
                int bx=offset.x<0?31:offset.x>0?0:edge,bz=offset.z<0?31:offset.z>0?0:edge;
                assert(v->at(ax,iy,az)==other->at(bx,iy,bz));
                assert(v->cave_liquid_at(ax,iy,az)==other->cave_liquid_at(bx,iy,bz));
            }
        }
        if(lod==0) {
            auto edit=std::make_shared<EditMap>();(*edit)[{wx,plan.top,wz}]=r.stone;
            auto revised=make_volume(key,g,edit,surfaces,timings);
            assert(revised->at(x,y,z)==r.stone && !revised->cave_liquid_at(x,y,z));
            assert(revised->at(x,y-1,z)==liquid && revised->cave_liquid_at(x,y-1,z));
        }
    }
    std::cout<<"CAVE_POOL_PASS type="<<fluid_name(plan.fluid)<<" top="<<plan.top<<" source=("<<wx<<','<<wz
             <<") rivers="<<(plan.rivers?plan.rivers->size():0)<<"; LOD0/1/2 caps, all halos, non-solid fluid mesh, local edits\n";
}
static void verify_coast() {
    auto cfg=settings();cfg.caves.plan.entrance_chance=1;
    auto r=blocks();Generator g(cfg,r,1337);auto dry=cfg;dry.caves.enabled=false;
    Generator bare(dry,r,1337);
    // Exact irregular mouth: shrinking every opening by the maximum noise
    // used to leave the second voxel dry even though it shared a water face.
    auto coast_regression=g.underground()->hydrology({-2,2});
    assert(coast_regression.source==WaterSource::surface_water && coast_regression.top==cfg.sea_level);
    assert(bare.sample(-1357,42,1892)==r.water && r.get(bare.sample(-1358,42,1892)).solid);
    assert(g.sample(-1357,42,1892)==r.water && g.sample(-1358,42,1892)==r.water);
    bool found=false;size_t openings=0,wet_openings=0;
    for(int bz=-2;bz<=2 && !found;bz++)for(int bx=-2;bx<=2 && !found;bx++) {
        auto plan=g.underground()->hydrology({bx,bz});
        if(plan.source!=WaterSource::ocean)continue;
        assert(plan.top==cfg.sea_level);
        found=true;
        for(int z=bz*3;z<bz*3+3;z++)for(int x=bx*3;x<bx*3+3;x++) {
            auto n=g.underground()->region({x,z});
            for(auto &p:n->network.passages)if(p.entrance) {
                bool touch=false;openings++;
                for(auto q:p.path) {
                    int wx=int(std::lround(q[0])),y=int(std::lround(q[1])),wz=int(std::lround(q[2]));
                    assert(!r.get(g.sample(wx,y,wz)).solid);
                    if(y<=cfg.sea_level)assert(g.sample(wx,y,wz)==r.water);
                    if(bare.sample(wx,y,wz)==r.water && bare.column(wx,wz).region==5)touch=true;
                }
                wet_openings+=touch;
            }
        }
    }
    assert(found && openings && wet_openings);
    std::cout<<"CAVE_COAST_PASS openings="<<openings<<" ocean_path_contacts="<<wet_openings<<'\n';
}
static void verify_colliders() {
    auto r=blocks();Volume v;v.min_y=-192;v.height=7;
    v.columns.resize(34*34);v.data.resize(34*34*(v.height+2));
    for(int z=-1;z<=32;z++)for(int x=-1;x<=32;x++) {
        v.data[voxel_index(v,x,1,z)]=((x+z)&1)?r.stone:r.dirt;
        if(x<11 && z<16)v.data[voxel_index(v,x,2,z)]=r.water;
        if(x>21 && z<7)v.data[voxel_index(v,x,2,z)]=r.lava;
        if(x>9 && x<13 && z>18 && z<25)v.data[voxel_index(v,x,2,z)]=r.stone;
    }
    auto slab=voxel_index(v,14,2,11);
    v.data[slab]=r.stone;v.shapes[slab]=pack_shape(1,0);
    auto ramp=voxel_index(v,9,2,24);
    v.data[ramp]=r.dirt;v.shapes[ramp]=pack_shape(3,1);
    MeshResult m;greedy(m,v,r,true);
    using Key=std::array<int,5>;
    auto coverage=[](const std::vector<std::array<float,3>> &faces) {
        std::map<Key,int> cells;
        assert(faces.size()%6==0);
        for(size_t i=0;i<faces.size();i+=6) {
            auto low=faces[i],high=faces[i];
            for(size_t k=1;k<6;k++)for(int axis=0;axis<3;axis++){
                low[axis]=std::min(low[axis],faces[i+k][axis]);high[axis]=std::max(high[axis],faces[i+k][axis]);
            }
            int d=low[0]==high[0]?0:low[1]==high[1]?1:2;
            int u=(d+1)%3,w=(d+2)%3;
            assert(low[d]==high[d] && high[u]>low[u] && high[w]>low[w]);
            const auto &a=faces[i],&b=faces[i+1],&c=faces[i+2];
            int sign=(b[u]-a[u])*(c[w]-a[w])-(b[w]-a[w])*(c[u]-a[u])<0?-1:1;
            for(int y=int(low[w]);y<int(high[w]);y++)for(int x=int(low[u]);x<int(high[u]);x++)
                cells[{d,int(low[d]),x,y,sign}]++;
        }
        return cells;
    };
    std::vector<std::array<float,3>> render;
    for(auto i:m.solid.indices){const auto &p=m.solid.vertices[i];render.push_back({p.x,p.y,p.z});}
    std::vector<std::array<float,3>> colliders;
    for(auto &r:m.collision_rects)for(auto p:r.faces())colliders.push_back(p);
    assert(colliders.size()<render.size()/2);
    assert(!m.water.indices.empty());
    assert(coverage(render)==coverage(colliders));
    auto cfg=settings();cfg.caves.enabled=false;Generator g(cfg,r,1337);
    auto edits=std::make_shared<EditMap>();auto appearances=std::make_shared<AppearanceMap>();
    (*edits)[{11,305,13}]=r.stone;(*edits)[{14,305,13}]=r.dirt;
    (*appearances)[{11,305,13}].shape=1;
    (*appearances)[{14,305,13}].shape=3;(*appearances)[{14,305,13}].rotation=1;
    auto tile=build_tile({0,0,0},1,g,edits,appearances,false);
    MeshResult partial;partial_blocks(partial,*tile.volume,r);
    assert(!partial.solid.indices.empty() && tile.partial_collision_faces.size()==partial.solid.indices.size());
    for(size_t i=0;i<tile.partial_collision_faces.size();i++) {
        auto &p=partial.solid.vertices[partial.solid.indices[i]];
        assert((tile.partial_collision_faces[i]==std::array<float,3>{p.x,p.y,p.z}));
    }
    for(size_t i=0;i<partial.solid.indices.size();i+=3) {
        const auto &a=partial.solid.vertices[partial.solid.indices[i]];
        const auto &b=partial.solid.vertices[partial.solid.indices[i+1]];
        const auto &c=partial.solid.vertices[partial.solid.indices[i+2]];
        float cross_x=(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y);
        float cross_y=(b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z);
        float cross_z=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
        assert(cross_x*a.nx+cross_y*a.ny+cross_z*a.nz<=.000001f);
    }
    std::cout<<"CAVE_COLLIDER_PASS visual_solid_triangles="<<render.size()/3<<" collision_triangles="
             <<colliders.size()/3<<" packed_rectangle_bytes="<<sizeof(CollisionRect)
             <<"; oriented coverage is identical around solids, water, lava and partial bases\n";
}
static void verify_seam_gaps() {
    auto r=blocks();r.blocks[r.stone].texture_layer=1;r.blocks[r.grass].texture_layer=2;
    Volume v;v.step=2;v.y_step=2;v.min_y=-32;v.height=32;
    v.columns.resize(34*34);for(auto &c:v.columns)c.height=24;
    v.data.resize(34*34*(v.height+2),r.air);
    for(int z=-1;z<=32;z++)for(int x=-1;x<=32;x++)for(int y=0;y<v.height;y++) {
        int wy=v.min_y+y*v.y_step;
        if(wy<=24 && !(wy>=-16 && wy<-4))v.data[voxel_index(v,x,y,z)]=wy==24?r.grass:r.stone;
        if(wy==-14)v.data[voxel_index(v,x,y,z)]=r.water;
    }
    Settings cfg;cfg.min_y=-32;cfg.cliff_strength=0;
    Generator g(cfg,r,1337);
    MeshResult mesh;mesh.key={0,0,1};
    collect_terrain_edges(mesh,v,g);
    const auto &edge=mesh.edges[0][0];
    auto neighbor=edge;
    assert(!neighbor.spans.empty() && neighbor.spans.back().material.texture==2);
    neighbor.spans.back().top-=1;
    Surface join;
    append_terrain_seam(join,2,0.f,0.f,1.f,edge,neighbor);
    assert(join.vertices.size()==4);
    for(const auto &vertex:join.vertices)
        assert(vertex.y>=25 && vertex.y<=26 && vertex.texture==2);
    std::cout<<"CAVE_SEAM_PASS: mixed LOD ground preserves the shared water and air opening\n";
}
int main(){solve_fixture();verify_pool(false);verify_pool(true);verify_coast();verify_colliders();verify_seam_gaps();}
