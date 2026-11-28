#include "core.h"
#include <opxy/q31.hpp>

namespace probestagemeter {
inline void initialize(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void start(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void destroy(Voice&, Shared&, opxy::u32) {}

// A plain saw at half scale, scaled by Level; its voices publish nothing, the stage does.
// Width, Cross and Output are unused.
inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch, const opxy::AudioContext&) {
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        output[i] = opxy::q31::multiply(static_cast<opxy::i32>(voice.phase) >> 1, parameters[0]);
        voice.phase += pitch[i];
    }
}

inline opxy::u32 magnitude(opxy::i32 sample) {
    return sample < 0 ? static_cast<opxy::u32>(-(sample + 1)) : static_cast<opxy::u32>(sample);
}

// Test fixture: the track stage passes the sum and publishes, per block, the left and
// right peak of the track's voice sum (words 0 and 1) and the louder channel's peak
// held since the last reset (word 2). A reset request clears the hold.
inline void track_reset(TrackState&, Shared&, opxy::u32) {}
inline void track_process(TrackState& state, Shared&, opxy::u32, opxy::Output sum,
                          opxy::Parameters, opxy::u32, const opxy::AudioContext& audio) {
    opxy::u32 left = 0, right = 0;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const opxy::u32 l = magnitude(sum[2 * i]), r = magnitude(sum[2 * i + 1]);
        if (l > left) left = l;
        if (r > right) right = r;
    }
    const opxy::u32 command = audio.pending_command();
    if (command) state.held = 0;
    const opxy::u32 louder = left > right ? left : right;
    if (louder > state.held) state.held = louder;
    audio.publish_word(0, left);
    audio.publish_word(1, right);
    audio.publish_word(2, state.held);
    audio.command_applied(command);
}
}
