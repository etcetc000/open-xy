// A send effect's bus shift (definition resources.busShift, SDK 1.16). An
// effect written for audio near Q31 full scale declares how many bits the send
// bus runs below it; its generated adapter (fx_object.cpp.in) raises both
// channels' 64 words by that many bits, saturating, before the effect's
// process, and lowers its result by as many, rounded to nearest, after it.
// Effects without busShift include none of this.
//
// The send hook (send_fx.cpp) tests the raw bus against one idle floor for
// every effect, whatever its shift. In a build with the idle capability every
// SDK effect's init records its vtable and its hold (how many quiet blocks its tail needs,
// adapters.send_hold) per send here; send_fx.cpp, which defines the record and
// is linked only with idle, uses them only while that send's object still has
// that vtable. Stock effects record nothing and get stock_hold.
#pragma once
#include <opxy/q31.hpp>
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace send_bus {
const u32 first_send = 14;
const u32 sends = 2;
const u32 frames = 64;
const u32 largest = 12;
// The hold for a send whose effect recorded none (stock's delay, reverb and
// chorus): a 512 KiB scratch request is 262144 frames as 16-bit mono, 4096
// blocks, plus 64 of margin. An effect's hold of `endless` never idles its send.
const u32 stock_hold = 4096 + 64;
const u32 endless = 0xffffffffu;
struct Slot {
    const void* volatile vtable;
    volatile u32 hold;
};
}}}

extern "C" opxy::platform::send_bus::Slot sdk_send_bus[opxy::platform::send_bus::sends] __asm__("sdk_send_bus");

namespace opxy { namespace platform { namespace send_bus {
// The effect's init: the send (slot 14 or 15) now holds an object of this vtable.
// The hook reads the record only while the vtable matches, and the vtable is
// cleared first, so it never pairs the new vtable with the old hold.
inline void declare(u32 slot, const void* vtable, u32 hold) {
    const u32 send = slot - first_send;
    if (send >= sends) return;
    sdk_send_bus[send].vtable = nullptr;
    sdk_send_bus[send].hold = hold;
    sdk_send_bus[send].vtable = vtable;
}

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
