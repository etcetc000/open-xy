#pragma once
#include "../types.hpp"

namespace opxy { namespace kernels {
// SDK-owned kernel: two wavefolders, one fed by an interpolated sine and one by
// a triangle, each oversampled twice, then crossfaded. Including this header
// links the device kernel (sdk/platform/blackfin/kernels/fold_pair.s) into the
// including machine only. Elsewhere, and for arguments outside the kernel's
// range, the portable reference below runs; both give the same samples.
//
// Tables hold 256 words: a signed 16-bit value in the low half and the signed
// slope to the next entry in the high half. `sine` is read at phase >> 24 with
// a 16-bit fraction; the fold tables at a 16-bit index >> 8 with an 8-bit one.
struct FoldPair {
    u32 sine_phase, triangle_phase;          // advanced by the call
    u32 sine_increment, triangle_increment;  // advanced by the call, before each sample
    u32 sine_step, triangle_step;            // added to each increment per sample
    i32 xfade_step;                          // added to the 15-bit crossfade per sample
    i32 sine_start, sine_delta;              // fold gain control, interpolated
    i32 triangle_start, triangle_delta;
    i32 balance_start, balance_delta;        // sine/triangle mix, interpolated
    const u32* sine;
    const u32* sine_fold;
    const u32* triangle_fold;
};

namespace fold_pair_detail {
inline i16 pair824(const u32* table, u32 phase) {
    const u32 pair = table[phase >> 24];
    return static_cast<i16>(static_cast<i32>(static_cast<i16>(pair)) +
                            ((static_cast<i32>(pair) >> 16) * static_cast<i32>((phase >> 8) & 0xffff) >> 16));
}
inline i16 pair88(const u32* table, u16 index) {
    const u32 pair = table[index >> 8];
    return static_cast<i16>(static_cast<i32>(static_cast<i16>(pair)) +
                            ((static_cast<i32>(pair) >> 16) * static_cast<i32>(index & 0xff) >> 8));
}
inline i16 gain(i32 parameter) { return static_cast<i16>(2048 + parameter - ((parameter + 15) >> 4)); }
}

// The definition: per sample, both increments step, the crossfade steps, and
// for each control value v = start + (delta * xfade >> 15). Each oscillator
// then takes two half-increment steps, folding (value * gain >> 15) + 32768,
// and the halved folds of both steps are summed. The output is
// a + ((b - a) * balance - a) / 65536 (unsigned 32-bit wrap, then arithmetic
// shift) with a the sine sum, b the triangle sum and balance = v << 1.
inline void fold_pair_reference(FoldPair& s, i16* out, unsigned size) {
    using namespace fold_pair_detail;
    u32 sine_phase = s.sine_phase, triangle_phase = s.triangle_phase;
    u32 sine_increment = s.sine_increment, triangle_increment = s.triangle_increment;
    i32 xfade = 0;
    while (size--) {
        xfade += s.xfade_step;
        sine_increment += s.sine_step;
        triangle_increment += s.triangle_step;
        const i16 sine_gain = gain(s.sine_start + ((s.sine_delta * xfade) >> 15));
        const i16 triangle_gain = gain(s.triangle_start + ((s.triangle_delta * xfade) >> 15));
        i32 sine_sum = 0, triangle_sum = 0;
        for (unsigned step = 0; step < 2; ++step) {
            sine_phase += sine_increment >> 1;
            i16 sine = pair824(s.sine, sine_phase);
            sine = static_cast<i16>((sine * sine_gain) >> 15);
            sine_sum += pair88(s.sine_fold, static_cast<u16>(sine + 32768)) >> 1;
            triangle_phase += triangle_increment >> 1;
            i16 triangle = static_cast<i16>(((triangle_phase >> 15) & ~1u) ^ (0u - (triangle_phase >> 31)));
            triangle = static_cast<i16>(triangle + 32768);
            triangle = static_cast<i16>((triangle * triangle_gain) >> 15);
            triangle_sum += pair88(s.triangle_fold, static_cast<u16>(triangle + 32768)) >> 1;
        }
        const i16 a = static_cast<i16>(sine_sum), b = static_cast<i16>(triangle_sum);
        const u16 balance = static_cast<u16>((s.balance_start + ((s.balance_delta * xfade) >> 15)) << 1);
        const u32 residual = static_cast<u32>(static_cast<i32>(b) - a) * balance - static_cast<u32>(a);
        *out++ = static_cast<i16>(a + (static_cast<i32>(residual) >> 16));
    }
    s.sine_phase = sine_phase; s.triangle_phase = triangle_phase;
    s.sine_increment = sine_increment; s.triangle_increment = triangle_increment;
}

// The device kernel's range: 1..24 samples, every interpolated control value
// in 0..32767 (both ends of each control in range) and a crossfade that never
// passes 32767. Calls outside it take the reference.
inline bool fold_pair_in_range(const FoldPair& s, unsigned size) {
    auto control = [](i32 start, i32 delta) {
        return static_cast<u32>(start) <= 32767u && static_cast<u32>(start + delta) <= 32767u;
    };
    return size - 1u < 24u && s.xfade_step >= 0 && s.xfade_step * static_cast<i32>(size) <= 32767 &&
           control(s.sine_start, s.sine_delta) && control(s.triangle_start, s.triangle_delta) &&
           control(s.balance_start, s.balance_delta);
}

#if defined(__bfin__)
extern "C" void opxy_fold_pair(FoldPair* s, i16* out, unsigned size);
inline void fold_pair(FoldPair& s, i16* out, unsigned size) {
    if (fold_pair_in_range(s, size)) opxy_fold_pair(&s, out, size);
    else fold_pair_reference(s, out, size);
}
#else
inline void fold_pair(FoldPair& s, i16* out, unsigned size) { fold_pair_reference(s, out, size); }
#endif
}} // namespace opxy::kernels
