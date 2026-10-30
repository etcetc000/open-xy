// Eight 7-bit drum controls in stock Drumline's four saved parameter words.
// Stock presets write each word >> 16 and restore it << 16. Nonnegative
// Q15 integers therefore preserve 60 bits: 56 control bits and a format tag.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
const u32 kDrumControlCount = 8;
const u32 kDrumStorageWords = 4;
const u32 kDrumStorageTag = 10;

// Enum-bin midpoint using only 32-bit division. For at most 128 models the
// rounding error is below 256 Q31 units, far from either Q15 bin boundary.
inline i32 drum_model_q31(u32 model, u32 count) {
    return count && count <= 128 && model < count
        ? static_cast<i32>((0x40000000u / count) * (2 * model + 1)) : 0;
}

inline bool drum_pack(const u8* controls, i32* words) {
    if (!controls || !words) return false;
    for (u32 i = 0; i < kDrumControlCount; ++i)
        if (controls[i] > 127) return false;
    u32 packed[kDrumStorageWords] = {};
    for (u32 i = 0; i < kDrumControlCount; ++i) {
        for (u32 bit = 0; bit < 7; ++bit) {
            const u32 position = i * 7 + bit;
            packed[position / 15] |= ((u32(controls[i]) >> bit) & 1u) << (position % 15);
        }
    }
    packed[3] |= kDrumStorageTag << 11;
    for (u32 i = 0; i < kDrumStorageWords; ++i)
        words[i] = static_cast<i32>(packed[i] << 16);
    return true;
}

// Untagged stock/default regions are not interpreted as machine settings.
// Failure leaves the output untouched so the caller can retain its defaults.
inline bool drum_unpack(const i32* words, u8* controls) {
    if (!words || !controls) return false;
    u32 packed[kDrumStorageWords];
    for (u32 i = 0; i < kDrumStorageWords; ++i) {
        if (words[i] < 0 || (static_cast<u32>(words[i]) & 65535u)) return false;
        packed[i] = static_cast<u32>(words[i]) >> 16;
    }
    if ((packed[3] >> 11) != kDrumStorageTag) return false;
    for (u32 i = 0; i < kDrumControlCount; ++i) {
        u32 value = 0;
        for (u32 bit = 0; bit < 7; ++bit) {
            const u32 position = i * 7 + bit;
            value |= ((packed[position / 15] >> (position % 15)) & 1u) << bit;
        }
        controls[i] = static_cast<u8>(value);
    }
    return true;
}
}}
