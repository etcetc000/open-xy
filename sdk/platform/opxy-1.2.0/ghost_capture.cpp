#include "ghost_capture.hpp"

namespace opxy { namespace platform {
namespace {
// Constant initialization: no C++ runtime, heap or static-constructor startup.
RecordingHistory history;
RecordTap tap;
RecordingContext context;
bool running;
RecordedNote snapshot[RecordingHistory::capacity];
u32 snapshot_count;
}

void capture_sync() {
    bool playing = false;
    const RecordingContext next = capture_context(playing);
    if (!(next == context) || playing != running) tap.cancel();
    history.sync(next, playing);
    context = next;
    running = playing;
}
void capture_cancel_gesture() { tap.cancel(); }

void capture_note_on(u32 track, u32 pitch, u32 velocity) {
    capture_sync();
    tap.cancel(); // Live playing is another control, including the immediate key path.
    if (track != context.track || !context.eligible || !running) return;
    const i32 radius = capture_collision_radius(track);
    if (radius >= 0)
        history.note_on(capture_position(track), pitch, velocity, static_cast<u32>(radius));
}

void capture_note_off(u32 track, u32 pitch) {
    capture_sync();
    tap.cancel();
    if (track == context.track) history.note_off(pitch);
}

void capture_tick(u32 delta) {
    capture_sync();
    if (!context.eligible || !running) return;
    // Stock shifts the signed scheduler delta by eight before scaling it.
    // A discontinuity outside that range cannot describe this bounded take.
    if (delta > 0x007fffffu) { history.clear(); tap.cancel(); return; }
    history.advance(capture_scaled_delta(context.track, delta));
}

void capture_control(u32 key, bool pressed, bool accepted, bool unmodified,
                     RecordTap::Time time) {
    capture_sync();
    if (key != 50 || !accepted || !context.eligible) { tap.cancel(); return; }
    if (pressed) {
        if (tap.press(time, unmodified))
            snapshot_count = history.snapshot(snapshot, RecordingHistory::capacity);
        return;
    }
    if (!tap.release(time)) return;
    if (!snapshot_count) return;
    if (capture_write(context.track, snapshot, snapshot_count)) {
        history.clear();
        capture_notify();
    }
}
}}
