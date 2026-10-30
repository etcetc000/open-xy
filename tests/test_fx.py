"""Send effects: generated adapters and catalog entries."""
import json
import os
import re
from dataclasses import replace
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling import adapters
from tooling.native import extend_catalog
from test_sample_services import HOST_CXX
from machine_fixtures import fixture

ROOT = Path(__file__).resolve().parents[1]


def stock_like_catalog():
    return (b'{\n\t"engine_types": [\n        {\n            "type": "a",\n            "default_params": [0]\n        }\n    ],\n'
            b'\t"fx_types": [\n        {\n            "type": "delay",\n            "id": 0,\n'
            b'            "default_params": [1, 2, 3, 4, 0, 0, 0, 0]\n        }\n    ],\n\t"x": 1\n}\n')


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

    def test_a_tempo_effect_reads_the_audio_transport(self):
        definition = dict(fixture('probefx').definition, resources={'transport': 1})
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_fx_object(Path(directory), replace(fixture('probefx'), definition=definition)).read_text()
            self.assertIn('#include "stock.hpp"', text)
            self.assertIn('process(object->state, opxy::platform::audio_transport(), left, right, parameters)', text)
            plain = adapters.generate_fx_object(Path(directory), fixture('probefx')).read_text()
            self.assertIn('process(object->state, left, right, parameters)', plain)

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
        ends = [catalog.index(b'\n    ]', catalog.index(key)) for key in (b'"engine_types"', b'"fx_types"')]
        profile = dict(stock_catalog_inserts=dict(zip(('engine_types', 'fx_types'), ends)))
        parsed = json.loads(extend_catalog(catalog, profile, [fixture('probefx')]))
        self.assertEqual(parsed['fx_types'][-1], json.loads(adapters.catalog_entry(fixture('probefx'))[1:]))
        self.assertEqual(parsed['engine_types'], json.loads(catalog)['engine_types'])
        self.assertEqual(extend_catalog(catalog, profile, []), catalog)


if __name__ == '__main__':
    unittest.main()
