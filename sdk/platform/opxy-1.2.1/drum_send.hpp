// Per-key sends for synthesis drum kits (resources.padMix, SDK 1.10), as a
// Machinedrum track's own delay and reverb sends. Stock mixes a track's voices
// into one stereo sum and scales that sum into the two send buses by the
// track's send amounts. On a padMix kit, a key either follows the track's send
// (TRK) or sends its own amount, whatever the track's send knob says:
// - followers still go through stock's send loop at the track's send amount,
//   which then reads a sum of the followers alone;
// - a key with its own amount goes into a second sum, each voice scaled by its
//   key's amount, which the hooks add to the send bus at the track's level
//   (stock's gain for the track's dry and sends alike), as stock would send it
//   with the track's send at full, after the track's volume.
// Both sums are built from the raw voices, so they take the track gain stock
// gives its own sum before any bus reads it (engine volume, scene mute and the
// drum or synth group level; drum_send_track_gain), once per sample.
// Stock takes each send's share out of the track's dry (drum_send_dry), so a key
// with its own amounts also gets the dry those amounts give: with the track's
// send at 100 into an effect that keeps no dry, a key at 0 plays dry, not silent.
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
// and FX II's as level x k (8d510e0a). Each send's gain per code, worked with
// stock's arithmetic from those positions (tests/fixtures/drum_send_mixer.cpp
// checks them):
const i32 kDrumSendGains[kDrumSends][kDrumSendCodes] = {
    {0, 0, 119304646, 334053011, 644245093, 1049880892, 1550960410, 2147483644},
    {0, 0, 357913941, 715827883, 1073741824, 1431655765, 1789569707, q31::maximum}};
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
// Saturating subtraction, as stock's `a - b (S)`.
inline i32 drum_send_subtract(i32 a, i32 b) {
#if defined(__bfin__)
    i32 result;
    __asm__("%0 = %1 - %2 (S);" : "=d"(result) : "d"(a), "d"(b) : "ASTAT");
    return result;
#else
    const i64 difference = static_cast<i64>(a) - b;
    return difference > q31::maximum ? q31::maximum
         : difference < -q31::maximum - 1 ? -q31::maximum - 1 : static_cast<i32>(difference);
#endif
}
// Stock's track gain (8d510e14..8d510e98): after the voice loop and before any
// bus reads the track's sum, the engine scales the sum in place by the track's
// engine volume (record word 15) x its scene mute x the drum or synth group
// level (0x84c696fc + 0x44 or + 0x48), smoothed: frame n of the block at the
// gain stepped n + 1 times with a saturating add, `gain` (R0 at 8d510ea8) where
// the ramp ended and `step` (R2) its step. The step is (target - gain) >>> 8
// (8d510e54..8d510e5e; 8d50e61c sets the shift), or 0 on the smoother's first
// block, whose ramp holds the target (8d510e4a..8d510e52). The target and gain
// are products of non-negative Q31 words, and 64 steps cover a quarter of the
// way (within 64 LSB of rounding), so no add saturates and the ramp's start is
// `gain` less 64 steps exactly. The followers' and own-amount sums are built
// from the raw voices, so each takes the same ramp here, once.
static inline __attribute__((noinline)) void drum_send_ramp(i32* __restrict words, i32 gain, i32 step) {
    for (u32 n = block_frames; n; --n, words += 2) {
        gain = q31::add(gain, step);
        words[0] = q31::multiply(gain, words[0]);
        words[1] = q31::multiply(gain, words[1]);
    }
}
// Before stock adds a voice into the track's sum, for one send. While every
// voice so far has followed the track, nothing is kept and stock's sum feeds
// the send loop. The first voice with an amount of its own starts `follow` as
// stock's sum so far (with whatever stock started the sum with) and `own` at
// silence; from then on a follower goes into `follow`, with stock's saturating
// add in stock's order, and an own-amount voice into `own` at its amount and
// into `raw` as it is (for its dry, drum_send_dry), a key at 0 included.
inline void drum_send_bus(u32 send, DrumSendBus& bus, u32& own_track, i32* follow, i32* own, i32* raw, u32 track,
                          const i32* sum, const i32* voice, u32 code) {
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
        for (u32 i = 0; i < kDrumSendWords; ++i) own[i] = raw[i] = 0;
        own_track = track + 1;
    }
    drum_send_add(raw, voice);
    const i32 gain = drum_send_gain(send, code);
    if (gain) drum_send_scale(own, voice, gain);
}
// Both sends: `follow`, `own` and `raw` hold a sum for each.
inline void drum_send_voice(DrumSendState& state, i32 (*follow)[kDrumSendWords], i32 (*own)[kDrumSendWords],
                            i32 (*raw)[kDrumSendWords], u32 track, const i32* sum, const i32* voice, u32 codes) {
    for (u32 s = 0; s < kDrumSends; ++s)
        drum_send_bus(s, state.bus[s], state.own[s], follow[s], own[s], raw[s], track, sum, voice,
                      drum_send_code(codes, s));
}
// What send `s`'s loop reads for `track`: the followers' sum while it is that track's, else stock's.
inline const i32* drum_send_source(const DrumSendState& state, u32 s, u32 track, const i32* sum) {
    return state.bus[s].track == track + 1 ? state.bus[s].source : sum;
}
// After stock's in-place track gain for `track` (8d510ea8): every sum the track
// keeps takes the ramp stock's sum just took. A track that kept none is left as
// stock mixes it.
inline void drum_send_track_gain(DrumSendState& state, i32 (*follow)[kDrumSendWords], i32 (*own)[kDrumSendWords],
                                 i32 (*raw)[kDrumSendWords], u32 track, i32 gain, i32 step) {
    const i32 start = static_cast<i32>(static_cast<u32>(gain) - static_cast<u32>(step) * block_frames);
    for (u32 s = 0; s < kDrumSends; ++s) {
        if (state.bus[s].track == track + 1) drum_send_ramp(follow[s], start, step);
        if (state.own[s] == track + 1) {
            drum_send_ramp(own[s], start, step);
            drum_send_ramp(raw[s], start, step);
        }
    }
}
// After stock's send loops for `track`: each own-amount sum into its bus at the
// track's `level` for this block. Stock's loops hold their gains for the block
// too: FX I's adds a step to its gain each word (8d5110aa), and that step is 0
// (R4, cleared at 8d5107a8, stored at 8d510d74); FX II's gain is constant.
inline void drum_send_own(DrumSendState& state, i32 (*own)[kDrumSendWords], i32* const* buses, u32 track, i32 level) {
    if (track >= kDrumSendTracks) return;
    for (u32 s = 0; s < kDrumSends; ++s)
        if (state.own[s] == track + 1) drum_send_scale(buses[s], own[s], level);
}

// Stock's dry for a track (8d510d0a..8d510e44) is D = L - G1 (1 - d1) - G2 (1 - d2),
// each subtraction saturating: L the level, G1 and G2 the send gains, d1 and d2
// what the effects on send slots 14 and 15 answer to their dry query (method
// word 7, given the send track's record; 0x7fffffff, taking nothing, for an
// empty slot). Both sends add, and D is negative when they take more than the
// level (both at 100 into effects that keep no dry: the dry inverted). From
// 8d511122 the engine scales D by the track's dry word x, smooths that target
// (the smoother at [FP-0x34]: current + (target - current) >>> W[+8] each word,
// or the target held when B[+0xa] is set) and adds the track's sum at it into
// its main out (8d51114a..8d51117e). A key with its own amount on send s should
// take its own curve c in place of the track's there: D + (1 - d) (G - L c). For
// the voices that override s, raw (as they are) and own (at their curves), both
// at the track gain, that is x (1 - d) G raw - x (1 - d) L own added before
// stock's loop, which adds them at D with every other voice. Each of those two
// gains runs a smoother of its own as stock runs the dry's (same shift, same
// reset), so while knobs move stock's smoothed D and the correction still add
// up to the key's own dry, smoothed. A track with no own-amount key gets nothing
// here.
struct DrumSendDryGain {
    i32 current;
    u32 live;                         // 1 while its send had own-amount voices in the last block
};
struct DrumSendDry {
    DrumSendDryGain gain[kDrumSends][2];   // per send: the raw voices', then the own sum's
};
struct DrumSendDryInput {
    i32 level;                        // L, [FP-0x68]
    i32 send[kDrumSends];             // G1, G2
    i32 wet[kDrumSends];              // 1 - d1, 1 - d2
    i32 x;                            // the track's dry word
    i32 shift;                        // stock's dry smoother: W[+8]
    bool reset;                       // and B[+0xa]
};
// One smoothed gain's start for a block and its step per word, as 8d511122..8d511148
// make the dry's: the target held on a reset (or on the first block the gain is
// used), else the current gain stepping by (target - current) >>> shift.
inline i32 drum_send_dry_start(DrumSendDryGain& gain, i32 target, const DrumSendDryInput& in, i32& step) {
    if (in.reset || !gain.live) {
        gain.live = 1;
        step = 0;
        return target;
    }
    step = drum_send_subtract(target, gain.current) >> in.shift;
    return gain.current;
}
inline void drum_send_dry_idle(DrumSendDry& dry) {
    for (u32 s = 0; s < kDrumSends; ++s) dry.gain[s][0].live = dry.gain[s][1].live = 0;
}
static inline __attribute__((noinline)) void drum_send_dry_mix(i32* __restrict out, const i32* __restrict raw,
                                                              const i32* __restrict own, i32 a, i32 step_a, i32 b,
                                                              i32 step_b, i32* end) {
    for (u32 n = kDrumSendWords; n; --n, ++out) {
        a = q31::add(a, step_a);
        b = q31::add(b, step_b);
        *out = q31::add(*out, drum_send_subtract(q31::multiply(a, *raw++), q31::multiply(b, *own++)));
    }
    end[0] = a;
    end[1] = b;
}
inline void drum_send_dry(DrumSendDry& dry, const DrumSendState& state, const i32 (*raw)[kDrumSendWords],
                          const i32 (*own)[kDrumSendWords], i32* out, u32 track, const DrumSendDryInput& in) {
    for (u32 s = 0; s < kDrumSends; ++s) {
        DrumSendDryGain* gain = dry.gain[s];
        if (state.own[s] != track + 1) {
            gain[0].live = gain[1].live = 0;
            continue;
        }
        // Stock's product G (1 - d) (8d510db6, 8d510e14), and L (1 - d), each at x.
        const i32 a = q31::multiply(in.x, q31::multiply(in.send[s], in.wet[s]));
        const i32 b = q31::multiply(in.x, q31::multiply(in.level, in.wet[s]));
        i32 step_a, step_b, end[2];
        const i32 from_a = drum_send_dry_start(gain[0], a, in, step_a);
        const i32 from_b = drum_send_dry_start(gain[1], b, in, step_b);
        drum_send_dry_mix(out, raw[s], own[s], from_a, step_a, from_b, step_b, end);
        gain[0].current = end[0];
        gain[1].current = end[1];
    }
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
