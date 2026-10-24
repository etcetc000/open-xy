#include <opxy/extension.hpp>
#include <opxy/record_tap.hpp>
#include "history.hpp"

namespace opxy { namespace extensions { namespace capture {
namespace {
RecordingHistory history;
RecordTap tap;
Context previous;
Note snapshot[RecordingHistory::capacity];
u32 snapshot_count;

void sync(const Context& context) {
    if (!(context.recording == previous.recording) || context.playing != previous.playing) tap.cancel();
    history.sync(context.recording, context.playing);
    previous = context;
}
void activity() { tap.cancel(); }
void note_on(const Context&, const LiveNote& note) {
    history.note_on(note.position, note.pitch, note.velocity, note.collision_radius);
}
void note_off(const Context&, const NoteOff& note) { history.note_off(note.pitch); }
void tick(const Context&, const Tick& tick) {
    if (tick.discontinuity) { history.clear(); tap.cancel(); return; }
    history.advance(tick.delta_q8);
}
void control(const Context& context, const Control& control, const Actions& actions) {
    if (control.key != Control::record_key || !control.record_accepted || !context.recording.eligible) {
        tap.cancel(); return;
    }
    if (control.pressed) {
        if (tap.press(control.time, control.unmodified_record))
            snapshot_count = history.snapshot(snapshot, RecordingHistory::capacity);
        return;
    }
    if (!tap.release(control.time) || !snapshot_count) return;
    if (actions.insert_notes(context, snapshot, snapshot_count)) {
        history.clear();
        actions.notify("captured");
    }
}
}
extern const Hooks hooks = {sync, note_on, note_off, tick, activity, control};
}}}
