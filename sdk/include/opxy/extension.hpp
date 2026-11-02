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
const u32 metronome = 1;
// Track keys 1-8 are keys track_first .. track_first + 7.
const u32 track_first = 14;
// The keyboard: keys keyboard_first .. keyboard_last, low to high.
const u32 keyboard_first = 26, keyboard_last = 49;
const u32 record = 50;
// The keyboard's ten black keys as stock numbers them where they enter values
// (its black-key index): 0..9, low to high, labelled 1..9 then 0 (black_label).
// Any other key: -1.
inline i32 black_index(u32 key) {
    switch (key) {
    case 27: return 0; case 29: return 1; case 31: return 2; case 34: return 3; case 36: return 4;
    case 39: return 5; case 41: return 6; case 43: return 7; case 46: return 8; case 48: return 9;
    default: return -1;
    }
}
inline char black_label(u32 index) { return static_cast<char>('0' + (index + 1) % 10); }
}
// A claim with a chord (profiles/extensions.json) also receives those keyboard
// keys while its own key is held, before stock and instead of it: stock neither
// plays nor sees them, the release included. They arrive in queued_control,
// accepted, with the claim's actions granted.
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
    // Take snapshot `slot` (0-9, opxy/snapshots.hpp) of every instrument
    // track's stored parameters, replacing what the slot held; once per
    // callback. Changes no track. Returns whether it was taken. Needs snapshot.
    bool (*take_snapshot)(u32 slot);
    // Write snapshot `slot` back into every instrument track that still holds
    // the engine it held then, through stock's own setter (stored and heard);
    // once per callback. False when the slot is empty or no track can take it.
    // Needs snapshot.
    bool (*load_snapshot)(u32 slot);
    // Empty the slots in `mask` (bit n for slot n); once per callback. Returns
    // how many held a snapshot. Needs snapshot.
    u32 (*clear_snapshots)(u32 mask);
};

// A layer: what a claim marked "layer" (profiles/extensions.json) opens for its
// owner, as stock's bar key opens its menu. Pressing the claim's key (both keys
// of a pair claim) opens it. Held, it is momentary: letting go closes it.
// Tapped (let go within tap_ms with nothing taken meanwhile), a pair claim's
// layer stays open (latched) until the pair is pressed again or a key the owner
// does not take; a single key's tap closes it and reaches stock as a press.
// While it is open the owner sees every key and encoder turn first, and takes
// what it wants; its draw hook paints over every page.
struct Layer {
    enum Kind : u32 {
        open,   // the gesture opened it
        close,  // it closed; nothing more until the next open
        keypress,  // a key press or release (key, pressed): return true to take it from stock
        turn,   // an encoder turn (key: the encoder 0-3, delta: raw counts): true takes it
        frame   // once per screen frame while open, before draw (elapsed_ms)
    };
    Kind kind;
    u32 key;
    bool pressed;
    i32 delta;
    u32 elapsed_ms;
    // Open after a tap, until the gesture again or a key the owner leaves.
    bool latched;
    // How long the gesture has been held so far (0 once latched).
    u32 held_ms;
};
const u32 tap_ms = 400;

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
    // 26-49) after stock handled it, and a claim's chord keys (keyboard keys)
    // instead of stock. Actions act only for their claim owner.
    OnQueuedControl queued_control;
    // A layer claim's events (Layer), and its painting while open.
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
    constexpr Hooks with_layer(OnLayer f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity, queued_control,
                     f, draw);
    }
    constexpr Hooks with_draw(OnDraw f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity, queued_control,
                     layer, f);
    }
    constexpr Hooks with_queued_control(OnQueuedControl f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity, f, layer, draw);
    }
};
}}
