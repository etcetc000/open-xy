"""SDK display amp envelope: the GUI-side stock read and its bounds."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.formats import ldr, mapped_offset, sha
from tooling.pipeline import ROOT, config
from test_sample_services import HOST_CXX


class AmpEnvelope(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_real_platform_read_and_its_bounds(self):
        platform = ROOT / 'sdk/platform/opxy-1.2.1'
        with tempfile.TemporaryDirectory(prefix='amp-envelope-') as directory:
            folder = Path(directory)
            # Substitute only the stock-address boundary, as other SDK fixtures do.
            stock = (platform / 'stock.hpp').read_text(encoding='utf-8')
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock, encoding='utf-8')
            (folder / 'amp_envelope.hpp').write_bytes((platform / 'amp_envelope.hpp').read_bytes())
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                '-I' + str(folder), '-I' + str(ROOT / 'sdk/include'), '-I' + str(platform),
                str(ROOT / 'tests/fixtures/amp_envelope.cpp'), '-o', str(binary)],
                capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_stock_getter_reads_words_8_on_and_the_envelope_page_draws_them(self):
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(stock), config()['stock_sha256'])
        blocks = ldr(stock)
        def code(address, length):
            offset = mapped_offset(blocks, address, length)
            return stock[offset:offset + length].hex()
        # 8d4d7680: its own GUI vector (08037b08), filled from 8d5ac9dc(86bd027c,
        # track, 8, 0) onwards, as the parameter getter 8d4d72a8 fills words 0..7.
        self.assertEqual(code(0x8d4d7680, 0x48),
            '00e80000e405866f09da0308a87a000003c8001830b34999010c0cda0308087b00009318056803c8'
            '0018f5bc31a3426000dabd867c02000006e392a903c80018a1a062a004cc0a42')
        # The envelope page's layout 8d527fdc: selected track 8d4d6f14, then
        # 8d4d7458(track, 8), whose first four words it draws as A, D, S, R.
        self.assertEqual(code(0x8d527fe6, 12), 'fde3977741603830fde3357a')


if __name__ == '__main__':
    unittest.main()
