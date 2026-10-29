"""The extension runtime, its generated registry and the 1.2.1 adapter, without a device.

Each extension's own tests live in extensions/<id>/tests/ and run here too: every
.cpp there is a host program that returns 0 when its checks pass.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'


def build_and_run(test, source, folder, includes=()):
    """Compile one host test program with the SDK's include paths and run it."""
    hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
    binary = Path(folder) / ('test.exe' if os.name == 'nt' else 'test')
    command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
               *('-I' + str(path) for path in (folder, *includes, ROOT / 'sdk/include', ROOT / 'sdk/platform', PLATFORM)),
               str(source), '-o', str(binary)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
    test.assertEqual(built.returncode, 0, built.stdout + built.stderr)
    ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
    test.assertEqual(ran.returncode, 0, f'{source.name}: check failed at line {ran.returncode}')


class KeyQueueHook(unittest.TestCase):
    def test_queued_event_keeps_absolute_call_and_exact_nop_padding(self):
        from tooling.firmware import hook_instruction, hook_target
        from tooling.pipeline import config
        patch = next(p for p in config()['patches'] if p.get('symbol') == 'sdk_extensions_key_queue')
        self.assertEqual(patch['length'], 12)
        target = 0x8d450380
        encoded = hook_instruction(patch, target)
        self.assertEqual(hook_target(patch, encoded), (target, True))
        for bad in (encoded[:-2], encoded + b'\0\0', encoded[:-1] + b'\1'):
            with self.assertRaises(ValueError):
                hook_target(patch, bad)
        with self.assertRaises(ValueError):
            hook_instruction(dict(patch, call=False), target)


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class Runtime(unittest.TestCase):
    def run_fixture(self, name, prepare=None):
        with tempfile.TemporaryDirectory(prefix='extension-test-') as folder:
            if prepare:
                prepare(Path(folder))
            build_and_run(self, ROOT / 'tests/fixtures' / (name + '.cpp'), folder)

    def test_double_tap_without_holds_chords_or_repeats(self):
        self.run_fixture('double_tap')

    def test_extension_observers_order_and_write_ownership(self):
        self.run_fixture('extension_runtime')

    def test_generated_registry_omits_disabled_extensions_and_preserves_order(self):
        from tooling.extensions import generate_collection
        def prepare(folder):
            with mock.patch('tooling.extensions.collection', return_value=[
                dict(id='capture', origin='official', claims=['record_double_tap'], actions=['edit_notes']),
                dict(id='observer', origin='community', claims=[], actions=[]),
            ]):
                generate_collection(folder)
        self.run_fixture('extension_selection', prepare)

    def test_stock_adapter_forwarding_undo_and_failure_with_capture_subscribed(self):
        # The adapter end to end, with stock doubles at its mapped addresses and
        # capture as the subscriber whose writes it carries out.
        from tooling.extensions import generate_collection, generate_extension
        def prepare(folder):
            stock = (PLATFORM / 'stock.hpp').read_text()
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock)
            (folder / 'extension_stock.cpp').write_text((PLATFORM / 'extension_stock.cpp').read_text().replace(
                '#include "../extension_runtime.hpp"', '#include <extension_runtime.hpp>'))
            generate_collection(folder)
            generate_extension(folder, 'capture')
        self.run_fixture('extension_stock', prepare)


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class ExtensionsOwnTests(unittest.TestCase):
    def test_each_extension_runs_its_own_tests(self):
        from tooling.extensions import collection
        found = 0
        for entry in collection():
            root = ROOT / 'extensions' / entry['id']
            for source in sorted((root / 'tests').glob('*.cpp')):
                found += 1
                with self.subTest(extension=entry['id'], test=source.name), \
                        tempfile.TemporaryDirectory(prefix='extension-own-test-') as folder:
                    build_and_run(self, source, folder, (root,))
        self.assertTrue(found, 'Every extension in the collection should carry tests in extensions/<id>/tests/')
