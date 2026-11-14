// A send effect's bus shift (definition resources.busShift, SDK 1.16). An
// effect written for audio near Q31 full scale declares how many bits the send
// bus runs below it; its generated adapter (fx_object.cpp.in) raises both
// channels' 64 words by that many bits, saturating, before the effect's
// process, and lowers its result by as many, rounded to nearest, after it.
// Effects without busShift include none of this.
#pragma once
#include <opxy/q31.hpp>
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace send_bus {
const u32 frames = 64;
const u32 largest = 12;

// x << Shift, held within the Q31 range: one saturating shift on the device.
template <u32 Shift> inline i32 raise(i32 x) {
    static_assert(Shift >= 1 && Shift <= largest, "busShift is 1..12 here");
    return q31::shift_left(x, Shift);
}

// x / 2^Shift rounded to nearest, halves up. Shift first, then add the dropped
// bit: never an add before a shift, and no overflow, so no saturation either.
// GCC and Clang define >> of a negative value as arithmetic, which leaves the
// compiler free to use immediate shifts here (the device has no rounding shift).
template <u32 Shift> inline i32 lower(i32 x) {
    static_assert(Shift >= 1 && Shift <= largest, "busShift is 1..12 here");
    const i32 half = x >> (Shift - 1);
    return (half >> 1) + (half & 1);
}

template <u32 Shift> inline void raise(i32* left, i32* right) {
    for (u32 i = 0; i < frames; ++i) {
        left[i] = raise<Shift>(left[i]);
        right[i] = raise<Shift>(right[i]);
    }
}

template <u32 Shift> inline void lower(i32* left, i32* right) {
    for (u32 i = 0; i < frames; ++i) {
        left[i] = lower<Shift>(left[i]);
        right[i] = lower<Shift>(right[i]);
    }
}
}}}
