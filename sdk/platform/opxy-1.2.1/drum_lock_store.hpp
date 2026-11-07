// Where a padMix kit's per-hit locks (drum_locks.hpp) are kept: the storage
// backend. Its interface is all the rest of the SDK uses:
// - drum_lock_store_edit: write one field of the held step's locks, from a turn;
// - drum_lock_store_take / drum_lock_store_note: the step a note starting on
//   `track` plays (taken at note start, after the sequencer's lock pass), and its
//   locks for the note's key;
// - drum_lock_store_held: the held step's locks for the selected key, for the screen;
// - drum_lock_store_neutral / _neutral_words: the stored base back to "no
//   lock", at load, after scramble and once a frame for the selected kit.
//
// This backend is the step-lock handles (lock_handles.hpp): every field, PAN
// included, lives in the SDK pool under the handle the step keeps in lock word 37,
// per key: a turn locks the field for the key the Shift row shows, and a note
// plays its own key's locks (falling back, field by field, to a step-wide entry,
// which this client does not write yet). Stock copies, moves, clears, saves and
// loads the handle with its step. Words 36 and 38 carry nothing: their stored
// base stays at no lock (36 at 0, 38 stock's centred pan term), as 37's does at 0,
// which the step hook compares the live word against.
#pragma once
#include "drum_locks.hpp"
#include "lock_handles.hpp"
#include "stock.hpp"

namespace opxy { namespace platform {
const u32 kDrumLockBase = 0x3864;          // a pattern's track object: its 58 stored words

// The key the Shift row shows and stock edits (stock.hpp drum_edit_key).
inline u32 drum_lock_key(u32 track) { return drum_edit_key(track); }
// A stored field value within what the kit locks (0, no lock, outside it).
inline i32 drum_lock_checked(const DrumMachine& machine, u32 field, i32 value) {
    i32 low, high;
    drum_lock_range(machine, field, low, high);
    return value >= low && value <= high && low != high ? value : 0;
}

inline void drum_lock_store_held(const DrumMachine& machine, u32 track, DrumHitLocks& locks) {
    drum_hit_clear(locks);
    const u32 key = drum_lock_key(track);
    for (u32 field = 0; field < kHitFields; ++field) {
        i32 value;
        if (sdk_project_locks_held_value(track, key, field, &value)) locks.offset[field] = drum_lock_checked(machine, field, value);
    }
}
// At note start (the payload builder's call, 8d595b50): the handle the
// sequencer's step hook noted for this note's step, taken once; 0 for a note
// played live or a step with no locks.
inline u32 drum_lock_store_take(u32 track) { return step_locks::take_step(track); }
inline void drum_lock_store_note(const DrumMachine& machine, u32 handle, u32 key, DrumHitLocks& locks) {
    drum_hit_clear(locks);
    if (!handle) return;
    i32 values[step_locks::kFields];
    const u32 mask = step_locks::handle_fields(handle, key, values);
    for (u32 field = 0; field < kHitFields; ++field)
        if (mask >> field & 1u) locks.offset[field] = drum_lock_checked(machine, field, values[field]);
}
// A turn of `field` with a step held, for the selected key: from what the step
// plays for it (or, for a send, `start`: 1 + the send code the key plays), into
// the pool; a field turned back to 0 is unlocked.
inline u32 drum_lock_store_edit(u32, u32 track, const DrumMachine& machine, u32 field, u32 event, i32 start) {
    if (field >= kHitFields || !event) return 0;
    const i32 delta = drum_lock_detents(at<const i32>(event), field);
    if (!delta) return 0;
    const u32 key = drum_lock_key(track);
    i32 lock = 0;
    if (!sdk_project_locks_held_value(track, key, field, &lock)) lock = 0;
    lock = drum_lock_checked(machine, field, lock);
    const i32 next = drum_lock_turn(machine, field, lock, start, delta);
    if (next == lock) return 0;
    return sdk_project_locks_held_set(track, key, field, next != 0, next) ? 1 : 0;
}
// A track object's stored words with no per-hit lock: 36, 37 at 0, 38 centred.
inline void drum_lock_store_neutral_words(volatile i32* stored) {
    stored[kDrumHitMixWord] = 0;
    stored[kDrumHitHandleWord] = 0;
    stored[kDrumHitPanWord] = kDrumPanTermNeutral;
}
// The track's stored words (its selected pattern's, 8d587708) back to no
// lock, each changed one also live through 8d5acea8, which sends it to the
// audio side; not stock's set, which would record the value as a lock while
// recording.
inline void drum_lock_store_neutral(u32 project, u32 track) {
    volatile i32* stored = at<volatile i32>(stock<u32(u32)>(0x8d587708)(track) + kDrumLockBase);
    for (u32 index = kDrumHitMixWord; index <= kDrumHitPanWord; ++index) {
        const i32 neutral = index == kDrumHitPanWord ? kDrumPanTermNeutral : 0;
        if (stored[index] == neutral) continue;
        stored[index] = neutral;
        stock<void(u32, u32, u32, i32)>(0x8d5acea8)(project, track, index, neutral);
    }
}
}}
