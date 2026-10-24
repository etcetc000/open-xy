"""Exercise the bounded ghost pattern and capture adapter without a device."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]


class CaptureHook(unittest.TestCase):
    def test_queued_event_keeps_absolute_call_and_exact_nop_padding(self):
        from tooling.firmware import hook_instruction, hook_target
        from tooling.pipeline import config
        patch = next(p for p in config()['patches'] if p.get('symbol') == 'sdk_capture_queued_event')
        self.assertEqual(patch['length'], 12)
        target = 0x8d450d60
        encoded = hook_instruction(patch, target)
        self.assertEqual(hook_target(patch, encoded), (target, True))
        for bad in (encoded[:-2], encoded + b'\0\0', encoded[:-1] + b'\1'):
            with self.assertRaises(ValueError):
                hook_target(patch, bad)
        with self.assertRaises(ValueError):
            hook_instruction(dict(patch, call=False), target)


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class RecordingHistory(unittest.TestCase):
    def run_fixture(self, name):
        hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        with tempfile.TemporaryDirectory(prefix='recording-history-') as folder:
            binary = Path(folder) / ('history.exe' if os.name == 'nt' else 'history')
            if name == 'ghost_capture':
                platform = ROOT / 'sdk/platform/opxy-1.2.0'
                stock = (platform / 'stock.hpp').read_text()
                stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                      'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
                for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                    stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                          'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
                (Path(folder) / 'stock.hpp').write_text(stock)
                (Path(folder) / 'ghost_capture_stock.cpp').write_text((platform / 'ghost_capture_stock.cpp').read_text().replace(
                    '#include "../extension_runtime.hpp"', '#include <extension_runtime.hpp>'))
                from tooling.extensions import generate_collection
                generate_collection(Path(folder))
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-I' + str(folder), '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/platform'),
                       '-I' + str(ROOT / 'sdk/platform/opxy-1.2.0'),
                       str(ROOT / 'tests/fixtures' / (name + '.cpp')), '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_ghost_pattern_replacement_context_and_note_lifetimes(self):
        self.run_fixture('recording_history')

    def test_double_tap_without_holds_chords_or_repeats(self):
        self.run_fixture('record_tap')

    def test_stock_adapter_capture_forwarding_undo_and_failure(self):
        self.run_fixture('ghost_capture')

    def test_extension_observers_order_and_write_ownership(self):
        self.run_fixture('extension_runtime')
