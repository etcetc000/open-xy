// SDK step locks by handle (docs/abi-1.2.1.md "Step-lock handles"). A step that holds
// SDK locks carries a 16-bit handle as an ordinary stock lock on word 37, a lockable
// word that no stock page, preset or engine is known to use; the values live in an SDK
// pool (lock_pool.hpp). Stock's own operations then move, copy, clear, snapshot, save
// and load the handle with its step's row, as they do every lock: copy and paste of a
// pattern or a step, page duplication, nudge, step clear and its restore, clearing locks
// or steps, adding a pattern. The SDK hooks none of them.
//
// - Sharing: after a copy two steps hold one handle. An edit through held_set first
//   gives its step a handle of its own when any other step, a clipboard or the
//   recorder's snapshot holds the same one (copy-on-write).
// - Saving: garbage collection keeps only the handles the saved project, stock's two
//   clipboards and the recorder's snapshot hold; the companion's open-xy.locks section
//   keeps their values (project_companion.cpp).
// - Loading: a load stages the companion's pool beside the active one. When the
//   companion matches the project and its open-xy.locks is a whole version 1 pool,
//   every handle the loaded project holds that the pool lacks is cleared, so none
//   dangles. Otherwise (no companion, one that does not match, a newer version, a
//   section cut short or missing) the handles stay: they play nothing here, no new
//   lock takes their numbers, and firmware that reads their values still finds them.
//   The staged pool becomes the active one when stock makes that project active;
//   handles in the clipboards and the snapshot are then carried over to it under new
//   numbers, and one with no values is left as it is.
// - Playback: the step hook notes, for each track, the handle of the step whose locks
//   stock just applied (gate included), for that step's note start to take.
#pragma once
#include "lock_pool.hpp"

namespace opxy { namespace platform { namespace step_locks {
// The lock word that carries the handle.
const u32 kHandleWord = 37;

// Whether `track` is a handle client, and whether a pattern's machine `engine` is one:
// the checks the backend asks, defined by the client's glue (drum_locks.cpp: an owned
// padMix kit). Another client (stock Drumline, a synth) adds itself there; the backend
// itself knows no machine. Only a client's patterns on instrument tracks 0..7 are
// searched, collected, swept and carried: word 37 on any other track stays stock's.
bool handle_client(u32 track);
bool pattern_client(u32 engine);

// The handle of the step whose note is now starting on `track`, as the sequencer's
// step hook noted it, or 0: the note was played live, or its step locks nothing. A
// note start takes it once.
u32 take_step(u32 track);
// The fields a handle locks for `key` (its own entry, else the step-wide one): a
// mask, and values by field.
u32 handle_fields(u32 handle, u32 key, i32* values);

// The held step (stock's step held for locks) of `track`'s current pattern: whether
// one is held, and `field`'s value there for `key`.
bool step_held();
bool held_value(u32 track, u32 key, u32 field, i32& value);
// Locks (`present`) or unlocks `field` for `key` at the held step: copy-on-write, a
// new handle written through stock's lock writer, the handle word cleared when none is
// left. False when no step is held or the pool is full (nothing changes).
bool held_set(u32 track, u32 key, u32 field, bool present, i32 value);
}}}

extern "C" {
// 8d5939aa: the sequencer's call of its lock pass for a step's note (8d5936c0).
opxy::u32 sdk_lock_step(opxy::u32 project, opxy::u32 track, opxy::u32 position) __asm__("sdk_lock_step");
// The companion's open-xy.locks (project_companion.cpp, through startup.cpp): its size
// after a save's garbage collection, within `budget` bytes; then its text.
opxy::u32 sdk_step_locks_size(void* project, opxy::u32 budget) __asm__("sdk_step_locks_size");
void sdk_step_locks_write(void* file) __asm__("sdk_step_locks_write");
// Reading: before a load, each byte of a section's pool, then whether the companion
// held a valid section that matches the loaded project.
void sdk_step_locks_begin() __asm__("sdk_step_locks_begin");
void sdk_step_locks_byte(opxy::u32 byte) __asm__("sdk_step_locks_byte");
void sdk_step_locks_loaded(void* project, opxy::u32 valid) __asm__("sdk_step_locks_loaded");
// held_value and held_set for units every build links (opxy::i32* for the reference),
// and once a GUI frame (sdk_send_refresh): a step note's handle no note start took expires.
opxy::u32 sdk_step_locks_held_value(opxy::u32 track, opxy::u32 key, opxy::u32 field, opxy::i32* value)
    __asm__("sdk_step_locks_held_value");
opxy::u32 sdk_step_locks_held_set(opxy::u32 track, opxy::u32 key, opxy::u32 field, opxy::u32 present, opxy::i32 value)
    __asm__("sdk_step_locks_held_set");
void sdk_step_locks_frame() __asm__("sdk_step_locks_frame");
// The same calls as every build links them (startup.cpp): the service's in a build with
// it, otherwise 0 and nothing. Units outside the drum_locks service call these, never
// step_locks:: directly, so a build without a padMix kit links.
opxy::u32 sdk_project_locks_held_value(opxy::u32, opxy::u32, opxy::u32, opxy::i32*) __asm__("sdk_project_locks_held_value");
opxy::u32 sdk_project_locks_held_set(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::i32)
    __asm__("sdk_project_locks_held_set");
void sdk_project_locks_frame() __asm__("sdk_project_locks_frame");
}
