// Per-key sends for synthesis drum kits (resources.padMix, SDK 1.10), as a
// Machinedrum track's own delay and reverb sends. Stock mixes a track's voices
// into one stereo sum and scales that sum into the two send buses by the
// track's send amounts. On a padMix kit, a key either follows the track's send
// (TRK) or sends its own amount, whatever the track's send knob says:
// - followers still go through stock's send loop at the track's send amount,
//   which then reads a sum of the followers alone;
// - a key with its own amount goes into a second sum, each voice scaled by its
//   key's amount, which the hooks add to the send bus at the track's level
//   (stock's gain for the track's dry and sends alike, so mute and volume still
//   apply), as stock would send it with the track's send at full.
// A send for which every voice on the track follows the track keeps stock's
// path exactly for that block. drum_send.cpp and drum_send_hooks.s place this
// in stock's mixer.
#pragma once
#include <opxy/audio.hpp>
#include <opxy/q31.hpp>
#include "drum_storage.hpp"

namespace opxy { namespace platform {
// A key's send codes live in stock's pad word +0x14, which reaches the audio
// side as the note payload's word 6 (8d5ad738). From bit 16 the word holds the
// kit's FILTER (5 bits) and SRR (4), then FX I's code in bits 25..27 and FX
// II's in 28..30. Code 0 follows the track's send exactly. A key the kit has
// not set up (drum_storage.hpp) follows the track whatever the word holds, so
// it plays as before.
const u32 kDrumSendPayload = 6, kDrumSendShift = 25, kDrumSendCodeBits = 3, kDrumSendCodes = 8, kDrumSends = 2;
// Both codes of a payload, FX I's in bits 0..2 and FX II's in 3..5.
inline u32 drum_send_codes(const i32* payload) {
    return payload && drum_marked_q31(payload[0]) && drum_pad_ready(payload[kDrumSendPayload])
        ? (static_cast<u32>(payload[kDrumSendPayload]) >> kDrumSendShift) & ((1u << (kDrumSends * kDrumSendCodeBits)) - 1) : 0;
}
// One send's code (0 FX I, 1 FX II) of what drum_send_codes gives.
inline u32 drum_send_code(u32 codes, u32 send) { return (codes >> (send * kDrumSendCodeBits)) & (kDrumSendCodes - 1); }
inline i32 drum_send_store(i32 word, u32 send, u32 code) {
    const u32 low = kDrumSendShift + send * kDrumSendCodeBits, mask = (kDrumSendCodes - 1) << low;
    return static_cast<i32>((static_cast<u32>(word) & ~mask) | ((code & (kDrumSendCodes - 1)) << low));
}
// A key's own send amount: code c of 1..7 is the knob position (c - 1) / 6,
// shown as 0, 17, 33, 50, 67, 83 and 100, through the same curve stock gives
// that send's knob, so a key at N sends what the track's knob at N sends. Stock
// builds FX I's gain as level x (0.8 k^2 + 0.2 k) of its knob k (8d510d7a..8d510daa)
// and FX II's as level x k (8d510e0a). The positions in Q31:
const i32 kDrumSendPositions[kDrumSendCodes] = {
    0, 0, 357913941, 715827883, 1073741824, 1431655765, 1789569707, q31::maximum};
// Each send's gain per code, worked with stock's arithmetic (drum_send_curve).
const i32 kDrumSendGains[kDrumSends][kDrumSendCodes] = {
    {0, 0, 119304646, 334053011, 644245093, 1049880892, 1550960410, 2147483644},
    {0, 0, 357913941, 715827883, 1073741824, 1431655765, 1789569707, q31::maximum}};
// Stock's curve for send `send`'s knob position, as its engine computes it.
inline i32 drum_send_curve(u32 send, i32 position) {
    if (send) return position;
    return q31::add(q31::multiply(q31::multiply(position, position), 1717986918), q31::multiply(position, 429496729));
}
inline i32 drum_send_gain(u32 send, u32 code) { return kDrumSendGains[send ? 1 : 0][code & (kDrumSendCodes - 1)]; }
// The amount a code shows, in percent: 0..100 for codes 1..7 (code 0 shows TRK).
inline i32 drum_send_percent(u32 code) {
    return code ? static_cast<i32>(((code & (kDrumSendCodes - 1)) - 1) * 100 + 3) / 6 : 0;
}

// Stock's track sum and a voice's output: 64 interleaved left/right Q31 frames.
const u32 kDrumSendWords = 2 * block_frames;
const u32 kDrumSendTracks = 8;

// What a send's source hook reads, in this order (drum_send_hooks.s): `track`
// is the stock track index plus one while that track's send loop takes
// `source` (the followers' sum), else 0.
struct DrumSendBus {
    u32 track;
    i32* source;
};
struct DrumSendState {
    DrumSendBus bus[kDrumSends];      // FX I's, then FX II's
    u32 own[kDrumSends];              // the track plus one while it has an own-amount sum for that send
    i32 level[kDrumSendTracks];       // each track's level the block before, for a smooth ramp
    u32 level_known;                  // which tracks' level is known
};
// Each track's mix starts with no voices of its own.
inline void drum_send_begin(DrumSendState& state) {
    for (u32 s = 0; s < kDrumSends; ++s) {
        state.bus[s].track = 0;
        state.bus[s].source = nullptr;
        state.own[s] = 0;
    }
}
// Counted, pointer-stepping loops, kept out of line so the compiler sees
// plain pointers (not stock's constant addresses) and makes each a hardware
// loop. File-local in every unit, so no unit exports them.
static inline __attribute__((noinline)) void drum_send_copy(i32* __restrict to, const i32* __restrict from) {
    for (u32 n = kDrumSendWords; n; --n) *to++ = *from++;
}
static inline __attribute__((noinline)) void drum_send_add(i32* __restrict to, const i32* __restrict from) {
    for (u32 n = kDrumSendWords; n; --n, ++to) *to = q31::add(*to, *from++);
}
static inline __attribute__((noinline)) void drum_send_scale(i32* __restrict to, const i32* __restrict from, i32 gain) {
    for (u32 n = kDrumSendWords; n; --n, ++to) *to = q31::add(*to, q31::multiply(gain, *from++));
}
// Into a send bus, `from` at a gain ramped linearly from `start` towards `end`
// over the block, as stock steps FX I's gain (8d5110aa).
static inline __attribute__((noinline)) void drum_send_ramp(i32* __restrict to, const i32* __restrict from, i32 start, i32 end) {
    const i32 step = (end >> 7) - (start >> 7);
    i32 gain = start;
    for (u32 n = kDrumSendWords; n; --n, ++to) {
        gain = q31::add(gain, step);
        *to = q31::add(*to, q31::multiply(gain, *from++));
    }
}
// Before stock adds a voice into the track's sum, for one send. While every
// voice so far has followed the track, nothing is kept and stock's sum feeds
// the send loop. The first voice with an amount of its own starts `follow` as
// stock's sum so far (with whatever stock started the sum with) and `own` at
// silence; from then on a follower goes into `follow`, with stock's saturating
// add in stock's order, and an own-amount voice into `own` at its amount.
inline void drum_send_bus(u32 send, DrumSendBus& bus, u32& own_track, i32* follow, i32* own, u32 track, const i32* sum,
                          const i32* voice, u32 code) {
    if (!code) {
        if (bus.track) drum_send_add(follow, voice);
        return;
    }
    if (!bus.track) {
        drum_send_copy(follow, sum);
        bus.track = track + 1;
        bus.source = follow;
    }
    if (own_track != track + 1) {
        for (u32 i = 0; i < kDrumSendWords; ++i) own[i] = 0;
        own_track = track + 1;
    }
    const i32 gain = drum_send_gain(send, code);
    if (gain) drum_send_scale(own, voice, gain);
}
// Both sends: `follow` and `own` hold a sum for each.
inline void drum_send_voice(DrumSendState& state, i32 (*follow)[kDrumSendWords], i32 (*own)[kDrumSendWords],
                            u32 track, const i32* sum, const i32* voice, u32 codes) {
    for (u32 s = 0; s < kDrumSends; ++s)
        drum_send_bus(s, state.bus[s], state.own[s], follow[s], own[s], track, sum, voice, drum_send_code(codes, s));
}
// What send `s`'s loop reads for `track`: the followers' sum while it is that track's, else stock's.
inline const i32* drum_send_source(const DrumSendState& state, u32 s, u32 track, const i32* sum) {
    return state.bus[s].track == track + 1 ? state.bus[s].source : sum;
}
// After stock's send loops for `track`: each own-amount sum into its bus at the
// track's `level`, ramped from the level the block before.
inline void drum_send_own(DrumSendState& state, i32 (*own)[kDrumSendWords], i32* const* buses, u32 track, i32 level) {
    if (track >= kDrumSendTracks) return;
    const u32 bit = 1u << track;
    const i32 start = state.level_known & bit ? state.level[track] : level;
    for (u32 s = 0; s < kDrumSends; ++s)
        if (state.own[s] == track + 1) drum_send_ramp(buses[s], own[s], start, level);
    state.level[track] = level;
    state.level_known |= bit;
}

// A padMix kit, for the voice hook to recognize its voices: its
// method table, and both send codes of the block one of its handles just rendered.
struct DrumSendKit {
    const void* vtable;
    u32 (*codes)(const void* handle);
};
}}

extern "C" {
// Generated per collection (drum_callbacks.cpp); a kit not in a browser
// selection names kit_absent, which no voice's method table is.
extern const opxy::platform::DrumSendKit sdk_drum_send_kits[];
extern const opxy::u32 sdk_drum_send_kit_count;
}
