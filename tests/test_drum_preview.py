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
            definition['resources'].update(modelCount=30, sharedBytes=700000)
            definition['parameters'][0].update(mapping=dict(kind='enum', labels=[str(i) for i in range(30)]), display='enum')
        return definition

    def test_drum_adapters_validate_model_count_without_changing_synth_defaults(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            definition = self.definition('drum')
            adapters.generate_voice_preview(root, definition, kind='drum')
            adapters.generate_display(root, definition=definition, kind='drum')
            self.assertIn('700000', (root / 'voice_preview.cpp').read_text())
            definition['resources']['modelCount'] = 129
            with self.assertRaisesRegex(ValueError, '2..128'):
                adapters.generate_voice_preview(root, definition, kind='drum')
            with self.assertRaises(ValueError):
                adapters.generate_display(root, definition=definition, kind='drum')
            adapters.generate_voice_preview(root, self.definition('synth'))

    def test_drum_requires_one_model_selector_and_two_functions_per_encoder(self):
        from tooling.definitions import validate_definition
        for mutation in ('count', 'selector', 'alternate'):
            definition = self.definition('drum')
            if mutation == 'count':
                definition['resources']['modelCount'] = 29
            elif mutation == 'selector':
                definition['parameters'][0].update(mapping=dict(kind='linear', minimum=0, maximum=1), display='percent')
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
