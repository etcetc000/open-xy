#include <opxy/extension.hpp>
#include <opxy/double_tap.hpp>
#include "history.hpp"

namespace opxy { namespace extensions { namespace capture {
namespace {
RecordingHistory history;
DoubleTap tap;
Context previous;
Note snapshot[RecordingHistory::capacity];
u32 snapshot_count;

void context(const Context& context) {
    if (!(context.recording == previous.recording) || context.playing != previous.playing) tap.cancel();
    history.sync(context.recording, context.playing);
    previous = context;
}
void input_activity() { tap.cancel(); }
void recordable_note_on(const Context&, const LiveNote& note) {
    history.note_on(note.position, note.pitch, note.velocity, note.collision_radius);
}
void selected_note_off(const Context&, const NoteOff& note) { history.note_off(note.pitch); }
void recording_tick(const Context&, const Tick& tick) {
    if (tick.discontinuity) { history.clear(); tap.cancel(); return; }
    history.advance(tick.delta_q8);
}
void queued_control(const Context& context, const Control& control, const Actions& actions) {
    if (control.key != keys::record) { tap.cancel(); return; }
    if (!control.accepted || !context.recording.recordable) {
        // Still end the tap, or the next press would read as a key repeat.
        if (!control.pressed) tap.release(control.time);
        tap.cancel(); return;
    }
    if (control.pressed) {
        if (tap.press(control.time, control.unmodified))
            snapshot_count = history.snapshot(snapshot, RecordingHistory::capacity);
        return;
    }
    if (!tap.release(control.time) || !snapshot_count) return;
    // The take replaces the track's sequence, with Undo. History holds at most
    // one full pattern, so a take that is written fits completely.
    if (actions.replace_notes(context, context.recording.track, snapshot, snapshot_count) == snapshot_count) {
        history.clear();
        actions.notify("captured");
    }
}
}
extern constexpr Hooks hooks = Hooks()
    .with_context(context)
    .with_recordable_note_on(recordable_note_on)
    .with_selected_note_off(selected_note_off)
    .with_recording_tick(recording_tick)
    .with_input_activity(input_activity)
    .with_queued_control(queued_control);
}}}
