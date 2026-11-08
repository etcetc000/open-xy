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
// The sequencer's tracks as they play, read-only. Bars are 16 steps; a track's
// last bar may be shorter. While stopped, `bar` is where the track stands.
const u32 sequencer_tracks = 16;
const u8 no_bar = 0xff;
struct SequencerTrack {
    u8 bars;       // Bars in the playing pattern (1-4); 0: the track has none.
    u8 bar;        // The bar the playhead is in.
    u8 shown;      // The bar shown and edited on the bar menu.
    u8 loop;       // The bar loop_bars holds the track in, or no_bar.
    u32 bar_ticks; // One bar in scale-1 pattern ticks (7680 at scale 1): its length in song time.
};
struct Sequencer { SequencerTrack track[sequencer_tracks]; };
// shift: Shift held at this update.
struct Context { RecordingContext recording; bool playing; Sequencer sequencer; bool shift; };
// The bar each track repeats, or no_bar to leave it to stock.
struct BarLoop { u8 bar[sequencer_tracks]; };
struct Note { i32 position; u32 length; u8 pitch, velocity; };
struct LiveNote { u32 track, pitch, velocity; i32 position; u32 collision_radius; };
struct NoteOff { u32 track, pitch; };
struct Tick { u32 delta_q8; bool discontinuity; };
using Time = __UINT64_TYPE__;
// Key numbers as stock reports them in queued controls.
namespace keys {
const u32 none = 0xffffffffu;
const u32 m1 = 6, m2 = 7, m3 = 8, m4 = 9;
const u32 bar = 25;  // Held: stock's bar menu; pressed: the next bar is shown.
const u32 record = 50;
const u32 play = 51, stop = 52;
}
// Stock's screen numbers (its current GUI page), as Control::screen reports them.
namespace screens {
const u32 bar_menu = 0x23;  // BAR's menu: BAR held, or kept open by pressing Shift first
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
    // The one other key held with this one, without Shift, or keys::none.
    u32 held;
    // Shift held when the event arrived.
    bool shift;
    // Stock's screen when the event arrived (screens::bar_menu, ...).
    u32 screen;
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
    // Hold tracks in one bar each: every playhead move, step-component jumps
    // and play start included, stays in that bar, and each pass resets
    // parameter locks as a pattern loop does. Bars must exist in the playing
    // pattern. All no_bar ends looping, and needs no claim. Stock ends it
    // when a looped track's pattern, the scene or the project changes.
    // Needs loop_bars. Returns whether the loop was taken.
    bool (*loop_bars)(const Context&, const BarLoop&);
    // Start playback from a point of the scene, in song ticks (scale 1: a bar
    // of a track is its bar_ticks), as if it had played from the start: every
    // track begins where stock's song clock puts it (bar N of the longest
    // pattern lines up with bar N mod its length of the others), and its step
    // keys show that bar. Playing, it restarts from there, as Play restarts
    // from the top. Counts in as Play would (recording armed), then starts
    // there; refused while playing with the count-in on. With Record held,
    // stopped, it arms recording as Record + Play does, and the start that
    // follows (Play with its count-in, or the first note) is from there.
    // Loops hold as usual. Needs play_from.
    // Returns whether playback was asked for.
    bool (*play_from)(const Context&, u32 song_tick);
};

using OnContext = void (*)(const Context&);
using OnRecordableNoteOn = void (*)(const Context&, const LiveNote&);
using OnSelectedNoteOff = void (*)(const Context&, const NoteOff&);
using OnRecordingTick = void (*)(const Context&, const Tick&);
using OnInputActivity = void (*)();
using OnQueuedControl = void (*)(const Context&, const Control&, const Actions&);
// A claimed key's label: 1-15 printable characters (nullptr: the slot is
// empty), drawn greyed out when not enabled.
struct KeyLabel { const char* text; bool enabled; };
using OnKeyLabel = KeyLabel (*)(const Context&, u32 key);
using OnSequencerChange = void (*)(const Context&, const Actions&);

// One callback per event, named for what it delivers. Each event lists its
// firmware sites in the profile ("events"); docs/extensions.md says how to
// widen one or propose another. An extension names the events it takes:
//
//     extern constexpr Hooks hooks = Hooks().with_context(context)
//                                           .with_queued_control(queued_control);
//
// All callbacks are synchronous on the controller path, in collection order.
// Arguments are borrowed for the callback. No allocation, blocking or I/O.
// Observers cannot transform live input or consume stock controls; only a
// claim may move a stock control (bar_menu: the bar menu's M keys go to Shift).
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
    // The label over a claimed key in stock's menu (bar_menu: the bar menu's
    // M1-M4 slots). While Shift is held the menu shows stock's own labels, as
    // Shift gives the keys back to stock. Asked after every context callback;
    // the SDK redraws a slot when its label changes.
    OnKeyLabel key_label;
    // A track's bar count or step scale changed while bars loop (or are
    // armed), after the context callback. loop_bars may move those loops
    // here, as for a claimed key, but not start one.
    OnSequencerChange sequencer_change;

    constexpr Hooks() : Hooks(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) {}
    // Every callback in order; prefer the with_ builders below.
    constexpr Hooks(OnContext a, OnRecordableNoteOn b, OnSelectedNoteOff c, OnRecordingTick d,
                    OnInputActivity e, OnQueuedControl f, OnKeyLabel g = nullptr,
                    OnSequencerChange h = nullptr)
        : context(a), recordable_note_on(b), selected_note_off(c), recording_tick(d),
          input_activity(e), queued_control(f), key_label(g), sequencer_change(h) {}

    constexpr Hooks with_context(OnContext f) const {
        return Hooks(f, recordable_note_on, selected_note_off, recording_tick, input_activity,
                     queued_control, key_label, sequencer_change);
    }
    constexpr Hooks with_recordable_note_on(OnRecordableNoteOn f) const {
        return Hooks(context, f, selected_note_off, recording_tick, input_activity,
                     queued_control, key_label, sequencer_change);
    }
    constexpr Hooks with_selected_note_off(OnSelectedNoteOff f) const {
        return Hooks(context, recordable_note_on, f, recording_tick, input_activity,
                     queued_control, key_label, sequencer_change);
    }
    constexpr Hooks with_recording_tick(OnRecordingTick f) const {
        return Hooks(context, recordable_note_on, selected_note_off, f, input_activity,
                     queued_control, key_label, sequencer_change);
    }
    constexpr Hooks with_input_activity(OnInputActivity f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, f,
                     queued_control, key_label, sequencer_change);
    }
    constexpr Hooks with_queued_control(OnQueuedControl f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity,
                     f, key_label, sequencer_change);
    }
    constexpr Hooks with_key_label(OnKeyLabel f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity,
                     queued_control, f, sequencer_change);
    }
    constexpr Hooks with_sequencer_change(OnSequencerChange f) const {
        return Hooks(context, recordable_note_on, selected_note_off, recording_tick, input_activity,
                     queued_control, key_label, f);
    }
};
}}
