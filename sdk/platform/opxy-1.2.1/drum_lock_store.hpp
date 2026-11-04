// Where a padMix kit's per-hit locks (drum_locks.hpp) are kept: the storage
// backend. Its interface is all the rest of the SDK uses:
// - drum_lock_store_edit: write one field of the held step's locks, from a turn;
// - drum_lock_store_note: the step's locks a note starting on `track` plays (GUI
//   side, in the sequencer's note start, after its lock pass);
// - drum_lock_store_held: the held step's locks, for the screen;
// - drum_lock_store_steps: stock's step count for a word the backend leaves to
//   stock's own editor, else 0;
// - drum_lock_store_neutral / _neutral_words: the stored base back to "no
//   lock", at load, after scramble and once a frame for the selected kit.
//
// This backend (interim) uses stock's own lock slots: PAN in word 38, stock's
// note-start pan term, edited by stock's editor and applied by stock; the other
// five packed into words 36 and 37 (drum_hit_decode), which the SDK writes
// through stock's lock writer 8d586f40, so stock saves, copies and clears them
// with the pattern like any lock.
#pragma once
#include "drum_locks.hpp"
#include "stock.hpp"

namespace opxy { namespace platform {
const u32 kDrumLockProject = 0x86bd027c;   // the GUI's project
const u32 kDrumLockBase = 0x3864;          // a pattern's track object: its 58 stored words
const u32 kDrumPanSteps = 201;             // PAN's lock: one -100..100 unit a detent

// The held step's value of a word as stock's editor shows it (8d5ac93c: its lock, else the stored word).
inline i32 drum_lock_held_word(u32 track, u32 index) {
    return stock<i32(u32, u32, u32)>(0x8d5ac93c)(kDrumLockProject, track, index);
}
inline void drum_lock_store_held(const DrumMachine&, u32 track, DrumHitLocks& locks) {
    drum_hit_decode(drum_lock_held_word(track, kDrumHitMixWord), drum_lock_held_word(track, kDrumHitPadWord),
                    drum_lock_held_word(track, kDrumHitPanWord), locks);
}
// At note start the sequencer has written the step's locks (or the stored
// base) into the live words (8d5accf8, then 8d5936c0).
inline void drum_lock_store_note(const DrumMachine&, u32 track, DrumHitLocks& locks) {
    const volatile i32* live = at<const volatile i32>(kDrumLockProject + 0x4080 + track * 0xe8);
    drum_hit_decode(live[kDrumHitMixWord], live[kDrumHitPadWord], live[kDrumHitPanWord], locks);
}
inline u32 drum_lock_store_steps(const DrumMachine& machine, u32 index) {
    return machine.pad_mix && index == kDrumHitPanWord ? kDrumPanSteps : 0;
}
// A turn of `field` with a step held: PAN through stock's editor (which steps
// word 38 by drum_lock_store_steps); the others in their packed word at the
// held step (8d5b7e4c, plus 16 x the track's page 8d585fb4) through stock's
// lock writer 8d586f40, which sets the step's mask bits, then the redraw
// request stock's editor makes after its own write (8d5ad4d2..8d5ad4d6).
// `start`: 1 + the send code the key plays, where a send lock starts from.
inline u32 drum_lock_store_edit(u32 project, u32 track, const DrumMachine& machine, u32 field, u32 event, i32 start) {
    if (field == kHitPan) return stock<u32(u32, u32, u32, u32)>(0x8d5ad5bc)(project, track, kDrumHitPanWord, event);
    const DrumHitSlot slot = drum_hit_slot(field);
    if (!slot.bits || !event) return 0;
    const i32 delta = *at<const i32>(event);
    const u32 half = static_cast<u32>(drum_lock_held_word(track, slot.word)) >> 16;
    const i32 lock = drum_hit_get(half, field);
    const i32 next = drum_lock_turn(machine, field, lock, start, delta);
    if (next == lock) return 0;
    const u32 step = stock<u32()>(0x8d5b7e4c)() + 16 * stock<u32(u32)>(0x8d585fb4)(track);
    stock<void(u32, u32, u32, u32)>(0x8d586f40)(track, step, slot.word, drum_hit_put(half, field, next));
    stock<void(u32)>(0x8d54b3b6)(stock<u32()>(0x8d54b438)());
    return 1;
}
// A track object's stored words with no per-hit lock: 36, 37 at 0, 38 centred.
inline void drum_lock_store_neutral_words(volatile i32* stored) {
    stored[kDrumHitMixWord] = 0;
    stored[kDrumHitPadWord] = 0;
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
