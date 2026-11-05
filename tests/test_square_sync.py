"""opxy::kernels::square_sync: the portable reference against the buffered master, slave and crossfade loops it
replaces, and the device form."""
import unittest

from kernel_checks import HOST_CXX, blackfin_compiler, device_build, run_host

# The original shape: the master renders its samples and a byte of wrap
# reports per sample, the slave reads those bytes, and a separate loop mixes
# the two buffers. Random phases, increments (gliding up and down, across the
# whole range including outside the device kernel's), carried steps and
# crossfades.
PROGRAM = r'''
#include <opxy/kernels/square_sync.hpp>
using namespace opxy;
static u32 r = 3;
static u32 next() { r = r * 1664525u + 1013904223u; return r ^ (r >> 15); }
static u32 tq(u32 n, u32 d) { return n < d ? 0 : d > 1 ? n / d : d ? n : 0xffffffffu; }
static i32 this_blep(u32 t) { if (t > 65535) t = 65535; return t * t >> 18; }
static i32 next_blep(u32 t) { if (t > 65535) t = 65535; t = 65535 - t; return -static_cast<i32>(t * t >> 18); }
static void render(kernels::SquareSyncOscillator& o, const u8* sync_in, i16* buffer, u8* sync_out, unsigned size) {
    const u32 pw = 0x80000000u;
    u32 phase = o.phase, inc = o.increment; bool high = o.high; i32 next_sample = o.next;
    while (size--) {
        bool sync_reset = false, self_reset = false, during = false; u32 reset_time = 0;
        inc += o.step;
        i32 this_sample = next_sample; next_sample = 0;
        if (sync_in && *sync_in) {
            reset_time = static_cast<u32>(*sync_in - 1) << 9;
            const u32 at = phase + (65535 - reset_time) * (inc >> 16);
            sync_reset = true;
            if (at < phase || (!high && at >= pw)) during = true;
            if (at >= pw) { this_sample -= this_blep(reset_time); next_sample -= next_blep(reset_time); }
        }
        if (sync_in) sync_in++;
        phase += inc;
        if (phase < inc) self_reset = true;
        if (sync_out) *sync_out++ = phase < inc ? static_cast<u8>(tq(phase, inc >> 7) + 1) : 0;
        if (during || !sync_reset) {
            if (!high && phase >= pw) { u32 t = tq(phase - pw, inc >> 16); this_sample += this_blep(t); next_sample += next_blep(t); high = true; }
            if (high && self_reset) {
                u32 t = tq(phase, inc >> 16); this_sample -= this_blep(t); next_sample -= next_blep(t); high = false;
                if (phase >= pw) { t = tq(phase - pw, inc >> 16); this_sample += this_blep(t); next_sample += next_blep(t); high = true; }
            }
        }
        if (sync_reset) { phase = reset_time * (inc >> 16); high = false; }
        next_sample += phase < pw ? 0 : 32767;
        *buffer++ = static_cast<i16>((this_sample - 16384) << 1);
    }
    o.phase = phase; o.increment = inc; o.high = high; o.next = next_sample;
}
static void oscillator(kernels::SquareSyncOscillator& o, unsigned size) {
    o.phase = next();
    o.increment = next() >> (next() % 12);
    const u32 target = next() % 3 ? next() >> (next() % 12) : o.increment;
    o.step = o.increment < target ? (target - o.increment) / size : ~((o.increment - target) / size);
    o.next = static_cast<i32>(next() % 70000) - 20000;
    o.high = next() & 1;
}
int main() {
    for (unsigned t = 0; t < 40000; ++t) {
        const unsigned size = next() % 24 + 1;
        kernels::SquareSync s;
        oscillator(s.master, size); oscillator(s.slave, size);
        s.balance_start = static_cast<i32>(next() % 32768);
        s.balance_delta = next() % 3 ? 0 : static_cast<i32>(next() % 32768) - s.balance_start;
        s.xfade_step = static_cast<i32>(32767 / size);
        kernels::SquareSync original = s;
        i16 out[24], master[24], slave[24]; u8 sync[24];
        kernels::square_sync(s, out, size);
        render(original.master, nullptr, master, sync, size);
        render(original.slave, sync, slave, nullptr, size);
        i32 xfade = 0;
        for (unsigned i = 0; i < size; ++i) {
            xfade += original.xfade_step;
            const u16 w = static_cast<u16>((original.balance_start + (original.balance_delta * xfade >> 15)) << 1);
            const i16 m = static_cast<i16>((master[i] * (65535 - w) + slave[i] * w) >> 16);
            if (out[i] != static_cast<i16>((m >> 2) * 3)) return 1;
        }
        const kernels::SquareSyncOscillator* a[2] = {&s.master, &s.slave};
        const kernels::SquareSyncOscillator* b[2] = {&original.master, &original.slave};
        for (unsigned k = 0; k < 2; ++k)
            if (a[k]->phase != b[k]->phase || a[k]->increment != b[k]->increment || a[k]->next != b[k]->next || a[k]->high != b[k]->high) return 2;
    }
    return 0;
}
'''

CALLER = r'''
#include <opxy/kernels/square_sync.hpp>
void render(opxy::kernels::SquareSync& s, opxy::i16* out, unsigned size) { opxy::kernels::square_sync(s, out, size); }
'''


class SquareSyncTest(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_reference_matches_the_buffered_loops(self):
        run_host(self, PROGRAM)

    @unittest.skipUnless(blackfin_compiler(), 'No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
    def test_device_kernel_builds_and_is_called(self):
        # Sample-for-sample equality of the device kernel with the reference
        # was checked on the BF707 functional simulator (random phases,
        # gliding increments across the kernel's range, carried steps,
        # crossfades and sizes) and over Macro Osc's audio corpus.
        device_build(self, 'square_sync', CALLER)


if __name__ == '__main__':
    unittest.main()
