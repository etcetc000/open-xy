"""Shared preview contracts for models with a drum-sized state pool."""
import copy
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from machine_fixtures import FOLDER
from tooling import adapters, preview


class DrumPreviewTests(unittest.TestCase):
    def definition(self, kind):
        definition = json.loads((FOLDER / 'probesynth/definition.json').read_text())
        if kind == 'drum':
            definition['presets'] = {}
            for control, parameter in enumerate(copy.deepcopy(definition['parameters'])):
                parameter['id'] += '_alt'
                parameter['label'] = 'Alt ' + str(control)
                parameter['control'] = control
                definition['parameters'].append(parameter)
            definition['resources'].update(modelCount=12, sharedBytes=700000)
        return definition

    def test_a_drum_kits_press_always_pages_its_whole_row(self):
        from tooling.definitions import validate_definition
        machine = SimpleNamespace(kind='drum', prefix='kit', definition=self.definition('drum'), engine=None,
                                  native_name='kit', id='kit')
        with tempfile.TemporaryDirectory() as folder:
            table = adapters.generate_synth_controls(folder, [machine]).read_text()
        self.assertRegex(table, r'kit_format, false, true[,}]')
        definition = self.definition('drum')
        definition['press'] = 'page'   # nothing to choose
        with self.assertRaisesRegex(ValueError, "a drum kit's press always pages its row"):
            validate_definition(definition, 'drum')

    def test_model_groups_name_consecutive_models_that_add_up_to_the_kit(self):
        from tooling.definitions import validate_definition
        definition = self.definition('drum')
        definition['resources'].update(modelCount=33, modelGroups=[
            dict(label='TRX', count=13), dict(label='EFM', count=8), dict(label='P-I', count=9), dict(label='GND', count=3)])
        validate_definition(definition, 'drum')
        machine = SimpleNamespace(kind='drum', prefix='kit', definition=definition)
        with tempfile.TemporaryDirectory() as folder:
            table = adapters.generate_drum_callbacks(folder, [machine]).read_text()
        self.assertIn('static const opxy::u8 kit_group_sizes[] = {13, 8, 9, 3};', table)
        self.assertIn('kit_group_names[] = {"TRX", "EFM", "P-I", "GND"};', table)
        self.assertIn('kit_drum_count, 4, kit_group_sizes, kit_group_names, 0, 0, nullptr, 0, nullptr, nullptr},', table)
        for groups in ([dict(label='ALL', count=33)], [dict(label='A', count=13), dict(label='B', count=19)],
                       [dict(label='A', count=33), dict(label='B', count=0)], [dict(label='A_B', count=30), dict(label='C', count=3)],
                       [dict(label='NINE CHAR', count=30), dict(label='C', count=3)],
                       [dict(label='A', count=30), dict(label='C', count=3, extra=1)]):
            definition['resources']['modelGroups'] = groups
            with self.subTest(groups=groups), self.assertRaises(ValueError):
                validate_definition(definition, 'drum')
        plain = SimpleNamespace(kind='drum', prefix='plain', definition=self.definition('drum'))
        with tempfile.TemporaryDirectory() as folder:
            self.assertIn('plain_drum_count, 0, nullptr, nullptr, 0, 0, nullptr, 0, nullptr, nullptr},', adapters.generate_drum_callbacks(folder, [plain]).read_text())

    def test_choke_same_key_is_a_drum_flag_and_not_a_synths(self):
        from tooling.definitions import validate_definition
        for value in (0, 1):
            definition = self.definition('drum')
            definition['resources']['chokeSameKey'] = value
            validate_definition(definition, 'drum')
            machine = SimpleNamespace(kind='drum', prefix='kit', definition=definition, native_name='kit')
            with tempfile.TemporaryDirectory() as folder:
                source = adapters.generate_voice(folder, machine).read_text()
            self.assertIn('const bool kChoke = %s;' % ('true' if value else 'false'), source)
        for value in (2, -1, True, '1'):
            definition = self.definition('drum')
            definition['resources']['chokeSameKey'] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                validate_definition(definition, 'drum')
        definition = self.definition('synth')
        definition['resources']['chokeSameKey'] = 1
        with self.assertRaises(ValueError):
            validate_definition(definition, 'synth')

    def test_pad_mix_is_a_drum_flag_in_the_table_and_the_voice(self):
        from tooling.definitions import validate_definition
        for value in (0, 1):
            definition = self.definition('drum')
            definition['resources']['padMix'] = value
            validate_definition(definition, 'drum')
            machine = SimpleNamespace(kind='drum', prefix='kit', definition=definition, native_name='kit')
            with tempfile.TemporaryDirectory() as folder:
                self.assertIn('const bool kPadMix = %s;' % ('true' if value else 'false'),
                              adapters.generate_voice(folder, machine).read_text())
                table = adapters.generate_drum_callbacks(folder, [machine]).read_text()
                self.assertIn('kit_drum_count, 0, nullptr, nullptr, %d, 0, nullptr, 0, nullptr, nullptr},' % value, table)
                # padMix implies the per-key sends: its Shift row edits FX I and FX II.
                self.assertEqual('{kit_vtable, kit_drum_send},' in table, bool(value))
                self.assertNotIn('kPadSend', adapters.generate_voice(folder, machine).read_text())
        for value in (2, -1, True, '1'):
            definition = self.definition('drum')
            definition['resources']['padMix'] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                validate_definition(definition, 'drum')
        definition = self.definition('synth')
        definition['resources']['padMix'] = 1
        with self.assertRaises(ValueError):
            validate_definition(definition, 'synth')

    def test_pad_controls_name_up_to_two_fields_in_nine_bits(self):
        from tooling.definitions import validate_definition
        fields = [dict(label='FILTER', bits=5, signed=True), dict(label='SRR', bits=4)]
        definition = self.definition('drum')
        definition['resources'].update(padMix=1, padControls=fields)
        validate_definition(definition, 'drum')
        machine = SimpleNamespace(kind='drum', prefix='kit', definition=definition, native_name='kit')
        with tempfile.TemporaryDirectory() as folder:
            table = adapters.generate_drum_callbacks(folder, [machine]).read_text()
            voice = adapters.generate_voice(folder, machine).read_text()
        self.assertIn('static const opxy::u8 kit_pad_bits[] = {5, 4};', table)
        self.assertIn('kit_pad_labels[] = {"FILTER", "SRR"};', table)
        self.assertIn('kit_drum_count, 0, nullptr, nullptr, 1, 2, kit_pad_bits, 1, kit_pad_labels, kit_drum_pad_text},', table)
        self.assertIn('const opxy::u32 kPadCount = 2;', voice)
        self.assertIn('static const opxy::u8 pad_bits[] = {5, 4};', voice)
        self.assertIn('const opxy::u32 kPadSigns = 1;', voice)
        bad = ([], fields + [dict(label='SEND', bits=3)], [dict(label='WIDE', bits=9), dict(label='B', bits=1)],
               [dict(label='A', bits=5), dict(label='B', bits=5)], [dict(label='TOO', bits=9, extra=0)],
               [dict(label='one', bits=1, signed=True)], [dict(label='bad_name', bits=2)], [dict(label='NINE CHAR', bits=2)],
               [dict(label='X', bits=2, extra=1)], [dict(label='X', bits=True)], [dict(bits=2)], 'FILTER',
               # The display's pad input is a signed byte: an unsigned field fits 7 bits.
               [dict(label='WIDE', bits=8)], [dict(label='X', bits=2, signed=1)])
        for good in ([dict(label='S8', bits=8, signed=True)], [dict(label='U7', bits=7), dict(label='B', bits=2)]):
            definition['resources']['padControls'] = good
            with self.subTest(good=good):
                validate_definition(definition, 'drum')
        for value in bad:
            definition['resources']['padControls'] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                validate_definition(definition, 'drum')
        definition['resources'].update(padMix=0, padControls=fields)
        with self.assertRaisesRegex(ValueError, 'needs padMix'):
            validate_definition(definition, 'drum')

    def test_drum_adapters_validate_model_count_without_changing_synth_defaults(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            definition = self.definition('drum')
            adapters.generate_voice_preview(root, definition, kind='drum')
            adapters.generate_display(root, definition=definition, kind='drum')
            self.assertIn('700000', (root / 'voice_preview.cpp').read_text())
            definition['resources']['modelCount'] = 65
            with self.assertRaisesRegex(ValueError, '2..64'):
                adapters.generate_voice_preview(root, definition, kind='drum')
            with self.assertRaises(ValueError):
                adapters.generate_display(root, definition=definition, kind='drum')
            adapters.generate_voice_preview(root, self.definition('synth'))

    def test_drum_requires_a_storable_model_count_and_two_functions_per_encoder(self):
        from tooling.definitions import validate_definition
        validate_definition(self.definition('drum'), 'drum')
        for mutation in ('count', 'alternate'):
            definition = self.definition('drum')
            if mutation == 'count':
                definition['resources']['modelCount'] = 1
            else:
                definition['parameters'][4]['control'] = definition['parameters'][5]['control']
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                validate_definition(definition, 'drum')

    def test_drum_preview_links_all_sources_with_fixed_memory(self):
        for kind, memory in [('synth', 2097152), ('drum', 2097152)]:
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                with patch('tooling.preview.subprocess.run', return_value=SimpleNamespace(returncode=0, stderr='')) as run, \
                        patch('tooling.preview.check_runtime') as check:
                    sources = tuple(f'port{i}.cpp' for i in range(30))
                    preview.compile_preview('clang++', self.definition(kind), root, root, sources, kind=kind)
                    command = run.call_args.args[0]
                    self.assertIn(f'-Wl,--initial-memory={memory}', command)
                    self.assertIn(f'-Wl,--max-memory={memory}', command)
                    for source in sources:
                        self.assertIn(str(root / source), command)
                    display = self.definition(kind)['resources']['display']
                    check.assert_called_once_with(root / 'engine.wasm', display.get('paths', 0), display.get('points', 0))


if __name__ == '__main__':
    unittest.main()
