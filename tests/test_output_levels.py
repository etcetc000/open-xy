"""Stock's output and input levels on a screen (docs/display.md#output-levels): raw words,
read from stock's level table only by a display that declares outputLevels."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tooling import adapters, definitions, native, preview
from tooling.machines import machine_at
import test_display_extension as fixture
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
SOURCE = '''#include "display.h"
namespace probesynth {
inline void display_reset(DisplayState& state) { state.frames = 0; }
inline void display(DisplayState& state, const opxy::draw::WithOutputLevels<opxy::draw::AudioInput>& input, Frame& frame) {
    ++state.frames;
    const opxy::i32 x = input.levels.available ? (input.levels.output_left & 2047) : -1;
    opxy::draw::affine(frame, 0, opxy::draw::Matrix{1025, 3, -5, 512, x, 0});
    opxy::draw::affine(frame, 1, opxy::draw::Matrix{-1024, 0, 0, 0, 0, 1024});
    for (unsigned i = 0; i < 4; ++i) opxy::draw::header(frame, i, "");
}
}
'''
HOST = r'''#include "output_levels.hpp"
namespace host { opxy::i32 table[40]; unsigned char role; }
namespace opxy { namespace platform {
Address host_stock_address(Address address) {
    if (address >= 0x080376e4u && address < 0x080376e4u + 40 * 4)
        return reinterpret_cast<Address>(&host::table[(address - 0x080376e4u) / 4]);
    if (address == 0x86c05ca4u) return reinterpret_cast<Address>(&host::role);
    return 0;
}
}}
int main() {
    for (int i = 0; i < 40; ++i) host::table[i] = 1000 + i;
    host::table[1] = -7;
    if (opxy::platform::output_levels().available) return 1;
    host::role = 1;
    const opxy::draw::OutputLevels l = opxy::platform::output_levels();
    if (!l.available || l.output_left != 1000 || l.output_right != -7 || l.input != 1002) return 2;
    return 0;
}
'''


def write_fixture(folder):
    machine = fixture.write_fixture(folder)
    path = folder / 'definition.json'
    value = json.loads(path.read_text())
    value['resources']['display']['outputLevels'] = 1
    path.write_text(json.dumps(value, indent=2) + '\n')
    (folder / 'display.h').write_text(fixture.DRAW_HEADER.replace('<opxy/draw.hpp>', '<opxy/output_levels.hpp>'))
    (folder / 'display.cpp').write_text(SOURCE)
    return machine_at(machine.root)


class OutputLevels(unittest.TestCase):
    run_command = fixture.DisplayExtension.run_command

    def test_declaration_input_and_native_binding(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            machine = write_fixture(folder / 'machine')
            for invalid in (2, True):
                trial = copy.deepcopy(machine.definition)
                trial['resources']['display']['outputLevels'] = invalid
                with self.assertRaises(ValueError):
                    definitions.validate_definition(trial, 'synth')
            self.assertEqual(adapters.display_extension(machine.definition), 9)
            adapters.generate_display(folder, definition=machine.definition)
            header = (folder / 'display_binding.hpp').read_text()
            self.assertIn('using DisplayInput = draw::WithOutputLevels<draw::AudioInput>;', header)
            # Read only on the device: the binding includes stock's table only there.
            self.assertIn('#if defined(__bfin__)\n#include "output_levels.hpp"\nnamespace opxy { namespace binding {\n'
                          'inline void snapshot_levels(DisplayInput& input) { input.levels = platform::output_levels(); }',
                          header)
            # The levels are an input, not a renderer option.
            display = machine.definition['resources']['display']
            self.assertNotIn('outputLevels', preview.drawing_bindings(native.display_ids(machine.prefix, display), display))
            plain = copy.deepcopy(machine.definition)
            del plain['resources']['display']['outputLevels']
            adapters.generate_display(folder, definition=plain)
            self.assertNotIn('output_levels', (folder / 'display_binding.hpp').read_text().split('#include <opxy/output_levels.hpp>')[1])
            # An SDK before 1.26 does not have the key.
            record = json.loads((machine.root / 'machine.json').read_text())
            record['sdk'] = '1.25'
            (machine.root / 'machine.json').write_text(json.dumps(record))
            with self.assertRaises(ValueError):
                machine_at(machine.root)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_the_device_reads_three_table_words_on_the_gui_processor_only(self):
        platform = ROOT / 'sdk/platform/opxy-1.2.1'
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            stock = (platform / 'stock.hpp').read_text()
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile u8*', 'const volatile unsigned char*'):
                stock = stock.replace(f'reinterpret_cast<{pointer}>(address)',
                                      f'reinterpret_cast<{pointer}>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock)
            (folder / 'output_levels.hpp').write_bytes((platform / 'output_levels.hpp').read_bytes())
            (folder / 'test.cpp').write_text(HOST)
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            self.run_command([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(folder),
                              '-I' + str(ROOT / 'sdk/include'), '-I' + str(platform), folder / 'test.cpp', '-o', binary])
            self.run_command([binary])

    @unittest.skipUnless(shutil.which('clang++') and shutil.which('node'), 'WASM compiler and Node required')
    def test_wasm_levels_reach_the_display(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            machine = write_fixture(folder / 'machine')
            out = folder / 'preview'; out.mkdir()
            preview.compile_preview('clang++', machine.definition, out, machine.root)
            self.run_command(['node', ROOT / 'tools/check_output_levels.mjs', out / 'engine.wasm'])


if __name__ == '__main__':
    unittest.main()
