// The track filter (M3) on a synthesis kit's voices (SDK 1.10). Stock builds a
// filter object for each voice index and runs it on every synth voice, but
// never for a kit voice: its drum renderer (8d51b274) has no filter stage. The
// kit adapter (drum_voice.cpp.in) runs stock's own object at the point stock's
// normal renderer 11a09308 does, after the machine's render and before the
// amplitude envelope, with no stock hook:
// - on/off and type are the track's (0x11b01dc8 byte, 0x84042700 word), as
//   11a097d0..11a09804 reads them; off costs one byte test and leaves the
//   voice's output exactly as before;
// - the object is built in the index's pool slot (0x84c6239c + index * 0x90)
//   by stock's factory 8d505220(type, &object, index, track) and reset through
//   its method +0x14 (R1 = 0), at a note's first filtered block and whenever
//   the track's type changes, as stock's voice constructor (8d51b0d0..8d51b180)
//   and type change (11a09e44..11a09e78) do;
// - each block the voice's 64 samples go down by 3 bits (stock's headroom,
//   11a0979c), through the object's mono process +0x8 (a kit voice answers
//   +0x20 with 0) with the track's record, the note's key (B[voice+0x50] *
//   0xffffff + 0x7fffff, saturated, 11a0980a..11a09836) and the last value of
//   the voice's filter envelope this block (0x11903500 + 0xfc, 11a09612),
//   which stock's drum renderer already computes for every kit voice from
//   words 32..35 (8d51b36e..8d51b3d4); then back up by 3 bits, saturating, so
//   a resonant peak above full scale clips rather than wraps.
#pragma once
#include <opxy/types.hpp>
#include <opxy/q31.hpp>
#include <opxy/audio.hpp>

namespace opxy { namespace platform {
const u32 kDrumFilterTracks = 16;
const u32 kDrumFilterPool = 128;           // the factories construct only below this index
const u32 kDrumFilterNone = 0xffffffffu;   // no filter built, or the track's M3 off
const u32 kDrumFilterHeadroom = 3;
const u32 kDrumFilterLadder = 16;          // the costliest type (stock's 4-pole core 11a0a220)
// Stock's object methods: process(object, block, record, key, envelope) and reset(object, 0).
typedef void (*DrumFilterProcess)(void*, i32*, const i32*, i32, i32);
typedef void (*DrumFilterReset)(void*, u32);
typedef u32 (*DrumFilterFactory)(u32 type, void** object, u32 index, u32 track);
const u32 kDrumFilterMono = 2, kDrumFilterResetMethod = 5;   // method-table entries +0x8, +0x14

// The key stock passes a voice's filter: its note scaled to about note / 128 of full scale.
inline i32 drum_filter_key(u32 note) { return q31::add(static_cast<i32>(note * 0x00ffffffu), 0x007fffff); }
// Counted, pointer-stepping loops, as drum_send.hpp's, file-local in every unit.
static inline __attribute__((noinline)) void drum_filter_down(i32* __restrict block) {
    for (u32 n = block_frames; n; --n, ++block) *block >>= kDrumFilterHeadroom;
}
// A sample back up by the headroom, saturating: Blackfin's one-instruction
// saturating shift, as stock's own (8d51a6b4), and the same result on the host.
inline i32 drum_filter_raise(i32 x) {
#if defined(__bfin__)
    i32 result;
    __asm__("%0 = %1 << 3 (S);" : "=d"(result) : "d"(x) : "ASTAT");
    return result;
#else
    return x > (q31::maximum >> kDrumFilterHeadroom) ? q31::maximum
         : x < ((-q31::maximum - 1) >> kDrumFilterHeadroom) ? -q31::maximum - 1
         : static_cast<i32>(static_cast<u32>(x) << kDrumFilterHeadroom);
#endif
}
static_assert(kDrumFilterHeadroom == 3, "drum_filter_raise shifts by 3");
static inline __attribute__((noinline)) void drum_filter_up(i32* __restrict block) {
    for (u32 n = block_frames; n; --n, ++block) *block = drum_filter_raise(*block);
}
inline const void* drum_filter_method(void* object, u32 entry) {
    return (*static_cast<const void* const* const*>(object))[entry];
}
// A block through a built object.
inline void drum_filter_run(void* object, i32* block, const i32* record, i32 key, i32 envelope) {
    drum_filter_down(block);
    reinterpret_cast<DrumFilterProcess>(const_cast<void*>(drum_filter_method(object, kDrumFilterMono)))(
        object, block, record, key, envelope);
    drum_filter_up(block);
}
// The object for `type`, built and reset in pool slot `index` when the type it
// holds (`built`) is another; null for a type stock has no filter for.
inline void* drum_filter_object(DrumFilterFactory factory, void*& object, u32& built, u32 type, u32 index, u32 track) {
    if (built != type) {
        object = nullptr;
        factory(type, &object, index, track);
        if (object)
            reinterpret_cast<DrumFilterReset>(const_cast<void*>(drum_filter_method(object, kDrumFilterResetMethod)))(object, 0);
        built = type;
    }
    return object;
}
}}
