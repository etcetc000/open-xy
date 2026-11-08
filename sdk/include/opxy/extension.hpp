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

// A copy of stock's LFO (the lfo_copies action): what stock keeps for its own
// LFO on an instrument track. type is stock's LFO type: 0 tremolo, 1 value,
// 2 random, 3 element, 6 duck. words are its parameter words, the values stock
// keeps in track words 24-31, which its LFO page edits.
struct Lfo {
    bool on;
    u32 type;
    i32 words[8];
};
namespace lfo_types { const u32 tremolo = 0, value = 1, random = 2, element = 3, duck = 6; }
// Copies per instrument track, beside stock's own LFO.
const u32 lfo_copies = 3;

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
    // Set copy `copy` (0 to lfo_copies - 1) of stock's LFO on instrument track
    // `track` (0-7): the audio role runs it through stock's own LFO beside
    // stock's, and the project companion keeps it. False, changing nothing, for
    // an unknown type or out-of-range numbers. Needs lfo_copies.
    bool (*set_lfo)(u32 track, u32 copy, const Lfo&);
    // Read an instrument track's LFO: index 0 is stock's own (what its LFO page
    // shows), 1 to lfo_copies its copies. False for out-of-range numbers. Needs
    // lfo_copies.
    bool (*get_lfo)(u32 track, u32 index, Lfo&);
};

// A layer: what a claim marked "page" (profiles/extensions.json) opens for its
// owner while that stock page shows, on any instrument track. While it is open
// the owner sees the page's key presses and encoder turns first and takes what
// it wants; stock never sees a taken one. Its draw hook paints over the page.
struct Layer {
    enum Kind : u32 {
        open,      // the page showed (or its track changed)
        close,     // the page went away; nothing more until the next open
        keypress,  // a key press or release (key, pressed): true takes it from stock
        turn,      // an encoder turn (key: the encoder 0-3, delta: detents): true takes it
        frame      // once per screen frame while open, before draw (elapsed_ms)
    };
    Kind kind;
    u32 key;
    bool pressed;
    i32 delta;
    u32 elapsed_ms;
    // Shift held during the key press or turn.
    bool shift;
    // The page's instrument track (0-7).
    u32 track;
};

// What a layer paints: its own nodes (extension.json "layer": {"nodes": [...]},
// layer.svg.fragment), each hidden until shown. Hidden again whenever the layer
// opens and closes. Offsets are whole pixels from where the artwork puts a node.
struct Canvas {
    void* self;
    void (*show_node)(void* self, u32 node, bool on, i32 dx, i32 dy);
    void (*text_node)(void* self, u32 node, const char* text);
    void show(u32 node, i32 dx = 0, i32 dy = 0) const { show_node(self, node, true, dx, dy); }
    void hide(u32 node) const { show_node(self, node, false, 0, 0); }
    // A text node's text (at most 31 characters).
    void text(u32 node, const char* value) const { text_node(self, node, value); }
};

using OnContext = void (*)(const Context&);
using OnRecordableNoteOn = void (*)(const Context&, const LiveNote&);
using OnSelectedNoteOff = void (*)(const Context&, const NoteOff&);
using OnRecordingTick = void (*)(const Context&, const Tick&);
using OnInputActivity = void (*)();
using OnQueuedControl = void (*)(const Context&, const Control&, const Actions&);
// Actions are granted as for the layer's claim.
using OnLayer = bool (*)(const Context&, const Layer&, const Actions&);
using OnDraw = void (*)(const Canvas&);

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
    // A page claim's layer events (Layer), and its painting while open.
    OnLayer layer;
    OnDraw draw;

    constexpr Hooks() : Hooks(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) {}
    // Every callback in order; prefer the with_ builders below.
    constexpr Hooks(OnContext a, OnRecordableNoteOn b, OnSelectedNoteOff c, OnRecordingTick d,
                    OnInputActivity e, OnQueuedControl f, OnLayer g = nullptr, OnDraw h = nullptr)
        : context(a), recordable_note_on(b), selected_note_off(c), recording_tick(d),
          input_activity(e), queued_control(f), layer(g), draw(h) {}

    constexpr Hooks with_context(OnContext f) const {
        return Hooks(f, recordable_note_on, selected_note_off, recording_tick, input_activity, queued_control, layer, draw);
    }
    constexpr Hooks with_recordable_note_on(OnRecordableNoteOn f) const {
        return Hooks(context, f, selected_note_off, recording_tick, input_activity, queued_control, layer, draw);
    }
    constexpr Hooks with_selected_note_off(OnSelectedNoteOff f) const {
        return Hooks(context, recordable_note_on, f, recording_tick, input_activity, queued_control, layer, draw);
    }
    constexpr Hooks with_recording_tick(OnRecordingTick f) const {
        return Hooks(context, recordable_note_on, selected_note_off, f, input_activity, queued_control, layer, draw);
    }
    constexpr Hooks with_input_activity(OnInputActivity f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, f, queued_control, layer, draw);
    }
    constexpr Hooks with_queued_control(OnQueuedControl f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity, f, layer, draw);
    }
    constexpr Hooks with_layer(OnLayer f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity, queued_control,
                     f, draw);
    }
    constexpr Hooks with_draw(OnDraw f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity, queued_control,
                     layer, f);
    }
};
}}
