// Per-key sends for synthesis drum kits (resources.padMix, SDK 1.10), as a
// Machinedrum track's own delay and reverb sends. Stock mixes a track's voices
// into one stereo sum and scales that sum into the two send buses by the
// track's send amounts. On a padMix kit, a key either follows the track's send
// (TRK) or sends its own amount, 0..100 percent in 1% steps (SDK 1.15), whatever
// the track's send knob says:
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
// A key's send, as the mixer takes it, is a 7-bit index (drum_send_index):
// - 0 follows the track's send exactly (TRK);
// - 1..101 is the key's own amount 0..100 percent, one a step, through the same
//   curve stock gives that send's knob, so a key at N sends what the track's knob
//   at N sends;
// - 102..108 is one of the 3-bit codes 1..7 of SDK 1.10..1.14 (0, 17, 33, 50, 67, 83
//   and 100 percent, at the knob positions (c - 1) / 6), with the gains it had then,
//   so a key nobody has edited since plays exactly as before.
// Where a key keeps its send (drum_storage.hpp drum_send_stored), and how a hit
// carries it to the audio side (drum_send_carry), are below.
const u32 kDrumSends = 2, kDrumSendIndexBits = 7, kDrumSendIndexMask = (1u << kDrumSendIndexBits) - 1;
const u32 kDrumSendAmounts = 101;                     // 0..100 percent
const u32 kDrumSendOld = kDrumSendAmounts;            // index kDrumSendOld + c is old code c
const u32 kDrumSendIndices = kDrumSendOld + 8;        // 0..108
// The old 3-bit code of a send (SDK 1.10): 0 TRK, 1..7 the seven positions. It
// stays in its place (a padMix pad word's bits 25..30, a drumline_page's send
// parameter) as the coarse copy of a key's amount: older firmware plays it, and
// it checks the amount's byte (drum_send_stored).
const u32 kDrumSendShift = 25, kDrumSendCodeBits = 3, kDrumSendCodes = 8;
inline u32 drum_send_index_of_code(u32 code) { return code & (kDrumSendCodes - 1) ? kDrumSendOld + (code & (kDrumSendCodes - 1)) : 0; }
inline u32 drum_send_index_of_amount(u32 amount) { return 1 + (amount < kDrumSendAmounts ? amount : kDrumSendAmounts - 1); }
// The percent an index shows: its amount, an old code's position (0, 17, 33, 50,
// 67, 83, 100), or -1 for TRK (and an index past 108).
inline i32 drum_send_percent(u32 index) {
    index &= kDrumSendIndexMask;
    if (index >= 1 && index <= kDrumSendAmounts) return static_cast<i32>(index - 1);
    // ((c - 1) 100 + 3) / 6, as a multiply: no divide on the Blackfin.
    if (index > kDrumSendOld && index < kDrumSendIndices) return static_cast<i32>((((index - kDrumSendOld - 1) * 100 + 3) * 10923) >> 16);
    return -1;
}
// The code nearest an amount, which a key with that amount keeps as its coarse
// copy: 1 + round(6 amount / 100), halves up, as a multiply (exact for 0..100).
inline u32 drum_send_shadow(u32 amount) {
    return 1 + (((6 * (amount < kDrumSendAmounts ? amount : kDrumSendAmounts - 1) + 50) * 1311) >> 17);
}
// A turn of a send: TRK below 0, then 0..100 one a step; `steps` is the turn's
// detents times its acceleration (drum_send_steps). An old code turns from the
// percent it shows. Returns the new index.
inline u32 drum_send_turn(u32 index, i32 steps) {
    i32 at = drum_send_percent(index) + steps;
    if (at < -1) at = -1;
    if (at > static_cast<i32>(kDrumSendAmounts) - 1) at = static_cast<i32>(kDrumSendAmounts) - 1;
    return at < 0 ? 0 : drum_send_index_of_amount(static_cast<u32>(at));
}
// A turn's steps on a send, from stock's three-word turn event ([1] the signed
// detents, [2] the speed): (speed + 1) a detent, as stock's pan editor
// accelerates (8d4d60b0), so a fast turn crosses 0..100 in a few detents and a
// slow one moves a percent.
inline i32 drum_send_steps(i32 detents, i32 speed) {
    if (detents > 127) detents = 127;
    if (detents < -127) detents = -127;
    i32 step = speed + 1;
    if (step < 1) step = 1;
    if (step > 25) step = 25;
    return detents * step;
}

// A send's index as a kit's own display reads it (draw::Input pad[2] and pad[3]):
// for a kit declaring SDK 1.15 or later (`percent`), 0 TRK and the percent + 1
// (1..101); for an older one its 3-bit code as SDK 1.10..1.14 gave it, 0 TRK and
// 1..7, an amount as its nearest step.
inline u32 drum_send_shown(u32 index, bool percent) {
    const i32 at = drum_send_percent(index);
    if (at < 0) return 0;
    if (percent) return static_cast<u32>(at) + 1;
    index &= kDrumSendIndexMask;
    return index > kDrumSendOld ? index - kDrumSendOld : drum_send_shadow(static_cast<u32>(at));
}

// Both sends of a key or a hit: FX I's index in bits 0..6, FX II's in 8..14.
const u32 kDrumSendPairShift = 8, kDrumSendPairMask = kDrumSendIndexMask | kDrumSendIndexMask << kDrumSendPairShift;
inline u32 drum_send_pair(u32 fx1, u32 fx2) {
    return (fx1 & kDrumSendIndexMask) | (fx2 & kDrumSendIndexMask) << kDrumSendPairShift;
}
inline u32 drum_send_index(u32 pair, u32 send) { return (pair >> (send ? kDrumSendPairShift : 0)) & kDrumSendIndexMask; }
inline u32 drum_send_with(u32 pair, u32 send, u32 index) {
    const u32 shift = send ? kDrumSendPairShift : 0;
    return (pair & ~(kDrumSendIndexMask << shift)) | (index & kDrumSendIndexMask) << shift;
}

// The payload carries a hit's pair in word 0's bits 0..6 and 8..14 (the GUI's
// publication hook writes them for every kit and Drumline pad, drum_locks.cpp).
// Word 0 is stock's reading of the pad's model byte, `bin * 0x01f07c1f +
// 0x00f83e0f` (8d5ad6ec..8d5ad708): mid-bin, 2^22 from an edge. A kit decodes it
// as (w >> 16) * 66 >> 15 (drum_marked_q31) and the dbox DSP as ((w >>> 8) * 66)
// >>> 23 (11a06d62..11a06d80), so no bit below 16 moves a bin. Bits 7 and 15
// stay the caller's (Drumline's page uses bit 7, drumline.hpp).
const u32 kDrumSendPayload = 0;
inline void drum_send_carry(i32* payload, u32 pair) {
    payload[kDrumSendPayload] = static_cast<i32>((static_cast<u32>(payload[kDrumSendPayload]) & ~kDrumSendPairMask) |
                                                 (pair & kDrumSendPairMask));
}
inline u32 drum_send_carried(const i32* payload) {
    return payload ? static_cast<u32>(payload[kDrumSendPayload]) & kDrumSendPairMask : 0;
}
// A kit's hit: the pair its payload carries, on a key the kit has set up (marked,
// with the mark in word 6, drum_storage.hpp); TRK on any other, whatever word 0's
// low bits hold.
inline u32 drum_send_codes(const i32* payload) {
    return payload && drum_marked_q31(payload[0]) && drum_pad_ready(payload[kDrumPadPayload]) ? drum_send_carried(payload) : 0;
}
// Where a key keeps its sends (SDK 1.15): one byte per send in its pad record,
// FX I's at +0x19 and FX II's at +0x1b (kDrumSendByte), and the old 3-bit code in
// its place as the amount's coarse copy. Stock's Drumline never reads or writes
// either byte (its editor 8d4d5f60, the payload builder's Drumline branch
// 8d5ad6ce..8d5ad776, the preset writer and reader, its page and its scramble
// 8d4d622c); +0x1b is a sampler's play mode. Pad copy and paste copy the whole
// 0x80-byte entry (8d581f34, 8d581f90), and projects save the 24 entries as one
// block (8d598e68), so the bytes go wherever stock moves the pad. A byte s and the
// code c read as:
// - the amount s - 1 when s is 1..101 and c is that amount's shadow
//   (drum_send_shadow), as a key edited since SDK 1.15 keeps it;
// - otherwise the old code c (0 TRK): a key from before, whose bytes stock left 0,
//   plays as it did. A code changed by older firmware since, or a byte stock left
//   from a sampler's pad, rarely matches, and then the code wins.
const u32 kDrumSendByte[kDrumSends] = {0x19, 0x1b};
inline u32 drum_send_stored(u32 byte, u32 code) {
    byte &= 0xff;
    if (byte >= 1 && byte <= kDrumSendAmounts && (code & (kDrumSendCodes - 1)) == drum_send_shadow(byte - 1)) return byte;
    return drum_send_index_of_code(code);
}
// What a record keeps for an index: its byte, and (the return) its code. An
// amount keeps itself and its shadow; TRK 0 and 0; an old code (a turn never
// makes one) 0 and itself.
inline u32 drum_send_store_index(unsigned char* record, u32 send, u32 index) {
    index &= kDrumSendIndexMask;
    const bool amount = index >= 1 && index <= kDrumSendAmounts;
    record[kDrumSendByte[send ? 1 : 0]] = static_cast<unsigned char>(amount ? index : 0);
    return amount ? drum_send_shadow(index - 1) : index > kDrumSendOld && index < kDrumSendIndices ? index - kDrumSendOld : 0;
}
// A padMix pad word's code of a send (bits 25..27 FX I, 28..30 FX II), and the
// word with another.
inline u32 drum_send_word_code(i32 word, u32 send) {
    return (static_cast<u32>(word) >> (kDrumSendShift + (send ? kDrumSendCodeBits : 0))) & (kDrumSendCodes - 1);
}
inline i32 drum_send_store(i32 word, u32 send, u32 code) {
    const u32 low = kDrumSendShift + (send ? kDrumSendCodeBits : 0), mask = (kDrumSendCodes - 1) << low;
    return static_cast<i32>((static_cast<u32>(word) & ~mask) | ((code & (kDrumSendCodes - 1)) << low));
}
// A padMix key's send pair as its record keeps it: TRK on a key the kit has not
// set up (drum_ready).
inline u32 drum_send_record_pair(const unsigned char* record) {
    if (!drum_ready(record)) return 0;
    const i32 word = reinterpret_cast<const i32*>(record)[kDrumPadWord / 4];
    return drum_send_pair(drum_send_stored(record[kDrumSendByte[0]], drum_send_word_code(word, 0)),
                          drum_send_stored(record[kDrumSendByte[1]], drum_send_word_code(word, 1)));
}
// Writes one send of a padMix key's record: its byte and its code.
inline void drum_send_record_set(unsigned char* record, u32 send, u32 index) {
    i32* word = reinterpret_cast<i32*>(record) + kDrumPadWord / 4;
    *word = drum_send_store(*word, send, drum_send_store_index(record, send, index));
}

// Each index's gain, worked with stock's arithmetic (tests/test_drum_send.py and
// tests/fixtures/drum_send_mixer.cpp check every entry): stock builds FX I's gain
// as level x (0.8 k^2 + 0.2 k) of its knob k (8d510d7a..8d510daa) and FX II's as
// level x k (8d510e0a). An amount N is the knob at round(N 2^31 / 100) (100 at
// 0x7fffffff), an old code c at (c - 1) / 6. Index 0 (TRK) and past 108 hold 0;
// the mixer never asks for TRK's.
const i32 kDrumSendLevels[kDrumSends][kDrumSendIndexMask + 1] = {
    {
     0, 0, 4466765, 9277128, 14431088, 19928647, 25769803, 31954555,
     38482905, 45354853, 52570398, 60129540, 68032281, 76278618, 84868552, 93802085,
     103079214, 112699940, 122664264, 132972186, 143623705, 154618821, 165957535, 177639846,
     189665754, 202035261, 214748364, 227805064, 241205361, 254949257, 269036750, 283467840,
     298242528, 313360812, 328822695, 344628174, 360777252, 377269925, 394106197, 411286066,
     428809534, 446676597, 464887258, 483441517, 502339373, 521580827, 541165879, 561094526,
     581366772, 601982614, 622942056, 644245093, 665891727, 687881961, 710215790, 732893217,
     755914242, 779278865, 802987083, 827038901, 851434315, 876173327, 901255935, 926682143,
     952451946, 978565347, 1005022345, 1031822942, 1058967135, 1086454926, 1114286313, 1142461300,
     1170979881, 1199842062, 1229047839, 1258597215, 1288490187, 1318726755, 1349306923, 1380230687,
     1411498049, 1443109009, 1475063566, 1507361719, 1540003472, 1572988820, 1606317767, 1639990309,
     1674006452, 1708366189, 1743069526, 1778116458, 1813506989, 1849241116, 1885318843, 1921740165,
     1958505086, 1995613601, 2033065718, 2070861429, 2109000739, 2147483644, 0, 119304646,
     334053011, 644245093, 1049880892, 1550960410, 2147483644, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0},
    {
     0, 0, 21474836, 42949673, 64424509, 85899346, 107374182, 128849019,
     150323855, 171798692, 193273528, 214748365, 236223201, 257698038, 279172874, 300647711,
     322122547, 343597384, 365072220, 386547057, 408021893, 429496730, 450971566, 472446403,
     493921239, 515396076, 536870912, 558345748, 579820585, 601295421, 622770258, 644245094,
     665719931, 687194767, 708669604, 730144440, 751619277, 773094113, 794568950, 816043786,
     837518623, 858993459, 880468296, 901943132, 923417969, 944892805, 966367642, 987842478,
     1009317315, 1030792151, 1052266988, 1073741824, 1095216660, 1116691497, 1138166333, 1159641170,
     1181116006, 1202590843, 1224065679, 1245540516, 1267015352, 1288490189, 1309965025, 1331439862,
     1352914698, 1374389535, 1395864371, 1417339208, 1438814044, 1460288881, 1481763717, 1503238554,
     1524713390, 1546188227, 1567663063, 1589137900, 1610612736, 1632087572, 1653562409, 1675037245,
     1696512082, 1717986918, 1739461755, 1760936591, 1782411428, 1803886264, 1825361101, 1846835937,
     1868310774, 1889785610, 1911260447, 1932735283, 1954210120, 1975684956, 1997159793, 2018634629,
     2040109466, 2061584302, 2083059139, 2104533975, 2126008812, 2147483647, 0, 357913941,
     715827883, 1073741824, 1431655765, 1789569707, 2147483647, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0}};
inline i32 drum_send_gain(u32 send, u32 index) { return kDrumSendLevels[send ? 1 : 0][index & kDrumSendIndexMask]; }

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
                          const i32* sum, const i32* voice, u32 index) {
    if (!index) {
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
    const i32 gain = drum_send_gain(send, index);
    if (gain) drum_send_scale(own, voice, gain);
}
// Both sends of a voice's pair (drum_send_pair): `follow`, `own` and `raw` hold a sum for each.
inline void drum_send_voice(DrumSendState& state, i32 (*follow)[kDrumSendWords], i32 (*own)[kDrumSendWords],
                            i32 (*raw)[kDrumSendWords], u32 track, const i32* sum, const i32* voice, u32 pair) {
    for (u32 s = 0; s < kDrumSends; ++s)
        drum_send_bus(s, state.bus[s], state.own[s], follow[s], own[s], raw[s], track, sum, voice,
                      drum_send_index(pair, s));
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

// A padMix kit, for the voice hook to recognize its voices: its method table,
// and the send pair (drum_send_pair) of the block one of its handles just rendered.
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
