#pragma once
#include "types.hpp"

namespace opxy { namespace packed16 {
// Place the low 16 bits of first in bits 0..15 and second in bits 16..31.
inline u32 pack(i32 first, i32 second) {
#if defined(__bfin__)
    u32 result;
    __asm__("%0 = PACK(%2.L, %1.L);" : "=d"(result) : "d"(first), "d"(second));
    return result;
#else
    return (static_cast<u32>(first) & 65535u) | (static_cast<u32>(second) << 16);
#endif
}

// Signed 16-bit dot product, saturated once after adding the two full products.
// There is no fractional shift, per-product rounding or accumulator state.
inline i32 dot(u32 a, u32 b) {
#if defined(__bfin__)
    register i32 real __asm__("R0");
    register i32 imaginary __asm__("R1");
    // The real component of A * conjugate(B) is a.low*b.low + a.high*b.high.
    // This register-only form leaves A0/A1 and the global rounding mode intact.
    __asm__("(R1:0) = cmul(%2,%3*) (IS);"
            : "=q0"(real), "=q1"(imaginary) : "d"(a), "d"(b) : "ASTAT", "CC");
    return real;
#else
    const u32 al = a & 65535u, ah = a >> 16;
    const u32 bl = b & 65535u, bh = b >> 16;
    const i32 x0 = al < 32768u ? static_cast<i32>(al) : static_cast<i32>(al) - 65536;
    const i32 x1 = ah < 32768u ? static_cast<i32>(ah) : static_cast<i32>(ah) - 65536;
    const i32 y0 = bl < 32768u ? static_cast<i32>(bl) : static_cast<i32>(bl) - 65536;
    const i32 y1 = bh < 32768u ? static_cast<i32>(bh) : static_cast<i32>(bh) - 65536;
    const i64 sum = static_cast<i64>(x0) * y0 + static_cast<i64>(x1) * y1;
    return sum > 2147483647 ? 2147483647
        : sum < -2147483647 - 1 ? -2147483647 - 1 : static_cast<i32>(sum);
#endif
}
}} // namespace opxy::packed16
