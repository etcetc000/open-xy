#pragma once
#include <opxy/telemetry.hpp>
#include "stock.hpp"
namespace opxy { namespace platform {
// Both stock paths obtain these blocks through 8d518d94(base, track).
// Normal synth slot2 receives the pointer as its third argument. Reject every
// other pointer; selected_track() belongs to the GUI and is not an audio owner.
inline u32 synth_audio_track(const i32* parameters) {
    const Address address = reinterpret_cast<Address>(parameters);
    const Address base = 0x84eab39c, stride = 0xe8;
    return address >= base && address < base + 8 * stride && (address - base) % stride == 0
        ? static_cast<u32>((address - base) / stride) : 8;
}
// Only the ordinary slot2 adapter calls this while the prepared amplitude
// buffer belongs to its voice. Raw, post-track and GUI contexts leave it null.
inline const i32* synth_prepared_amplitude(const i32* parameters) {
    return synth_audio_track(parameters)<8 ? reinterpret_cast<const i32*>(0x11903400u) : nullptr;
}
template <u32 Words> inline u32 take_audio(TelemetryBank<Words>& bank, u32 track, u32* output) {
    // Explicit capability gate. OP-XY has two Blackfin processing cores;
    // coherent shared storage or a stock IPC handoff has not been established.
    // CLI on the GUI core alone cannot make audio read/clear atomic. Never read
    // or clear producer storage here, and never synthesize an audio snapshot.
    (void)bank; (void)track;
    for (u32 i = 0; i < telemetry_capacity; ++i) output[i] = 0;
    return 0;
}
}}
