#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace extensions {
struct ArrangementSurface;
struct ArrangementContext;

// Opaque project/pattern identities are only for equality, never dereferencing.
// Positions and lengths use stock pattern ticks (480 per step at scale 1).
struct RecordingContext {
    u32 project, pattern, track, length, scale, quantization;
    // The selected track's pattern can be edited: a project, an instrument
    // track, a pattern, a length and a supported scale.
    bool editable;
    // Live notes on it can be recorded: editable, and the recorder is not in a
    // step or sample workflow.
    bool recordable;
    bool operator==(const RecordingContext& other) const {
        return project == other.project && pattern == other.pattern &&
            track == other.track && length == other.length && scale == other.scale &&
            quantization == other.quantization && editable == other.editable &&
            recordable == other.recordable;
    }
};
struct Context { RecordingContext recording; bool playing; };
struct Note { i32 position; u32 length; u8 pitch, velocity; };
// Equality-only identity; never persist across project loads or dereference tokens.
struct PatternRef { u32 project, generation, track, index; };
struct PatternInfo { PatternRef ref; u32 length, scale; };
// Pattern notes use the same stock tick units as Note.
const u32 pattern_note_capacity = 120;
// Borrowed only during pattern_read_result. Failure has count zero and notes null.
struct PatternReadResult { u32 request; PatternRef ref; bool success; const Note* notes; u32 count; };
struct LiveNote { u32 track, pitch, velocity; i32 position; u32 collision_radius; };
struct NoteOff { u32 track, pitch; };
struct Tick { u32 delta_q8; bool discontinuity; };
using Time = __UINT64_TYPE__;
// Key numbers as stock reports them in queued controls.
namespace keys { const u32 record = 50; }
struct Control {
    u32 key;
    bool pressed;
    // Stock took the event as input: input was enabled and, for Record, stock
    // registered the press or release.
    bool accepted;
    // No other key and no Shift held when the event arrived.
    bool unmodified;
    Time time;
};

// Each action needs its permission (profiles/extensions.json) and works only in
// a queued_control callback for a key the extension claims, once stock accepted
// it. The SDK validates the context again and owns undo/publication: one stock
// undo transaction per callback. Append new actions at the end.
struct Actions {
    // Add notes to an instrument track's pattern (0-7) beside its existing
    // notes. Returns the inserted prefix length. Needs edit_notes.
    u32 (*insert_notes)(const Context&, u32 track, const Note*, u32 count);
    // Show a short popup (1-31 characters), once per callback. Needs notify.
    void (*notify)(const char* text);
    // Replace an instrument track's pattern notes with these. Refused, changing
    // nothing, while stock is still recording a held note there. Needs edit_notes.
    u32 (*replace_notes)(const Context&, u32 track, const Note*, u32 count);
    // Read an existing source independently of the scene selection. Needs
    // read_patterns; stopped normal-recorder mode only, within queued_control.
    // Tracks 0..7, source indices 0..15. False leaves out unchanged.
    bool (*discover_pattern)(u32 track, u32 index, PatternInfo& out);
    // Copy all notes, or none. count is zero on failure; out stays unchanged.
    // A stale handle, held note, malformed list or insufficient capacity fails.
    // A successful empty pattern has count zero. No stock pointers escape.
    bool (*read_pattern)(const PatternRef&, Note* out, u32 capacity, u32& count);
    // Queue a stopped snapshot on the GUI service. Zero means refused/busy.
    // Needs read_patterns and a pattern_read_result callback. One SDK request
    // may be outstanding; its owner receives completion on the controller path.
    u32 (*request_pattern_read)(const PatternRef&);
    // Controller-side surface intent or owned pattern-result callback only.
    // Copies <=2048 bytes into one pending GUI reply; false means busy/stale.
    // Needs surface_reply. Never retain an Actions table for later use.
    bool (*post_arrangement_reply)(const ArrangementContext&, const u8*, u32);
};

using OnPatternReadResult = void (*)(const PatternReadResult&);
using OnContext = void (*)(const Context&);
using OnRecordableNoteOn = void (*)(const Context&, const LiveNote&);
using OnSelectedNoteOff = void (*)(const Context&, const NoteOff&);
using OnRecordingTick = void (*)(const Context&, const Tick&);
using OnInputActivity = void (*)();
using OnQueuedControl = void (*)(const Context&, const Control&, const Actions&);

// One callback per event, named for what it delivers. Each event lists its
// firmware sites in the profile ("events"); docs/extensions.md says how to
// widen one or propose another. An extension names the events it takes:
//
//     extern constexpr Hooks hooks = Hooks().with_context(context)
//                                           .with_queued_control(queued_control);
//
// All callbacks are synchronous on the controller path, in collection order.
// Arguments are borrowed for the callback. No allocation, blocking or I/O.
// Observers cannot transform live input or consume stock controls.
struct Hooks {
    // Selected-track recording context and play state; before every other event.
    OnContext context;
    // A live onset stock accepted for recording, on the selected recordable
    // instrument track, while playing. Sequenced notes and step entry excluded.
    OnRecordableNoteOn recordable_note_on;
    // A live release on the selected track, playing or not; it may have no
    // preceding recordable onset.
    OnSelectedNoteOff selected_note_off;
    // The selected track's scaled recording time, while playing and recordable.
    OnRecordingTick recording_tick;
    // Any live note input, accepted or not, on any track.
    OnInputActivity input_activity;
    // A queued control (Record and other queued keys; not keyboard keys
    // 26-49) after stock handled it. Actions act only for their claim owner.
    OnQueuedControl queued_control;
    OnPatternReadResult pattern_read_result;
    const ArrangementSurface* arrangement;

    constexpr Hooks() : Hooks(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) {}
    // Every callback in order; prefer the with_ builders below.
    constexpr Hooks(OnContext a, OnRecordableNoteOn b, OnSelectedNoteOff c, OnRecordingTick d,
                    OnInputActivity e, OnQueuedControl f, OnPatternReadResult g = nullptr, const ArrangementSurface* surface = nullptr)
        : context(a), recordable_note_on(b), selected_note_off(c), recording_tick(d),
          input_activity(e), queued_control(f), pattern_read_result(g), arrangement(surface) {}

    constexpr Hooks surface(const ArrangementSurface* value) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity, queued_control, pattern_read_result, value);
    }

    constexpr Hooks with_context(OnContext f) const {
        return Hooks(f, recordable_note_on, selected_note_off, recording_tick, input_activity, queued_control, pattern_read_result, arrangement);
    }
    constexpr Hooks with_recordable_note_on(OnRecordableNoteOn f) const {
        return Hooks(context, f, selected_note_off, recording_tick, input_activity, queued_control, pattern_read_result, arrangement);
    }
    constexpr Hooks with_selected_note_off(OnSelectedNoteOff f) const {
        return Hooks(context, recordable_note_on, f, recording_tick, input_activity, queued_control, pattern_read_result, arrangement);
    }
    constexpr Hooks with_recording_tick(OnRecordingTick f) const {
        return Hooks(context, recordable_note_on, selected_note_off, f, input_activity, queued_control, pattern_read_result, arrangement);
    }
    constexpr Hooks with_input_activity(OnInputActivity f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, f, queued_control, pattern_read_result, arrangement);
    }
    constexpr Hooks with_queued_control(OnQueuedControl f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity, f, pattern_read_result, arrangement);
    }
    constexpr Hooks with_pattern_read_result(OnPatternReadResult f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity, queued_control, f, arrangement);
    }

};
}}
