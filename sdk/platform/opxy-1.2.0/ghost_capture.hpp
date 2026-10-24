// Internal service boundary. Stock addresses and input forwarding stay in the
// version adapter; the history and gesture state do not own firmware objects.
#pragma once
#include "../recording_history.hpp"
#include "../record_tap.hpp"

namespace opxy { namespace platform {
void capture_sync();
void capture_cancel_gesture();
void capture_note_on(u32 track, u32 pitch, u32 velocity);
void capture_note_off(u32 track, u32 pitch);
void capture_tick(u32 delta);
void capture_control(u32 key, bool pressed, bool accepted, bool unmodified,
                     RecordTap::Time time);

RecordingContext capture_context(bool& playing);
i32 capture_position(u32 track);
u32 capture_scaled_delta(u32 track, u32 delta);
i32 capture_collision_radius(u32 track);
bool capture_record_held();
u32 capture_write(u32 track, const RecordedNote* notes, u32 count);
void capture_notify();
}}
