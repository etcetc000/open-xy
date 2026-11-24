// Each instrument track's keyboard octave, published for the audio side
// (keyboard_octaves.cpp). Audio maps keys with it instead of calling GUI
// project getters, which can assert on the audio role's unavailable pattern
// state. Publish the effective keyboard octave, which stock may scale for
// the instrument type, rather than its raw stored octave control.
#pragma once
#include <opxy/types.hpp>

extern "C" {
extern volatile opxy::i32 sdk_track_octaves[8] __asm__("sdk_track_octaves");
void sdk_publish_octave(opxy::u32 track, opxy::i32 octave) __asm__("sdk_publish_octave");
}

namespace opxy { namespace platform {
// GUI only: refresh after a stored-octave edit. The player compositor refreshes
// all tracks once per frame, including while other pages are selected, to catch
// project/instrument changes. Note/audio callbacks use the published word.
void refresh_keyboard_octave(u32 track);
void refresh_keyboard_octaves();
// At octave 0 the keyboard's lowest key, F, plays note 53; each octave adds 12.
const u32 kLowestKey = 53;
}}
