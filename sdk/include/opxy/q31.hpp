#pragma once
#include "types.hpp"

namespace opxy { namespace q31 {
// SDK-owned platform boundary. Engine source always calls the same operations.
const i32 maximum = 2147483647;
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
    i32 result;
    __asm__("%0 = %1 + %2 (S);" : "=d"(result) : "d"(a), "d"(b) : "ASTAT");
    return result;
#else
    const i64 sum = static_cast<i64>(a) + b;
    return sum > maximum ? maximum : sum < -maximum - 1 ? -maximum - 1 : static_cast<i32>(sum);
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

// A control's value between evenly spaced anchors (SDK 1.7), as the
// definition's "anchors" mapping: `count` values (2..256) at equal steps of
// travel, the first at 0 and the last at full scale. Five anchors fall exactly
// where the stock captions read 00, 25, 50, 75 and 99. Like the other controls
// it reads the control in Q15. Neighbouring values must differ by less than 2^31.
inline i32 anchored(i32 control, const i32* values, u32 count) {
    const u32 q15 = control < 0 ? 0 : static_cast<u32>(control) >> 16;
    if (q15 == 32767) return values[count - 1];
    const u32 position = q15 * (count - 1), index = position >> 15;
    return values[index] + multiply(values[index + 1] - values[index], static_cast<i32>((position & 32767) << 16));
}


}} // namespace opxy::q31
