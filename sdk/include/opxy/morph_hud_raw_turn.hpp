// Morph v0.0.6: physical A-bank pulse decoder. GUI-only, plain POD.
// Mirrors V1's two signed raw pulses per detent. Not a stock-turn index.
#pragma once
#include <opxy/types.hpp>
namespace morph_raw_turn {
using opxy::u32; using opxy::i32;
struct Sampler {
 u32 previous[4]; i32 half[4];
 u32 primed,shift,k4;
 void reset() { for(u32 i=0;i<4u;++i){previous[i]=0u;half[i]=0;}primed=0u;shift=0u;k4=0u; }
 void sample(const u32 (&raw)[4], bool shifted,bool k4held,i32 (&steps)[4]) {
  for(u32 i=0;i<4u;++i)steps[i]=0;
  if(!primed || shift!=(shifted?1u:0u) || k4!=(k4held?1u:0u)) {
   for(u32 i=0;i<4u;++i){previous[i]=raw[i];half[i]=0;}
   primed=1u;shift=shifted?1u:0u;k4=k4held?1u:0u;return;
  }
  for(u32 i=0;i<4u;++i){
   const u32 now=raw[i],prior=previous[i];previous[i]=now;
   if(now==prior || now==0u)continue;
   half[i]+=(now&0x80000000u)?-1:1;
   if(half[i]>=2){steps[i]=1;half[i]-=2;}
   else if(half[i]<=-2){steps[i]=-1;half[i]+=2;}
  }
 }
};
static_assert(sizeof(Sampler)==44u,"raw pulse sampler size changed");
}
