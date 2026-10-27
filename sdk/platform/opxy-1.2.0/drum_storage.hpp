// A drum key's model and eight 7-bit controls in stock Drumline's saved pad
// fields. Stock presets write each of the four parameter words >> 16 and
// restore it << 16, so nonnegative Q15 integers preserve 60 bits: the eight
// controls (56 bits) and the model (4 bits). The pad's model byte (Drumline's
// instrument type in its low nibble, its choke group in the high one) marks
// the layout with type 7, which stock's editor skips and its default kit never
// uses; the choke group stays stock's.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
const u32 kDrumControlCount = 8;
const u32 kDrumStorageWords = 4;
const u32 kDrumModelLimit = 16;   // models a kit can store
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
// over 66 bins (8d5aba1c). Type 7 is bins 42..47.
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

// Bit k of the 60: controls in bits 0..55, the model in 56..59.
inline bool drum_pack(u32 model, const u8* controls, i32* words) {
    if (!controls || !words || model >= kDrumModelLimit) return false;
    for (u32 i = 0; i < kDrumControlCount; ++i)
        if (controls[i] > 127) return false;
    u32 packed[kDrumStorageWords] = {};
    for (u32 k = 0; k < 60; ++k) {
        const u32 bit = k < 56 ? (u32(controls[k / 7]) >> (k % 7)) & 1u : (model >> (k - 56)) & 1u;
        packed[k / 15] |= bit << (k % 15);
    }
    for (u32 i = 0; i < kDrumStorageWords; ++i)
        words[i] = static_cast<i32>(packed[i] << 16);
    return true;
}

// The caller checks the mark first. Failure leaves the outputs untouched, so
// the caller keeps its defaults.
inline bool drum_unpack(const i32* words, u8* model, u8* controls) {
    if (!words || !model || !controls) return false;
    u32 packed[kDrumStorageWords];
    for (u32 i = 0; i < kDrumStorageWords; ++i) {
        if (words[i] < 0 || (static_cast<u32>(words[i]) & 65535u)) return false;
        packed[i] = static_cast<u32>(words[i]) >> 16;
    }
    u32 values[kDrumControlCount] = {}, m = 0;
    for (u32 k = 0; k < 60; ++k) {
        const u32 bit = (packed[k / 15] >> (k % 15)) & 1u;
        if (k < 56) values[k / 7] |= bit << (k % 7);
        else m |= bit << (k - 56);
    }
    *model = static_cast<u8>(m);
    for (u32 i = 0; i < kDrumControlCount; ++i) controls[i] = static_cast<u8>(values[i]);
    return true;
}
}}
