#pragma once
#include "types.hpp"

namespace opxy { namespace q31 {
// SDK-owned platform boundary. Engine source always calls the same operations.
const i32 maximum = 2147483647;
// Full signed 32 x 32 product, without a shift or saturation.
inline i64 wide(i32 a, i32 b) {
#if defined(__bfin__)
    register i64 result __asm__("R0");
    __asm__("(R1:0) = %1 * %2 (IS,NS);" : "=d"(result) : "d"(a), "d"(b) : "ASTAT", "CC");
    return result;
#else
    return static_cast<i64>(a) * b;
#endif
}
inline i32 multiply(i32 a, i32 b) {
#if defined(__bfin__)
    // Blackfin's saturating, truncating (T) multiply: the same result as the
    // portable path below in one instruction.
    i32 result;
    __asm__("%0 = %1 * %2 (T);" : "=d"(result) : "d"(a), "d"(b) : "ASTAT", "CC");
    return result;
#else
    // Defined C++11 semantics, including negative products and MIN * MIN.
    // Avoid implementation-defined signed right shift and signed overflow.
    const i64 product = static_cast<i64>(a) * b;
    const i64 scale = static_cast<i64>(1) << 31;
    i64 result = product / scale;
    if (product < 0 && product % scale != 0) --result;
    return result > maximum ? maximum : static_cast<i32>(result);
#endif
}

// Saturating addition (SDK 1.7): the sum, held within the Q31 range.
inline i32 add(i32 a, i32 b) {
#if defined(__bfin__)
    // Blackfin's saturating add: the same result as the portable path below in one instruction.
    // Unlike the other helpers it declares only ASTAT changed, as it has since SDK 1.7, and keeps
    // that so machines built with it keep their code. The others declare CC as well, alike.
    i32 result;
    __asm__("%0 = %1 + %2 (S);" : "=d"(result) : "d"(a), "d"(b) : "ASTAT");
    return result;
#else
    const i64 sum = static_cast<i64>(a) + b;
    return sum > maximum ? maximum : sum < -maximum - 1 ? -maximum - 1 : static_cast<i32>(sum);
#endif
}

// Saturating left shift (SDK 1.16): value * 2^count, held within the Q31 range;
// count 0..31.
inline i32 shift_left(i32 value, u32 count) {
#if defined(__bfin__)
    // Blackfin's saturating shift: the same result as the portable path below in one instruction.
    i32 result;
    __asm__("%0 = ASHIFT %1 BY %2.L (S);" : "=d"(result) : "d"(value), "d"(count) : "ASTAT", "CC");
    return result;
#else
    const i64 shifted = static_cast<i64>(value) * (static_cast<i64>(1) << count);
    return shifted > maximum ? maximum : shifted < -maximum - 1 ? -maximum - 1 : static_cast<i32>(shifted);
#endif
}

inline i32 half(i32 value) {
#if defined(__bfin__)
    i32 result;
    __asm__("%0 = %1 >>> 1;" : "=d"(result) : "d"(value) : "ASTAT", "CC");
    return result;
#else
    return value / 2 - (value < 0 && value % 2 != 0);
#endif
}

// Like the helpers above, these update the arithmetic flags on the device but
// never read, write or save ASTAT.
const i32 minimum = -maximum - 1;

// A 64-bit value held within the Q31 range.
inline i32 saturate(i64 value) {
    return value > maximum ? maximum : value < minimum ? minimum : static_cast<i32>(value);
}

// Saturating subtraction: a - b, held within the Q31 range.
inline i32 subtract(i32 a, i32 b) {
#if defined(__bfin__)
    i32 result;
    __asm__("%0 = %1 - %2 (S);" : "=d"(result) : "d"(a), "d"(b) : "ASTAT", "CC");
    return result;
#else
    return saturate(static_cast<i64>(a) - b);
#endif
}

// Arithmetic right shift: value / 2^count rounded toward minus infinity, as a
// sign-filling shift does; count 0..31. Other counts are undefined and differ
// between host and device.
inline i32 shift_right(i32 value, u32 count) {
#if defined(__bfin__)
    i32 result;
    __asm__("%0 >>>= %2;" : "=d"(result) : "0"(value), "d"(count) : "ASTAT", "CC");
    return result;
#else
    const i64 divisor = static_cast<i64>(1) << count;
    return static_cast<i32>(value >= 0 ? value / divisor : -1 - (-1 - static_cast<i64>(value)) / divisor);
#endif
}

// Arithmetic shift by a signed count (SDK 1.29), the processor's ASHIFT: for a
// count of -31..-1, shift_right by -count; for 0..31, value * 2^count wrapped
// to 32 bits, as an unsigned shift wraps (shift_left is the saturating one). Other counts are
// undefined and differ between host and device. One instruction on the device
// whichever way the count goes; like the helpers above, it never reads, writes
// or saves ASTAT.
__attribute__((always_inline)) inline i32 shift(i32 value, i32 count) {
#if defined(__bfin__)
    i32 result;
    __asm__("%0 = ASHIFT %1 BY %2.L;" : "=d"(result) : "d"(value), "d"(count) : "ASTAT", "CC");
    return result;
#else
    if (count < 0) return shift_right(value, static_cast<u32>(-count));
    const u32 bits = static_cast<u32>(value) << count;
    return bits > static_cast<u32>(maximum) ? -static_cast<i32>(~bits) - 1 : static_cast<i32>(bits);
#endif
}

}} // namespace opxy::q31
