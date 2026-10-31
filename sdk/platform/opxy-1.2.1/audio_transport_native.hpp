#pragma once
#include <opxy/audio_transport.hpp>
#include "audio_telemetry.hpp"
#include "telemetry/protocol.hpp"
namespace opxy { namespace platform { namespace telemetry {
namespace wire = opxy_transport::telemetry;
u32 enter();
void leave(u32 token);
wire::Bank* construct_bank(void* storage, volatile u32* words, u32 count, u32 mode, bool reset, u32 prefix);
struct Snapshot { u32 count, sequence, requested, applied; };
Snapshot capture(u32 engine, u32 track, u32* output, u32 capacity);
void frame_end(u32 engine, u32 track, u32 reset);
void reset_screen(u32 engine);

template <u32 Words, u32 Mode, bool Reset, u32 Prefix = 1> struct NativeBank {
    static_assert(Words > 0 && Words <= 256 && Mode <= 2 && (Mode != 1 || Words == 1) && (Mode != 2 || (Prefix >= 1 && Prefix <= 24 && Words > Prefix)),
                  "Invalid native telemetry contract");
    volatile u32 words[8][Words];
    alignas(wire::Bank) unsigned char state[8][sizeof(wire::Bank)];
    u32 ready;
    wire::Bank* resolve(u32 track) {
        if (track >= 8) return nullptr;
        const u32 token = enter();
        if (!(ready & (1u << track))) {
            construct_bank(state[track], words[track], Words, Mode, Reset, Prefix);
            ready |= 1u << track;
        }
        leave(token);
        return reinterpret_cast<wire::Bank*>(state[track]);
    }
    static AudioContext context(u32 track, wire::Bank* bank, const i32* amplitude = nullptr) {
        return bank ? AudioContext(track, bank->words, bank->count,
            Reset ? &bank->requested_command : nullptr, Reset ? &bank->applied_command : nullptr, Reset ? &bank->requested_target : nullptr, audio_frame_index(), true, amplitude)
            : AudioContext(8, nullptr, 0);
    }
    static void begin(wire::Bank* bank) { if (bank) bank->begin_update(); }
    static void end(wire::Bank* bank) { if (bank) bank->end_update(); }
};
// Generated registry: only configured synth IDs, matching the current audio
// track owner, can resolve to a role-local bank. No author-provided pointers.
wire::Bank* resolve_bank(void*, wire::u16 engine, wire::u8 track);
bool configuration(u32 engine, u32& words, u32& mode, bool& reset, u32& prefix);
}}}
