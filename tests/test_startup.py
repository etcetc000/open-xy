"""Generated startup glue: its inputs and how it reaches stock.

Its behavior runs on the host in test_sample_services.py (fixtures/startup.cpp).
"""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from tooling.definitions import ROOT
from tooling.native import DEFINITIONS
from tooling.startup import generate, render
from tooling.stock_tables import EFFECT_COUNT, ENGINE_COUNT
from test_sample_services import HOST_CXX


class Startup(unittest.TestCase):
    def test_stock_is_reached_only_through_link_time_definitions(self):
        # Browser kits relink this object, so it names stock and machine IDs as symbols.
        text = render(['probesynth', 'probesources', 'probesample'], ['probefx'], ['probesample'])
        self.assertIsNone(re.search(r'0x[0-9a-fA-F]+', text))
        stock = set(re.findall(r'__asm__\("(stock_\w+)"\)', text))
        self.assertEqual(stock, {'stock_catalog_insert', 'stock_gui_role'})
        self.assertLessEqual(stock, set(DEFINITIONS))
        for prefix in ('probesynth', 'probesources', 'probesample'):
            self.assertIn(f'__asm__("{prefix}_engine_id")', text)
        self.assertNotIn('probesample_screen_register', text)

    def test_rejects_invalid_machine_lists(self):
        for prefixes in ([], ['probesynth', 'probesynth'], ['Bad'], ['x%d' % i for i in range(ENGINE_COUNT + 1)]):
            with self.subTest(prefixes=prefixes[:3]), self.assertRaises(ValueError):
                render(prefixes)
        for effects, stock_page in ((['probesynth'], ()), ((), ['other'])):
            with self.subTest(effects=effects), self.assertRaises(ValueError):
                render(['probesynth'], effects, stock_page)
        with self.assertRaises(ValueError):
            render(['probesynth'], ['f%d' % i for i in range(EFFECT_COUNT + 1)])

    def test_fills_the_relocated_tables(self):
        # A collection may use every engine and effect record the firmware relocates.
        engines, effects = ['x%d' % i for i in range(ENGINE_COUNT)], ['f%d' % i for i in range(EFFECT_COUNT)]
        text = render(engines, effects)
        for prefix in (engines[-1], effects[-1]):
            self.assertIn(f'"{prefix}_', text)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_a_collection_of_players_alone_compiles_without_warnings(self):
        # No engine or effect row calls the id helper, which must not then be reported unused.
        with tempfile.TemporaryDirectory(prefix='startup-') as folder:
            generate(folder, [], player_pages=['probeplayer'])
            stock = (ROOT / 'sdk/platform/opxy-1.2.1/stock.hpp').read_text()
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (Path(folder) / 'stock.hpp').write_text(stock)
            result = subprocess.run([HOST_CXX, '-std=c++11', '-Wall', '-Werror', '-fsyntax-only', '-I' + folder,
                                     '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'),
                                     str(Path(folder) / 'startup.cpp')], capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
