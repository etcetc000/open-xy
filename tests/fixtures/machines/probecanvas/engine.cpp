#include "core.h"
#include <opxy/q31.hpp>

namespace probecanvas {
inline void initialize(Voice& voice, Shared&, opxy::u32) { voice.position = 0; }
inline void start(Voice& voice, Shared&, opxy::u32) { voice.position = 0; }
inline void destroy(Voice&, Shared&, opxy::u32) {}

// Test fixture: a sawtooth at the Level control; the screen is what it tests.
inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        output[i] = opxy::q31::multiply(static_cast<opxy::i32>(voice.position) >> 1, parameters[2]);
        voice.position += pitch[i];
    }
}
}
