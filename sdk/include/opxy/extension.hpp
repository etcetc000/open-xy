#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace extensions {

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
struct LiveNote { u32 track, pitch, velocity; i32 position; u32 collision_radius; };
struct NoteOff { u32 track, pitch; };
struct Tick { u32 delta_q8; bool discontinuity; };
using Time = __UINT64_TYPE__;
// Key numbers as stock reports them in queued controls.
namespace keys {
const u32 record = 50;
// Keys an instrument track's page sees (Layer::keypress on a page layer).
const u32 m1 = 6, m2 = 7, m3 = 8, m4 = 9;
// The four encoder clicks: encoder_click + 0 .. 3.
const u32 encoder_click = 10;
}
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

// LFOs per instrument track with lfo_copies: stock's own and three copies of it.
const u32 lfo_count = 4;

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
    // Show LFO `lfo` (0 to lfo_count - 1) of instrument track `track` (0-7) on
    // stock's LFO page: stock's own LFO storage then holds it, so the page, its
    // controls and the type list edit it, and the one it held runs on as a
    // copy. All lfo_count LFOs always run (docs/extensions.md, Four LFOs). An
    // LFO shown for the first time starts as a copy of the one it replaces,
    // off. False for out-of-range numbers. Needs lfo_copies.
    bool (*show_lfo)(u32 track, u32 lfo);
    // Which LFO the track's LFO page shows (0 to lfo_count - 1). Needs
    // lfo_copies; 0 without it.
    u32 (*shown_lfo)(u32 track);
};

// A layer: what a claim marked "page" (profiles/extensions.json) opens for its
// owner while that stock page shows, on any instrument track. While it is open
// the owner sees the page's key presses and encoder turns first and takes what
// it wants; stock never sees a taken one. Stock keeps drawing the page.
struct Layer {
    enum Kind : u32 {
        open,      // the page showed (or its track changed)
        close,     // the page went away; nothing more until the next open
        keypress,  // a key press or release (key, pressed): true takes it from stock
        turn       // an encoder turn (key: the encoder 0-3, delta: detents): true takes it
    };
    Kind kind;
    u32 key;
    bool pressed;
    i32 delta;
    // Shift held during the key press or turn.
    bool shift;
    // The page's instrument track (0-7).
    u32 track;
};

using OnContext = void (*)(const Context&);
using OnRecordableNoteOn = void (*)(const Context&, const LiveNote&);
using OnSelectedNoteOff = void (*)(const Context&, const NoteOff&);
using OnRecordingTick = void (*)(const Context&, const Tick&);
using OnInputActivity = void (*)();
using OnQueuedControl = void (*)(const Context&, const Control&, const Actions&);
// Actions are granted as for the layer's claim.
using OnLayer = bool (*)(const Context&, const Layer&, const Actions&);

// One callback per event, named for what it delivers. Each event lists its
// firmware sites in the profile ("events"); docs/extensions.md says how to
// widen one or propose another. An extension names the events it takes:
//
//     extern constexpr Hooks hooks = Hooks().with_context(context)
//                                           .with_queued_control(queued_control);
//
// All callbacks are synchronous on the controller path, in collection order.
// Arguments are borrowed for the callback. No allocation, blocking or I/O.
// Observers cannot transform live input or consume stock controls; only an open
// layer takes keys and encoder turns from stock.
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
    // A page claim's layer events (Layer).
    OnLayer layer;

    constexpr Hooks() : Hooks(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) {}
    // Every callback in order; prefer the with_ builders below.
    constexpr Hooks(OnContext a, OnRecordableNoteOn b, OnSelectedNoteOff c, OnRecordingTick d,
                    OnInputActivity e, OnQueuedControl f, OnLayer g = nullptr)
        : context(a), recordable_note_on(b), selected_note_off(c), recording_tick(d),
          input_activity(e), queued_control(f), layer(g) {}

    constexpr Hooks with_context(OnContext f) const {
        return Hooks(f, recordable_note_on, selected_note_off, recording_tick, input_activity, queued_control, layer);
    }
    constexpr Hooks with_recordable_note_on(OnRecordableNoteOn f) const {
        return Hooks(context, f, selected_note_off, recording_tick, input_activity, queued_control, layer);
    }
    constexpr Hooks with_selected_note_off(OnSelectedNoteOff f) const {
        return Hooks(context, recordable_note_on, f, recording_tick, input_activity, queued_control, layer);
    }
    constexpr Hooks with_recording_tick(OnRecordingTick f) const {
        return Hooks(context, recordable_note_on, selected_note_off, f, input_activity, queued_control, layer);
    }
    constexpr Hooks with_input_activity(OnInputActivity f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, f, queued_control, layer);
    }
    constexpr Hooks with_queued_control(OnQueuedControl f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity, f, layer);
    }
    constexpr Hooks with_layer(OnLayer f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity, queued_control, f);
    }
};
}}
