#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace extensions {
const u32 recording_capacity = 120;
// SDK-normalized views. Raw coordinates are track-scaled ticks * 1680.
// Each frame covers a nonwrapping half-open interval [from_raw, to_raw).
struct RecordingSlot { u32 generation, effective_raw; bool used, active; };
struct RecordingFrame {
    u32 lap, from_raw, to_raw, length_raw;
    RecordingSlot slots[recording_capacity];
};
struct RecordingSelection { bool slots[recording_capacity]; };
struct RecordedSlot { u32 slot, generation, intended_lap; };
// Exactly one policy can own destructive edits before stock traversal. The SDK
// validates its selection, protects active notes, owns undo and publishes once.
struct RecordingPolicy {
    const char* name;
    void (*reset)();
    void (*recorded)(const RecordedSlot&);
    void (*select)(const RecordingFrame&, RecordingSelection&);
};
}}
