#include "game/world_generation/voxel_core.h"
#include <iostream>
#include <cmath>
using namespace altaria;
int main(){
 Settings s;s.generator_version=3;s.min_y=-896;s.continental_scale=5300;s.mountain_scale=680;s.mountain_amplitude=204;s.warp_amplitude=34;s.cliff_strength=6;s.river_width=.028f;
 Registry r;std::string e;
 for(int i=0;i<8;i++){BlockDef b;b.id=i;b.key=std::to_string(i);b.solid=i!=0&&i!=5;r.add(b,e);}
 Generator g(s,r,1337);
 std::vector<float> x,z;
 constexpr int step=24,span=321;
 for(int j=0;j<span;j++)for(int i=0;i<span;i++){x.push_back((i-span/2)*step);z.push_back((j-span/2)*step);}
 auto c=g.columns(x,z);
 float bestCoast=0,bestRiver=0;
 for(int j=8;j<span-8;j++)for(int i=8;i<span-8;i++){
  const int k=j*span+i;
  if(c[k].height>=s.sea_level&&c[k].height<60&&c[k].slope>.32f&&c[k].beach_mask<.2f){
   for(int d:{-1,1,-span,span})if(c[k+d*3].region==5&&c[k-d*4].height>75){
    const float score=c[k-d*4].height-std::hypot(x[k],z[k])*.04f;
    if(score>bestCoast){bestCoast=score;std::cout<<"COAST "<<x[k+d*5]<<' '<<z[k+d*5]<<" -> "<<x[k-d*4]<<' '<<c[k-d*4].height<<' '<<z[k-d*4]<<'\n';}
   }
  }
  if(c[k].river>.6f&&c[k].height<s.sea_level-1&&c[k].region!=5){
   const float score=c[k].river-std::hypot(x[k],z[k])*.0003f;
   if(score>bestRiver){bestRiver=score;std::cout<<"RIVER "<<x[k]<<' '<<z[k]<<" height="<<c[k].height<<'\n';}
  }
 }
}
