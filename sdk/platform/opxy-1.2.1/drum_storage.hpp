// A drum key's model and eight controls in stock Drumline's saved pad fields.
// Stock presets write each of the four parameter words >> 16 and restore it
// << 16, so nonnegative Q15 integers preserve 60 bits. A kit of up to 16
// models stores eight 7-bit controls (56 bits) and a 4-bit model; a larger kit
// (up to 64) stores slots 6 and 7 at 64 steps to give its model 6 bits. The
// pad's model byte (Drumline's
// instrument type in its low nibble, its choke group in the high one) marks
// the layout with type 7, which stock's editor skips and its default kit never
// uses; the choke group stays stock's.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
const u32 kDrumControlCount = 8;
const u32 kDrumStorageWords = 4;
const u32 kDrumModelLimit = 64;   // models a kit can store
const u32 kDrumNarrowModels = 16; // up to this many, every control keeps 7 bits
const u32 kDrumModelMark = 7;     // the model byte's low nibble on a kit's pad

// Enum-bin midpoint using only 32-bit division. For at most 128 models the
// rounding error is below 256 Q31 units, far from either Q15 bin boundary.
inline i32 drum_model_q31(u32 model, u32 count) {
    return count && count <= 128 && model < count
        ? static_cast<i32>((0x40000000u / count) * (2 * model + 1)) : 0;
}

// The pad's model byte, marked; stock's choke groups (high nibble 0..5) stay.
inline u8 drum_mark(u8 byte) {
    const u32 group = byte >> 4;
    return static_cast<u8>(((group > 5 ? 0u : group) << 4) | kDrumModelMark);
}
inline bool drum_marked(u8 byte) { return (byte & 15u) == kDrumModelMark && (byte >> 4) <= 5; }

// A key's choke group, as Shift and the second encoder offer it: none, then
// 1..3. The high nibble is stock's play mode, as presets name it: gate 0,
// oneshot 1, mute1 2, loop 3, mute2 4, mute3 5. Pads sharing a mute cut each
// other off; gate and loop read as none.
const u32 kDrumChokeChoices = 4;
const u8 kDrumChokeGroups[kDrumChokeChoices] = {1, 2, 4, 5};
inline u32 drum_choke(u8 byte) {
    const u32 group = byte >> 4;
    for (u32 i = 0; i < kDrumChokeChoices; ++i)
        if (kDrumChokeGroups[i] == group) return i;
    return 0;
}
inline u8 drum_mark_choke(u32 choice) {
    return static_cast<u8>((kDrumChokeGroups[choice < kDrumChokeChoices ? choice : 0] << 4) | kDrumModelMark);
}
// The note payload's first word: stock's Q31 of the model byte, 6 type + group
// over 66 bins (8d5ad6ec). Type 7 is bins 42..47.
inline bool drum_marked_q31(i32 word) {
    if (word < 0) return false;
    const u32 bin = ((static_cast<u32>(word) >> 16) * 66u) >> 15;
    return bin >= 6 * kDrumModelMark && bin < 6 * kDrumModelMark + 6;
}

// The values a note plays: each stored control plus its track parameter as an
// offset around Q31 0.5, clamped to 0..127. Step locks and modulation reach a
// kit only as these offsets; null parameters are neutral. The model has none.
inline void drum_offset(const u8* controls, const i32* parameters, u8* live) {
    for (u32 i = 0; i < kDrumControlCount; ++i) {
        i32 value = controls[i];
        if (parameters) value += static_cast<i32>((parameters[i] < 0 ? 0u : static_cast<u32>(parameters[i])) >> 24) - 64;
        live[i] = static_cast<u8>(value < 0 ? 0 : value > 127 ? 127 : value);
    }
}

// Whether a kit of `models` stores `slot` at 64 steps: slots 6 and 7 of a kit
// with more than 16 models.
inline bool drum_coarse(u32 models, u32 slot) { return models > kDrumNarrowModels && slot >= 6; }
// A coarse slot's step for a value 0..127, to the nearest (63 is 127), and back.
inline u32 drum_coarse_step(u32 value) {
    if (value >= 126) return 63;
    const u32 s = (value + 1) >> 1;
    return s > 62 ? 62 : s;
}
inline u8 drum_coarse_value(u32 step) { return static_cast<u8>(step >= 63 ? 127 : step << 1); }
// The value a slot stores: itself, or a coarse slot's nearest step.
inline u8 drum_stored(u32 models, u32 slot, u32 value) {
    return drum_coarse(models, slot) ? drum_coarse_value(drum_coarse_step(value)) : static_cast<u8>(value);
}

// Bit k of the 60: controls in bits 0..55, the model in 56..59. A larger kit
// keeps every other bit there: slots 6 and 7 store their step in their top six
// bits, and their lowest bits (42, 49) hold the model's bits 4 and 5. So a pad
// stored while the kit had at most 16 models reads slots 0..5 exactly, 6 and 7
// at the step below, and its model when those two were even.
inline u32 drum_bit(u32 k, u32 models, u32 model, const u8* controls) {
    if (k >= 56) return (model >> (k - 56)) & 1u;
    const u32 slot = k / 7, bit = k % 7;
    if (!drum_coarse(models, slot)) return (u32(controls[slot]) >> bit) & 1u;
    return bit ? (drum_coarse_step(controls[slot]) >> (bit - 1)) & 1u : (model >> (slot == 6 ? 4 : 5)) & 1u;
}
inline bool drum_pack(u32 model, const u8* controls, i32* words, u32 models = kDrumNarrowModels) {
    if (!controls || !words || model >= models || models > kDrumModelLimit) return false;
    for (u32 i = 0; i < kDrumControlCount; ++i)
        if (controls[i] > 127) return false;
    u32 packed[kDrumStorageWords] = {};
    for (u32 k = 0; k < 60; ++k)
        packed[k / 15] |= drum_bit(k, models, model, controls) << (k % 15);
    for (u32 i = 0; i < kDrumStorageWords; ++i)
        words[i] = static_cast<i32>(packed[i] << 16);
    return true;
}

// The caller checks the mark first. Failure leaves the outputs untouched, so
// the caller keeps its defaults.
inline bool drum_unpack(const i32* words, u8* model, u8* controls, u32 models = kDrumNarrowModels) {
    if (!words || !model || !controls) return false;
    u32 packed[kDrumStorageWords];
    for (u32 i = 0; i < kDrumStorageWords; ++i) {
        if (words[i] < 0 || (static_cast<u32>(words[i]) & 65535u)) return false;
        packed[i] = static_cast<u32>(words[i]) >> 16;
    }
    u32 values[kDrumControlCount] = {}, m = 0;
    for (u32 k = 0; k < 60; ++k) {
        const u32 bit = (packed[k / 15] >> (k % 15)) & 1u;
        const u32 slot = k / 7;
        if (k >= 56) m |= bit << (k - 56);
        else if (!drum_coarse(models, slot)) values[slot] |= bit << (k % 7);
        else if (k % 7) values[slot] |= bit << (k % 7 - 1);
        else m |= bit << (slot == 6 ? 4 : 5);
    }
    *model = static_cast<u8>(m);
    for (u32 i = 0; i < kDrumControlCount; ++i)
        controls[i] = drum_coarse(models, i) ? drum_coarse_value(values[i]) : static_cast<u8>(values[i]);
    return true;
}
}}
