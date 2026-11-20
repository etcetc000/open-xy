#pragma once
#include <opxy/sample_engine.hpp>
#include <opxy/types.hpp>

// The second pass of a sample voice, as one SDK routine: read two neighbouring
// frames at each precomputed playhead position, interpolate linearly, apply the
// pad level and write interleaved stereo 32-bit words. A machine computes the
// positions itself (its own clock, wraps and play-once rules) and hands this
// routine the read. On the device it is hand-written BF707 code
// (sdk/platform/blackfin/sample_kernel.s); on the host, and as the definition of
// every word, it is sample_interpolate_reference below. A machine that does not
// include this header links neither.
namespace opxy {
// Where and how the frames are read.
struct SampleTaps {
    const i16* origin;  // the region's first frame in playback order (its last frame when reverse);
                        // any i16 address: a stereo origin that is not 4-byte aligned is read in
                        // 16-bit halves (a little slower), never with a faulting 32-bit load
    u32 shift;          // positions are frame numbers << shift plus a fraction; 11 <= shift <= 31
    u32 last;           // (frames - 1) << shift: at or past it, the next frame is the region's first;
                        // every position must be below frames << shift (nothing checks it)
    u32 reverse;        // 1: frame n is read n frames before origin
    u32 channels;       // 1 or 2 (interleaved)
    u32 level;          // unsigned Q27 gain; sample_unity_level leaves words as v << 15
};

// One word of the level stage: v << 15 scaled by level / 2^27, truncated toward
// zero, saturated to the 32-bit range: sign(v) * min((|v| * level) >> 12, 2^31 - 1
// (2^31 when negative)).
inline i32 sample_level_word(i32 v, u32 level) {
    if (level == sample_unity_level) return v * 32768;
    const u32 negative = static_cast<u32>(v) >> 31;
    const u32 m = static_cast<u32>(v < 0 ? -v : v);
    const u32 upper = m * (level >> 16);
    const u32 r = (upper << 4) + ((m * (level & 65535u)) >> 12);
    const bool saturated = upper >= 0x08000000u || (r >> 31) != 0;
    const u32 magnitude = saturated ? 0x7fffffffu + negative : r;
    return static_cast<i32>((magnitude ^ (0u - negative)) + negative);
}

// The definition: frame by frame, with these exact operations.
//   offset    = position >> shift, negated when reverse (two's complement)
//   following = offset + 1 (offset - 1 when reverse), or 0 when position >= last
//   a, b      = the frames at origin + offset and origin + following (channel by channel)
//   f         = (position << (32 - shift)) >> 18, the fraction's top 14 bits
//   v         = (a * (16384 - f) + b * f) >> 14 (arithmetic), per channel
//   words     = sample_level_word(v, level) for left and right (mono: both from the one channel)
inline void sample_interpolate_reference(i32* output, const u32* positions, u32 count, const SampleTaps& taps) {
    const u32 flip = taps.reverse ? ~0u : 0u, width = taps.channels == 2 ? 1u : 0u, up = 32u - taps.shift;
    for (u32 i = 0; i < count; ++i) {
        const u32 position = positions[i];
        const u32 offset = ((position >> taps.shift) ^ flip) - flip;
        const u32 following = position >= taps.last ? 0u : offset + (flip | 1u);
        const i16* const at = taps.origin + static_cast<i32>(offset << width);
        const i16* const after = taps.origin + static_cast<i32>(following << width);
        const i32 fraction = static_cast<i32>((position << up) >> 18);
        const i32 rest = 16384 - fraction;
        const i32 left = (i32(at[0]) * rest + i32(after[0]) * fraction) >> 14;
        const i32 right = width ? (i32(at[1]) * rest + i32(after[1]) * fraction) >> 14 : left;
        output[2 * i] = sample_level_word(left, taps.level);
        output[2 * i + 1] = sample_level_word(right, taps.level);
    }
}

// Two playheads mixed: a crossfade from one grain to another.
struct SampleFade {
    const u32* from;     // the outgoing playhead's positions
    const u32* to;       // the incoming playhead's positions
    const u16* weights;  // the incoming share, 0 to 16384 (16384 is all incoming)
};
// The definition: each playhead's frame exactly as sample_interpolate_reference
// reads it (a for from, b for to, per channel), then per channel
//   v     = (a * (16384 - w) + b * w) >> 14 (arithmetic)
//   words = sample_level_word(v, level) (mono: both from the one channel)
// A weight of 0 gives the from playhead's words exactly.
inline void sample_crossfade_reference(i32* output, const SampleFade& fade, u32 count, const SampleTaps& taps) {
    const u32 flip = taps.reverse ? ~0u : 0u, width = taps.channels == 2 ? 1u : 0u, up = 32u - taps.shift;
    for (u32 i = 0; i < count; ++i) {
        i32 value[2][2];
        for (u32 k = 0; k < 2; ++k) {
            const u32 position = k ? fade.to[i] : fade.from[i];
            const u32 offset = ((position >> taps.shift) ^ flip) - flip;
            const u32 following = position >= taps.last ? 0u : offset + (flip | 1u);
            const i16* const at = taps.origin + static_cast<i32>(offset << width);
            const i16* const after = taps.origin + static_cast<i32>(following << width);
            const i32 fraction = static_cast<i32>((position << up) >> 18);
            const i32 rest = 16384 - fraction;
            value[k][0] = (i32(at[0]) * rest + i32(after[0]) * fraction) >> 14;
            value[k][1] = width ? (i32(at[1]) * rest + i32(after[1]) * fraction) >> 14 : value[k][0];
        }
        const i32 take = fade.weights[i], keep = 16384 - take;
        const i32 left = (value[0][0] * keep + value[1][0] * take) >> 14;
        const i32 right = (value[0][1] * keep + value[1][1] * take) >> 14;
        output[2 * i] = sample_level_word(left, taps.level);
        output[2 * i + 1] = sample_level_word(right, taps.level);
    }
}
}

extern "C" void opxy_sample_interpolate(opxy::i32* output, const opxy::u32* positions, opxy::u32 count,
                                        const opxy::SampleTaps* taps);
extern "C" void opxy_sample_crossfade(opxy::i32* output, const opxy::SampleFade* fade, opxy::u32 count,
                                      const opxy::SampleTaps* taps);

namespace opxy {
// The routine: output[0 .. 2 * count - 1] for positions[0 .. count - 1], any
// count; nothing else is written. On the host it is the reference above. On the
// device it is sample_kernel.s, which no test in the SDK runs: its words were
// compared with the reference's on a BF707 simulator outside the SDK, and
// tests/test_sample_kernel.py pins the code that was compared.
inline void sample_interpolate(i32* output, const u32* positions, u32 count, const SampleTaps& taps) {
    opxy_sample_interpolate(output, positions, count, &taps);
}
// The crossfade: output[0 .. 2 * count - 1], any count; nothing else is written.
inline void sample_crossfade(i32* output, const SampleFade& fade, u32 count, const SampleTaps& taps) {
    opxy_sample_crossfade(output, &fade, count, &taps);
}
}
