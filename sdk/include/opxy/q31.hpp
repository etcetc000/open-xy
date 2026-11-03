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

// The full saturating signed fractional product, in Q1.63. Its high word is
// multiply(a, b); low retains the bits needed for caller-defined rounding.
struct Product { i32 high; u32 low; };

inline Product multiply_full(i32 a, i32 b) {
#if defined(__bfin__)
    // The pinned assembler requires parentheses around this register pair.
    // Neither accumulator nor the global rounding mode is changed.
    register u32 low __asm__("R0");
    register i32 high __asm__("R1");
    __asm__("(R1:0) = %2 * %3;"
            : "=q0"(low), "=q1"(high) : "d"(a), "d"(b) : "ASTAT", "CC");
    return {high, low};
#else
    const i64 product = static_cast<i64>(a) * b;
    if (product == (static_cast<i64>(1) << 62)) return {maximum, 0xffffffffu};
    const unsigned long long bits = static_cast<unsigned long long>(product) << 1;
    const u32 upper = static_cast<u32>(bits >> 32);
    // Convert the signed high word without an out-of-range unsigned cast.
    const i32 high = upper <= static_cast<u32>(maximum)
        ? static_cast<i32>(upper) : -1 - static_cast<i32>(~upper);
    return {high, static_cast<u32>(bits)};
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


}} // namespace opxy::q31
