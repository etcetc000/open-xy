#pragma once
#include "../types.hpp"

namespace opxy { namespace kernels {
// SDK-owned kernel: a band-limited square hard-synced to a master square, both
// at half duty, crossfaded with the master. Including this header links the
// device kernel (sdk/platform/blackfin/kernels/square_sync.s) into the
// including machine only. Elsewhere, and for increments outside the kernel's
// range, the portable reference below runs; both give the same samples.
//
// Each oscillator is a phase that wraps at 2^32 and goes high at 2^31. Its
// edges are smoothed by a two-sample polynomial step (this sample and the
// next), placed at the edge's sub-sample time; the master reports each wrap's
// time to the slave, which restarts there.
struct SquareSyncOscillator {
    u32 phase;        // advanced by the call
    u32 increment;    // advanced by the call: step is added before each sample
    u32 step;
    i32 next;         // the next sample's carried step, updated
    u32 high;         // 0 or 1, updated
};

struct SquareSync {
    SquareSyncOscillator master, slave;
    i32 balance_start, balance_delta;  // slave weight, 15-bit, interpolated
    i32 xfade_step;                    // added to the 15-bit crossfade per sample
};

namespace square_sync_detail {
const u32 high_edge = 0x80000000u;
// The edge's sub-sample time, in 1/65536 of a sample, held below 65536.
inline u32 edge_time(u32 past, u32 increment) {
    const u32 d = increment >> 16;
    return (past >> 16) >= d ? 65535u : past / d;
}
// The exact quotient, with the machine helper's results for divisors 0 and 1.
inline u32 quotient(u32 n, u32 d) { return n < d ? 0 : d > 1 ? n / d : d ? n : 0xffffffffu; }
inline i32 this_step(u32 t) { if (t > 65535) t = 65535; return static_cast<i32>(t * t >> 18); }
inline i32 next_step(u32 t) { if (t > 65535) t = 65535; t = 65535 - t; return -static_cast<i32>(t * t >> 18); }

// One sample of an oscillator; `reset` is the master's wrap report (0: none,
// else the wrap's time in 1/128 sample plus one) for the slave, and the
// master's own report is returned.
inline u32 sample(SquareSyncOscillator& o, u32 reset, i16& out, bool master) {
    o.increment += o.step;
    i32 now = o.next;
    o.next = 0;
    bool synced = false, through = false;
    u32 reset_time = 0;
    if (reset) {
        reset_time = (reset - 1) << 9;
        const u32 at_reset = o.phase + (65535 - reset_time) * (o.increment >> 16);
        synced = true;
        if (at_reset < o.phase || (!o.high && at_reset >= high_edge)) through = true;
        if (at_reset >= high_edge) { now -= this_step(reset_time); o.next -= next_step(reset_time); }
    }
    o.phase += o.increment;
    const bool wrapped = o.phase < o.increment;
    u32 report = 0;
    if (master && wrapped) report = (quotient(o.phase, o.increment >> 7) + 1) & 0xff;
    if (through || !synced) {
        if (!o.high && o.phase >= high_edge) {
            const u32 t = edge_time(o.phase - high_edge, o.increment);
            now += this_step(t); o.next += next_step(t); o.high = 1;
        }
        if (o.high && wrapped) {
            const u32 t = edge_time(o.phase, o.increment);
            now -= this_step(t); o.next -= next_step(t); o.high = 0;
            if (o.phase >= high_edge) {
                const u32 rise = edge_time(o.phase - high_edge, o.increment);
                now += this_step(rise); o.next += next_step(rise); o.high = 1;
            }
        }
    }
    if (synced) { o.phase = reset_time * (o.increment >> 16); o.high = 0; }
    o.next += o.phase < high_edge ? 0 : 32767;
    out = static_cast<i16>((now - 16384) * 2);
    return report;
}
}

// The definition: per sample, the master steps and reports any wrap, the
// slave steps with that report, and with v = start + (delta * xfade >> 15)
// for the crossfade stepping by xfade_step, w = (v << 1) & 0xffff and
// m = (master * (65535 - w) + slave * w) >> 16, the sample is (m >> 2) * 3.
inline void square_sync_reference(SquareSync& s, i16* out, unsigned size) {
    using namespace square_sync_detail;
    i32 xfade = 0;
    while (size--) {
        i16 master, slave;
        const u32 report = sample(s.master, 0, master, true);
        sample(s.slave, report, slave, false);
        xfade += s.xfade_step;
        const u32 w = static_cast<u32>(s.balance_start + ((s.balance_delta * xfade) >> 15)) << 1 & 0xffff;
        const i32 m = (master * static_cast<i32>(65535 - w) + slave * static_cast<i32>(w)) >> 16;
        *out++ = static_cast<i16>((static_cast<i16>(m) >> 2) * 3);
    }
}

// The device kernel's range: every increment over the call in 2^16..2^31 - 1
// (8 Hz to half the sample rate), so that a wrap is never also a rise and its
// report fits a byte; 1..24 samples; the master and slave not high-only.
// Calls outside it take the reference.
inline bool square_sync_in_range(const SquareSync& s, unsigned size) {
    auto in_range = [size](const SquareSyncOscillator& o) {
        const u32 last = o.increment + o.step * size;
        const i64 drift = static_cast<i64>(static_cast<i32>(o.step)) * size;
        return o.increment - 0x10000u < 0x7fff0000u && last - 0x10000u < 0x7fff0000u &&
               static_cast<i64>(last) - o.increment == drift && o.high <= 1;
    };
    return size - 1u < 24u && in_range(s.master) && in_range(s.slave);
}

#if defined(__bfin__)
extern "C" void opxy_square_sync(SquareSync* s, i16* out, unsigned size);
inline void square_sync(SquareSync& s, i16* out, unsigned size) {
    if (square_sync_in_range(s, size)) opxy_square_sync(&s, out, size);
    else square_sync_reference(s, out, size);
}
#else
inline void square_sync(SquareSync& s, i16* out, unsigned size) { square_sync_reference(s, out, size); }
#endif
}} // namespace opxy::kernels
