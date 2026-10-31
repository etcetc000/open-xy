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
from tooling.definitions import ROOT, control_positions, paged, validate_definition
from machine_fixtures import FOLDER, fixture
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
        self.engine = json.loads((FOLDER / 'probesynth/definition.json').read_text(encoding='utf-8'))

    def test_catalog_entries_use_the_stock_layout_and_q15_defaults(self):
        for m in (fixture('probesynth'), fixture('probesources')):
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
        invalid['presets']['Triangle']['unknown'] = 0.5
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
        changed['cppNamespace'] = 'probesynth'
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
                           ('nodes', 1), ('nodes', 257), ('texts', -1), ('texts', 33), ('texts', True),
                           ('palette', 0), ('palette', 17), ('kind', 'drawing')):
            changed = copy.deepcopy(self.engine)
            changed['resources']['display'][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                validate_definition(changed, 'synth')
        # A display without paths declares no points.
        without = fixture('probesources').definition
        validate_definition(without, 'synth')
        changed = copy.deepcopy(without)
        changed['resources']['display']['points'] = 2
        with self.assertRaisesRegex(ValueError, '0 points'):
            validate_definition(changed, 'synth')

    def test_every_kind_shares_the_controls_schema(self):
        for name in ('probesample', 'probefx'):
            m = fixture(name)
            value = copy.deepcopy(m.definition)
            value['parameters'][0]['label'] = 'Feedback amount'
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'Control labels'):
                validate_definition(value, m.kind)
        effect = copy.deepcopy(fixture('probefx').definition)
        effect['resources']['voiceCapacity'] = 8
        with self.assertRaisesRegex(ValueError, 'resources'): validate_definition(effect, 'effect')
        sample = copy.deepcopy(fixture('probesample').definition)
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
        sample = copy.deepcopy(fixture('probesample').definition)
        sample['parameters'][-1]['control'] = 0
        with self.assertRaisesRegex(ValueError, 'parameter fields'): validate_definition(sample, 'sample')

    def absent(self, identity):
        return dict(id=identity, label='', default=0, mapping=dict(kind='linear', minimum=0, maximum=1),
                    display='percent', absent=True)

    def test_an_absent_slot_keeps_later_indices_but_no_position_label_or_preset(self):
        # Slots: the engine's four, an absent alternate, a real alternate on 3, and slot 3 itself absent.
        value = copy.deepcopy(self.engine)
        value['parameters'][3] = self.absent('none3')
        value['parameters'] += [self.absent('none4'), self.alternate(3, 'tune')]
        for preset in value['presets'].values():
            preset.pop(self.engine['parameters'][3]['id'], None)
            preset.update(tune=0)
        validate_definition(value, 'synth')
        self.assertEqual(control_positions(value), [0, 1, 2, None, None, 3])
        # Position 3 starts on its absent slot and its press reaches the alternate; the absent alternate is unreachable.
        self.assertEqual(control_cycles(value), [0, 1, 2, 5, 4, 3, 6, 7])
        rejected = []
        for key, bad in (('label', 'X'), ('default', 0.5), ('absent', False), ('display', 'enum')):
            changed = copy.deepcopy(value); changed['parameters'][4][key] = bad
            rejected.append(changed)
        positioned = copy.deepcopy(value); positioned['parameters'][4]['control'] = 1
        rejected.append(positioned)
        preset = copy.deepcopy(value)
        for p in preset['presets'].values(): p['none4'] = 0
        rejected.append(preset)
        for bad in rejected:
            with self.subTest(parameter=bad['parameters'][4]), self.assertRaises(ValueError):
                validate_definition(bad, 'synth')
        blank = copy.deepcopy(self.engine); blank['parameters'][0]['label'] = ''
        with self.assertRaises(ValueError): validate_definition(blank, 'synth')
        effect = copy.deepcopy(fixture('probefx').definition)
        effect['parameters'].append(self.absent('none'))
        with self.assertRaisesRegex(ValueError, 'Only a synth'): validate_definition(effect, 'effect')
        machines = [SimpleNamespace(kind='synth', prefix='gap', identity=dict(engine=5), definition=value)]
        with tempfile.TemporaryDirectory() as directory:
            text = generate_synth_controls(directory, machines).read_text(encoding='utf-8')
        # The row's last field: the absent slots' mask (slots 3 and 4), which are not continuous either.
        row = re.search(r'^    \{gap_engine_id, gap_ready, \{([\d, ]+)\}, (\d+), \{[\d, ]+\}, gap_format, false, false, (\d+)\},$', text, re.M)
        self.assertEqual(row.group(1), '0, 1, 2, 5, 4, 3, 6, 7')
        self.assertEqual(int(row.group(2)) & 24, 0)
        self.assertEqual(row.group(3), '24')
        self.assertNotIn('    case 3:', text.split('gap_format')[1].split('default:')[0])

    def test_a_paged_synth_can_blank_a_position_on_its_second_page(self):
        # Slots: the engine's four, then page 2: a real alternate on 0, an absent one on 1.
        value = copy.deepcopy(self.engine)
        value['parameters'] += [self.alternate(0, 'p0'), dict(self.absent('none5'), control=1)]
        for preset in value['presets'].values():
            preset.update(p0=0)
        value['press'] = 'page'
        validate_definition(value, 'synth')
        self.assertEqual(control_positions(value), [0, 1, 2, 3, 0, 1])
        # Position 1's press reaches the empty slot, whose turns do nothing on the device.
        self.assertEqual(control_cycles(value), [4, 5, 2, 3, 0, 1, 6, 7])
        unpaged = copy.deepcopy(value); del unpaged['press']
        early = copy.deepcopy(value); early['parameters'][3] = dict(self.absent('none3'), control=3)
        for bad in (unpaged, early):
            with self.subTest(press=bad.get('press')), self.assertRaises(ValueError):
                validate_definition(bad, 'synth')

    def test_a_paged_synth_has_one_alternate_per_position(self):
        value = copy.deepcopy(self.engine)
        value['parameters'] += [self.alternate(c, 'p%d' % c) for c in (3, 0, 1, 2)]
        for preset in value['presets'].values():
            preset.update(p0=0, p1=0, p2=0, p3=0)
        value['press'] = 'page'
        validate_definition(value, 'synth')
        self.assertTrue(paged(value))
        self.assertEqual(control_cycles(value), [5, 6, 7, 4, 3, 0, 1, 2])
        self.assertFalse(paged(self.engine))
        rejected = []
        for bad in ('next', True, None):
            changed = copy.deepcopy(value); changed['press'] = bad
            rejected.append(changed)
        short = copy.deepcopy(value); del short['parameters'][-1]
        for preset in short['presets'].values(): del preset['p2']
        validate_definition(short, 'synth')
        following = control_cycles(short)
        self.assertEqual(following[:4], [5, 6, 2, 4])
        shown = list(range(4))
        shown = [following[s] for s in shown]
        self.assertEqual(shown, [5, 6, 2, 4])
        self.assertEqual([following[s] for s in shown], list(range(4)))
        empty = copy.deepcopy(self.engine); empty['press'] = 'page'
        rejected.append(empty)
        twice = copy.deepcopy(value); twice['parameters'][-1]['control'] = 3
        rejected.append(twice)
        for bad in rejected:
            with self.subTest(press=bad['press'], count=len(bad['parameters'])), self.assertRaises(ValueError):
                validate_definition(bad, 'synth')
        sample = copy.deepcopy(fixture('probesample').definition)
        sample['press'] = 'page'
        with self.assertRaisesRegex(ValueError, 'press'): validate_definition(sample, 'sample')
        # The control table marks the paged synth's row.
        machines = [SimpleNamespace(kind='synth', prefix='paged', identity=dict(engine=9), definition=value),
                    SimpleNamespace(kind='synth', prefix='plain', identity=dict(engine=5), definition=self.engine)]
        with tempfile.TemporaryDirectory() as directory:
            text = generate_synth_controls(directory, machines).read_text(encoding='utf-8')
        self.assertEqual(re.findall(r'^    \{(\w+)_engine_id, .*, (true|false), \d+\},$', text, re.M),
                         [('paged', 'true'), ('plain', 'false')])

    def test_an_effect_may_page_and_have_a_shift_row(self):
        effect = copy.deepcopy(fixture('probefx').definition)
        effect['parameters'] += [self.alternate(i, f'p{i}') for i in range(4)]
        effect['press'] = 'page'
        enum = lambda i, labels: dict(id=f's{i}', label=f'S {i}', default=0, display='enum',
                                      mapping=dict(kind='enum', labels=labels))
        effect['shift'] = [enum(0, ['a', 'b']), enum(1, ['c', 'd', 'e'])]
        validate_definition(effect, 'effect')
        self.assertTrue(paged(effect))
        rejected = []
        for shift in ([], [enum(i, ['a', 'b']) for i in range(5)],
                      [dict(enum(0, ['a', 'b']), display='percent', mapping=dict(kind='linear', minimum=0, maximum=1))],
                      [dict(enum(0, ['a', 'b']), id='drive')],
                      [enum(i, [f'c{n}' for n in range(256)]) for i in range(3)]):
            rejected.append(dict(effect, shift=shift))
        synth = copy.deepcopy(self.engine); synth['shift'] = [enum(0, ['a', 'b'])]
        for bad, kind in [(r, 'effect') for r in rejected] + [(synth, 'synth')]:
            with self.subTest(kind=kind, shift=len(bad['shift'])), self.assertRaises(ValueError):
                validate_definition(bad, kind)

    def test_an_effects_controls_after_four_alternate_on_the_send_page(self):
        # Without `control`, an effect's fifth to eighth controls alternate on positions 0..3.
        effect = copy.deepcopy(fixture('probefx').definition)
        base = effect['parameters'][0]
        effect['parameters'] = [dict(copy.deepcopy(base), id='p%d' % i, label='P%d' % i) for i in range(8)]
        effect['presets'] = {}
        validate_definition(effect, 'effect')
        self.assertEqual(control_cycles(effect), [4, 5, 6, 7, 0, 1, 2, 3])
        # One may name another position instead.
        effect['parameters'][7]['control'] = 0
        validate_definition(effect, 'effect')
        self.assertEqual(control_cycles(effect), [4, 5, 6, 3, 7, 1, 2, 0])
        machines = [SimpleNamespace(kind='effect', prefix='alts', identity=dict(effect=23), definition=effect)]
        with tempfile.TemporaryDirectory() as directory:
            text = generate_synth_controls(directory, machines).read_text(encoding='utf-8')
        self.assertEqual(re.findall(r'^    \{alts_fx_id, alts_ready, \{([\d, ]+)\},', text, re.M), ['4, 5, 6, 3, 7, 1, 2, 0'])

    def test_control_table_owns_all_synths_and_preserves_alternates(self):
        alternates = copy.deepcopy(self.engine)
        alternates['parameters'].append(self.alternate(2))
        machines = [SimpleNamespace(kind='synth', prefix='plain', identity=dict(engine=5), definition=self.engine),
                    SimpleNamespace(kind='synth', prefix='alt', identity=dict(engine=9), definition=alternates),
                    fixture('probesample'), fixture('probefx')]
        with tempfile.TemporaryDirectory() as directory:
            text = generate_synth_controls(directory, machines).read_text(encoding='utf-8')
        # Each row names its machine's engine or effect ID symbol, which the linker supplies.
        self.assertEqual(re.findall(r'^    \{(\w+), (\w+)_ready, \{([\d, ]+)\},', text, re.M),
                         [('plain_engine_id', 'plain', '0, 1, 2, 3, 4, 5, 6, 7'),
                          ('alt_engine_id', 'alt', '0, 1, 4, 3, 2, 5, 6, 7'),
                          ('probefx_fx_id', 'probefx', '0, 1, 2, 3, 4, 5, 6, 7')])

    def test_renderers_generate_for_every_declared_shape(self):
        """Each shape's renderer is data for the SDK renderer: artwork IDs in binding order, and an
        object whose C++ size equals the registered size (checked by the generated static_assert)."""
        shapes = [dict(stateBytes=60, stateAlignment=4, paths=1, points=2, nodes=64, texts=0, palette=1),
                  dict(stateBytes=60, stateAlignment=4, paths=4, points=64, nodes=4, texts=4, palette=16),
                  dict(stateBytes=60, stateAlignment=4, paths=0, points=0, nodes=256, texts=1, palette=8)]
        for display in shapes:
            definition = copy.deepcopy(fixture('probesynth').definition)
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
                         *('-I' + str(path) for path in (ROOT / 'sdk/include', ROOT / 'sdk/platform/opxy-1.2.1',
                                                         FOLDER / 'probesynth')), source.name],
                        cwd=directory, capture_output=True, text=True, timeout=120, **hidden)
                    self.assertEqual(result.returncode, 0, result.stderr)

if __name__ == '__main__':
    unittest.main()
