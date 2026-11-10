"""The extension runtime, its generated registry and the 1.2.1 adapter, without a device.

Extensions live in their own folders and repositories, with their own tests
(sdk.py test --extension). The SDK's probe extension (tests/fixtures/extension_probe)
exercises the adapter here.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
PROBE = ROOT / 'tests/fixtures/extension_probe'


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


def local_includes(text):
    """A platform unit copied beside the test, including the SDK's shared headers by path."""
    for header in ('extension_runtime.hpp', 'bar_loop.hpp'):
        text = text.replace('#include "../' + header + '"', '#include <' + header + '>')
    return text


def mapped_stock(folder):
    """stock.hpp with every absolute address mapped through host_stock_address."""
    stock = (PLATFORM / 'stock.hpp').read_text()
    stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                          'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
    for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
        stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                              'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
    (folder / 'stock.hpp').write_text(stock)


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

    def test_loop_bars_grant_validation_and_ending(self):
        self.run_fixture('extension_loop_runtime')

    def test_bar_loop_hooks_fold_split_and_end_with_stock_doubles(self):
        def prepare(folder):
            mapped_stock(folder)
            (folder / 'extension_loop.cpp').write_text(local_includes((PLATFORM / 'extension_loop.cpp').read_text()))
        self.run_fixture('extension_loop', prepare)

    def test_generated_registry_omits_disabled_extensions_and_preserves_order(self):
        from tooling.extensions import Extension, generate_collection
        def prepare(folder):
            generate_collection(folder, [
                Extension('capture', PROBE, ('record_double_tap',), ('edit_notes',), {}),
                Extension('observer', PROBE, (), (), {}),
            ])
        self.run_fixture('extension_selection', prepare)

    def test_stock_adapter_events_keys_and_writes_with_the_probe_subscribed(self):
        from tooling.extensions import extension_at, generate_collection, generate_extension
        def prepare(folder):
            mapped_stock(folder)
            for unit in ('extension_stock.cpp', 'extension_loop.cpp'):
                (folder / unit).write_text(local_includes((PLATFORM / unit).read_text()))
            probe = extension_at(PROBE)
            generate_collection(folder, [probe])
            generate_extension(folder, probe)
        self.run_fixture('extension_stock', prepare)
