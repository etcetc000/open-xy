#include "core.h"
#include <opxy/q31.hpp>
#include <opxy/telemetry.hpp>

namespace probescope {
inline void initialize(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void start(Voice& voice, Shared&, opxy::u32) { voice.phase = 0; }
inline void destroy(Voice&, Shared&, opxy::u32) {}

// Test fixture: a triangle that adds its block peak to word 0 and keeps its first 62
// samples, as signed 16-bit pairs, in words 1..31 for its scope. A reset request
// zeroes the scope for one block.
inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch, const opxy::AudioContext& audio) {
    opxy::i16 scope[62];
    opxy::i32 peak = 0;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        opxy::u32 folded = voice.phase & 0x7fffffffu;
        if (voice.phase & 0x80000000u) folded = 0x7fffffffu - folded;
        output[i] = opxy::q31::multiply(static_cast<opxy::i32>(folded) - 0x40000000, parameters[2]);
        if (i < 62) scope[i] = static_cast<opxy::i16>(output[i] >> 16);
        if ((output[i] >> 16) > peak) peak = output[i] >> 16;
        voice.phase += pitch[i];
    }
    audio.add_packed16(0, static_cast<opxy::u32>(peak & 0xffff));
    const opxy::u32 command = audio.pending_command();
    audio.publish_packed16(1, command ? nullptr : scope, 31);
    audio.command_applied(command);
}
}
