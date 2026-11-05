#pragma once
#include "../q31.hpp"

namespace opxy { namespace kernels {
// SDK-owned kernel: four phase streams read through two interpolated waves,
// each crossfaded from the first wave to the second, then averaged. Including
// this header links the device kernel (sdk/platform/blackfin/kernels/wave_quad.s)
// into the including machine only. Elsewhere, and for arguments outside the
// kernel's range, the portable reference below runs; both give the same samples.
//
// A wave holds 128 words: a signed 16-bit value in the low half and the signed
// slope in the high half. A 31-bit position p reads entry p >> 24 with the
// 24-bit fraction f = p & 0xffffff as value + (slope * f >> 16), kept to 16 bits.
struct WaveQuad {
    const u32* a;        // the wave at balance 0
    const u32* b;        // the wave approached as balance grows
    u32 phase[4];        // advanced by the call, each before its sample
    u32 increment[4];
    u32 balance;         // 0..65535: weight of b in 1/65536
};

namespace wave_quad_detail {
// floor(a * b / 2^31): no operand pair here reaches the multiply's saturation.
inline i32 fraction_product(i32 a, i32 b) { return q31::multiply(a, b); }
inline i16 read(const u32* wave, u32 position) {
    const u32 pair = wave[position >> 24];
    const i32 value = static_cast<i16>(pair), slope = static_cast<i32>(pair) >> 16;
    return static_cast<i16>(value + fraction_product(slope * 32768, static_cast<i32>(position & 0xffffff)));
}
}

// The definition: per sample, each stream adds its increment to its phase and
// reads position phase >> 1 from both waves as l and r; its value is
// l + floor((r - l) * balance / 65536). The sample is the four values' sum >> 2.
inline void wave_quad_reference(WaveQuad& s, i16* out, unsigned size) {
    using namespace wave_quad_detail;
    while (size--) {
        i32 sum = 0;
        for (unsigned k = 0; k < 4; ++k) {
            s.phase[k] += s.increment[k];
            const i32 l = read(s.a, s.phase[k] >> 1), r = read(s.b, s.phase[k] >> 1);
            sum += static_cast<i16>(l + fraction_product((r - l) * 32768, static_cast<i32>(s.balance)));
        }
        *out++ = static_cast<i16>(sum >> 2);
    }
}

// The device kernel takes 1..65535 for the balance; 0 (only a) and wider
// values take the reference.
inline bool wave_quad_in_range(const WaveQuad& s) { return s.balance - 1u < 65535u; }

#if defined(__bfin__)
extern "C" void opxy_wave_quad(WaveQuad* s, i16* out, unsigned size);
inline void wave_quad(WaveQuad& s, i16* out, unsigned size) {
    if (wave_quad_in_range(s)) opxy_wave_quad(&s, out, size);
    else wave_quad_reference(s, out, size);
}
#else
inline void wave_quad(WaveQuad& s, i16* out, unsigned size) { wave_quad_reference(s, out, size); }
#endif
}} // namespace opxy::kernels
