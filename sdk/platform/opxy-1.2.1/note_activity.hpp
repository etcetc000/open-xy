// GUI-side note gates. Only this platform layer knows stock's note map and
// release routing. No audio callback reads or writes this state.
#pragma once
#include <opxy/draw.hpp>

extern "C" {
void sdk_display_note_activity(opxy::u32 track, opxy::draw::Input* input) __asm__("sdk_display_note_activity");
opxy::u32 sdk_note_off(opxy::u32 track, opxy::u32 note) __asm__("sdk_note_off");
}
