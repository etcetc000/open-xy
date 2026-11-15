"""zero_voice: a new owner's voice cleared by words, by bytes, or only its declared prefix."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.definitions import ROOT
from test_sample_services import HOST_CXX

PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'

PROGRAM = r'''
#include <cstring>
#include "voice_zero.hpp"
using opxy::u32;
struct Whole { u32 a[5]; };                                   // words, all of it
struct Prefix { static constexpr u32 zeroed_bytes = 8; u32 a[6]; };  // words, the first 8 bytes
struct Bytes { unsigned char a[7]; };                         // bytes, all of it
struct OddPrefix { static constexpr u32 zeroed_bytes = 3; u32 a[3]; };  // a prefix not a whole word
struct IntPrefix { static const int zeroed_bytes = 4; u32 a[2]; };  // any integral constant
struct Empty { static constexpr u32 zeroed_bytes = 0; u32 a[2]; };
struct NotAssignable { const u32 a[3]; static constexpr u32 zeroed_bytes = 4; };

template<class T> int check(unsigned zeroed, int code) {
    alignas(T) unsigned char storage[sizeof(T) + 8];
    std::memset(storage, 0xff, sizeof storage);
    opxy::platform::zero_voice(*reinterpret_cast<T*>(storage));
    for (unsigned i = 0; i < sizeof storage; ++i)
        if (storage[i] != (i < zeroed ? 0 : 0xff)) return code;
    return 0;
}

int main() {
    if (int r = check<Whole>(sizeof(Whole), 1)) return r;
    if (int r = check<Prefix>(8, 2)) return r;
    if (int r = check<Bytes>(sizeof(Bytes), 3)) return r;
    if (int r = check<OddPrefix>(3, 4)) return r;
    if (int r = check<IntPrefix>(4, 5)) return r;
    if (int r = check<Empty>(0, 6)) return r;
    if (int r = check<NotAssignable>(4, 7)) return r;
    return 0;
}
'''

TOO_LONG = r'''
#include "voice_zero.hpp"
struct Voice { static constexpr unsigned zeroed_bytes = 12; unsigned a[2]; };
void clear(Voice& v) { opxy::platform::zero_voice(v); }
'''


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class VoiceZero(unittest.TestCase):
    def compile(self, text, run=True):
        with tempfile.TemporaryDirectory() as directory:
            source, program = Path(directory) / 'zero.cpp', Path(directory) / 'zero'
            source.write_text(text, encoding='utf-8')
            built = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I', str(ROOT / 'sdk/include'),
                                    '-I', str(PLATFORM), str(source), '-o', str(program)], capture_output=True, text=True)
            if not run or built.returncode:
                return built
            return subprocess.run([str(program)])

    def test_clears_exactly_the_declared_prefix_or_the_whole_voice(self):
        self.assertEqual(self.compile(PROGRAM).returncode, 0)

    def test_a_prefix_longer_than_the_voice_does_not_compile(self):
        built = self.compile(TOO_LONG, run=False)
        self.assertNotEqual(built.returncode, 0)
        self.assertIn('zeroed_bytes exceeds the voice', built.stderr)

    def test_both_voice_adapters_use_it(self):
        for template, call in (('voice.cpp.in', 'zero_voice(slot.voice);'), ('drum_voice.cpp.in', 'zero_voice(slot->voice);')):
            with self.subTest(template=template):
                text = (PLATFORM / template).read_text(encoding='utf-8')
                self.assertIn('#include "voice_zero.hpp"', text)
                self.assertIn('opxy::platform::' + call, text)
                self.assertNotIn('bytes[i] = 0', text)


if __name__ == '__main__':
    unittest.main()
