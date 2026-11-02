// Stock OS 1.2.1 keyboard octaves, published for the audio side
// (keyboard_octaves.hpp). Linked with the octaves service, which every machine
// kind that maps keys by position uses.
#include "keyboard_octaves.hpp"
#include "barrier.hpp"
#include "stock.hpp"

extern "C" {
opxy::u32 sdk_octave_store(opxy::u32, opxy::i32) __asm__("sdk_octave_store");
}
volatile opxy::i32 sdk_track_octaves[8];

using opxy::i32;
using opxy::u32;
using namespace opxy::platform;

void sdk_publish_octave(u32 track, i32 octave) {
    if (track >= kTracks) return;
    sdk_track_octaves[track] = octave;
    barrier();
}

void opxy::platform::refresh_keyboard_octave(u32 track) {
    if (track >= kTracks) return;
    sdk_publish_octave(track, stock<i32(u32)>(0x8d58f65c)(track));
}

void opxy::platform::refresh_keyboard_octaves() {
    for (u32 track = 0; track < kTracks; ++track) refresh_keyboard_octave(track);
}

// 8d58f5c2: stock clamps and stores the octave on the GUI side. Read back its
// effective keyboard octave: some instrument types step by two octaves.
u32 sdk_octave_store(u32 track, i32 octave) {
    const u32 result = stock<u32(u32, i32)>(0x8d58573c)(track, octave);
    refresh_keyboard_octave(track);
    return result;
}
