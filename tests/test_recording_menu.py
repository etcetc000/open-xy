"""Exercise the real system-menu adapter with stock calls mapped to boundary doubles."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class RecordingMenu(unittest.TestCase):
    def test_stock_menu_ownership_values_and_failures(self):
        hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        platform = ROOT / 'sdk/platform/opxy-1.2.0'
        with tempfile.TemporaryDirectory(prefix='recording-menu-') as folder:
            folder = Path(folder)
            stock = (platform / 'stock.hpp').read_text()
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock)
            (folder / 'recording_menu.cpp').write_bytes((platform / 'recording_menu.cpp').read_bytes())
            binary = folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-I' + str(folder), '-I' + str(ROOT / 'sdk/include'), '-I' + str(platform),
                       str(ROOT / 'tests/fixtures/recording_menu.cpp'), '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            # Each case has fresh process-lifetime storage, as on a new boot.
            for case in range(10):
                with self.subTest(case=case):
                    ran = subprocess.run([str(binary), str(case)], capture_output=True,
                                         text=True, timeout=30, **hidden)
                    self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
