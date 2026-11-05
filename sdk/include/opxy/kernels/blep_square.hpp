#pragma once
#include "../divide.hpp"
#include "../types.hpp"

namespace opxy { namespace kernels {
// SDK-owned kernel: a band-limited square oscillator with quadratic BLEP
// corrections at each edge (the analog square of Mutable Instruments Braids),
// scaled and written to, or added to, a 16-bit buffer. Including this header
// links the device kernel (sdk/platform/blackfin/kernels/blep_square.s) into
// the including machine only. Elsewhere, and for states outside the kernel's
// range, the portable reference below runs; both give the same samples.
struct BlepSquare {
    u32 phase;           // advanced by the call
    u32 increment;       // advanced by the call, before each sample
    u32 increment_step;  // added to the increment per sample (wrapping)
    i32 next_sample;     // the next sample's pending value, carried between calls
    bool high;           // the square's state, carried between calls
};

namespace blep_square_detail {
// The edge position in 1/65536 of a sample, clamped: n / (increment >> 16).
inline u32 position(u32 n, u32 increment) {
    const u32 d = increment >> 16;
    if (d == 0) return 65535;
    const u32 t = n / d;
    return t > 65535 ? 65535 : t;
}
inline i32 this_blep(u32 t) { return static_cast<i32>(t * t >> 18); }
inline i32 next_blep(u32 t) { t = 65535 - t; return -static_cast<i32>(t * t >> 18); }
}

// Per sample: the increment steps and the phase advances by it; a crossing of
// the half cycle raises the square and a wrap lowers it, each correcting this
// sample and the next by the edge's sub-sample position. The sample written is
// (int16)((this - 16384) << 1) * gain >> 6, added to *out when `mix`.
inline void blep_square_reference(BlepSquare& s, i16* out, unsigned size, i32 gain, bool mix) {
    using namespace blep_square_detail;
    const u32 half = 0x80000000u;
    u32 phase = s.phase, increment = s.increment;
    bool high = s.high;
    i32 next = s.next_sample;
    while (size--) {
        increment += s.increment_step;
        i32 now = next;
        next = 0;
        phase += increment;
        const bool wrapped = phase < increment;
        if (!high && phase >= half) {
            const u32 t = position(phase - half, increment);
            now += this_blep(t); next += next_blep(t); high = true;
        }
        if (high && wrapped) {
            const u32 t = position(phase, increment);
            now -= this_blep(t); next -= next_blep(t); high = false;
            if (phase >= half) {
                const u32 r = position(phase - half, increment);
                now += this_blep(r); next += next_blep(r); high = true;
            }
        }
        next += phase < half ? 0 : 32767;
        const i16 value = static_cast<i16>(static_cast<i16>(static_cast<u32>(now - 16384) << 1) * gain >> 6);
        *out = mix ? static_cast<i16>(*out + value) : value;
        ++out;
    }
    s.phase = phase; s.increment = increment; s.high = high; s.next_sample = next;
}

// The device kernel's range: 1..256 samples, gain 0..63, a state whose square
// matches its phase's half, and increments that stay below half a cycle
// without wrapping (each lies between the first sample's and the last's).
// blep_square_last gives the last sample's increment, or false out of range.
inline bool blep_square_last(const BlepSquare& s, unsigned size, u32& last) {
    const u32 step = s.increment_step, first = s.increment + step, steps = size - 1;
    if (s.high != (s.phase >> 31) || first >= 0x80000000u) return false;
    const bool rising = step < 0x80000000u;
    const u32 magnitude = rising ? step : 0u - step;
    // steps < 256: the travel is below 2^31 exactly when this high part is below 2^15.
    if ((magnitude >> 16) * steps >= 0x8000u) return false;
    const u32 travel = magnitude * steps;
    if (rising ? travel >= 0x80000000u - first : travel > first) return false;
    last = rising ? first + travel : first - travel;
    return true;
}
inline bool blep_square_in_range(const BlepSquare& s, unsigned size, i32 gain) {
    u32 last;
    return size - 1u < 256u && static_cast<u32>(gain) <= 63u && blep_square_last(s, size, last);
}

// Renders and returns true when the call is in the kernel's range; otherwise
// renders nothing and returns false, so the caller can run its own loop.
// blep_squares renders 1..4 squares at once, the first written and the others
// added (all must be in range), as one device call.
#if defined(__bfin__)
// One oscillator of the device call: `reciprocal` is floor(2^31 / d) - 65536
// when every sample's increment >> 16 is the same d in 16385..32767 (edge
// positions by multiply), else 0 (edge positions by divide steps).
struct BlepSquareCall { BlepSquare* s; i32 k; u32 mix; u32 reciprocal; u32 divisor; u32 increment_step; i32 gain; };
extern "C" void opxy_blep_square(const BlepSquareCall* calls, i16* out, unsigned size, unsigned count);
inline bool blep_square_call(BlepSquare& s, unsigned size, i32 gain, bool mix, BlepSquareCall& call) {
    u32 last;
    if (!blep_square_last(s, size, last)) return false;
    const u32 d = (s.increment + s.increment_step) >> 16;
    call.s = &s; call.k = gain << 9; call.mix = mix; call.divisor = d;
    call.reciprocal = d > 16384u && d == last >> 16 ? divide::quotient16(0x80000000u - (d << 16), d) : 0u;
    call.increment_step = s.increment_step; call.gain = gain;
    return true;
}
inline bool blep_square(BlepSquare& s, i16* out, unsigned size, i32 gain, bool mix) {
    BlepSquareCall call;
    if (size - 1u >= 256u || static_cast<u32>(gain) > 63u || !blep_square_call(s, size, gain, mix, call)) return false;
    opxy_blep_square(&call, out, size, 1);
    return true;
}
inline bool blep_squares(BlepSquare* const* squares, unsigned count, i16* out, unsigned size, i32 gain) {
    BlepSquareCall calls[4];
    if (count - 1u >= 4u || size - 1u >= 256u || static_cast<u32>(gain) > 63u) return false;
    for (unsigned i = 0; i < count; ++i) if (!blep_square_call(*squares[i], size, gain, i > 0, calls[i])) return false;
    opxy_blep_square(calls, out, size, count);
    return true;
}
#else
inline bool blep_square(BlepSquare& s, i16* out, unsigned size, i32 gain, bool mix) {
    if (!blep_square_in_range(s, size, gain)) return false;
    blep_square_reference(s, out, size, gain, mix);
    return true;
}
inline bool blep_squares(BlepSquare* const* squares, unsigned count, i16* out, unsigned size, i32 gain) {
    if (count - 1u >= 4u) return false;
    for (unsigned i = 0; i < count; ++i) if (!blep_square_in_range(*squares[i], size, gain)) return false;
    for (unsigned i = 0; i < count; ++i) blep_square_reference(*squares[i], out, size, gain, i > 0);
    return true;
}
#endif
}} // namespace opxy::kernels
