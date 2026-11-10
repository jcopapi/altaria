#include "game/world_generation/terrain/smooth_field.h"
#include "game/world_generation/block_shapes.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <chrono>
#include <sstream>
using namespace altaria;
static int checks=0;
static void check(bool condition,const char *message) {++checks;if(!condition){std::cerr<<"FAIL: "<<message<<"\n";std::exit(1);}}
int main() {
    std::vector<SmoothCell> staircase;
    for(int x=0;x<4;x++)for(int y=0;y<=x;y++) {SmoothCell c;c.origin={float(x),float(y),0};c.block=3;c.owner=staircase.size()+1;c.modified=true;staircase.push_back(c);}
    SmoothField slope(staircase);
    check(slope.distance({.9f,1.1f,.5f})<0,"Smoothing fills the diagonal between steps, beyond rounded stair corners");
    for(auto &c:staircase)c.modified=false;
    SmoothField cubic(staircase);
    check(cubic.distance({.9f,1.1f,.5f})>0,"Removing modifier recovers the cubic staircase");
    for(auto &c:staircase)c.modified=true;
    std::reverse(staircase.begin(),staircase.end());SmoothField reordered(staircase);
    for(int z=-2;z<6;z++)for(int y=-2;y<18;y++)for(int x=-2;x<18;x++) {
        std::array<float,3> p{x*.25f,y*.25f,z*.25f};
        check(slope.distance(p)==reordered.distance(p),"Density is independent of source insertion order");
    }
    SmoothCell c;c.block=3;c.modified=true;
    SmoothField isolated({c});
    check(isolated.distance({.5f,.5f,.5f})<0,"Isolated modified cube retains a solid core");
    check(isolated.distance({.03f,.03f,.5f})>0,"Isolated cube corners are rounded in all axes");
    auto owner=isolated.owner_at({.5f,.5f,.5f});check(owner && owner->block==3,"Mesh ownership retains block identity");
    c.modified=false;SmoothField ordinary({c});
    check(ordinary.distance({.03f,.03f,.5f})<0,"Ordinary block preserves its cube corners");
    Registry registry;std::string error;
    for(int i=0;i<8;i++){BlockDef b;b.id=i;b.key="block:"+std::to_string(i);b.solid=i!=0 && i!=5;check(registry.add(b,error),"Registry fixture");}
    Settings settings;WorldModel model(std::make_shared<Generator>(settings,registry,1337));
    auto filename="/tmp/altaria-terrain-modifier-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".log";
    SurfaceJournal journal(filename);
    Appearance original;original.shape=2;original.rotation=3;original.painted=true;original.tint=0x214365;
    auto modified=original;modified.terrain=1;
    check(journal.append({1,1,{0,220,0},modified},error),"S3 persists independent modifier");
    AppearanceMap restored;
    check(journal.load(restored,registry,settings,error),"S3 restores modifier record");
    check(restored.at({0,220,0})==modified,"Original form, rotation, paint and Terrain survive reload");
    modified.terrain=0;
    check(journal.append({1,2,{0,220,0},modified},error),"S3 persists explicit removal");
    restored.clear();check(journal.load(restored,registry,settings,error),"Removal restores on reload");
    check(restored.at({0,220,0}).shape==original.shape && restored.at({0,220,0}).rotation==original.rotation && restored.at({0,220,0}).terrain==0,"Removing Terrain does not replace original shape or rotation");
    // The old unversioned/S2 checksum remains readable. Generate a valid S3
    // record with default modifier, then remove only its extra tag and column.
    Appearance legacy=original;legacy.terrain=-1;
    check(journal.append({2,1,{1,220,0},legacy},error),"Default modifier record");
    std::vector<std::string> lines;{std::ifstream f(filename);std::string line;while(std::getline(f,line))lines.push_back(line);}
    { std::istringstream words(lines.back());std::vector<std::string> tokens;std::string w;while(words>>w)tokens.push_back(w);
      check(tokens.size()==16,"S3 layout includes separate modifier column");tokens[0]="S2";tokens.erase(tokens.begin()+14);
      std::ostringstream line;for(auto &t:tokens)line<<t<<" ";lines.back()=line.str();}
    {std::ofstream f(filename);for(auto &line:lines)f<<line<<"\n";}
    restored.clear();check(journal.load(restored,registry,settings,error),"S2 records remain readable beside S3 records");
    check(restored.at({1,220,0})==original,"Legacy default modifier preserves original shape");
    IVec3 mined{3,220,0};
    check(model.apply({1,1,mined,3},error),"Place mining regression source");
    auto active=original;active.terrain=1;
    check(model.apply_surface({1,2,mined,active},error),"Apply modifier before mining");
    check(model.apply({1,3,mined,0},error),"Mine source into air");
    active.terrain=0;
    check(model.apply_surface({1,4,mined,active},error),"Modifier-only reset succeeds after mining");
    auto invalid=active;invalid.tint=0xff00ff;
    check(!model.apply_surface({1,5,mined,invalid},error),"Reset exemption cannot paint an air cell");
    invalid=active;invalid.terrain=1;
    check(!model.apply_surface({1,6,mined,invalid},error),"Air cannot receive a positive modifier");
    std::filesystem::remove(filename);
    std::cout<<"TERRAIN_MODIFIER_CORE_PASS "<<checks<<" checks\n";
}
