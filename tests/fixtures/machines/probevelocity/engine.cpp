#include "core.h"
#include <opxy/q31.hpp>

namespace probevelocity {
// Callbacks run serially per voice; owner is a stable nonzero voice ID.
inline void initialize(Voice& voice, Shared&, opxy::u32) { voice.velocity = 0; }
inline void start(Voice&, Shared&, opxy::u32) {}
inline void destroy(Voice&, Shared&, opxy::u32) {}
// Test fixture: the note's onset reaches the engine, and its level renders.
// The optional note hook stores the onset's velocity between initialize and
// start; the render echoes it, so the level follows the note's velocity.
inline void note(Voice& voice, Shared&, opxy::u32 velocity) { voice.velocity = velocity; }
inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output,
                   opxy::Parameters, opxy::Pitch) {
    for (unsigned i = 0; i < opxy::block_frames; ++i)
        output[i] = static_cast<opxy::i32>(voice.velocity << 16);
}
}
