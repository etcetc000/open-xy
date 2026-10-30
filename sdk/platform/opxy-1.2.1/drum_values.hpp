// GUI-region decoding shared by the screen and the optional pad editor.
#pragma once
#include "drum_machines.hpp"
#include "drum_storage.hpp"
#include "stock.hpp"

namespace opxy { namespace platform {
// Stock's GUI record stores its canonical pad key at +26; the selected key
// passed by the controller can instead be the transposed parent note. The
// audio adapter also uses this canonical 53..76 pad for defaults.
inline u32 drum_canonical_key(u32 track, u32 key, const void* region) {
    const u32 stored_key = static_cast<const unsigned char*>(region)[26];
    return stored_key >= 53 && stored_key < 77 ? stored_key : stock<u32(u32, u32)>(0x8d589210)(track, key);
}
inline bool drum_region_values(const DrumMachine& machine, u32 track, u32 key,
                               const void* region, u8* model, u8* controls) {
    if (!region || !model || !controls) return false;
    const unsigned char* record = static_cast<const unsigned char*>(region);
    const u32 canonical = drum_canonical_key(track, key, region);
    machine.defaults(canonical, model, controls);
    // A pad the kit has not stored (stock's defaults, or the eight-value
    // layout of earlier SDK builds) keeps the key's defaults.
    if (drum_marked(record[24])) drum_unpack(static_cast<const i32*>(region) + 1, model, controls, machine.model_count);
    if (*model >= machine.model_count) machine.defaults(canonical, model, controls);
    return *model < machine.model_count;
}
}}
