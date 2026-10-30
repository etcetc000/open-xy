#include "core.h"
#include <opxy/q31.hpp>

namespace probesynth {
// Callbacks run serially per voice; owner is a stable nonzero voice ID.
inline void initialize(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void start(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void destroy(Voice&, Shared&, opxy::u32) {}

// Test fixture: a triangle/pulse blend, small enough to compile anywhere the
// tests build machines (previews, CPU estimates, generated adapters).
inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    const opxy::i32 half = 0x40000000;  // 0.5 in Q31: half-scale output
    const opxy::i32 blend = parameters[0];
    // Width 0..1 sets the pulse's duty cycle from 25% to 75% of the 32-bit phase.
    const opxy::u32 width = 0x40000000u + static_cast<opxy::u32>(parameters[1]);
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        opxy::u32 folded = voice.phase & 0x7fffffffu;
        if (voice.phase & 0x80000000u) folded = 0x7fffffffu - folded;
        const opxy::i32 triangle = static_cast<opxy::i32>(folded) - half;
        const opxy::i32 pulse = voice.phase < width ? half : -half;
        const opxy::i32 mixed = opxy::q31::multiply(triangle, 0x7fffffff - blend)
                             + opxy::q31::multiply(pulse, blend);
        output[i] = opxy::q31::multiply(mixed, parameters[2]);
        voice.phase += pitch[i];
    }
}
}
