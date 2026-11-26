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

// Saturating 1.15 arithmetic: one or two instructions each on the device, and
// the same results on the host. The device forms update the arithmetic flags,
// as any add or multiply does, but never read, write or save ASTAT, so none of
// them changes the rounding mode (see multiply_nearest).
const i16 maximum = 32767;
const i16 minimum = -32767 - 1;

// A word held within the 1.15 range.
inline i16 saturate(i32 value) {
#if defined(__bfin__)
    // A saturating shift into the top half and back: the shift does the clamp.
    i32 result;
    __asm__("%0 = %1 << 16 (S);" : "=d"(result) : "d"(value) : "ASTAT", "CC");
    return static_cast<i16>(result >> 16);
#else
    return value > maximum ? maximum : value < minimum ? minimum : static_cast<i16>(value);
#endif
}

inline i16 add(i16 a, i16 b) {
#if defined(__bfin__)
    i16 result;
    __asm__("%0.L = %1.L + %2.L (S);" : "=d"(result) : "d"(a), "d"(b) : "ASTAT", "CC");
    return result;
#else
    return saturate(static_cast<i32>(a) + b);
#endif
}

inline i16 subtract(i16 a, i16 b) {
#if defined(__bfin__)
    i16 result;
    __asm__("%0.L = %1.L - %2.L (S);" : "=d"(result) : "d"(a), "d"(b) : "ASTAT", "CC");
    return result;
#else
    return saturate(static_cast<i32>(a) - b);
#endif
}

// The value shifted left past its redundant sign bits, so its magnitude fills
// the word: 1 becomes 16384, -1 becomes -32768 and 0 stays 0. Any 1.15 value.
inline i16 normalize(i16 value) {
#if defined(__bfin__)
    i16 result, count;
    __asm__("%1.L = SIGNBITS %2.L;\n\t%0.L = ASHIFT %2.L BY %1.L (S);"
            : "=d"(result), "=&d"(count) : "d"(value) : "ASTAT", "CC");
    return result;
#else
    if (value == 0) return 0;
    // Double until the bit below the sign differs from it.
    u32 bits = static_cast<u16>(value < 0 ? ~value : value);
    i32 shifted = value;
    while (bits < 0x4000u && shifted != minimum) { bits <<= 1; shifted *= 2; }
    return static_cast<i16>(shifted);
#endif
}

// The 1.15 product a * b, truncated toward minus infinity; -1 * -1 saturates
// to maximum. The 1.15 counterpart of q31::multiply.
inline i16 multiply(i16 a, i16 b) {
#if defined(__bfin__)
    i16 result;
    __asm__("%0.L = %1.L * %2.L (T);" : "=d"(result) : "d"(a), "d"(b) : "ASTAT", "CC");
    return result;
#else
    const i32 product = static_cast<i32>(a) * b;
    // Floor division by 2^15, without an implementation-defined signed shift.
    return saturate(product >= 0 ? product / 32768 : -1 - (-1 - product) / 32768);
#endif
}

// The 1.15 product a * b rounded to the nearest step; -1 * -1 saturates to
// maximum. On the device it is the processor's rounding multiply in whatever
// rounding mode is current; it never reads or sets the mode. An exact tie
// rounds by that mode (docs/api.md); the host takes the even neighbour. Do not
// depend on which way a tie falls.
inline i16 multiply_nearest(i16 a, i16 b) {
#if defined(__bfin__)
    i16 result;
    __asm__("%0.L = %1.L * %2.L;" : "=d"(result) : "d"(a), "d"(b) : "ASTAT", "CC");
    return result;
#else
    const i32 product = static_cast<i32>(a) * b;
    i32 quotient = product >= 0 ? product / 32768 : -1 - (-1 - product) / 32768;
    const u32 rest = static_cast<u32>(product) & 32767u;
    if (rest > 16384u || (rest == 16384u && (quotient & 1))) ++quotient;
    return saturate(quotient);
#endif
}

// SDK 1.28: more of the processor's own 16-bit operations, one instruction each
// on the device and the same results on the host, so code that wants exactly
// that instruction calls a helper rather than writing assembly. Like the
// helpers above, each one that is an instruction declares the arithmetic flags
// changed and never reads, writes or saves ASTAT. They are always_inline so that
// even an unoptimized build never turns one into a call.

// Half of a 1.15 value rounded toward minus infinity, as an arithmetic shift
// right by one does: the 1.15 counterpart of q31::half.
__attribute__((always_inline)) inline i16 half(i16 value) {
#if defined(__bfin__)
    i16 result;
    __asm__("%0.L = %1.L >>> 1;" : "=d"(result) : "d"(value) : "ASTAT", "CC");
    return result;
#else
    return static_cast<i16>(value / 2 - (value < 0 && value % 2 != 0));
#endif
}

// a - b wrapped to 16 bits rather than saturated: 32767 - -1 is -32768. The
// processor's plain 16-bit subtract; subtract is the saturating one.
__attribute__((always_inline)) inline i16 subtract_wrapped(i16 a, i16 b) {
#if defined(__bfin__)
    i16 result;
    __asm__("%0.L = %1.L - %2.L;" : "=d"(result) : "d"(a), "d"(b) : "ASTAT", "CC");
    return result;
#else
    const i32 bits = (static_cast<i32>(a) - b) & 0xffff;
    return static_cast<i16>(bits > maximum ? bits - 0x10000 : bits);
#endif
}

// A pair: two 1.15 values in one word, low in bits 0..15 and high in bits
// 16..31, such as a stereo frame. low and high read the halves; pack makes a
// pair; multiply_pair and add_pair work on both halves in one instruction, each
// half exactly as multiply or add.
__attribute__((always_inline)) inline i16 low(u32 pair) {
    return static_cast<i16>(static_cast<u16>(pair));
}
__attribute__((always_inline)) inline i16 high(u32 pair) {
    return static_cast<i16>(static_cast<u16>(pair >> 16));
}

// The pair of two 1.15 values: first in the low half, second in the high half.
__attribute__((always_inline)) inline u32 pack(i16 first, i16 second) {
#if defined(__bfin__)
    u32 result;
    __asm__("%0 = PACK (%1.L, %2.L);" : "=d"(result) : "d"(second), "d"(first) : "ASTAT", "CC");
    return result;
#else
    return static_cast<u16>(first) | static_cast<u32>(static_cast<u16>(second)) << 16;
#endif
}

// Each half of a pair times b, as multiply: truncated, -1 * -1 saturating.
__attribute__((always_inline)) inline u32 multiply_pair(u32 pair, i16 b) {
#if defined(__bfin__)
    u32 result;
    __asm__("%0.L = %1.L * %2.L, %0.H = %1.H * %2.L (T);" : "=d"(result) : "d"(pair), "d"(b) : "ASTAT", "CC");
    return result;
#else
    return pack(multiply(low(pair), b), multiply(high(pair), b));
#endif
}

// The halves of two pairs added, each saturating as add does.
__attribute__((always_inline)) inline u32 add_pair(u32 a, u32 b) {
#if defined(__bfin__)
    u32 result;
    __asm__("%0 = %1 +|+ %2 (S);" : "=d"(result) : "d"(a), "d"(b) : "ASTAT", "CC");
    return result;
#else
    return pack(add(low(a), low(b)), add(high(a), high(b)));
#endif
}
}} // namespace opxy::q15
