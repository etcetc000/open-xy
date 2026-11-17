// M1 matches the original V1 SDK selector: stock slots 0..3, metadata-driven SDK alternates.
#pragma once
#include <opxy/types.hpp>
namespace morph_m1 {
using opxy::u32;
const u32 kMissing=0xffffffffu;
struct Map {
 u32 primary[4];
 u32 alternate[4];
 u32 shown[4];
 u32 dual;
 u32 secondary;
};
inline void clear(Map& m) {
 for(u32 i=0;i<4u;++i) m.primary[i]=m.alternate[i]=m.shown[i]=kMissing;
 m.dual=0u;m.secondary=0u;
}
inline bool in_supported_bank(u32 slot) {return slot<8u;}
inline bool supported_for_edit(const Map& m,u32 position,u32& out) {
 if(position>=4u || !in_supported_bank(m.shown[position]))return false;
 out=m.shown[position];return true;
}
}
extern "C" bool sdk_morph_hud_m1_map(opxy::u32, morph_m1::Map*)
 __asm__("sdk_morph_hud_m1_map");
extern "C" bool sdk_morph_hud_m1_toggle(opxy::u32)
 __asm__("sdk_morph_hud_m1_toggle");
