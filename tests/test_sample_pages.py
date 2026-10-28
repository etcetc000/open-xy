"""Sample capabilities are independent of a machine name and its assigned numbers."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from dataclasses import replace

from tooling import adapters, native, catalog
from tooling.definitions import validate_definition
from tooling.machines import machine_at, assign_numbers
from machine_fixtures import fixture


def drawing_sample(name='orbit'):
    source = fixture('probesynth')
    definition = copy.deepcopy(source.definition)
    definition['id'] = name
    definition['resources'] = dict(voiceCapacity=256, display=definition['resources']['display'], singleSample=1)
    return replace(source, id=name, kind='sample', definition=definition, sources=(), includes=(),
                   identity=dict(engine=36, page=84))


class SamplePages(unittest.TestCase):
    def test_catalog_updates_review_changes_to_saved_sample_assignment(self):
        first = drawing_sample()
        definition = copy.deepcopy(first.definition)
        definition['resources'].pop('singleSample')
        second = replace(first, definition=definition)
        old = dict(id=first.id, kind='sample', status='published', tag='v1', commit='0' * 40)
        new = dict(old, tag='v2', commit='1' * 40)
        with patch.object(catalog, 'fetch', side_effect=[first, second]):
            with self.assertRaisesRegex(ValueError, 'sample assignment mode'):
                catalog.release_controls([old], [new])
        with patch.object(catalog, 'fetch', side_effect=[first, second]):
            self.assertIn('sample assignment mode', catalog.release_controls([old], [new], allow=True)[0])

    def test_capabilities_validate_independently_and_require_15(self):
        m = drawing_sample()
        for single in (0, 1):
            definition = copy.deepcopy(m.definition)
            definition['resources']['singleSample'] = single
            validate_definition(definition, 'sample')
        for bad in (True, 2, -1, '1'):
            definition = copy.deepcopy(m.definition)
            definition['resources']['singleSample'] = bad
            with self.assertRaises(ValueError): validate_definition(definition, 'sample')
        for resource, value in (('voiceCapacity', 128), ('padSettings', 1), ('drumControls', 1)):
            definition = copy.deepcopy(m.definition)
            definition['resources'][resource] = value
            with self.assertRaises(ValueError): validate_definition(definition, 'sample')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'definition.json').write_text(json.dumps(m.definition))
            record = dict(schema=1, sdk='1.5', kind='sample', description='Sample capability fixture', identity=m.identity)
            def load():
                (root / 'machine.json').write_text(json.dumps(record))
                return machine_at(root)
            self.assertEqual(load().display, 'drawing')
            record['sdk'] = '1.4'
            with self.assertRaisesRegex(ValueError, '1.5'): load()
            record['sdk'] = '1.5'
            record['identity']['page'] = 7
            with self.assertRaises(ValueError): load()

    def test_two_external_screens_remap_and_restore_without_changing_objects(self):
        machines = [drawing_sample('orbit'), drawing_sample('sliceview')]
        assigned, layout = assign_numbers(machines)
        self.assertEqual([m.identity for m in assigned], [dict(engine=36, page=84), dict(engine=37, page=85)])
        restored, after = assign_numbers(list(reversed(machines)), layout)
        self.assertEqual(after, layout)
        self.assertEqual(restored[0].identity, assigned[1].identity)
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            a = adapters.generate_sample_callbacks(first, machines).read_text()
            b = adapters.generate_sample_callbacks(second, assigned).read_text()
            self.assertEqual(a, b)  # Screen numbers are link-time identities, not baked into routing.
            self.assertIn('{sliceview_engine_id, SamplePage::drawing, sliceview_ready,', b)
            for m in assigned:
                text = adapters.generate_registration(first, m).read_text()
                self.assertIn('sdk_engine_ready(&record, &screen)', text)
                self.assertIn(m.prefix + '_screen_register()', text)
                voice = adapters.generate_sample_voice(first, m).read_text()
                self.assertIn('sdk_synth_title_slot(engine_id(), index)', voice)
            controls = adapters.generate_synth_controls(first, assigned).read_text()
            self.assertIn('orbit_ready', controls)
            self.assertIn('sliceview_ready', controls)
        self.assertEqual([step for _, m, step in native.plan(assigned) if m and m.id == 'orbit'],
                         ['display', 'screen', 'registration', 'sample_voice'])

    def test_single_sample_without_a_screen_keeps_stock_page(self):
        definition = copy.deepcopy(fixture('probesample').definition)
        definition['id'] = 'oneshot'
        definition['resources'] = dict(voiceCapacity=256, singleSample=1)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'definition.json').write_text(json.dumps(definition))
            record = dict(schema=1, sdk='1.5', kind='sample', description='Single sample fixture',
                          identity=dict(engine=44, page=2))
            (root / 'machine.json').write_text(json.dumps(record))
            self.assertEqual(machine_at(root).display, 'stock')
            record['identity']['page'] = 7
            (root / 'machine.json').write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError, 'singleSample'): machine_at(root)
