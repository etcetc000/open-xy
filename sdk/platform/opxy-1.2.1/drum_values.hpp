// GUI-region decoding shared by the screen and the optional pad editor.
#pragma once
#include "drum_machines.hpp"
#include "drum_storage.hpp"
#include "stock.hpp"

namespace opxy { namespace platform {
inline bool drum_region_values(const DrumMachine& machine, u32 track, u32 key,
                               const void* region, u8* output) {
    if (!region || !output) return false;
    // Stock's GUI record stores its canonical pad key at +26; the selected
    // key passed by the controller can instead be the transposed parent note.
    // The audio adapter also uses this canonical 53..76 pad for defaults.
    const u32 stored_key = static_cast<const u8*>(region)[26];
    const u32 canonical = stored_key >= 53 && stored_key < 77 ? stored_key
        : stock<u32(u32, u32)>(0x8d589210)(track, key);
    machine.defaults(canonical, output);
    drum_unpack(static_cast<const i32*>(region) + 1, output);
    if (output[0] >= machine.model_count) machine.defaults(canonical, output);
    return output[0] < machine.model_count;
}
}}
