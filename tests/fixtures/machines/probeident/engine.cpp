#include "core.h"
#include <opxy/q31.hpp>

namespace probeident {
inline void initialize(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void start(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void destroy(Voice&, Shared&, opxy::u32) {}

// A saw at half scale, scaled by Level, once the audio processor has the
// device identity; silence before. Its sound proves the identity reached the
// audio processor; the screen shows the GUI's copy.
inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch) {
    opxy::u8 identity[opxy::device_identity_bytes];
    const bool known = opxy::device_identity(identity, opxy::device_identity_bytes);
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        output[i] = known ? opxy::q31::multiply(static_cast<opxy::i32>(voice.phase) >> 1, parameters[0]) : 0;
        voice.phase += pitch[i];
    }
}
}
