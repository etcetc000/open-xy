"""opxy::kernels::additive: the portable reference against the partial loop it replaces, and the device form."""
import unittest

from kernel_checks import HOST_CXX, blackfin_compiler, device_build, run_host

# The reference against twelve int32 partials written with the SDK's q31
# multiply, as a machine computes them, over random sine tables, phases and
# spectra (amplitudes and targets 0..32767 summing to at most 32767), and the
# range check's refusals.
PROGRAM = r'''
#include <opxy/kernels/additive.hpp>
#include <opxy/q31.hpp>
using namespace opxy;
static u32 r = 7;
static u32 next() { r = r * 1664525u + 1013904223u; return r ^ (r >> 13); }
static void spectrum(i32* v) {
    const u32 mode = next() % 4; i32 left = 32767;
    for (unsigned k = 0; k < 12; ++k) {
        i32 x = mode == 0 ? static_cast<i32>(next() % (left + 1)) : mode == 1 ? (k == next() % 12 ? left : 0)
              : mode == 2 ? 2730 : static_cast<i32>(next() % (left / 3 + 1));
        if (x > left) x = left;
        v[k] = x; left -= x;
    }
}
static u32 pack(i32 lo, i32 hi) { return static_cast<u16>(lo) | static_cast<u32>(static_cast<u16>(hi)) << 16; }
int main() {
    static u32 sine[512];
    for (u32& word : sine) word = next();
    sine[0] = 0x7fff8000u; sine[511] = 0x80008000u;
    for (unsigned t = 0; t < 20000; ++t) {
        i32 a[12], target[12];
        spectrum(a); spectrum(target);
        if (t % 7 == 0) for (i32& x : a) x = 0;
        u32 amplitude[6], packed[6];
        for (unsigned j = 0; j < 6; ++j) { amplitude[j] = pack(a[2 * j], a[2 * j + 1]); packed[j] = pack(target[2 * j], target[2 * j + 1]); }
        kernels::Additive s;
        s.phase = next(); s.increment = next() >> (t % 13); s.previous = static_cast<i16>(next()); s.unused = 0;
        s.amplitude = amplitude; s.target = packed; s.sine = sine;
        if (!kernels::additive_in_range(s)) return 3;
        const unsigned pairs = next() % 12 + 1;
        u32 phase = s.phase; i16 previous = s.previous, out[24];
        kernels::additive(s, out, pairs);
        for (unsigned i = 0; i < pairs; ++i) {
            phase += s.increment;
            i32 sum = 0; u32 h = phase;
            for (unsigned k = 0; k < 12; ++k, h += phase) {
                const u32 pair = sine[h >> 23];
                const i16 value = static_cast<i16>(static_cast<i16>(pair) + ((static_cast<i16>(pair >> 16) * static_cast<i16>(h >> 8)) >> 16));
                sum += q31::multiply(static_cast<i32>(value) * 65536, a[k]);
                a[k] += (target[k] - a[k]) >> 8;
            }
            if (out[2 * i] != static_cast<i16>((sum + previous) >> 1) || out[2 * i + 1] != static_cast<i16>(sum)) return 1;
            previous = static_cast<i16>(sum);
        }
        if (s.phase != phase || s.previous != previous) return 2;
        for (unsigned j = 0; j < 6; ++j) if (amplitude[j] != pack(a[2 * j], a[2 * j + 1])) return 2;
    }
    u32 amplitude[6] = {}, packed[6] = {32767, 1, 0, 0, 0, 0};
    kernels::Additive s{0, 0, 0, 0, amplitude, packed, nullptr};
    if (kernels::additive_in_range(s)) return 4;     // targets sum past 32767
    packed[0] = 0x8000u; packed[1] = 0;
    if (kernels::additive_in_range(s)) return 5;     // a negative target
    return 0;
}
'''

CALLER = r'''
#include <opxy/kernels/additive.hpp>
void render(opxy::kernels::Additive& s, opxy::i16* out, unsigned pairs) { opxy::kernels::additive(s, out, pairs); }
'''


class AdditiveTest(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_reference_matches_the_partial_loop(self):
        run_host(self, PROGRAM)

    @unittest.skipUnless(blackfin_compiler(), 'No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
    def test_device_kernel_builds_and_is_called(self):
        # Sample-for-sample equality of the device kernel with the reference
        # was checked on the BF707 functional simulator (random tables, phases
        # and spectra, 1..12 pairs) and over Macro Osc's audio corpus.
        device_build(self, 'additive', CALLER)


if __name__ == '__main__':
    unittest.main()
