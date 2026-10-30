"""Allocation-free arranger model; no firmware adapter or device behavior implied."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.pipeline import ROOT
from test_sample_services import HOST_CXX


class ArrangerNativeTests(unittest.TestCase):
    def test_screen_registration_uses_linked_table(self):
        source = (ROOT / 'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        self.assertIn('__asm__("__stock_screen_slots")', source)
        self.assertNotIn('0x8d4cf380', source)
        self.assertIn('!= slots_address()', source)
        self.assertIn('reinterpret_cast<Base* volatile*>(slots_address())', source)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_NATIVE_PACKAGE'), 'Private native package required')
    def test_native_arranger_resolves_relocated_screen_table(self):
        from test_native_package import object_symbols
        from tooling.formats import elf32
        from tooling.stock_tables import SCREEN_SLOTS
        package = Path(os.environ['OPXY_TEST_NATIVE_PACKAGE'])
        _, undefined = object_symbols((package / 'analysis/sdk/arranger_ui.o').read_bytes())
        self.assertIn('__stock_screen_slots', undefined)
        _, symbols = elf32((package / 'analysis/module.elf').read_bytes())
        self.assertEqual(symbols['__stock_screen_slots']['value'], SCREEN_SLOTS)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_bounded_model_transport_and_semantic_chord(self):
        hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        with tempfile.TemporaryDirectory(prefix='arranger-native-') as directory:
            binary = Path(directory) / ('arranger.exe' if os.name == 'nt' else 'arranger')
            result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT / 'sdk/include'), str(ROOT / 'tests/fixtures/arranger.cpp'), '-o', str(binary)],
                capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
