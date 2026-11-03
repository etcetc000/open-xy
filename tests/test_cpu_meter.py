"""The cpu meter: its arithmetic on the host, and its firmware edits and artwork."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tooling import native
from tooling.formats import ldr, mapped_offset, sha
from tooling.pipeline import ROOT, config
from test_sample_services import HOST_CXX

# Each hook's site, its length, and the stock call it replaces (None: instructions).
SITES = {
    'sdk_cpu_audio': (0x8c0c6ffe, 8, 0x11a02864), 'sdk_cpu_track': (0x8d510bea, 4, 0x8d51c708),
    'sdk_cpu_aux': (0x8d51234e, 4, 0x8d50fdc0), 'sdk_cpu_tape': (0x8d511486, 4, 0x8d50fa88),
    'sdk_cpu_fx1': (0x8d5116a0, 4, 0x8d50f594), 'sdk_cpu_fx2': (0x8d511908, 4, 0x8d50f594),
    'sdk_cpu_master': (0x8d511b80, 8, 0x11a02c10), 'sdk_cpu_dispatch': (0x8c0c2384, 8, None),
    'sdk_cpu_page_list': (0x8c088456, 8, None), 'sdk_cpu_page_fmt': (0x8c09a4fa, 8, None),
    'sdk_cpu_page_hdr': (0x8c09a50c, 8, None), 'sdk_cpu_page_more': (0x8c09a544, 12, None),
    'sdk_cpu_page_end': (0x8c09a638, 8, None)}


def hooks():
    return {p['symbol']: p for p in config()['patches'] if p.get('symbol') in SITES}


class CpuMeter(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_window_reports_indicator_and_settings(self):
        with tempfile.TemporaryDirectory(prefix='cpu-meter-') as directory:
            binary = Path(directory) / ('test.exe' if os.name == 'nt' else 'test')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'),
                str(ROOT / 'tests/fixtures/cpu_meter.cpp'), '-o', str(binary)],
                capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_every_build_carries_the_meter(self):
        found = hooks()
        self.assertEqual(set(found), set(SITES))
        for symbol, (address, length, _target) in SITES.items():
            p = found[symbol]
            self.assertEqual((int(p['address'], 16), p['length']), (address, length), symbol)
            self.assertTrue(p['call'] and 'service' not in p, symbol)
        for unit in ('cpu_meter', 'cpu_meter_hooks', 'cpu_overlay', 'overlay'):
            self.assertIn(unit, native.SDK_UNITS)
            self.assertNotIn(unit, native.UNIT_SERVICES)

    def test_overlay_artwork_holds_the_nodes_it_binds(self):
        artwork = (ROOT / native.OVERLAY_ARTWORK).read_text(encoding='utf-8')
        source = (ROOT / 'sdk/platform/opxy-1.2.1/cpu_overlay.cpp').read_text(encoding='utf-8')
        rows = len(re.findall(r'"bd_k\d+"', source))
        self.assertEqual(rows, len(re.findall(r'"bd_v\d+"', source)))
        ids = ['indicator', 'breakdown', 'ind_cpu', 'ind_alert'] + [f'bd_{k}{i}' for k in 'kv' for i in range(rows)]
        self.assertEqual(len(re.findall(r'id="bd_[kv]\d+"', artwork)), 2 * rows)
        for node in ids:
            self.assertIn(f'id="{node}"', artwork)
            self.assertIn(f'"{node}"', source)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_sites_hold_the_stock_bytes_they_replace(self):
        from tooling.firmware import decode_branch
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(stock), config()['stock_sha256'])
        image = ldr(stock)
        for symbol, p in hooks().items():
            address, length, target = SITES[symbol]
            offset = mapped_offset(image, address, length)
            before = stock[offset:offset + length]
            self.assertEqual((before.hex(), offset), (p['before'], int(p['file_offset'], 16)), symbol)
            if target is None:
                continue
            if length == 8:  # CALL.A: 0xde00, then the target's high and low halves
                self.assertEqual(before[:2], b'\x00\xde', symbol)
                called = int.from_bytes(before[2:4], 'little') << 16 | int.from_bytes(before[4:6], 'little')
            else:
                called = decode_branch(address, before)[0]
            self.assertEqual(called, target, symbol)


if __name__ == '__main__':
    unittest.main()
