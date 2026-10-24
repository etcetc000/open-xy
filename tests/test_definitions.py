import copy
import os
from pathlib import Path
import re
import subprocess
from types import SimpleNamespace
import tempfile
import json
import unittest

from tooling.adapters import (catalog_defaults, catalog_entry, control_cycles, generate_display, generate_screen,
                               generate_synth_controls, screen_bytes)
from tooling.definitions import ROOT, control_positions, validate_definition
from tooling.machines import machine
from tooling.native import display_ids


def layout_compiler():
    """A host C++ compiler that type-checks 32-bit layouts (renderer static_asserts), or None."""
    compiler = os.environ.get('OPXY_TEST_HOST_CXX', 'g++')
    with tempfile.TemporaryDirectory(prefix='layout-cxx-') as folder:
        probe = Path(folder) / 'probe.cpp'
        probe.write_text('static_assert(sizeof(void*) == 4, "32-bit");\n')
        hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        try:
            result = subprocess.run([compiler, '-m32', '-fsyntax-only', str(probe)], capture_output=True, timeout=60,
                                    **hidden)
        except (OSError, subprocess.SubprocessError):
            return None
        return compiler if result.returncode == 0 else None


LAYOUT_CXX = layout_compiler()


class Definitions(unittest.TestCase):
    def setUp(self):
        self.engine = json.loads((ROOT / 'machines/fm2op/definition.json').read_text(encoding='utf-8'))

    def test_catalog_entries_use_the_stock_layout_and_q15_defaults(self):
        for m in (machine('fm2op'), machine('macroosc')):
            text = catalog_entry(m)
            # Indented like the stock entries it follows; ends its own line.
            self.assertTrue(text.startswith(',\n        {\n            "type": ') and text.endswith('\n        }\n'))
            entry = json.loads(text[1:])
            self.assertEqual(entry['type'], m.id)
            defaults = [p['default'] for p in m.definition['parameters']]
            params = entry['default_params']
            self.assertEqual(len(params), 8)
            self.assertEqual(params[len(defaults):], [0] * (8 - len(defaults)))
            for value, default in zip(params, defaults):
                self.assertLessEqual(abs(value - default * 32767), 0.5)

    def test_bad_controls_fail_before_generating_code(self):
        for value in (-0.1, 1.1, True, float('nan'), float('inf'), 10 ** 400):
            invalid = copy.deepcopy(self.engine)
            invalid['parameters'][0]['default'] = value
            with self.subTest(value=str(value)[:20]), self.assertRaises(ValueError):
                validate_definition(invalid, 'synth')
        invalid = copy.deepcopy(self.engine)
        invalid['parameters'][1]['id'] = invalid['parameters'][0]['id']
        with self.assertRaisesRegex(ValueError, 'Duplicate'): validate_definition(invalid, 'synth')
        invalid = copy.deepcopy(self.engine)
        invalid['parameters'][0]['mapping']['values'] = [1, 0.5, 2]
        with self.assertRaisesRegex(ValueError, 'increase'): validate_definition(invalid, 'synth')
        invalid = copy.deepcopy(self.engine)
        invalid['parameters'][0]['label'] = '<script>'
        with self.assertRaises(ValueError): validate_definition(invalid, 'synth')

    def test_mappings_must_be_supported(self):
        invalid = copy.deepcopy(self.engine)
        invalid['parameters'][0]['mapping']['kind'] = 'javascript'
        with self.assertRaisesRegex(ValueError, 'Unsupported'): validate_definition(invalid, 'synth')
        invalid = copy.deepcopy(self.engine)
        invalid['presets']['Glass']['unknown'] = 0.5
        with self.assertRaises(ValueError): validate_definition(invalid, 'synth')
        # Each linear control has one spelling.
        for mapping in (dict(kind='anchors', values=[0, 1]), dict(kind='power', maximum=1, exponent=1),
                        dict(kind='linear', minimum=1, maximum=1)):
            invalid = copy.deepcopy(self.engine)
            invalid['parameters'][0]['mapping'] = mapping
            with self.subTest(mapping=mapping), self.assertRaises(ValueError):
                validate_definition(invalid, 'synth')
        valid = copy.deepcopy(self.engine)
        valid['parameters'][0]['mapping'] = dict(kind='linear', minimum=-12, maximum=12)
        valid['parameters'][0]['display'] = 'semitones'
        validate_definition(valid, 'synth')

    def test_catalog_uses_q15_with_positive_half_rounding(self):
        changed = copy.deepcopy(self.engine)
        changed['parameters'][0]['default'] = 0.5
        self.assertEqual(catalog_defaults(changed)[0], 16384)

    def test_resource_declarations_reject_unknown_layouts_and_rates(self):
        for key, value in (('voiceBytes', 0), ('voiceBytes', 41),
                           ('voiceAlignment', 3), ('voiceAlignment', 16),
                           ('outputPeakQ31', 0), ('voiceBytes', True)):
            changed = copy.deepcopy(self.engine)
            changed['resources'][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                validate_definition(changed, 'synth')
        for key in ('blockFrames', 'sampleRate'):
            changed = copy.deepcopy(self.engine)
            changed['resources'][key] = 64
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'resources'):
                validate_definition(changed, 'synth')
        changed = copy.deepcopy(self.engine)
        changed['cppNamespace'] = 'fm2op'
        with self.assertRaisesRegex(ValueError, 'definition'): validate_definition(changed, 'synth')
        for name in ('delete', 'opxy'):
            changed = copy.deepcopy(self.engine)
            changed['id'] = name
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'keyword'):
                validate_definition(changed, 'synth')

    def test_displays_declare_their_shape_within_limits(self):
        for key, value in (('stateBytes', 61), ('stateBytes', 0),
                           ('stateAlignment', 3), ('stateAlignment', 8),
                           ('paths', 0), ('paths', 5), ('points', 1), ('points', 65),
                           ('nodes', 1), ('nodes', 257), ('texts', -1), ('texts', 5), ('texts', True),
                           ('palette', 0), ('palette', 17), ('kind', 'drawing')):
            changed = copy.deepcopy(self.engine)
            changed['resources']['display'][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                validate_definition(changed, 'synth')
        # A display without paths declares no points.
        without = machine('macroosc').definition
        validate_definition(without, 'synth')
        changed = copy.deepcopy(without)
        changed['resources']['display']['points'] = 2
        with self.assertRaisesRegex(ValueError, '0 points'):
            validate_definition(changed, 'synth')

    def test_every_kind_shares_the_controls_schema(self):
        for identity in ('timestretch', 'ringmod'):
            m = machine(identity)
            value = copy.deepcopy(m.definition)
            value['parameters'][0]['label'] = 'Feedback amount'
            with self.subTest(identity=identity), self.assertRaisesRegex(ValueError, 'Control labels'):
                validate_definition(value, m.kind)
        effect = copy.deepcopy(machine('ringmod').definition)
        effect['resources']['voiceCapacity'] = 8
        with self.assertRaisesRegex(ValueError, 'resources'): validate_definition(effect, 'effect')
        sample = copy.deepcopy(machine('timestretch').definition)
        sample['parameters'] = []
        with self.assertRaisesRegex(ValueError, 'Control count'): validate_definition(sample, 'sample')


    def alternate(self, control=3, identity='alt'):
        return dict(id=identity, label='Alt', control=control, default=0.25,
                    mapping=dict(kind='linear', minimum=0, maximum=1), display='percent')

    def test_synth_alternates_name_their_control_position(self):
        value = copy.deepcopy(self.engine)
        value['parameters'] += [self.alternate(3, 'a'), self.alternate(1, 'b'), self.alternate(3, 'c')]
        for preset in value['presets'].values():
            preset.update(a=0, b=0, c=0)
        validate_definition(value, 'synth')
        self.assertEqual(control_positions(value), [0, 1, 2, 3, 3, 1, 3])
        # Each position cycles through its own slot, then its alternates in slot order.
        self.assertEqual(control_cycles(value), [0, 5, 2, 4, 6, 1, 3, 7])
        self.assertIsNone(control_cycles(self.engine))
        catalog = catalog_defaults(value)
        self.assertEqual(catalog[4:], [8192, 8192, 8192, 0])
        rejected = []
        first = copy.deepcopy(value); first['parameters'][0]['control'] = 0
        rejected.append(first)
        missing = copy.deepcopy(value); del missing['parameters'][4]['control']
        rejected.append(missing)
        for bad in (4, -1, True, '3', None):
            changed = copy.deepcopy(value); changed['parameters'][4]['control'] = bad
            rejected.append(changed)
        many = copy.deepcopy(value)
        many['parameters'] += [self.alternate(0, 'd'), self.alternate(0, 'e')]
        rejected.append(many)
        for bad in rejected:
            with self.subTest(parameters=[p.get('control') for p in bad['parameters']]), self.assertRaises(ValueError):
                validate_definition(bad, 'synth')
        sample = copy.deepcopy(machine('timestretch').definition)
        sample['parameters'][-1]['control'] = 0
        with self.assertRaisesRegex(ValueError, 'parameter fields'): validate_definition(sample, 'sample')

    def test_control_table_owns_all_synths_and_preserves_alternates(self):
        alternates = copy.deepcopy(self.engine)
        alternates['parameters'].append(self.alternate(2))
        machines = [SimpleNamespace(kind='synth', prefix='plain', identity=dict(engine=5), definition=self.engine),
                    SimpleNamespace(kind='synth', prefix='alt', identity=dict(engine=9), definition=alternates),
                    machine('timestretch'), machine('ringmod')]
        with tempfile.TemporaryDirectory() as directory:
            text = generate_synth_controls(directory, machines).read_text(encoding='utf-8')
        self.assertEqual(re.findall(r'^    \{(\d+), (\w+)_ready, \{([\d, ]+)\},', text, re.M),
                         [('5', 'plain', '0, 1, 2, 3, 4, 5, 6, 7'), ('9', 'alt', '0, 1, 4, 3, 2, 5, 6, 7'),
                          ('1', 'ringmod', '0, 1, 2, 3, 4, 5, 6, 7')])

    def test_renderers_generate_for_every_declared_shape(self):
        """Each shape's renderer is data for the SDK renderer: artwork IDs in binding order, and an
        object whose C++ size equals the registered size (checked by the generated static_assert)."""
        shapes = [dict(stateBytes=60, stateAlignment=4, paths=1, points=2, nodes=64, texts=0, palette=1),
                  dict(stateBytes=60, stateAlignment=4, paths=4, points=64, nodes=4, texts=4, palette=16),
                  dict(stateBytes=60, stateAlignment=4, paths=0, points=0, nodes=256, texts=1, palette=8)]
        for display in shapes:
            definition = copy.deepcopy(machine('fm2op').definition)
            definition['resources']['display'] = display
            validate_definition(definition, 'synth')
            m = SimpleNamespace(prefix='shape', native_name='shape', definition=definition)
            with self.subTest(display=display), tempfile.TemporaryDirectory() as directory:
                generate_display(directory, machine=SimpleNamespace(definition=definition, native_name='shape',
                                                                    prefix='shape'))
                ids = display_ids('shape', display)
                source = generate_screen(directory, m)
                text = source.read_text(encoding='utf-8')
                self.assertEqual(re.findall(r'"(shape_[0-9a-z_]+)"', text), ids)
                self.assertIn(', %d)' % screen_bytes(m), text)
                if LAYOUT_CXX:
                    hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
                    result = subprocess.run(
                        [LAYOUT_CXX, '-m32', '-std=gnu++11', '-fsyntax-only', '-Wall', '-Werror', '-ffreestanding',
                         *('-I' + str(ROOT / path) for path in ('sdk/include', 'sdk/platform/opxy-1.2.0',
                                                               'machines/fm2op')), source.name],
                        cwd=directory, capture_output=True, text=True, timeout=120, **hidden)
                    self.assertEqual(result.returncode, 0, result.stderr)

if __name__ == '__main__':
    unittest.main()
