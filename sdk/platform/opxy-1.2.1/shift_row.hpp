// An effect's shift row (SDK 1.7): up to four enum controls that Shift and the
// send page's encoders choose. Their choices are packed into one word of the
// send track, which no stock page edits and no stock code reads, so stock
// stores, saves and sends it to the audio side as any track word
// (docs/abi-1.2.1.md#effect-pages-and-shift-rows). The GUI (synth_controls.cpp)
// packs it; the effect's adapter (fx_object.cpp.in) unpacks it for the DSP.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace shift_row {
// The send track's word, and the audio side's copy of every track's words.
const u32 kWord = 36;
const u32 kAudioWords = 0x84eab39c, kTrackBytes = 0xe8;
const u32 kControls = 4;

inline u32 states(const u8* choices, u32 count) {
    u32 total = 1;
    for (u32 i = 0; i < count; ++i) total *= choices[i];
    return total;
}

// The word as stock stores a stepped choice: the middle of its bucket.
inline i32 midpoint(u32 choice, u32 count) {
    const u32 bucket = 0x7fffffffu / count;
    return static_cast<i32>(choice * bucket + bucket / 2);
}

// The packed word's choices, the first control most significant.
inline void decode(i32 word, const u8* choices, u32 count, u32* fields) {
    const u32 total = states(choices, count);
    u32 index = word < 0 ? 0 : static_cast<u32>(word) / (0x7fffffffu / total);
    if (index >= total) index = total - 1;
    for (u32 i = count; i-- > 0;) {
        fields[i] = index % choices[i];
        index /= choices[i];
    }
}

inline i32 encode(const u32* fields, const u8* choices, u32 count) {
    u32 index = 0;
    for (u32 i = 0; i < count; ++i) index = index * choices[i] + fields[i];
    return midpoint(index, states(choices, count));
}
}}}
