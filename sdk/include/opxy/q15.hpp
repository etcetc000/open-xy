#pragma once
#include "types.hpp"

namespace opxy { namespace q15 {
// The product of two 16-bit values held in the low halves of words: a sine
// table entry times an amplitude's top half, say. GCC tends to widen such
// products into 32-bit multiplies, which cost more on the Blackfin; this is
// always the one 16 x 16 multiply.
inline i32 product(i32 a, i32 b) {
#if defined(__bfin__)
    i32 result;
    __asm__("%0 = %1.L * %2.L (IS);" : "=d"(result) : "d"(a), "d"(b));
    return result;
#else
    return static_cast<i32>(static_cast<i16>(a)) * static_cast<i16>(b);
#endif
}
// The top half of a word as a 16-bit value: what product() takes for an
// amplitude held in Q31.
inline i32 top(i32 word) { return static_cast<i32>(static_cast<u32>(word) >> 16); }
}} // namespace opxy::q15
