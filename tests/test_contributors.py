import re
import unittest
import tempfile
from pathlib import Path
from tooling.adapters import generate_voice

from tooling.contributors import validate_source_boundary
from tooling.native import check_sources
from machine_fixtures import NAMES, fixture


class ContributorBoundary(unittest.TestCase):
    def test_voice_adapter_includes_only_the_machine_and_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            source = generate_voice(directory, fixture('probesynth')).read_text(encoding='utf-8')
        self.assertEqual(re.findall(r'#include "([^"]+)"', source), ['core.h', 'engine.cpp', 'stock.hpp', 'synth_controls.hpp'])
        self.assertIn('probesynth::render(slot->voice, shared, slot->owner, output, controls, increments);', source)
        self.assertIn('const opxy::i32* controls = parameters;', source)
        self.assertNotIn('sdk_benchmark', source)
        # A sweep build's voices take the swept controls while their own machine is under test.
        with tempfile.TemporaryDirectory() as directory:
            swept = generate_voice(directory, fixture('probesynth'), sweep=True).read_text(encoding='utf-8')
        self.assertIn('sdk_benchmark_engine == probesynth_engine_id', swept)
        self.assertIn('const opxy::i32* controls = swept ? swept : parameters;', swept)

    def test_machines_have_no_platform_branches(self):
        check_sources([fixture(name) for name in NAMES])

    def test_platform_specific_code_is_rejected(self):
        for source in ('#ifdef _WIN32\n#endif', '#if 1\n#endif',
                       '#include <windows.h>', '#include "io.h"',
                       '__asm__("RTS;");', 'bool x = __bfin__;'):
            with self.subTest(source=source), self.assertRaises(ValueError):
                validate_source_boundary(source)

    def test_portable_example_and_explanatory_comments_are_allowed(self):
        validate_source_boundary('''#pragma once
#include "opxy/q31.hpp"
// The SDK owns #ifdef _WIN32 and __asm__.
/* Never include <windows.h> in an instrument. */
opxy::i32 sample(opxy::i32 x) { return opxy::q31::half(x); }
''')


if __name__ == '__main__':
    unittest.main()
