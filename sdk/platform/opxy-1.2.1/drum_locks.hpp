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
// - pan_term: stock's note-start pan term (word 38) the hit plays, which stock
//   combines with the pad's pan itself (8d51a688): neutral at 0x40000000.
// - offset[kHitPan]: a pan offset to fold into the payload instead (a backend
//   that does not use word 38); offset[kHitLevel]: dB; offset[kHitPad1/2]: the
//   pad controls' units.
// - offset[kHitSend1/2]: an override of the key's send: 1 + its send code
//   (1 TRK, 2..8 the amounts 0..100, drum_send.hpp).
struct DrumHitLocks {
    i32 pan_term;
    i32 offset[kHitFields];
    bool any() const {
        for (u32 i = 0; i < kHitFields; ++i) if (offset[i]) return true;
        return false;
    }
};
inline void drum_hit_clear(DrumHitLocks& locks) {
    locks.pan_term = kDrumPanTermNeutral;
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
// Stock's note-start pan (8d51a688): a pad's pan word with the pan term. Above
// its centre the term pushes towards the right by (term - 1/2) x 2 x (1 - pan);
// at or below, it scales the pan towards the left by 2 x term.
inline i32 drum_pan_push(i32 pan_word, i32 term) {
    const i32 centre = 0x40000000;
    if (term > centre)
        return q31::add(pan_word, q31::multiply(q31::add(q31::add(term, -centre), q31::add(term, -centre)),
                                                q31::add(q31::maximum, -pan_word)));
    return q31::multiply(pan_word, q31::add(term, term));
}
// The pan a key at `pan` plays with a pan term, back in stock's -100..100.
inline i32 drum_pan_played(i32 pan, i32 term) {
    if (term == kDrumPanTermNeutral) return pan;
    const i32 word = drum_pan_push(drum_pan_word(pan), term) - 0x40000000;
    const i32 step = 0x00a3d70a;
    const i32 played = word >= 0 ? (word + step / 2) / step : -((-word + step / 2) / step);
    return drum_lock_clamp(played, -kDrumPanLimit, kDrumPanLimit);
}

// A note's pad payload with a step's locks: a pan offset into word 5, level
// into word 7 through stock's dB-to-gain (`gain`), the pad controls and send
// overrides into word 6, each clamped to its field; pan and level are the
// key's stored ones. The pad must be marked for word 6 to reach the adapter
// (drum_lock_mark). False when no lock moves anything.
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
// Whether a step's locks need a marked pad's payload: word 6's fields reach
// the adapter only for one.
inline bool drum_lock_needs_mark(const DrumHitLocks& locks) {
    return locks.offset[kHitPad1] || locks.offset[kHitPad2] || locks.offset[kHitSend1] || locks.offset[kHitSend2];
}

// An unmarked pad (one the kit has not stored) plays its key's defaults and
// neutral pad fields, whatever stock left in its words. Before pad-control or
// send locks go into its payload, give the payload the form a marked pad has:
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

// The interim backend's codec (drum_lock_store.hpp). PAN is stock's own word
// 38; the other five are packed into two lockable words stock never reads
// (36 and 37, traced dead), 16 bits each as a lock slot keeps them, each
// field's 0 meaning "not locked", so a word of 0 (load, presets, old projects)
// locks nothing:
// - word 36: LEVEL in bits 0..5 (offset + 31), FX I in 6..9 and FX II in
//   10..13 (1 + the send code);
// - word 37: pad control 1 in bits 0..7 and pad control 2 in 8..15 (offset + 128).
const u32 kDrumHitPanWord = 38, kDrumHitMixWord = 36, kDrumHitPadWord = 37;
struct DrumHitSlot { u8 word, low, bits, bias; };
inline DrumHitSlot drum_hit_slot(u32 field) {
    switch (field) {
    case kHitLevel: return {kDrumHitMixWord, 0, 6, 31};
    case kHitSend1: return {kDrumHitMixWord, 6, 4, 0};
    case kHitSend2: return {kDrumHitMixWord, 10, 4, 0};
    case kHitPad1: return {kDrumHitPadWord, 0, 8, 128};
    case kHitPad2: return {kDrumHitPadWord, 8, 8, 128};
    default: return {0, 0, 0, 0};
    }
}
// A field from its word's 16-bit slot, and back.
inline i32 drum_hit_get(u32 half, u32 field) {
    const DrumHitSlot slot = drum_hit_slot(field);
    const u32 value = slot.bits ? (half >> slot.low) & ((1u << slot.bits) - 1) : 0;
    return value ? static_cast<i32>(value) - slot.bias : 0;
}
inline u32 drum_hit_put(u32 half, u32 field, i32 lock) {
    const DrumHitSlot slot = drum_hit_slot(field);
    if (!slot.bits) return half;
    const u32 mask = ((1u << slot.bits) - 1) << slot.low;
    const u32 value = lock ? static_cast<u32>(lock + slot.bias) & ((1u << slot.bits) - 1) : 0;
    return ((half & ~mask) | (value << slot.low)) & 0xffffu;
}
// A step's locks from words 36, 37 and 38 as Q31 words (a lock slot's 16 bits
// on top). A packed word with any low bit set is no lock of ours: stock's
// slots and the SDK's neutral never have one, but slide's blend between two
// steps' locks (8d5937bc) does, and would decode as nonsense.
inline void drum_hit_decode(i32 mix, i32 pads, i32 pan_term, DrumHitLocks& locks) {
    drum_hit_clear(locks);
    locks.pan_term = pan_term;
    const u32 words[2] = {static_cast<u32>(mix), static_cast<u32>(pads)};
    for (u32 field = kHitLevel; field < kHitFields; ++field) {
        const u32 word = words[drum_hit_slot(field).word - kDrumHitMixWord];
        if (!(word & 0xffffu)) locks.offset[field] = drum_hit_get(word >> 16, field);
    }
}
}}
