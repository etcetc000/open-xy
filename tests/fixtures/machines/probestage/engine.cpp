#include "core.h"
#include <opxy/q31.hpp>

namespace probestage {
inline void initialize(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void start(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void destroy(Voice&, Shared&, opxy::u32) {}

// A plain saw at half scale, scaled by Level.
inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        output[i] = opxy::q31::multiply(static_cast<opxy::i32>(voice.phase) >> 1, parameters[0]);
        voice.phase += pitch[i];
    }
}

// The track stage: a widener on the track's stereo sum. The left channel passes;
// the right is delayed by 1 to 512 frames (Width) and takes Cross of the left in
// place of as much of itself; both are scaled by Output. The SDK zeroes the state
// before track_reset, so the line starts silent.
inline void track_reset(TrackState&, Shared&, opxy::u32) {}
inline void track_process(TrackState& state, Shared&, opxy::u32, opxy::Output sum,
                          opxy::Parameters parameters, opxy::u32) {
    const opxy::u32 delay = 1 + (static_cast<opxy::u32>(parameters[1]) >> 22);
    const opxy::i32 cross = parameters[2] >> 1, keep = 0x7fffffff - cross, gain = parameters[3];
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const opxy::i32 left = sum[2 * i], right = sum[2 * i + 1];
        const opxy::i32 delayed = state.line[(state.write - delay) & (kLineFrames - 1)];
        state.line[state.write & (kLineFrames - 1)] = right;
        ++state.write;
        sum[2 * i] = opxy::q31::multiply(left, gain);
        sum[2 * i + 1] = opxy::q31::multiply(
            opxy::q31::add(opxy::q31::multiply(delayed, keep), opxy::q31::multiply(left, cross)), gain);
    }
}
}
