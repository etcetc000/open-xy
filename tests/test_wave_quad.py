"""opxy::kernels::wave_quad: the portable reference against the wave reads it replaces, and the device form."""
import unittest

from kernel_checks import HOST_CXX, blackfin_compiler, device_build, run_host

# The reference against the same samples written with the SDK's q31 multiply,
# as a machine computes them: random waves (any value and slope), phases,
# increments and balances, including both balance ends and 0.
PROGRAM = r'''
#include <opxy/kernels/wave_quad.hpp>
#include <opxy/q31.hpp>
using namespace opxy;
static u32 r = 99;
static u32 next() { r = r * 1664525u + 1013904223u; return r ^ (r >> 13); }
static i16 read(const u32* w, u32 p) {
    const u32 pair = w[p >> 24];
    return static_cast<i16>(static_cast<i16>(pair) + q31::multiply((static_cast<i32>(pair) >> 16) * 32768, static_cast<i32>(p & 0xffffff)));
}
static i16 fade(const u32* a, const u32* b, u32 p, u16 w) {
    const i32 l = read(a, p), rr = read(b, p);
    return static_cast<i16>(l + q31::multiply((rr - l) * 32768, w));
}
int main() {
    static u32 table[128 * 4];
    for (u32& word : table) word = next();
    table[0] = 0x7fff7fffu; table[1] = 0x80008000u; table[2] = 0x80007fffu; table[3] = 0x7fff8000u;
    for (unsigned t = 0; t < 20000; ++t) {
        kernels::WaveQuad s;
        s.a = table + 128 * (next() % 4); s.b = table + 128 * (next() % 4);
        for (unsigned k = 0; k < 4; ++k) { s.phase[k] = next(); s.increment[k] = next() >> (t % 9); }
        s.balance = t < 3 ? (t == 0 ? 0 : t == 1 ? 1 : 65535) : next() & 0xffff;
        const unsigned size = next() % 24 + 1;
        u32 phase[4] = {s.phase[0], s.phase[1], s.phase[2], s.phase[3]};
        i16 out[24];
        kernels::wave_quad(s, out, size);
        for (unsigned i = 0; i < size; ++i) {
            i32 sum = 0;
            for (unsigned k = 0; k < 4; ++k) { phase[k] += s.increment[k]; sum += fade(s.a, s.b, phase[k] >> 1, static_cast<u16>(s.balance)); }
            if (out[i] != static_cast<i16>(sum >> 2)) return 1;
        }
        for (unsigned k = 0; k < 4; ++k) if (phase[k] != s.phase[k]) return 2;
    }
    return 0;
}
'''

CALLER = r'''
#include <opxy/kernels/wave_quad.hpp>
void render(opxy::kernels::WaveQuad& s, opxy::i16* out, unsigned size) { opxy::kernels::wave_quad(s, out, size); }
'''


class WaveQuadTest(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_reference_matches_the_wave_reads(self):
        run_host(self, PROGRAM)

    @unittest.skipUnless(blackfin_compiler(), 'No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
    def test_device_kernel_builds_and_is_called(self):
        # Sample-for-sample equality of the device kernel with the reference
        # was checked on the BF707 functional simulator (random waves, phases
        # and balances, every size 1..24) and over Macro Osc's audio corpus.
        device_build(self, 'wave_quad', CALLER)


if __name__ == '__main__':
    unittest.main()
