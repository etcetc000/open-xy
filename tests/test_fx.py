"""Send effects: generated adapters and catalog entries."""
import json
import os
import re
from dataclasses import replace
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling import adapters, definitions
from tooling.definitions import ROOT
from tooling.native import CXX_FLAGS, extend_catalog
from test_divide import blackfin_compiler
from test_sample_services import HOST_CXX
from machine_fixtures import fixture

ROOT = Path(__file__).resolve().parents[1]


def stock_like_catalog():
    return (b'{\n\t"engine_types": [\n        {\n            "type": "a",\n            "default_params": [0]\n        }\n    ],\n'
            b'\t"fx_types": [\n        {\n            "type": "delay",\n            "id": 0,\n'
            b'            "default_params": [1, 2, 3, 4, 0, 0, 0, 0]\n        }\n    ],\n\t"seq_types": [\n    ],\n\t"x": 1\n}\n')


class Generation(unittest.TestCase):
    def test_registration_describes_the_effect_record(self):
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_registration(directory, fixture('probefx')).read_text(encoding='utf-8')
        self.assertIn('Record record = {probefx_fx_factory, probefx_fx_parameter_name, probefx_fx_name, '
                      'probefx_fx_id, probefx_fx_page_id, 0};', text)
        self.assertIn('int probefx_fx_register() { return sdk_register_effect(&record); }', text)
        for engine_only in ('sdk_register_engine', 'sdk_engine_ready', 'screen'):
            self.assertNotIn(engine_only, text)

    def test_object_adapter_fits_the_stock_slot(self):
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_fx_object(directory, fixture('probefx')).read_text(encoding='utf-8')
        self.assertIn('static_assert(sizeof(State) <= opxy::effect_state_bytes', text)
        self.assertIn('static_assert(sizeof(Object) <= 480', text)
        self.assertIn('const void* const probefx_fx_vtable[8]', text)
        labels = ', '.join('"%s"' % p['label'].lower() for p in fixture('probefx').definition['parameters'])
        self.assertIn('{%s}' % labels, text)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_stock_dry_query_uses_declared_slot_or_unity(self):
        # Execute the generated vtable with a trivial wet DSP. Only widen the
        # object header for the host; the native build retains the 4-byte check.
        for slot in (None, 0, 3, 7):
            with self.subTest(slot=slot), tempfile.TemporaryDirectory() as directory:
                folder = Path(directory)
                m = replace(fixture('probefx'), dry_parameter=slot)
                generated = adapters.generate_fx_object(folder, m)
                text = generated.read_text().replace('__builtin_offsetof(Object, state) == 4',
                                                     '__builtin_offsetof(Object, state) == sizeof(void*)')
                generated.write_text(text)
                (folder / 'fx.cpp').write_text('''#include <opxy/effect.hpp>
namespace probefx {
struct State { unsigned initialized; };
void initialize(State& state) { state.initialized = 1; }
void process(State& state, opxy::Channel left, opxy::Channel right, opxy::Parameters) {
    if (state.initialized) { left[0] = 123; right[0] = -456; }
}
}
''')
                source = (ROOT / 'tests/fixtures/fx_object.cpp').read_text()
                (folder / 'test.cpp').write_text(source.replace('@SLOT@', str(-1 if slot is None else slot)))
                binary = folder / ('test.exe' if os.name == 'nt' else 'test')
                hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
                command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                           '-I' + str(ROOT / 'sdk/include'), str(folder / 'test.cpp'), '-o', str(binary)]
                result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_a_buffered_effect_gets_its_send_slots_scratch(self):
        definition = dict(fixture('probefx').definition, resources={'bufferBytes': 64})
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            generated = adapters.generate_fx_object(folder, replace(fixture('probefx'), definition=definition))
            text = generated.read_text()
            self.assertIn('static_assert(sizeof(State) <= opxy::effect_buffered_state_bytes', text)
            generated.write_text(text.replace('__builtin_offsetof(Object, state) == 4',
                                              '__builtin_offsetof(Object, state) == sizeof(void*)'))
            # Stock functions resolve through host_stock_address on the host.
            stock = (ROOT / 'sdk/platform/opxy-1.2.1/stock.hpp').read_text()
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            stock = stock.replace('reinterpret_cast<T*>(address)', 'reinterpret_cast<T*>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock)
            (folder / 'fx.cpp').write_text('''#include <opxy/effect.hpp>
namespace probefx {
struct State { unsigned initialized; };
void initialize(State& state) { state.initialized = 1; }
void process(State&, opxy::Buffer buffer, opxy::Channel left, opxy::Channel, opxy::Parameters) {
    left[0] = static_cast<opxy::i32>(buffer.count);
    if (buffer.words) buffer.words[0] = 7;
}
}
''')
            (folder / 'test.cpp').write_text((ROOT / 'tests/fixtures/fx_buffer.cpp').read_text())
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(folder),
                       '-I' + str(ROOT / 'sdk/include'), str(folder / 'test.cpp'), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, 'fixture step %d failed' % result.returncode)

    def test_a_shift_row_reaches_the_dsp_as_four_more_words(self):
        enum = lambda i, labels: dict(id=f's{i}', label=f'S {i}', default=0, display='enum',
                                      mapping=dict(kind='enum', labels=labels))
        definition = dict(fixture('probefx').definition, shift=[enum(0, ['a', 'b', 'c']), enum(1, ['x', 'y'])])
        effect = replace(fixture('probefx'), definition=definition)
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_fx_object(Path(directory), effect).read_text()
            table = adapters.generate_synth_controls(Path(directory), [effect]).read_text()
        # The slot finds the send's packed word on the audio side; the DSP gets 12 words.
        self.assertIn('struct Object { const void* const* vtable; State state; opxy::u32 slot; };', text)
        self.assertIn('static_assert(sizeof(State) + 4 <= opxy::effect_state_bytes', text)
        self.assertIn('object->slot = slot;', text)
        self.assertIn('static const opxy::u8 kShiftChoices[2] = {3, 2};', text)
        self.assertIn('process(object->state, left, right, words)', text)
        # The control table names the row and captions its choices as slots 8 and 9.
        self.assertIn('static const char* const probefx_shift_titles[] = {"s 0", "s 1"};', table)
        self.assertIn('probefx_format, true, false, 0, 0, 2, {3, 2, 1, 1}, probefx_shift_titles},', table)
        self.assertIn('case 8: {', table)
        self.assertIn('case 9: {', table)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_a_shift_row_packs_and_unpacks_every_choice(self):
        source = r'''#include "shift_row.hpp"
#include <cstdio>
using namespace opxy;
namespace row = opxy::platform::shift_row;
int main() {
    const u8 choices[4] = {6, 4, 4, 3};
    for (u32 a = 0; a < 6; ++a) for (u32 b = 0; b < 4; ++b) for (u32 c = 0; c < 4; ++c) for (u32 d = 0; d < 3; ++d) {
        const u32 fields[4] = {a, b, c, d};
        u32 back[4];
        row::decode(row::encode(fields, choices, 4), choices, 4, back);
        for (u32 i = 0; i < 4; ++i) {
            if (back[i] != fields[i]) { std::printf("field %u of %u %u %u %u", i, a, b, c, d); return 1; }
            // A machine reads each choice's word as any enum control.
            const i32 word = row::midpoint(back[i], choices[i]);
            if ((((static_cast<u32>(word) >> 16) * choices[i]) >> 15) != fields[i]) return 2;
        }
    }
    u32 low[4];
    row::decode(0, choices, 4, low);  // a fresh project's word: every first choice
    return low[0] || low[1] || low[2] || low[3] ? 3 : 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / 'test.cpp').write_text(source)
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(ROOT / 'sdk/include'),
                       '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'), str(folder / 'test.cpp'), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + 'step %d' % result.returncode)

    def test_a_tempo_effect_reads_the_audio_transport(self):
        definition = dict(fixture('probefx').definition, resources={'transport': 1})
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_fx_object(Path(directory), replace(fixture('probefx'), definition=definition)).read_text()
            self.assertIn('#include "stock.hpp"', text)
            self.assertIn('process(object->state, opxy::platform::audio_transport(), left, right, parameters)', text)
            plain = adapters.generate_fx_object(Path(directory), fixture('probefx')).read_text()
            self.assertIn('process(object->state, left, right, parameters)', plain)

    def test_a_bus_shift_is_0_to_12_bits_on_effects_only(self):
        base = fixture('probefx').definition
        for n in (0, 1, 9, 12):
            definitions.validate_definition(dict(base, resources={'busShift': n}), 'effect')
        for n in (-1, 13, '9', 9.0, True):
            with self.subTest(n=n), self.assertRaises(ValueError):
                definitions.validate_definition(dict(base, resources={'busShift': n}), 'effect')
        with self.assertRaises(ValueError):
            definitions.validate_definition(dict(base, resources={'busShift': 9}), 'player')

    def test_without_a_bus_shift_the_object_source_is_unchanged(self):
        effect = lambda **resources: replace(fixture('probefx'), definition=dict(fixture('probefx').definition,
                                                                                 resources=resources))
        with tempfile.TemporaryDirectory() as directory:
            for resources in ({}, {'bufferBytes': 64, 'transport': 1}):
                plain = adapters.generate_fx_object(Path(directory), effect(**resources), benchmark=True).read_text()
                zero = adapters.generate_fx_object(Path(directory), effect(busShift=0, **resources), benchmark=True).read_text()
                self.assertEqual(plain, zero)
                self.assertNotIn('send_bus', plain)

    def test_a_bus_shift_surrounds_process(self):
        enum = dict(id='s0', label='S 0', default=0, display='enum', mapping=dict(kind='enum', labels=['a', 'b']))
        definition = dict(fixture('probefx').definition, shift=[enum], resources={'busShift': 9})
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_fx_object(Path(directory), replace(fixture('probefx'), definition=definition)).read_text()
        self.assertIn('#include "send_bus.hpp"', text)
        body = text[text.index('static void process('):text.index('void* probefx_fx_factory(void* storage)')]
        # The audio is raised before process and lowered after it; the shift row's words are untouched.
        order = ['send_bus::raise<9>(left, right);', 'probefx::process(object->state', 'send_bus::lower<9>(left, right);']
        self.assertEqual([body.index(s) for s in order], sorted(body.index(s) for s in order))
        self.assertEqual((body.count('send_bus::raise'), body.count('send_bus::lower')), (1, 1))

    @unittest.skipUnless(blackfin_compiler(), 'No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
    def test_the_device_raise_is_one_saturating_shift_a_word(self):
        # q31::shift_left's device form: ASHIFT ... (S) saturates where << would wrap.
        with tempfile.TemporaryDirectory() as directory:
            source, listing = Path(directory) / 'bus.cpp', Path(directory) / 'bus.s'
            source.write_text('#include "send_bus.hpp"\nusing opxy::i32;\n'
                              'void up(i32* l, i32* r) { opxy::platform::send_bus::raise<9>(l, r); }\n'
                              'i32 one(i32 x, opxy::u32 n) { return opxy::q31::shift_left(x, n); }\n')
            subprocess.run([str(blackfin_compiler()), *CXX_FLAGS, '-I', str(ROOT / 'sdk/include'),
                            '-I', str(ROOT / 'sdk/platform/opxy-1.2.1'), '-S', str(source), '-o', str(listing)], check=True)
            text = listing.read_text()
        self.assertEqual(len(re.findall(r'ASHIFT R\d BY R\d\.L \(S\)', text)), 3)  # two channels, and one()
        self.assertNotIn('call', text)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_a_bus_shift_raises_the_block_and_lowers_the_result(self):
        for n in (0, 1, 9, 12):
            with self.subTest(busShift=n), tempfile.TemporaryDirectory() as directory:
                folder = Path(directory)
                resources = {'busShift': n} if n else {}
                m = replace(fixture('probefx'), definition=dict(fixture('probefx').definition, resources=resources))
                generated = adapters.generate_fx_object(folder, m)
                generated.write_text(generated.read_text().replace('__builtin_offsetof(Object, state) == 4',
                                                                   '__builtin_offsetof(Object, state) == sizeof(void*)'))
                (folder / 'fx.cpp').write_text('''#include <opxy/effect.hpp>
namespace probefx {
opxy::i32 seen_left[64], seen_right[64], result_left[64], result_right[64];
bool pass;
struct State { unsigned blocks; };
void initialize(State& state) { state.blocks = 0; }
void process(State& state, opxy::Channel left, opxy::Channel right, opxy::Parameters) {
    ++state.blocks;
    for (unsigned i = 0; i < 64; ++i) {
        seen_left[i] = left[i];
        seen_right[i] = right[i];
        if (!pass) { left[i] = result_left[i]; right[i] = result_right[i]; }
    }
}
}
''')
                (folder / 'test.cpp').write_text((ROOT / 'tests/fixtures/fx_bus_shift.cpp').read_text())
                binary = folder / ('test.exe' if os.name == 'nt' else 'test')
                hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
                command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', f'-DSHIFT={n}',
                           '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'),
                           str(folder / 'test.cpp'), '-o', str(binary)]
                result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr + 'step %d' % result.returncode)

    def test_send_pages_name_only_division_controls(self):
        parameters = [dict(p) for p in fixture('probefx').definition['parameters']]
        parameters[0] = dict(parameters[0], display='division', mapping={'kind': 'linear', 'minimum': 0, 'maximum': 1})
        effect = replace(fixture('probefx'), definition=dict(fixture('probefx').definition, parameters=parameters))
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_synth_controls(Path(directory), [effect]).read_text()
            body = text[text.index('static bool probefx_format'):text.index('const SynthControls')]
            self.assertIn('case 0:\n        control_division(text, value);', body)
            self.assertNotIn('case 1', body)
            self.assertNotIn('control_tenths', body)

    def test_device_and_browser_name_divisions_alike(self):
        native = (ROOT / 'sdk/platform/opxy-1.2.1/synth_controls.cpp').read_text()
        browser = (ROOT / 'sdk/browser/controls.mjs').read_text()
        names = lambda text, pattern: re.findall(r'"([^"]+)"', re.search(pattern, text, re.S).group(1).replace("'", '"'))
        self.assertEqual(names(native, r'names\[8\] = \{(.*?)\};'), names(browser, r'DIVISIONS = \[(.*?)\];'))
        self.assertEqual(len(names(native, r'names\[8\] = \{(.*?)\};')), 8)

    def test_catalog_entry_is_appended_to_fx_types_only(self):
        catalog = stock_like_catalog()
        ends = [catalog.index(b'\n    ]', catalog.index(key)) for key in (b'"engine_types"', b'"fx_types"', b'"seq_types"')]
        profile = dict(stock_catalog_inserts=dict(zip(('engine_types', 'fx_types', 'seq_types'), ends)))
        parsed = json.loads(extend_catalog(catalog, profile, [fixture('probefx')]))
        self.assertEqual(parsed['fx_types'][-1], json.loads(adapters.catalog_entry(fixture('probefx'))[1:]))
        self.assertEqual(parsed['engine_types'], json.loads(catalog)['engine_types'])
        self.assertEqual(extend_catalog(catalog, profile, []), catalog)


if __name__ == '__main__':
    unittest.main()
