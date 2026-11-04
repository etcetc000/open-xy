"""Internal stock selector behavior with two independent GUI clients."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import test_sample_services as services

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'


class StockBrowser(unittest.TestCase):
    @unittest.skipUnless(services.HOST_CXX, 'No working host C++ compiler')
    def test_clients_validation_modal_lifetime_and_stock_callbacks(self):
        with tempfile.TemporaryDirectory(prefix='stock-browser-') as directory:
            folder = Path(directory)
            stock = (PLATFORM / 'stock.hpp').read_text().replace(
                'typedef __UINTPTR_TYPE__ Address;',
                'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock)
            for name in ('stock_browser.hpp', 'stock_browser.cpp'):
                (folder / name).write_bytes((PLATFORM / name).read_bytes())
            binary = folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
            command = [services.HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                       '-I' + str(folder), '-I' + str(ROOT / 'sdk/include'),
                       str(ROOT / 'tests/fixtures/stock_browser.cpp'), str(folder / 'stock_browser.cpp'),
                       '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
