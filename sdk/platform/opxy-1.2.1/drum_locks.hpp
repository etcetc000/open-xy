// Step locks of a padMix kit's Shift fields (SDK 1.10). Stock locks a step's
// value of each of a track's words 0..41 (one 16-bit slot a word, any number
// a step) through its own editor, storage, copy and slide. M3's filter words
// 16..19 lock on every track but do nothing for an SDK kit voice: stock's drum
// renderer has no filter stage. A padMix kit uses them as per-hit offsets to
// the played key's PAN, LEVEL and its two pad controls:
// - Shift and a turn with a step held write the step's lock through stock's
//   editor (synth_controls.cpp), one unit a detent (sdk_param_steps);
// - at note start the GUI adds the live words (the step's locks, else the
//   neutral base) into the note's pad payload before stock publishes it
//   (drum_locks.cpp), so each hit plays its own offsets and voices still
//   sounding keep theirs;
// - their stored base stays neutral (0x40000000): at load, after scramble and
//   whenever the selected kit's base is not.
// A word holds stock's stepped value: the centre of bucket k of 2r + 1, where
// r is the field's reach either side, so neutral is bucket r, offset 0. A
// word of exactly 0 (a preset's or an old project's default) is neutral too:
// stock's steps never store it.
#pragma once
#include "drum_machines.hpp"
#include "drum_storage.hpp"

namespace opxy { namespace platform {
const u32 kDrumLockWord = 16;      // pan, level, pad control 1, pad control 2
const u32 kDrumLocks = 4;
const u32 kDrumNoLock = kDrumLocks;
const i32 kDrumLockNeutral = 0x40000000;
const i32 kDrumLockPan = 100, kDrumLockLevel = 30, kDrumLockPad = 127;

// The lock a Shift choice writes on a padMix kit, or kDrumNoLock: the model,
// the choke group and the sends are the key's or the track's alone.
inline u32 drum_lock(const DrumMachine& machine, u32 choice) {
    if (!machine.pad_mix) return kDrumNoLock;
    if (choice == DrumMachine::kShiftPan) return 0;
    if (choice == DrumMachine::kShiftLevel) return 1;
    if (choice >= DrumMachine::kShiftPad && choice - DrumMachine::kShiftPad < kDrumKitPadFields &&
        choice - DrumMachine::kShiftPad < machine.pad_count)
        return 2 + (choice - DrumMachine::kShiftPad);
    return kDrumNoLock;
}

// A lock's reach either side of neutral: pan +-100, level +-30 dB, a pad
// control its whole span (at most 127, as stock steps at most 255 buckets);
// 0 for a lock the kit does not have.
inline i32 drum_lock_reach(const DrumMachine& machine, u32 lock) {
    if (!machine.pad_mix || lock >= kDrumLocks) return 0;
    if (lock == 0) return kDrumLockPan;
    if (lock == 1) return kDrumLockLevel;
    const u32 field = lock - 2;
    if (field >= machine.pad_count || !machine.pad_bits) return 0;
    const bool sign = (machine.pad_signs >> field & 1u) != 0;
    const i32 span = drum_field_max(machine.pad_bits[field], sign) - drum_field_min(machine.pad_bits[field], sign);
    return span < kDrumLockPad ? span : kDrumLockPad;
}
inline u32 drum_lock_buckets(i32 reach) { return reach > 0 ? static_cast<u32>(2 * reach + 1) : 0; }

// A word's offset, decoded as stock's step 8d5ad2e8 finds a bucket.
inline i32 drum_lock_offset(i32 word, i32 reach) {
    if (word <= 0 || reach <= 0) return 0;
    const u32 buckets = drum_lock_buckets(reach);
    u32 bucket = ((static_cast<u32>(word) >> 8) * buckets) >> 23;
    if (bucket >= buckets) bucket = buckets - 1;
    return static_cast<i32>(bucket) - reach;
}
// The word stock stores for an offset: its bucket's centre.
inline i32 drum_lock_word(i32 offset, i32 reach) {
    const u32 buckets = drum_lock_buckets(reach);
    if (!buckets) return kDrumLockNeutral;
    const i32 width = static_cast<i32>(0x7fffffffu / buckets);
    return width * (offset + reach) + (width >> 1);
}
// A track's four lock words as offsets.
inline void drum_lock_offsets(const DrumMachine& machine, const i32* words, i32* offsets) {
    for (u32 i = 0; i < kDrumLocks; ++i) offsets[i] = words ? drum_lock_offset(words[i], drum_lock_reach(machine, i)) : 0;
}
inline i32 drum_lock_clamp(i32 value, i32 low, i32 high) { return value < low ? low : value > high ? high : value; }

// Stock's payload words for a pad (8d5ad660): word 5 its pan, word 0 its
// model byte's bin, 6 x type + group, over 66 (8d5ad6ec..8d5ad708).
inline i32 drum_pan_word(i32 pan) { return pan * 0x00a3d70a + 0x40000000; }
inline i32 drum_marked_word(u8 byte) {
    return static_cast<i32>((6 * kDrumModelMark + (drum_mark(byte) >> 4)) * 0x01f07c1fu + 0x00f83e0fu);
}

// A note's pad payload with a step's offsets: pan into word 5, level into
// word 7 through stock's dB-to-gain (`gain`), the kit's pad controls into
// word 6, each clamped to its field. pan and level are the key's stored ones.
// The pad must be marked for its pad controls to reach the adapter
// (drum_lock_mark). False when no offset moves anything.
inline bool drum_lock_fold(const DrumMachine& machine, const i32* offsets, i32 pan, i32 level, i32* payload,
                           i32 (*gain)(i32)) {
    bool moved = false;
    if (offsets[0]) {
        payload[5] = drum_pan_word(drum_lock_clamp(pan + offsets[0], -kDrumPanLimit, kDrumPanLimit));
        moved = true;
    }
    if (offsets[1]) {
        const i32 played = drum_lock_clamp(level, kDrumLevelFloor, kDrumLevelCeiling);
        payload[kDrumGainWord] = gain(drum_lock_clamp(played + offsets[1], kDrumLevelFloor, kDrumLevelCeiling));
        moved = true;
    }
    for (u32 field = 0; field < kDrumKitPadFields; ++field) {
        u32 low, bits;
        bool sign;
        if (!offsets[2 + field] || !machine.pad_field(field, low, bits, sign)) continue;
        const i32 now = drum_field_get(payload[kDrumPadPayload], low, bits, sign);
        const i32 next = drum_lock_clamp(now + offsets[2 + field], drum_field_min(bits, sign), drum_field_max(bits, sign));
        payload[kDrumPadPayload] = drum_field_set(payload[kDrumPadPayload], low, bits, next);
        moved = true;
    }
    return moved;
}

// An unmarked pad (one the kit has not stored) plays its key's defaults and
// neutral pad fields, whatever stock left in its words. Before pad-control
// offsets go into its payload, give the payload the form a marked pad has:
// the mark (stock's bin, its choke group kept), the defaults packed into
// words 1..4, and word 6's pad fields and sends cleared. It plays as before.
inline bool drum_lock_mark(i32* payload, u8 byte, u32 model, const u8* controls, const DrumCodec& codec) {
    i32 words[kDrumStorageWords];
    if (!drum_pack(model, controls, words, codec)) return false;
    payload[0] = drum_marked_word(byte);
    for (u32 i = 0; i < kDrumStorageWords; ++i) payload[1 + i] = words[i];
    payload[kDrumPadPayload] &= ~static_cast<i32>(kDrumPadMask);
    return true;
}
}}
