// Private Morph MIDI event access. GUI-side stock CC dispatch only. C++11.
#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace morphmidi {
inline bool matches(u32 track,u32 cc,u32 value){return track==11u && cc==15u && value<=127u;}
struct Latest {
 u32 value,epoch;bool pending;
 void clear(){pending=false;}
 void offer(u32 next,u32 generation){value=next;epoch=generation;pending=true;}
};
}}
extern "C" void sdk_morph_midi_cc(opxy::u32 track,opxy::u32 cc,opxy::u32 value)
    __asm__("sdk_morph_midi_cc");
extern "C" bool sdk_morph_midi_take(opxy::u32* value);
extern "C" void sdk_morph_midi_clear();
