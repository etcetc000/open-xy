// Maestro's bounded chord records, shared by the native bridge and host checks.
#pragma once
#include <opxy/player.hpp>

namespace opxy { namespace platform {
struct PlayerTone {
    i32 delay;
    u8 pitch, velocity, unused[2];
};
struct PlayerChord {
    PlayerTone tones[player::max_tones];
    u32 count, root;
};
static_assert(sizeof(PlayerChord) == 72, "Stock Maestro chord record");
const u32 kPlayerChords = 8;

// Stock decrements delay each step, emitting on zero. Negative delays have
// sounded; queued tones stay dark. Note-off removes the whole root's record.
// GUI observations are advisory: bound every count read from sequencer state.
inline void player_activity(const volatile PlayerChord* records, u32 held, u32 (&played)[4]) {
    for (u32 i = 0; i < 4; ++i) played[i] = 0;
    if (held > kPlayerChords) held = kPlayerChords;
    for (u32 i = 0; i < held; ++i) {
        u32 count = records[i].count;
        if (count > player::max_tones) count = player::max_tones;
        for (u32 j = 0; j < count; ++j) {
            const u32 note = records[i].tones[j].pitch;
            if (note < 128 && records[i].tones[j].delay < 0)
                played[note / 32] |= 1u << (note % 32);
        }
    }
}

inline bool player_black(u32 note) {
    const u32 pc = note % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}
inline u32 player_white(u32 note) {
    static const u8 positions[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    return 7 * (note / 12) + positions[note % 12];
}
struct PlayerRange { u32 low, high, whites; };
inline PlayerRange player_range(const u32 (&played)[4], u32 lowest) {
    u32 low = lowest < 105 ? lowest : 104, high = low + 23;
    for (u32 n = 0; n < 128; ++n) {
        if (!(played[n / 32] & (1u << (n % 32)))) continue;
        if (n < low) low = n;
        if (n > high) high = n;
    }
    // Whole F-to-E octaves, except the partial octaves at MIDI's ends.
    low = low < 5 ? 0 : 5 + (low - 5) / 12 * 12;
    high = high < 5 ? 4 : 16 + (high - 5) / 12 * 12;
    if (high > 127) high = 127;
    return PlayerRange{low, high, player_white(high) - player_white(low) + 1};
}
}}
