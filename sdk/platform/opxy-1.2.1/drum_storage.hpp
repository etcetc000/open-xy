// A drum key's model and eight controls in stock Drumline's saved pad fields.
// Stock presets write each of the four parameter words >> 16 and restore it
// << 16, so nonnegative Q15 integers preserve 60 bits. A kit of up to 16
// models stores eight 7-bit controls (56 bits) and a 4-bit model; a larger kit
// (up to 64) stores every control at 7 bits too, in a prefix code (DrumCodec).
// The pad's model byte (Drumline's
// instrument type in its low nibble, its choke group in the high one) marks
// the layout with type 7, which stock's editor skips and its default kit never
// uses; the choke group stays stock's.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
const u32 kDrumControlCount = 8;
const u32 kDrumStorageWords = 4;
const u32 kDrumModelLimit = 64;   // models a kit can store
const u32 kDrumNarrowModels = 16; // up to this many, the 4-bit model layout
const u32 kDrumFullModels = 15;   // a larger kit's models with eight controls
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

// Bit k of the 60 is bit k % 15 of word k / 15, each word shifted up 16.
inline bool drum_words(const u32* packed, i32* words) {
    for (u32 i = 0; i < kDrumStorageWords; ++i) words[i] = static_cast<i32>(packed[i] << 16);
    return true;
}
inline bool drum_bits(const i32* words, u32* packed) {
    for (u32 i = 0; i < kDrumStorageWords; ++i) {
        if (words[i] < 0 || (static_cast<u32>(words[i]) & 65535u)) return false;
        packed[i] = static_cast<u32>(words[i]) >> 16;
    }
    return true;
}
inline void drum_put(u32* packed, u32 at, u32 bits, u32 value) {
    for (u32 b = 0; b < bits; ++b) packed[(at + b) / 15] |= ((value >> b) & 1u) << ((at + b) % 15);
}
// Runs on every block of a marked key, so no division: the Blackfin's is a software call.
inline u32 drum_get(const u32* packed, u32 at, u32 bits) {
    u32 word = 0;
    while (at >= 15) { at -= 15; ++word; }
    u32 value = packed[word] >> at;
    if (at + bits > 15) value |= packed[word + 1] << (15 - at);
    return value & ((1u << bits) - 1u);
}

// A kit of up to 16 models: the controls in bits 0..55, 7 each, and the model
// in 56..59.
inline bool drum_pack(u32 model, const u8* controls, i32* words) {
    if (!controls || !words || model >= kDrumNarrowModels) return false;
    for (u32 i = 0; i < kDrumControlCount; ++i)
        if (controls[i] > 127) return false;
    u32 packed[kDrumStorageWords] = {};
    for (u32 i = 0; i < kDrumControlCount; ++i) drum_put(packed, 7 * i, 7, controls[i]);
    drum_put(packed, 56, 4, model);
    return drum_words(packed, words);
}

// The caller checks the mark first. Failure leaves the outputs untouched, so
// the caller keeps its defaults.
inline bool drum_unpack(const i32* words, u8* model, u8* controls) {
    u32 packed[kDrumStorageWords];
    if (!words || !model || !controls || !drum_bits(words, packed)) return false;
    *model = static_cast<u8>(drum_get(packed, 56, 4));
    for (u32 i = 0; i < kDrumControlCount; ++i) controls[i] = static_cast<u8>(drum_get(packed, 7 * i, 7));
    return true;
}

// A kit of more than 16 models keeps every control at 7 bits in the same 60
// bits, by a prefix code. Its full models, those using all eight controls
// (parameter_count 8), number at most 15: a key on one stores its rank among
// them in bits 56..59 and its eight controls in 0..55. A key on any other
// model stores 15 there, the model in bits 49..54 and its first seven
// controls in 0..48; its eighth is unused (the model's count is at most
// seven), so it reads the caller's value, the key's default. Decoding is
// shifts and masks only. Keys are read in their kit's current layout: a kit
// that grows past 16 models does not carry over the keys it saved before.
struct DrumCodec {
    u32 models;                 // the kit's model count
    u32 fulls;                  // its full models, in model order (more than 15: it does not fit)
    u8 full[kDrumFullModels];
    u8 rank[kDrumModelLimit];   // a full model's place among them, else kDrumFullModels
    bool wide() const { return models > kDrumNarrowModels; }
    bool fits() const { return models >= 1 && models <= kDrumModelLimit && (!wide() || fulls <= kDrumFullModels); }
};
inline DrumCodec drum_codec(u32 models, u32 (*count)(u32)) {
    DrumCodec codec = {};
    codec.models = models;
    for (u32 m = 0; m < kDrumModelLimit; ++m) codec.rank[m] = kDrumFullModels;
    for (u32 m = 0; models <= kDrumModelLimit && m < models; ++m) {
        if (count(m) < kDrumControlCount) continue;
        if (codec.fulls < kDrumFullModels) {
            codec.full[codec.fulls] = static_cast<u8>(m);
            codec.rank[m] = static_cast<u8>(codec.fulls);
        }
        ++codec.fulls;
    }
    return codec;
}

inline bool drum_pack(u32 model, const u8* controls, i32* words, const DrumCodec& codec) {
    if (!codec.wide()) return drum_pack(model, controls, words);
    if (!controls || !words || !codec.fits() || model >= codec.models) return false;
    const u32 rank = codec.rank[model], stored = rank < kDrumFullModels ? 8 : 7;
    for (u32 i = 0; i < stored; ++i)
        if (controls[i] > 127) return false;
    u32 packed[kDrumStorageWords] = {};
    for (u32 i = 0; i < stored; ++i) drum_put(packed, 7 * i, 7, controls[i]);
    if (rank < kDrumFullModels) {
        drum_put(packed, 56, 4, rank);
    } else {
        drum_put(packed, 49, 6, model);
        drum_put(packed, 56, 4, kDrumFullModels);
    }
    return drum_words(packed, words);
}

inline bool drum_unpack(const i32* words, u8* model, u8* controls, const DrumCodec& codec) {
    if (!codec.wide()) return drum_unpack(words, model, controls);
    u32 packed[kDrumStorageWords];
    if (!words || !model || !controls || !codec.fits() || !drum_bits(words, packed)) return false;
    const u32 rank = drum_get(packed, 56, 4);
    if (rank < kDrumFullModels && rank >= codec.fulls) return false;
    const u32 m = rank < kDrumFullModels ? codec.full[rank] : drum_get(packed, 49, 6);
    const u32 stored = rank < kDrumFullModels ? 8 : 7;
    *model = static_cast<u8>(m);
    for (u32 i = 0; i < stored; ++i) controls[i] = static_cast<u8>(drum_get(packed, 7 * i, 7));
    return true;
}
}}
