#include "core.h"
#include <opxy/q31.hpp>

namespace voiceheap {
// The adapter zeroes a voice before initialize: the echo starts silent.
inline void initialize(Voice&, Shared&, opxy::u32) {}
inline void start(Voice& voice, Shared&, opxy::u32) { voice.angle = 0; voice.level = 0x7fffffff; }
inline void destroy(Voice&, Shared&, opxy::u32) {}

// A sine from the angle: a parabola on each half cycle, peaking at half scale.
inline opxy::i32 sine(opxy::u32 angle) {
    const opxy::i32 t = static_cast<opxy::i32>(angle & 0x7fffffffu);  // the position in the half cycle, as Q31
    const opxy::i32 bent = opxy::q31::multiply(t, 0x7fffffff - t) << 1;
    return angle & 0x80000000u ? -bent : bent;
}

// A decaying sine plus its echo: time sets its lag (64 to 4095 samples),
// feedback how much of the echo returns, level the output, decay how fast the
// sine fades.
inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    const opxy::u32 lag = 64 + (static_cast<opxy::u32>(parameters[0]) >> 19) * 63 / 64;
    const opxy::i32 feedback = parameters[1] >> 1;
    const opxy::i32 keep = 0x7fffffff - (parameters[3] >> 12);
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const opxy::i32 dry = opxy::q31::multiply(sine(voice.angle), voice.level);
        const opxy::i32 wet = voice.echo[(voice.write - lag) & (kEchoFrames - 1)];
        voice.echo[voice.write & (kEchoFrames - 1)] = (dry >> 1) + opxy::q31::multiply(wet, feedback);
        output[i] = opxy::q31::multiply((dry >> 1) + (wet >> 1), parameters[2]);
        voice.angle += pitch[i];
        voice.level = opxy::q31::multiply(voice.level, keep);
        ++voice.write;
    }
}
}
