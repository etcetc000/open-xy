// Morph v0.0.6 bridge layer A: physical AUX gesture queue. C++11.
// Deliberately private: no changes to opxy::control::Input or stock SDK ABI.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
struct MorphAuxEvents {
    u32 clicks;
    i32 turns[4];
    void clear() { clicks = 0; for (u32 i=0; i<4; ++i) turns[i]=0; }
    void click(u32 index) { if (index<4) clicks |= 1u << index; }
    // Clamp each pending raw detent sum to 16-bit signed. Never overflow i32.
    void turn(u32 index, i32 delta) {
        if (index>=4) return;
        const i32 safe = delta>32767 ? 32767 : delta < -32768 ? -32768 : delta;
        const i32 sum = turns[index]+safe;
        turns[index] = sum > 32767 ? 32767 : sum < -32768 ? -32768 : sum;
    }
    void take(u32* out_clicks, i32* out_turns) {
        if (out_clicks) *out_clicks=clicks;
        if (out_turns) for (u32 i=0;i<4;++i) out_turns[i]=turns[i];
        clear();
    }
};
}}

// Consumer runs on Morph's GUI control tick; read-and-clear, no stock writes.
extern "C" void sdk_morph_aux_events_take(opxy::u32 track, opxy::u32* clicks, opxy::i32* turns);
