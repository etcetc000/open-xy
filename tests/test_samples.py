"""Sample-playback engines: flag-1 registration, stock-page readiness and dispatch."""
import json
import copy
import shutil
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

from tooling import adapters
from tooling import machines
from tooling.definitions import ANALYSIS_HEAP_LIMIT, ROOT, validate_definition
from tooling import firmware
from machine_fixtures import FOLDER, fixture


class Generation(unittest.TestCase):
    def test_optional_services_are_explicit_integer_flags(self):
        definition = copy.deepcopy(fixture('probesample').definition)
        for key in ('sampleAnalysis', 'chokeSameKey', 'padSettings', 'drumControls', 'stereoOutput', 'sliceTiming'):
            for value in (True, -1, 2, '1', None):
                invalid = copy.deepcopy(definition)
                invalid['resources'][key] = value
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    validate_definition(invalid, 'sample')
        for value in (1, 4092, 4097, 2 * 1024 * 1024 + 4):
            invalid = copy.deepcopy(definition)
            invalid['resources']['sampleAnalysis'] = value
            with self.subTest(heap=value), self.assertRaises(ValueError):
                validate_definition(invalid, 'sample')
        drum_only = copy.deepcopy(definition)
        del drum_only['resources']['padSettings']
        with self.assertRaisesRegex(ValueError, 'padSettings'):
            validate_definition(drum_only, 'sample')
        timed = copy.deepcopy(definition)
        timed['resources']['sliceTiming'] = 1
        validate_definition(timed, 'sample')
        timed['resources']['drumControls'] = 0
        with self.assertRaisesRegex(ValueError, 'sliceTiming needs drumControls'):
            validate_definition(timed, 'sample')
        definition['resources'] = {'voiceCapacity': 256}
        validate_definition(definition, 'sample')
        # Stock passes its 27 voice wrappers' numbers as indices: 27 covers them.
        for capacity in (27, 32):
            definition['resources'] = {'voiceCapacity': capacity}
            validate_definition(definition, 'sample')
        definition['resources'] = {'voiceCapacity': 26}
        with self.assertRaisesRegex(ValueError, 'voiceCapacity 27 or more'):
            validate_definition(definition, 'sample')

    def test_a_refused_index_gets_an_inert_object_only_below_256(self):
        plain = fixture('probesample')
        def adapter(capacity):
            m = replace(plain, identity=dict(engine=46, page=2),
                        definition=dict(plain.definition, resources=dict(plain.definition['resources'], voiceCapacity=capacity)))
            with tempfile.TemporaryDirectory() as directory:
                return adapters.generate_sample_voice(directory, m).read_text(encoding='utf-8')
        full = adapter(256)
        self.assertIn('    if (index >= 256) return nullptr;', full)
        self.assertNotIn('refused', full)
        small = adapter(32)
        self.assertIn('    if (index >= 32) return refused();', small)
        self.assertIn('probesample_voice_refusals', small)
        # Apart from the refusal, the adapter is the same text.
        stereo = plain.definition['resources'].get('stereoOutput', 0)
        self.assertEqual(small.replace(adapters.sample_refusal('probesample', stereo), '')
                         .replace('if (index >= 32) return refused();', 'if (index >= 256) return nullptr;')
                         .replace('voices[32]', 'voices[256]'), full)

    def test_analysis_budgets_share_one_allowance_on_the_device(self):
        # The cache holds every machine's analyses within one allowance (fixtures/sample_cache.cpp
        # exercises it), so a collection's budgets may add up to more than the device will hold.
        cache = (ROOT / 'sdk/platform/opxy-1.2.1/sample_cache.cpp').read_text(encoding='utf-8')
        self.assertEqual(ANALYSIS_HEAP_LIMIT, 2 * 1024 * 1024)
        self.assertIn('constexpr u32 kAllowanceBytes = 2u * 1024u * 1024u;', cache)
        with tempfile.TemporaryDirectory() as temporary:
            entries = []
            for number, name in enumerate(('probeone', 'probetwo')):
                folder = Path(temporary).resolve() / name
                shutil.copytree(FOLDER / 'probesample', folder)
                definition = json.loads((folder / 'definition.json').read_text(encoding='utf-8'))
                definition['id'] = name
                definition['resources']['sampleAnalysis'] = ANALYSIS_HEAP_LIMIT
                (folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
                record = json.loads((folder / 'machine.json').read_text(encoding='utf-8'))
                record['identity'] = dict(engine=47 + number, page=7)
                (folder / 'machine.json').write_text(json.dumps(record), encoding='utf-8')
                entries.append({'path': str(folder)})
            budgets = [m.definition['resources']['sampleAnalysis'] for m in machines.collection(entries)]
        self.assertEqual(budgets, [ANALYSIS_HEAP_LIMIT, ANALYSIS_HEAP_LIMIT])

    def test_absolute_calls_reject_invalid_width_opcode_padding_and_alignment(self):
        for target in (0, 0x8d450380, 0xfffffffe):
            self.assertEqual(firmware.absolute_target(firmware.absolute_call(target)), target)
        for target in (-2, 1, 0x100000000):
            with self.assertRaises(ValueError): firmware.absolute_call(target)
        valid = firmware.absolute_call(0x8d450380)
        for bad in (valid[:-1], valid + b'\0', b'\1' + valid[1:], valid[:4] + b'\x61' + valid[5:], valid[:-1] + b'\1'):
            with self.assertRaises(ValueError): firmware.absolute_target(bad)

    def test_dispatch_table_is_generated_for_any_sample_machines(self):
        first = fixture('probesample')
        second = replace(first, id='slicer', identity=dict(engine=46, page=2))
        with tempfile.TemporaryDirectory() as directory:
            others = [fixture('probesynth'), fixture('probefx')]
            text = adapters.generate_sample_callbacks(directory, [first, second, *others]).read_text()
        # Engine IDs are symbols the linker supplies; each machine keeps its stock page.
        self.assertIn('{probesample_engine_id, 7, probesample_ready, probesample_sample_defaults, ', text)
        self.assertIn('{slicer_engine_id, 2, slicer_ready, ', text)
        self.assertNotIn('probesynth', text)
        self.assertNotIn('probefx', text)

    def test_empty_collection_dispatch_remains_stock(self):
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_sample_callbacks(directory, []).read_text()
        self.assertNotIn('_ready', text)

    def test_slice_storage_covers_all_compiled_machines(self):
        plain = fixture('probesample')
        resources = dict(plain.definition['resources'], sliceTiming=1)
        timed = replace(plain, id='timed', definition=dict(plain.definition, resources=resources))
        for machines, expected in [([], 0), ([plain], 0),
                                   ([fixture('probesynth'), fixture('probefx')], 0),
                                   ([timed], 1), ([plain, timed], 1), ([timed, plain], 1)]:
            with self.subTest(machines=[m.id for m in machines]), tempfile.TemporaryDirectory() as directory:
                text = adapters.generate_sample_slice_bridge(directory, machines).read_text()
                self.assertIn(f'#define OPXY_SAMPLE_SLICE_TIMING {expected}\n', text)
                self.assertIn('#include "drum_controls_stock.cpp"', text)

    def test_engine_units_expand_every_placeholder(self):
        m = replace(fixture('probesample'), identity=dict(engine=46, page=2))
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('@', adapters.generate_sample_voice(directory, m).read_text(encoding='utf-8'))

    def test_registration_uses_flag_one_and_the_stock_page(self):
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_registration(directory, fixture('probesample')).read_text(encoding='utf-8')
        self.assertIn('probesample_engine_id, probesample_page_id, 1};', text)
        self.assertIn('int probesample_ready() { return sdk_engine_ready(&record, nullptr); }', text)
        self.assertNotIn('screen', text)

    def test_a_drawing_engine_stays_on_the_samplers_page(self):
        m = replace(fixture('probesample'), display='drawing', identity=dict(engine=46, page=90))
        self.assertEqual(m.sample_page, 2)
        with tempfile.TemporaryDirectory() as directory:
            self.assertIn('probesample_engine_id, 2, probesample_ready',
                          adapters.generate_sample_callbacks(directory, [m]).read_text(encoding='utf-8'))
        self.assertEqual(fixture('probesample').sample_page, fixture('probesample').identity['page'])

    def test_catalog_entry_and_stock_page_startup(self):
        entry = json.loads(adapters.catalog_entry(fixture('probesample')).strip().lstrip(','))
        self.assertEqual(entry['type'], 'probesample')


if __name__ == '__main__':
    unittest.main()
