#pragma once
#include "draw.hpp"
#include "telemetry.hpp"
namespace opxy {
const u32 audio_transport_capacity = 256;
namespace draw {
// Telemetry extends the current drawing input, including effect-row state.
struct TelemetryInput : Input {
    u32 audio_words[audio_transport_capacity];
    u32 audio_count, audio_sequence, audio_requested_command, audio_applied_command;
    u32 audio_reset_supported;
    mutable u32 audio_reset_request;
    bool request_audio_reset(u32 target = 1) const {
        if (!target || !audio_reset_supported || (audio_requested_command &&
            audio_requested_command != audio_applied_command)) return false;
        audio_reset_request = target;
        return true;
    }
};
static_assert(sizeof(TelemetryInput) == 1132, "Extended telemetry input ABI5");
}
// Serialized host/worklet bank. Native uses the qualified role-local protocol
// bank instead. A host snapshot runs between audio quanta, never during render.
template <u32 Words, u32 Mode, bool Reset, u32 Prefix = 1> struct HostTelemetryBank {
    static_assert(Words > 0 && Words <= audio_transport_capacity && Mode <= 2,
                  "Invalid telemetry contract");
    static_assert((Mode != 1 || Words == 1) && (Mode != 2 || (Prefix >= 1 && Prefix <= 24 && Words > Prefix)), "Packed-only or mixed telemetry extent");
    volatile u32 words[8][Words];
    volatile u32 requested[8], applied[8], targets[8];
    u32 sequences[8], commands;
    AudioContext context(u32 track) {
        return track < 8 ? AudioContext(track, words[track], Words,
            Reset ? &requested[track] : nullptr, Reset ? &applied[track] : nullptr, Reset ? &targets[track] : nullptr)
            : AudioContext(8, nullptr, 0);
    }
    u32 request_reset(u32 track, u32 target = 1) {
        if (!Reset || track >= 8 || !target) return 0;
        if (requested[track] && requested[track] != applied[track]) return requested[track];
        if (!++commands) ++commands;
        targets[track] = Mode == 2 ? target : 1;
        requested[track] = commands;
        return commands;
    }
    u32 take(u32 track, u32* output, u32 capacity) {
        for (u32 i = 0; i < capacity; ++i) output[i] = 0;
        if (track >= 8 || capacity < Words) return 0;
        for (u32 i = 0; i < Words; ++i) {
            output[i] = words[track][i];
            if ((Mode == 1 && i == 0) || (Mode == 2 && i < Prefix)) words[track][i] = 0;
        }
        if (!++sequences[track]) ++sequences[track];
        return Words;
    }
    u32 sequence(u32 track) const { return track < 8 ? sequences[track] : 0; }
    u32 requested_command(u32 track) const { return track < 8 ? requested[track] : 0; }
    u32 applied_command(u32 track) const { return track < 8 ? applied[track] : 0; }
};
}
