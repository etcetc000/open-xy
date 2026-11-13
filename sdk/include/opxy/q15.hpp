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

// Sum squared distances between two word-aligned windows of an even sample
// count. Each difference must fit in i16 and the total must fit in u32.
template <u32 Count>
inline u32 squared_distance(const i16* left, const i16* right) {
    static_assert(Count >= 4 && Count % 2 == 0, "A distance window needs at least two sample pairs");
#if defined(__bfin__)
    u32 result, sample;
    __asm__ volatile(
        "I0 = %[right];\n\t"
        "L0 = 0;\n\t"
        "A1 = A0 = 0;\n\t"
        "%[result] = [%[left]++];\n\t"
        "%[sample] = [I0++];\n\t"
        "LSETUP (1f, 2f) LC0 = %[loops];\n\t"
        "1: %[result] = %[result] -|- %[sample];\n\t"
        "2: A1 += %[result].H * %[result].H, A0 += %[result].L * %[result].L (IS) || %[result] = [%[left]++] || %[sample] = [I0++];\n\t"
        "%[result] = %[result] -|- %[sample];\n\t"
        "A1 += %[result].H * %[result].H, A0 += %[result].L * %[result].L (IS);\n\t"
        "A0 += A1;\n\t"
        "%[result] = A0.W;"
        : [result] "=&d" (result), [sample] "=&d" (sample), [left] "+&a" (left)
        : [right] "a" (right), [loops] "a" (Count / 2 - 1)
        : "I0", "L0", "A0", "A1", "LC0", "LT0", "LB0", "ASTAT", "CC", "memory");
    return result;
#else
    u32 result = 0;
    for (u32 i = 0; i < Count; ++i) {
        const i32 delta = static_cast<i32>(left[i]) - right[i];
        result += static_cast<u32>(delta * delta);
    }
    return result;
#endif
}
}} // namespace opxy::q15
