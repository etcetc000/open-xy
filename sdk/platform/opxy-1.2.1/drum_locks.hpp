// Per-hit step locks of a padMix kit's Shift fields (SDK 1.10): with a step
// held, Shift and a turn of PAN, LEVEL, a pad control, FX I or FX II lock that
// field for the step's hits, on top of the played key's own value. At note
// start the GUI folds the step's locks into the note's pad payload before
// stock publishes it (drum_locks.cpp), so each hit plays its own locks and
// voices still sounding keep theirs.
//
// What the locks are lives here (DrumHitLocks, the fold); where a step keeps
// them is a storage backend behind a narrow interface (drum_lock_store.hpp):
// write a field for the held step, read the playing step's fields at note
// start (and the held step's, for the screen). The backend can change without
// touching editing, folding or showing.
#pragma once
#include "drum_machines.hpp"
#include "drum_storage.hpp"
#include "drum_send.hpp"

namespace opxy { namespace platform {
// The per-hit fields, in the order the kit's Shift rows show them.
enum DrumHitField { kHitPan, kHitLevel, kHitPad1, kHitPad2, kHitSend1, kHitSend2, kHitFields };
const u32 kDrumNoLock = kHitFields;
const i32 kDrumLockPan = 100, kDrumLockLevel = 30, kDrumLockPad = 127;
// Stock's note-start pan term (word 38) at its centre: a hit's pan as its pad's.
const i32 kDrumPanTermNeutral = 0x40000000;

// A step's locks, as a backend reads them. Each field's 0 is "not locked".
// - offset[kHitPan]: a pan offset, folded into the payload's word 5;
//   offset[kHitLevel]: dB; offset[kHitPad1/2]: the pad controls' units.
// - offset[kHitSend1/2]: an override of the key's send: 1 + its send code
//   (1 TRK, 2..8 the amounts 0..100, drum_send.hpp).
struct DrumHitLocks {
    i32 offset[kHitFields];
    bool any() const {
        for (u32 i = 0; i < kHitFields; ++i) if (offset[i]) return true;
        return false;
    }
};
inline void drum_hit_clear(DrumHitLocks& locks) {
    for (u32 i = 0; i < kHitFields; ++i) locks.offset[i] = 0;
}

// The field a Shift choice locks on a padMix kit, or kDrumNoLock: the model
// and the choke group are the key's alone.
inline u32 drum_lock(const DrumMachine& machine, u32 choice) {
    if (!machine.pad_mix) return kDrumNoLock;
    if (choice == DrumMachine::kShiftPan) return kHitPan;
    if (choice == DrumMachine::kShiftLevel) return kHitLevel;
    if (choice >= DrumMachine::kShiftPad && choice - DrumMachine::kShiftPad < kDrumPadFields) {
        const u32 position = choice - DrumMachine::kShiftPad;
        if (position >= kDrumKitPadFields) return kHitSend1 + (position - kDrumKitPadFields);
        return position < machine.pad_count ? kHitPad1 + position : kDrumNoLock;
    }
    return kDrumNoLock;
}

// A field's lowest and highest locked value: pan +-100, level +-30 dB, a pad
// control its span (at most 127) either way, a send TRK (1) to 100 (8); both
// 0 for a field the kit does not have.
inline void drum_lock_range(const DrumMachine& machine, u32 field, i32& low, i32& high) {
    low = high = 0;
    if (!machine.pad_mix || field >= kHitFields) return;
    if (field == kHitPan) { low = -kDrumLockPan; high = kDrumLockPan; return; }
    if (field == kHitLevel) { low = -kDrumLockLevel; high = kDrumLockLevel; return; }
    if (field >= kHitSend1) { low = 1; high = static_cast<i32>(kDrumSendCodes); return; }
    const u32 pad = field - kHitPad1;
    if (pad >= machine.pad_count || !machine.pad_bits) return;
    const bool sign = (machine.pad_signs >> pad & 1u) != 0;
    const i32 span = drum_field_max(machine.pad_bits[pad], sign) - drum_field_min(machine.pad_bits[pad], sign);
    high = span < kDrumLockPad ? span : kDrumLockPad;
    low = -high;
}
inline i32 drum_lock_clamp(i32 value, i32 low, i32 high) { return value < low ? low : value > high ? high : value; }
// A turn's step for a lock, in the field's own units, as the unlocked field
// moves (sdk_drum_edit): stock's turn event is three words, [0] the continuous
// motion stock adds to a Q31 word (8d5ad254), [1] the signed detents and [2] the
// speed (8d5ad2e8). PAN moves (speed + 1) units a detent, as stock's pan editor
// (8d4d60b0); every other field one. Never word [0]: taken as units, any turn
// clamped a lock to one end of its range or the other.
inline i32 drum_lock_detents(const i32* event, u32 field) {
    if (!event) return 0;
    i32 delta = event[1];
    if (delta > 127) delta = 127;
    if (delta < -127) delta = -127;
    if (field != kHitPan) return delta;
    i32 step = event[2] + 1;
    if (step < 1) step = 1;
    if (step > 200) step = 200;
    return delta * step;
}
// A turn of `delta` detents on a field's lock: a send starts from what the key
// plays (`start`, 1 + its code), any other field from no offset.
inline i32 drum_lock_turn(const DrumMachine& machine, u32 field, i32 lock, i32 start, i32 delta) {
    i32 low, high;
    drum_lock_range(machine, field, low, high);
    if (low == high) return lock;
    const i32 from = lock ? lock : field >= kHitSend1 ? start : 0;
    return drum_lock_clamp(from + delta, low, high);
}

// Stock's payload words for a pad (8d5ad660): word 5 its pan, word 0 its
// model byte's bin, 6 x type + group, over 66 (8d5ad6ec..8d5ad708).
inline i32 drum_pan_word(i32 pan) { return pan * 0x00a3d70a + 0x40000000; }
inline i32 drum_marked_word(u8 byte) {
    return static_cast<i32>((6 * kDrumModelMark + (drum_mark(byte) >> 4)) * 0x01f07c1fu + 0x00f83e0fu);
}
// A note's pad payload with a step's locks: a pan offset into word 5, level
// into word 7 through stock's dB-to-gain (`gain`), the pad controls and send
// overrides into word 6, each clamped to its field; pan and level are the
// key's as it plays them (`level` 0 dB on a key the kit has not set up). Word
// 6 and 7 reach the adapter only for a key set up (drum_lock_setup). False
// when no lock moves anything.
inline bool drum_lock_fold(const DrumMachine& machine, const DrumHitLocks& locks, i32 pan, i32 level, i32* payload,
                           i32 (*gain)(i32)) {
    bool moved = false;
    if (locks.offset[kHitPan]) {
        payload[5] = drum_pan_word(drum_lock_clamp(pan + locks.offset[kHitPan], -kDrumPanLimit, kDrumPanLimit));
        moved = true;
    }
    if (locks.offset[kHitLevel]) {
        const i32 played = drum_lock_clamp(level, kDrumLevelFloor, kDrumLevelCeiling);
        payload[kDrumGainWord] = gain(drum_lock_clamp(played + locks.offset[kHitLevel], kDrumLevelFloor, kDrumLevelCeiling));
        moved = true;
    }
    for (u32 field = 0; field < kDrumKitPadFields; ++field) {
        u32 low, bits;
        bool sign;
        const i32 offset = locks.offset[kHitPad1 + field];
        if (!offset || !machine.pad_field(field, low, bits, sign)) continue;
        const i32 now = drum_field_get(payload[kDrumPadPayload], low, bits, sign);
        const i32 next = drum_lock_clamp(now + offset, drum_field_min(bits, sign), drum_field_max(bits, sign));
        payload[kDrumPadPayload] = drum_field_set(payload[kDrumPadPayload], low, bits, next);
        moved = true;
    }
    for (u32 send = 0; send < kDrumSends; ++send) {
        const i32 lock = locks.offset[kHitSend1 + send];
        if (lock < 1 || lock > static_cast<i32>(kDrumSendCodes)) continue;
        payload[kDrumPadPayload] = drum_send_store(payload[kDrumPadPayload], send, static_cast<u32>(lock - 1));
        moved = true;
    }
    return moved;
}
// Whether a step's locks need a key set up: the level and word 6's fields reach
// the adapter only for one (drum_storage.hpp). PAN is stock's, on every key.
inline bool drum_lock_needs_setup(const DrumHitLocks& locks) {
    for (u32 field = kHitLevel; field < kHitFields; ++field)
        if (locks.offset[field]) return true;
    return false;
}

// A key the kit has not set up plays at 0 dB with neutral pad fields, and an
// unmarked one its key's defaults, whatever stock left in its record. Before
// level, pad-control or send locks go into its payload, give the payload a
// set-up key's form, as the kit's first edit of the key would: the mark
// (stock's bin, its choke group kept) and the defaults packed into words 1..4
// if it is unmarked, word 6 the set-up value with every field neutral and word
// 7 0 dB (`unity`). It plays as before, plus the locks.
inline bool drum_lock_setup(i32* payload, u8 byte, u32 model, const u8* controls, const DrumCodec& codec, i32 unity) {
    if (!drum_marked_q31(payload[0])) {
        i32 words[kDrumStorageWords];
        if (!drum_pack(model, controls, words, codec)) return false;
        payload[0] = drum_marked_word(byte);
        for (u32 i = 0; i < kDrumStorageWords; ++i) payload[1 + i] = words[i];
    }
    payload[kDrumPadPayload] = static_cast<i32>(kDrumPadReady);
    payload[kDrumGainWord] = unity;
    return true;
}

// The track words around the per-hit locks, each one's stored base kept at no
// lock (drum_lock_store.hpp): 37 carries the step's handle (lock_handles.hpp); 36
// carries nothing on a kit's track; 38 is stock's note-start pan term (8d51af24),
// kept centred, as PAN's locks go into the payload's word 5 instead.
const u32 kDrumHitPanWord = 38, kDrumHitMixWord = 36, kDrumHitHandleWord = 37;
}}
