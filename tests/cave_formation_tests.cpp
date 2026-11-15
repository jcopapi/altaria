#include "game/world_generation/terrain_mesh.cpp"
#include <cassert>
#include <iostream>
#include <set>
using namespace altaria;
using namespace altaria::caves;

static void shape_cases() {
    Chamber room{1337,{60,-130,-80},{45,28,38},2,false,"giant"};
    for(int k=0;k<=int(FormationKind::vent);k++) {
        FormationRule rule;rule.id="fixture:formation_"+std::to_string(k);rule.kind=FormationKind(k);
        rule.radius={5.5f,5.5f};rule.height={10,10};
        assert(valid_formation(rule));
        auto a=plan_formation(room,rule,0,8,9),b=plan_formation(room,rule,0,8,9);
        assert(a.id==b.id && a.base==b.base && a.box.lo==b.box.lo && a.box.hi==b.box.hi);
        size_t count=0;
        for(int y=int(std::ceil(a.box.lo[1]));y<=a.box.hi[1];y++)
            for(int z=int(std::ceil(a.box.lo[2]));z<=a.box.hi[2];z++)
                for(int x=int(std::ceil(a.box.lo[0]));x<=a.box.hi[0];x++)
                    count+=formation_voxel(a,{float(x),float(y),float(z)},false)>0;
        assert(count>25);
        auto outside=a.box.hi;outside[0]+=1;
        assert(!formation_voxel(a,outside,false));
        if(rule.kind==FormationKind::stalactite)assert(a.vertical==-1);
        if(rule.kind==FormationKind::root)assert(a.vertical==1);
    }
    FormationRule ice;ice.id="fixture:lake_ice";ice.kind=FormationKind::ice_sheet;ice.water=FormationWater::wet;
    auto patch=plan_formation(room,ice,0,8,8);
    auto p=patch.base;p[1]=-130;
    assert(formation_voxel(patch,p,true,false,-130)==8);
    assert(!formation_voxel(patch,p,false,false,-130));
    assert(!formation_voxel(patch,p,false,true,-130));
    assert(!formation_voxel(patch,p,true,false,-131));
}
static void support_cases() {
    const FormationKind kinds[]={FormationKind::crystal,FormationKind::fossil,FormationKind::root,FormationKind::fungus};
    size_t kept=0,removed=0,checked=0;
    for(auto kind:kinds)for(uint64_t id=1;id<=25;id++) {
        Chamber room{id,{60.125f,-130.37f,-80.23f},{45,28,38},2,false,"giant"};
        FormationRule rule;rule.id=kind==FormationKind::fossil?"altaria:fossil":kind==FormationKind::root?"altaria:roots":
                                        kind==FormationKind::crystal?"altaria:crystal":"altaria:fungus";
        rule.kind=kind;rule.radius={3.2f,8.8f};rule.height={3.5f,15};rule.count=6;
        auto carved=[&](Point p) {
            float d=0;for(int a=0;a<3;a++){float q=(p[a]-room.center[a])/room.radius[a];d+=q*q;}
            return d<1;
        };
        for(int index=0;index<6;index++) {
            auto f=plan_formation(room,rule,index,8,9);
            auto mask=support_formation(f,[&](int x,int y,int z) {
                FormationCell c;c.rock=true;c.allowed=true;c.carved=carved({float(x),float(y),float(z)});return c;
            });
            std::set<std::array<int,3>> voxels;
            for(int z=mask.origin[2];z<mask.origin[2]+mask.size[2];z++)
                for(int y=mask.origin[1];y<mask.origin[1]+mask.size[1];y++)
                    for(int x=mask.origin[0];x<mask.origin[0]+mask.size[0];x++)
                        if(mask.contains({float(x),float(y),float(z)}))voxels.insert({x,y,z});
            kept+=mask.kept;removed+=mask.removed;checked++;
            while(!voxels.empty()) {
                std::vector<std::array<int,3>> wave{*voxels.begin()};voxels.erase(wave[0]);bool anchored=false;
                for(size_t cursor=0;cursor<wave.size();cursor++)for(int axis=0;axis<3;axis++)for(int step:{-1,1}) {
                    auto next=wave[cursor];next[axis]+=step;
                    anchored|=!carved({float(next[0]),float(next[1]),float(next[2])});
                    if(voxels.erase(next))wave.push_back(next);
                }
                assert(anchored);
            }
        }
    }
    assert(kept>10000 && removed>0);
    Chamber tall{19,{60,-100,60},{80,200,80},3,false,"giant"};
    FormationRule pillar;pillar.id="fixture:tall_pillar";pillar.kind=FormationKind::pillar;pillar.radius={3,3};
    auto p=plan_formation(tall,pillar,0,8,8);
    assert(p.height>256);
    size_t probes=0;
    auto mask=support_formation(p,[&](int,int y,int) {
        probes++;FormationCell c;c.rock=true;c.allowed=true;
        c.carved=y>p.box.lo[1]+2 && y<p.box.hi[1]-2;return c;
    });
    assert(probes>0 && mask.kept>1000);
    std::cout<<"CAVE_FORMATION_SUPPORT_PASS placements="<<checked<<" supported="<<kept<<" removed="<<removed<<'\n';
}
static Registry blocks() {
    Registry r;std::string error;
    const char *keys[]={"air","stone","dirt","grass","sand","water","snow","bedrock","fixture:detail","fixture:accent"};
    for(int i=0;i<10;i++) {
        BlockDef b;b.id=i;b.key=keys[i];b.solid=i!=0 && i!=5;b.water=i==5;b.liquid=b.water;
        assert(r.add(b,error));
    }
    BlockDef root;
    root.id = 10;
    root.key = "fixture:root";
    root.solid = false;
    root.placeable = false;
    root.root_plant = true;
    assert(r.add(root, error));
    return r;
}
static void every_profile_in_world() {
    auto r=blocks();std::string error;
    for(int kind=0;kind<=int(FormationKind::vent);kind++) {
        Settings s;s.min_y=-448;s.vegetation=false;s.caves.enabled=true;
        s.caves.plan.archetypes={{"fixture:wide",0,3,{36,12,36},{50,14,50},1}};
        FormationRule f;f.kind=FormationKind(kind);f.id="fixture:profile";
        f.rock="fixture:detail";f.accent="fixture:accent";f.count=6;
        if (f.kind == FormationKind::root) {
            f.rock = f.accent = "fixture:root";
            f.ceiling = true;
        }
        f.radius={4,6};f.height={8,13};
        s.caves.biomes.formations={f};s.caves.biomes.definitions[0].formations={f.id};
        Generator g(s,r,1337);assert(g.valid_generation(error));
        auto plan=g.underground()->region({-1,0});bool observed=false;
        for(const auto &feature:plan->formations) {
            auto q=g.underground()->query(feature.box);
            auto mask=q.support.at(feature.id);if(!mask->kept)continue;
            size_t cell=0;while(!(mask->bits[cell/64]&(uint64_t(1)<<(cell%64))))cell++;
            int x=mask->origin[0]+int(cell%mask->size[0]);
            int y=mask->origin[1]+int((cell/mask->size[0])%mask->size[1]);
            int z=mask->origin[2]+int(cell/(size_t(mask->size[0])*mask->size[1]));
            auto id=g.terrain_sample(x,y,z);
            assert(f.kind == FormationKind::root ? id == 10 && !r.get(id).solid : id == 8 || id == 9);
            StageTimings t;TileKey tile{int(std::floor(x/32.0))*32,int(std::floor(z/32.0))*32,0};
            auto volume=make_volume(tile,g,std::make_shared<EditMap>(),std::make_shared<AppearanceMap>(),t);
            assert(volume->at(x-tile.x,y-volume->min_y,z-tile.z)==id);
            if (f.kind == FormationKind::root) {
                MeshResult mesh;
                mesh.key = tile;
                root_cards(mesh, *volume, g);
                if (mesh.solid.vertices.empty())
                    continue;
                assert(mesh.partial_collision_faces.empty());
            }
            observed=true;break;
        }
        assert(observed);
    }
}
static void root_lighting() {
    const auto r = blocks();
    const Generator g(Settings{}, r, 1337);
    Volume volume;
    volume.height = 8;
    volume.data.resize(34 * 34 * (volume.height + 2), r.air);
    volume.has_root_plants = true;
    volume.data[voxel_index(volume, 4, 2, 4)] = 10;
    volume.data[voxel_index(volume, 4, 1, 4)] = r.stone;
    volume.data[voxel_index(volume, 12, 5, 4)] = 10;
    volume.data[voxel_index(volume, 12, 6, 4)] = r.stone;
    volume.data[voxel_index(volume, 20, 4, 4)] = 10;
    MeshResult mesh;
    root_cards(mesh, volume, g);
    assert(mesh.solid.vertices.size() == 16 && mesh.root_lights.size() == 1);
    assert(mesh.root_lights[0][1] < 6.f && mesh.root_lights[0][1] > 5.f);
    for (const auto &vertex : mesh.solid.vertices)
        assert(vertex.y >= 5.f && vertex.v == (vertex.y == 6.f ? 0.f : 1.f));
}
int main() {
    shape_cases();support_cases();every_profile_in_world();root_lighting();
    Registry r=blocks();Settings s;s.min_y=-448;s.vegetation=false;s.caves.enabled=true;
    s.caves.plan.entrance_chance=1;s.caves.plan.infernal_chance=1;
    Archetype chamber{"giant",0,3,{36,10,36},{50,14,50},1};
    s.caves.plan.archetypes={chamber};
    auto bare=s;
    FormationRule detail;detail.id="fixture:spire";detail.kind=FormationKind::stalagmite;
    detail.rock="fixture:detail";detail.accent="fixture:accent";detail.count=6;
    detail.radius={4,6};detail.height={8,13};
    s.caves.biomes.formations.push_back(detail);
    s.caves.biomes.definitions[0].formations.push_back(detail.id);
    Generator g(s,r,1337),base(bare,r,1337);
    std::string error;assert(g.valid_generation(error));
    auto plan=g.underground()->region({-1,0});assert(plan && !plan->formations.empty());
    size_t tested=0,placed=0,accented=0,passage_points=0;
    IVec3 edit;bool have_edit=false;
    const auto &f=plan->formations.front();
    for(const auto &p:plan->network.passages)for(auto point:p.path) {
        int x=int(std::lround(point[0])),y=int(std::lround(point[1])),z=int(std::lround(point[2]));
        if(!r.get(base.terrain_sample(x,y,z)).solid){assert(!r.get(g.terrain_sample(x,y,z)).solid);passage_points++;}
    }
    for(int level:{0,1,2}) {
        int step=1<<level,size=32*step;
        TileKey key{int(std::floor(f.base[0]/size))*size,int(std::floor(f.base[2]/size))*size,level};
        StageTimings t;auto empty=std::make_shared<EditMap>();auto surface=std::make_shared<AppearanceMap>();
        auto v=make_volume(key,g,empty,surface,t);
        for(int x=-1;x<=32;x++)for(int z=-1;z<=32;z++) {
            int wx=key.x+x*step,wz=key.z+z*step;
            for(int i=1;i<v->height;i++) {
                auto id=v->at(x,i,z);
                if(id!=8 && id!=9)continue;
                int y=v->min_y+i*v->y_step;
                assert(g.terrain_sample(wx,y,wz)==id);
                assert(base.terrain_sample(wx,y,wz)==r.air);
                tested++;placed++;accented+=id==9;
                if(level==0 && x>=0 && x<32 && z>=0 && z<32 && !have_edit){edit={wx,y,wz};have_edit=true;}
            }
        }
        for(auto offset:{IVec3{-size,0,0},IVec3{size,0,0},IVec3{0,0,-size},IVec3{0,0,size}}) {
            auto neighbor=make_volume({key.x+offset.x,key.z+offset.z,level},g,empty,surface,t);
            for(int edge=0;edge<32;edge++)for(int y=1;y<v->height;y++) {
                int ax=offset.x<0?-1:offset.x>0?32:edge,az=offset.z<0?-1:offset.z>0?32:edge;
                int bx=offset.x<0?31:offset.x>0?0:edge,bz=offset.z<0?31:offset.z>0?0:edge;
                assert(v->at(ax,y,az)==neighbor->at(bx,y,bz));
            }
        }
        if(level==0) {
            assert(have_edit);
            auto removed=std::make_shared<EditMap>();(*removed)[edit]=r.air;
            auto after=make_volume(key,g,removed,surface,t);
            assert(after->at(edit.x-key.x,edit.y-v->min_y,edit.z-key.z)==r.air);
        }
    }
    assert(tested>30 && placed>30 && accented>0 && passage_points>50);
    std::cout<<"CAVE_FORMATIONS_PASS profiles=10 formed_voxels="<<placed<<" accent_voxels="<<accented
             <<" protected_path_points="<<passage_points<<"; scalar/LOD0-2, 4 halos and edit-last\n";
}
