"""Execute drum GUI routing with real SDK controls and stock boundary doubles."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from types import SimpleNamespace

from tooling.adapters import generate_drum_callbacks

from test_sample_services import HOST_CXX, ROOT


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class DrumServices(unittest.TestCase):
    def test_generated_classification_does_not_require_gui_screen(self):
        platform = ROOT / 'sdk/platform/opxy-1.2.1'
        machine = SimpleNamespace(kind='drum', prefix='probe', definition={'resources': {'modelCount': 12}})
        with tempfile.TemporaryDirectory(prefix='drum-classification-') as folder:
            folder = Path(folder)
            generated = generate_drum_callbacks(folder, [machine])
            binary = folder / ('classification.exe' if os.name == 'nt' else 'classification')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                       '-I' + str(ROOT / 'sdk/include'), '-I' + str(platform),
                       str(generated), str(platform / 'drum_machines.cpp'),
                       str(ROOT / 'tests/fixtures/drum_classification.cpp'), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_pad_edit_alternates_shift_and_model_changes(self):
        platform = ROOT / 'sdk/platform/opxy-1.2.1'
        with tempfile.TemporaryDirectory(prefix='drum-services-') as folder:
            folder = Path(folder)
            stock = (platform / 'stock.hpp').read_text()
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock)
            # Keep the shared inline decoder beside the mocked stock header.
            (folder / 'drum_values.hpp').write_text((platform / 'drum_values.hpp').read_text())
            sources = []
            for name in ('synth_controls.cpp', 'drum_synthesis_stock.cpp', 'drum_values_stock.cpp', 'drum_machines.cpp'):
                target = folder / name
                target.write_text((platform / name).read_text())
                sources.append(str(target))
            binary = folder / ('drum_gui.exe' if os.name == 'nt' else 'drum_gui')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(folder),
                       '-I' + str(ROOT / 'sdk/include'), '-I' + str(platform),
                       str(ROOT / 'tests/fixtures/drum_gui.cpp'), *sources, '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
