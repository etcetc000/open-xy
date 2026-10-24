// Each instrument track's keyboard octave, published for the audio side
// (keyboard_octaves.cpp). Audio maps keys with it instead of calling GUI
// project getters, which can assert on the audio role's unavailable pattern
// state. Stock's octave setter publishes every change, also while a project
// or preset loads; GUI-side lookups may publish what they read.
#pragma once
#include <opxy/types.hpp>

extern "C" {
extern volatile opxy::i32 sdk_track_octaves[8] __asm__("sdk_track_octaves");
void sdk_publish_octave(opxy::u32 track, opxy::i32 octave) __asm__("sdk_publish_octave");
}

namespace opxy { namespace platform {
// At octave 0 the keyboard's lowest key, F, plays note 53; each octave adds 12.
const u32 kLowestKey = 53;
}}
