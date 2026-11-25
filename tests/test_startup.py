"""Generated startup glue: its inputs and how it reaches stock.

Its behavior runs on the host in test_sample_services.py (fixtures/startup.cpp).
"""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from tooling import records
from tooling.definitions import ROOT
from tooling.native import DEFINITIONS
from tooling.startup import generate, render
from tooling.stock_tables import EFFECT_COUNT, ENGINE_COUNT
from test_sample_services import HOST_CXX


class Startup(unittest.TestCase):
    def test_stock_is_reached_only_through_link_time_definitions(self):
        # Browser kits relink this object, so it names stock as symbols, and no machine at all:
        # it registers the selected machines from their records (machine_record.hpp).
        for flags in ((), (True, True, True, True)):
            text = render(*flags)
            self.assertIsNone(re.search(r'0x[0-9a-fA-F]+', text))
            stock = set(re.findall(r'__asm__\("(stock_\w+)"\)', text))
            self.assertEqual(stock, {'stock_catalog_insert', 'stock_gui_role'})
            self.assertLessEqual(stock, set(DEFINITIONS))
            self.assertNotIn('probe', text)
            self.assertIn('sdk_machine_records', text)

    def test_the_aux_service_needs_the_control_service(self):
        with self.assertRaisesRegex(ValueError, 'control service'):
            render(aux=True)
        self.assertIn('sdk_aux_startup();', render(control=True, aux=True))

    def test_a_selection_fills_at_most_the_relocated_tables(self):
        # A collection may use every engine and effect record the firmware relocates, and no more.
        engines = [SimpleNamespace(id='x%d' % i, prefix='x%d' % i, kind='synth') for i in range(ENGINE_COUNT)]
        effects = [SimpleNamespace(id='f%d' % i, prefix='f%d' % i, kind='effect') for i in range(EFFECT_COUNT)]
        text = records.assembly(engines + effects)
        for m in (engines[-1], effects[-1]):
            self.assertIn(f'.long {m.prefix}_record', text)
        for extra in (SimpleNamespace(id='x', prefix='x', kind='sample'), SimpleNamespace(id='f', prefix='f', kind='effect')):
            with self.assertRaisesRegex(ValueError, 'More machines'):
                records.assembly(engines + effects + [extra])
        for bad in ([], [engines[0], engines[0]]):
            with self.assertRaises(ValueError):
                records.assembly(bad)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_startup_compiles_without_warnings(self):
        with tempfile.TemporaryDirectory(prefix='startup-') as folder:
            generate(folder)
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
