#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace extensions {

// Opaque project/pattern identities are only for equality, never dereferencing.
// Positions and lengths use stock pattern ticks (480 per step at scale 1).
struct RecordingContext {
    u32 project, pattern, track, length, scale, quantization;
    bool eligible;
    bool operator==(const RecordingContext& other) const {
        return project == other.project && pattern == other.pattern &&
            track == other.track && length == other.length && scale == other.scale &&
            quantization == other.quantization && eligible == other.eligible;
    }
};
struct Context { RecordingContext recording; bool playing; };
struct Note { i32 position; u32 length; u8 pitch, velocity; };
struct LiveNote { u32 track, pitch, velocity; i32 position; u32 collision_radius; };
struct NoteOff { u32 track, pitch; };
struct Tick { u32 delta_q8; bool discontinuity; };
using Time = __UINT64_TYPE__;
struct Control {
    static const u32 record_key = 50;
    u32 key;
    bool pressed, record_accepted, unmodified_record;
    Time time;
};

// Callable only during an authorized control callback. The SDK validates the
// context again and owns undo/publication. Returns the inserted prefix length.
// A notification is allowed only after this callback successfully edits notes.
struct Actions {
    u32 (*insert_notes)(const Context&, const Note*, u32 count);
    void (*notify)(const char* text);
};

// All callbacks are synchronous on the controller path, in collection order.
// Arguments are borrowed for the callback. No allocation, blocking or I/O.
// Note observers cannot transform live input or consume stock controls.
struct Hooks {
    void (*context)(const Context&);
    void (*note_on)(const Context&, const LiveNote&);
    void (*note_off)(const Context&, const NoteOff&);
    void (*tick)(const Context&, const Tick&);
    void (*activity)();
    void (*control)(const Context&, const Control&, const Actions&);
};
}}
