"""Sample-playback engines: flag-1 registration, stock-page readiness and dispatch."""
import json
import copy
import tempfile
import unittest
from dataclasses import replace

from tooling import adapters
from tooling.machines import check_analysis_budgets
from tooling.definitions import validate_definition
from tooling import firmware
from machine_fixtures import fixture


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

    def test_analysis_budgets_share_one_collection_limit(self):
        def analyzing(name, budget):
            definition = copy.deepcopy(fixture('probesample').definition)
            definition['resources']['sampleAnalysis'] = budget
            return replace(fixture('probesample'), id=name, definition=definition)
        check_analysis_budgets([analyzing('one', 1 << 20), analyzing('two', 1 << 20)])
        with self.assertRaisesRegex(ValueError, 'analysis budgets'):
            check_analysis_budgets([analyzing('one', 1 << 20), analyzing('two', (1 << 20) + 4)])

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

    def test_catalog_entry_and_stock_page_startup(self):
        entry = json.loads(adapters.catalog_entry(fixture('probesample')).strip().lstrip(','))
        self.assertEqual(entry['type'], 'probesample')


if __name__ == '__main__':
    unittest.main()
