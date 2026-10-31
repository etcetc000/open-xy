// Aux modules: synths that declare "aux" and that the user puts on aux tracks
// 1..6 (stock tracks 8..13: brain, punch-in, MIDI, CV, audio and tape) with
// Shift and M1, as an engine on an instrument track; Shift and the track's key
// then list the module's presets. Generated metadata lists the collection's
// modules; GUI state is in aux_modules.cpp.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
const u32 kAuxFirst = 8;   // aux track 1, stock's brain
const u32 kAuxTracks = 6;  // aux tracks 1..6; 7 and 8 are the sends
// A module's four controls are its aux track's words 4..7. Stock's own module
// on the track keeps words 0..3, and no stock aux page shows words 4..7.
const u32 kAuxControlWord = 4;
// The most modules a selector lists, beside the track's stock module, and the
// most presets a module lists (tooling/definitions.py checks it).
const u32 kAuxModuleLimit = 16;
const u32 kAuxPresetLimit = 32;

// A module, and for each control the instrument-track parameter word it sets
// on tracks 0..7 while it is on an aux track (a macro), or -1; and its presets:
// names (1..15 printable characters) and each one's four control words. In a
// browser kit an unselected machine's ready resolves to a function returning 0.
struct AuxModule {
    const char* id;    // absolute symbol: the engine ID, supplied at link time
    const char* page;  // absolute symbol: its page ID
    int (*ready)();
    const char* label;  // the selector's row, 1..15 printable characters
    signed char macro[4];
    u32 presets;
    const char* const* preset_names;
    const i32 (*preset_values)[4];
    u32 engine() const { return static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(id)); }
    u32 screen() const { return static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(page)); }
};
}}

extern "C" {
extern const opxy::platform::AuxModule sdk_aux_modules[] __asm__("sdk_aux_modules");
extern const opxy::u32 sdk_aux_module_count __asm__("sdk_aux_module_count");
}
