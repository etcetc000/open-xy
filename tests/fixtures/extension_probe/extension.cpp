#include <opxy/extension.hpp>

// The SDK's adapter test drives this extension through the 1.2.1 adapter and
// reads what it saw. It is a test fixture, not a shipped extension.
namespace opxy { namespace extensions { namespace probe {
struct Seen {
    u32 contexts, notes_on, notes_off, ticks, activity, controls;
    Context context;
    LiveNote note;
    NoteOff off;
    Tick tick;
    Control control;
};
Seen seen;
// What an accepted Record release does: 0 nothing, 1 replace, 2 insert, 3 replace track 3.
u32 command, written;
const Note take[2] = {{-12, 40, 60, 90}, {480, 60, 64, 100}};

namespace {
void context(const Context& c) { ++seen.contexts; seen.context = c; }
void recordable_note_on(const Context&, const LiveNote& n) { ++seen.notes_on; seen.note = n; }
void selected_note_off(const Context&, const NoteOff& n) { ++seen.notes_off; seen.off = n; }
void recording_tick(const Context&, const Tick& t) { ++seen.ticks; seen.tick = t; }
void input_activity() { ++seen.activity; }
void queued_control(const Context& c, const Control& k, const Actions& actions) {
    ++seen.controls;
    seen.control = k;
    if (k.key != keys::record || k.pressed || !command) return;
    if (command == 4) {
        PatternInfo info{}; Note snapshot[pattern_note_capacity]; u32 count=0;
        written = actions.discover_pattern(2,1,info) &&
            actions.read_pattern(info.ref,snapshot,pattern_note_capacity,count) ? count : 0;
        return;
    }
    const u32 track = command == 3 ? 3 : c.recording.track;
    written = command == 2 ? actions.insert_notes(c, track, take, 2) : actions.replace_notes(c, track, take, 2);
    if (written) actions.notify("probed");
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
