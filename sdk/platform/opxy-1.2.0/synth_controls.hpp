// Owned synth/FX controls: generated metadata for alternate selection,
// continuous fine turns and synth captions; GUI state lives in synth_controls.cpp.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
// A synth or effect. Pressing a synth encoder moves its
// position from slot s to next[s]; each position starts on its own slot 0..3,
// and a slot no position reaches maps to itself. In a browser kit an
// unselected machine's ready resolves to a function returning 0. `choices`
// counts each enum control's labels (0 for the others).
struct SynthControls {
    const char* id;  // absolute symbol: the engine or effect ID, supplied at link time
    int (*ready)();
    u8 next[parameter_slots];
    u32 continuous;
    bool (*format)(u32 slot, i32 value, char* text);
    bool effect;
    u8 choices[parameter_slots];
    u8 absent;  // bit s: slot s is an empty placeholder, shown blank and never edited
    u32 engine() const { return static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(id)); }
};
// GUI-only, bounded one-decimal formatting. Caller provides 16 bytes.
void control_tenths(char* text, u32 tenths, const char* suffix);
u32 control_ratio(i32 value, const u32* anchors, u32 count);
// Stock delay's name for the note length a division control selects.
void control_division(char* text, i32 value);
}}

extern "C" {
extern const opxy::platform::SynthControls sdk_synth_controls[] __asm__("sdk_synth_controls");
extern const opxy::u32 sdk_synth_control_count __asm__("sdk_synth_control_count");
// The parameter slot each of a track's four page positions edits and shows:
// 0..3 unless an owned synth's encoder press selected an alternate. GUI only.
void sdk_synth_shown(opxy::u32 track, opxy::u8* shown) __asm__("sdk_synth_shown");
bool sdk_synth_value(opxy::u32 track, opxy::u32 slot, opxy::i32 value, char* text) __asm__("sdk_synth_value");
// Stock's step of an owned synth's enum control (an instrument track's slot
// 0..7) for an encoder event: the next choice's midpoint in `stepped`, or false
// to keep stock's own step. GUI only.
bool sdk_synth_step(opxy::u32 track, opxy::u32 slot, opxy::i32 value, const opxy::i32* event, opxy::i32& stepped)
    __asm__("sdk_synth_step");
// The slot shown at page position `index` while engine is the selected track's
// owned synth, as last refreshed; other indices and engines map to themselves.
// For stock control titles.
opxy::u32 sdk_synth_title_slot(opxy::u32 engine, opxy::u32 index) __asm__("sdk_synth_title_slot");
// The send page's value text for position `index` of an owned effect, through
// the hook in screen_hooks.s; every other caption reaches stock unchanged.
opxy::u32 sdk_fx_value_text(opxy::u32 widget, const char* text, opxy::u32 index, opxy::i32 value)
    __asm__("sdk_fx_value_text");
}
