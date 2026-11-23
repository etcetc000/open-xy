#include "core.h"
#include <opxy/q31.hpp>

namespace probekit27 {
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
namespace probekit27 {
void drum_defaults(opxy::u32 key, opxy::u8* model, opxy::u8* controls) {
    *model = key % 3;
    for (opxy::u32 i = 0; i < 8; ++i) controls[i] = 64;
}
void drum_select(Voice&, Shared&, opxy::u32, opxy::u32) {}
const char* model_name(opxy::u32 model) { return model == 0 ? "A" : model == 1 ? "B" : "C"; }
const char* parameter_name(opxy::u32, opxy::u32 slot) {
    static const char* const names[8] = {"Shape", "Width", "Level", "Motion", "X0", "X1", "X2", "X3"};
    return slot < 8 ? names[slot] : "";
}
opxy::u32 parameter_count(opxy::u32) { return 8; }
// Shape (slot 0) is one dial over every model: 128 values inside each, past 127 into the next
// model and below 0 into the previous, ends held; eight values a detent, sixteen on a fast turn.
// Every other control moves one value a detent within the key's model.
opxy::u32 edit_model_parameter(opxy::u32 model, opxy::u32 slot, opxy::u32 value, opxy::i32 delta, opxy::u32 speed) {
    const opxy::i32 here = static_cast<opxy::i32>(model * 128 + value);
    const opxy::i32 low = slot == 0 ? 0 : static_cast<opxy::i32>(model * 128);
    const opxy::i32 high = slot == 0 ? 3 * 128 - 1 : static_cast<opxy::i32>(model * 128 + 127);
    opxy::i32 next = here + (slot == 0 ? delta * (speed ? 16 : 8) : delta);
    if (next < low) next = low;
    if (next > high) next = high;
    return static_cast<opxy::u32>(next);
}
}
