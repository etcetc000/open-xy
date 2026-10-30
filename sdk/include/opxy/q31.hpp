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

inline i32 half(i32 value) {
#if defined(__bfin__)
    i32 result;
    __asm__("%0 = %1 >>> 1;" : "=d"(result) : "d"(value) : "ASTAT", "CC");
    return result;
#else
    return value / 2 - (value < 0 && value % 2 != 0);
#endif
}


}} // namespace opxy::q31
