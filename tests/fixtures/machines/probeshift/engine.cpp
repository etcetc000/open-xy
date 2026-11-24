#include "core.h"
#include <opxy/q31.hpp>

namespace probeshift {
// Callbacks run serially per voice; owner is a stable nonzero voice ID.
inline void initialize(Voice& voice, Shared&, opxy::u32 owner) { voice.phase = 0; voice.noise = owner; }
inline void start(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void destroy(Voice&, Shared&, opxy::u32) {}

// Test fixture: a triangle/pulse blend on page 1 and simple page-2 controls, with a
// Shift row (SDK 1.13) in parameters 8..15: noise, drift, octave (an enum of five),
// low cut, slide, an absent entry (13), ratio and balance.
inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    const opxy::i32 half = 0x40000000;  // 0.5 in Q31: half-scale output
    const opxy::i32 blend = parameters[0];
    const opxy::u32 width = 0x40000000u + static_cast<opxy::u32>(parameters[1]);
    // Octave: choice 0..4 (-2..+2), read as any enum control.
    const opxy::u32 octave = ((static_cast<opxy::u32>(parameters[10]) >> 16) * 5) >> 15;
    const opxy::i32 noise = parameters[8] >> 1;  // at most half scale
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        opxy::u32 folded = voice.phase & 0x7fffffffu;
        if (voice.phase & 0x80000000u) folded = 0x7fffffffu - folded;
        const opxy::i32 triangle = static_cast<opxy::i32>(folded) - half;
        const opxy::i32 pulse = voice.phase < width ? half : -half;
        const opxy::i32 mixed = opxy::q31::multiply(triangle, 0x7fffffff - blend)
                             + opxy::q31::multiply(pulse, blend);
        voice.noise = voice.noise * 1664525u + 1013904223u;
        const opxy::i32 hiss = opxy::q31::multiply(static_cast<opxy::i32>(voice.noise) >> 1, noise);
        const opxy::i32 tone = opxy::q31::multiply(mixed, 0x7fffffff - noise) + hiss;
        output[i] = opxy::q31::multiply(tone, parameters[2]);
        const opxy::u32 step = octave >= 2 ? pitch[i] << (octave - 2) : pitch[i] >> (2 - octave);
        voice.phase += step;
    }
}
}
