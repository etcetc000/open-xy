"""Experimental Morph bridge: bounded inputs and authenticated physical hooks."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from tooling.pipeline import ROOT, config
from tooling.formats import ldr, mapped_offset, sha
from test_sample_services import HOST_CXX


class MorphBridge(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_bounded_input_and_context_contracts(self):
        with tempfile.TemporaryDirectory(prefix='morph-contracts-') as directory:
            binary = Path(directory) / ('contracts.exe' if os.name == 'nt' else 'contracts')
            result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                                     '-I' + str(ROOT / 'sdk/include'),
                                     str(ROOT / 'tests/fixtures/morph_contracts.cpp'), '-o', str(binary)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('Morph bridge contract checks', result.stdout)

    def test_physical_hook_scope(self):
        hooks = [p for p in config()['patches'] if p.get('symbol', '').startswith('sdk_morph_')]
        self.assertEqual([(p['symbol'], int(p['address'], 16), p['length'], p['service'], p['call'])
                          for p in hooks],
                         [('sdk_morph_midi_cc', 0x8d58e818, 4, 'control', True),
                          ('sdk_morph_pad_event', 0x8d5b27f6, 4, 'control', True)])

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'No private stock OS fixture')
    def test_hook_preimages_match_private_stock(self):
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        profile = config()
        self.assertEqual(sha(data), profile['stock_sha256'])
        for patch in profile['patches']:
            if not patch.get('symbol', '').startswith('sdk_morph_'):
                continue
            offset = mapped_offset(ldr(data), int(patch['address'], 16), patch['length'])
            self.assertEqual(offset, int(patch['file_offset'], 16))
            self.assertEqual(data[offset:offset + patch['length']].hex(), patch['before'])
