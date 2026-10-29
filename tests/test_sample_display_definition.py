import copy
import json
from pathlib import Path
import shutil
import tempfile
import unittest

from tooling.definitions import ROOT, validate_definition
from tooling.machines import machine_at

class SampleDisplayDefinitionTests(unittest.TestCase):
    def definition(self):
        value = json.loads((ROOT / 'tests/fixtures/machines/probesample/definition.json').read_text())
        value['resources']['sampleDisplay'] = 1
        value['resources']['display'] = dict(stateBytes=4, stateAlignment=4, paths=0, points=0, nodes=1, texts=0, palette=1)
        return value

    def test_declaration_is_explicit_and_paired(self):
        value = self.definition()
        validate_definition(value, 'sample')
        for removed in ('sampleDisplay', 'display'):
            invalid = copy.deepcopy(value)
            del invalid['resources'][removed]
            with self.assertRaisesRegex(ValueError, 'declared together'):
                validate_definition(invalid, 'sample')

    def test_alternate_controls_are_required_and_sample_actions_rejected(self):
        value = self.definition()
        while len(value['parameters']) < 5:
            item = copy.deepcopy(value['parameters'][0])
            item['id'] = 'extra' + str(len(value['parameters']))
            value['parameters'].append(item)
        value['press'] = 'page'
        with self.assertRaisesRegex(ValueError, 'parameter fields'):
            validate_definition(value, 'sample')
        value['parameters'][4]['control'] = 0
        validate_definition(value, 'sample')
        value['parameters'].pop()
        value['press'] = 'action'
        with self.assertRaisesRegex(ValueError, 'press needs'):
            validate_definition(value, 'sample')

    def test_requires_sdk18_and_marks_drawing(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder).resolve() / 'probesample'
            shutil.copytree(ROOT / 'tests/fixtures/machines/probesample', path)
            (path / 'definition.json').write_text(json.dumps(self.definition()))
            metadata = json.loads((path / 'machine.json').read_text())
            metadata.pop('identity')
            metadata['base'] = 'drumSampler'
            metadata['sdk'] = '1.7'
            (path / 'machine.json').write_text(json.dumps(metadata))
            with self.assertRaisesRegex(ValueError, '1.8'):
                machine_at(path)
            metadata['sdk'] = '1.8'
            (path / 'machine.json').write_text(json.dumps(metadata))
            self.assertEqual(machine_at(path).display, 'drawing')
