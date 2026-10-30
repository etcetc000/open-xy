#include "core.h"
#include "wave.h"
#include <opxy/q31.hpp>

namespace probesources {
// Callbacks run serially per voice; owner is a stable nonzero voice ID.
inline void initialize(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void start(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void destroy(Voice&, Shared&, opxy::u32) {}

// Test fixture: a triangle from lib/wave.cpp at the Level control.
inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        output[i] = opxy::q31::multiply(triangle(voice.phase), parameters[2]);
        voice.phase += pitch[i];
    }
}
}
