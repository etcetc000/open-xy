import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest

from tooling import steps
from tooling.definitions import validate_definition
from tooling.firmware import patch_active
from tooling.machines import assign_numbers, assign_steps, check_layout_numbers, machine_at
from tooling.pipeline import check_patch_boundaries, config

from machine_fixtures import FOLDER, fixture

SLOTS = ['pulse', 'hold', 'multiply', 'velocity', 'ramp_up', 'ramp_down', 'random', 'portamento', 'bend',
         'tonality', 'jump']
CONDITIONS = ['skip_param', 'skip_component', 'skip_trig']
# Stock 1.2.1's unused method-table entries (+0x18, +0x24) stay its no-ops.
METHODS = ['reset_all', 'reset_track', 'condition', 'apply', 'mute', '0x8d582da4', 'enter', 'next', '0x8d582d80',
           'trigs']


class StepEdits(unittest.TestCase):
    def setUp(self):
        self.profile = config()
        self.edits = [p for p in self.profile['patches'] if p['kind'] == 'step']

    def test_replaceable_components_and_their_edits(self):
        # All fourteen keys in white-key order.
        self.assertEqual(steps.slots(self.profile), SLOTS + CONDITIONS)
        # An object component: its methods +0x04..+0x28, picker name and ten setting rows; a
        # condition key has no object, so its name and rows only.
        self.assertEqual([(p['slot'], p['length'], p['words']) for p in self.edits],
                         [row for slot in SLOTS for row in ((slot, 40, METHODS), (slot, 4, ['name']),
                                                            (slot, 80, ['settings']))] +
                         [row for slot in CONDITIONS for row in ((slot, 4, ['name']), (slot, 80, ['settings']))])
        self.assertEqual(self.profile['patches'][-1]['kind'], 'startup')
        names = [int(p['address'], 16) for p in self.edits if p['words'] == ['name']]
        self.assertEqual(names, [0x8400d4a4 + 4 * t for t in range(14)])

    def test_condition_keys_are_hooked_only_in_step_builds(self):
        # Registration after stock's objects, and the three condition decisions.
        hooks = [p for p in self.profile['patches'] if p.get('service') == 'steps']
        self.assertEqual([(p['address'], p['symbol'], p['call'], p['length']) for p in hooks],
                         [('0x8d583828', 'sdk_step_register', True, 4)] +
                         [(a, 'sdk_step_condition', True, 4) for a in ('0x8d583996', '0x8d5839fa', '0x8d583c26')])
        self.assertEqual(self.profile['services']['steps'], ['step'])
        for hook in hooks:
            self.assertFalse(patch_active(hook, {'samples'}))
            self.assertTrue(patch_active(hook, {'steps'}))

    def test_computed_bytes_point_into_the_module(self):
        symbols = {'probestep_step_' + m: 0x8d2d1000 + 4 * i for i, m in enumerate(steps.METHODS)}
        symbols.update(probestep_step_name=0x8d2d2000, probestep_step_settings=0x8d2d3000)
        low, high = 0x8d2d0000, 0x8d2d4000
        for patch in self.edits:
            data = steps.after(patch, 'probestep', symbols.__getitem__)
            steps.check_after(patch, data, low, high)
            words = struct.unpack('<%dI' % (len(data) // 4), data)
            if patch['words'] == ['settings']:
                self.assertEqual(words, tuple(0x8d2d3000 + 16 * i for i in range(20)))
            elif patch['words'] == ['name']:
                self.assertEqual(words, (0x8d2d2000,))
            else:
                self.assertEqual(words, tuple(int(w, 16) if w.startswith('0x') else symbols['probestep_step_' + w]
                                              for w in METHODS))
            with self.assertRaisesRegex(ValueError, 'outside the module'):
                steps.check_after(patch, data, high, high + 0x1000)
        methods = self.edits[0]
        wrong = bytearray(steps.after(methods, 'probestep', symbols.__getitem__))
        wrong[20:24] = struct.pack('<I', 0x8d2d1000)  # the unused +0x18 entry moved into the module
        with self.assertRaisesRegex(ValueError, 'outside the module'):
            steps.check_after(methods, bytes(wrong), low, high)

    def test_a_slot_changes_only_when_a_machine_replaces_it(self):
        for patch in self.edits:
            self.assertFalse(patch_active(patch, set()))
            self.assertFalse(patch_active(patch, {'samples'}))
            self.assertEqual(patch_active(patch, {steps.service('velocity')}), patch['slot'] == 'velocity')

    def test_profile_shape_is_checked(self):
        cases = [dict(words=['apply', 'apply']), dict(words=['settings'], length=4), dict(service='samples'),
                 dict(slot=''), dict(words=['bogus']), dict(profiling=False)]
        for change in cases:
            with self.subTest(change=change):
                profile = copy.deepcopy(self.profile)
                [p for p in profile['patches'] if p['kind'] == 'step'][-1].update(change)  # skip trig's rows
                with self.assertRaises(ValueError):
                    check_patch_boundaries(profile, len(profile['patches']))
        # A build's profile carries the bytes, and an unreplaced slot keeps stock's.
        built = copy.deepcopy(self.profile)
        for patch in built['patches']:
            if patch['kind'] == 'step':
                patch['after'] = patch['before']
        check_patch_boundaries(built, len(built['patches']))


class StepMachines(unittest.TestCase):
    def definition(self, **change):
        value = dict(schema=1, id='steppy', name='Steppy', settings=[['a', 'b']] * 10, callbacks=['apply'])
        value.update(change)
        return value

    def test_definition(self):
        definition = validate_definition(self.definition(), 'step')
        self.assertEqual((definition['parameters'], definition['presets'], definition['resources']), ([], {}, {}))
        for change in (dict(settings=[['a', 'b']] * 9), dict(settings=[['a']] * 10),
                       dict(settings=[['a', 'b']] * 9 + [['A', 'b']]), dict(settings=[['a', 'x' * 15]] * 10),
                       dict(name='Eleven char'), dict(parameters=[]), dict(settings=[['', 'b']] * 10),
                       dict(callbacks=[]), dict(callbacks=['apply', 'apply']), dict(callbacks=['render'])):
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate_definition(self.definition(**change), 'step')
        self.assertEqual(steps.label_lines(definition)[:3], ['a', 'b', 'a'])

    def test_machine_record(self):
        m = fixture('probestep')
        self.assertEqual((m.kind, m.identity, m.display), ('step', {'step': 'velocity'}, 'stock'))
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder) / 'probestep'
            folder.mkdir()
            for name in ('definition.json', 'step.cpp'):
                (folder / name).write_bytes((FOLDER / 'probestep' / name).read_bytes())
            record = json.loads((FOLDER / 'probestep/machine.json').read_text(encoding='utf-8'))
            for change in (dict(identity={'step': 'skip trig'}), dict(identity={'engine': 40, 'page': 90}),
                           dict(sdk='1.4'), dict(presets='presets'), dict(sources=['extra.cpp'])):
                with self.subTest(change=change):
                    (folder / 'machine.json').write_text(json.dumps(dict(record, **change)), encoding='utf-8')
                    with self.assertRaises(ValueError):
                        machine_at(folder)

    def test_assignment(self):
        slots = ['velocity', 'random', 'portamento']
        # Requested when free, else the first free; a layout's choice comes first.
        self.assertEqual(assign_steps(slots, None, [('a', 'velocity'), ('b', 'velocity')]),
                         {'a': 'velocity', 'b': 'random'})
        self.assertEqual(assign_steps(slots, {'b': {'step': 'velocity'}}, [('a', 'velocity'), ('b', 'random')]),
                         {'b': 'velocity', 'a': 'random'})
        # A layout's component no longer replaceable is given again.
        self.assertEqual(assign_steps(slots, {'a': {'step': 'pulse'}}, [('a', 'velocity')]), {'a': 'velocity'})
        with self.assertRaisesRegex(ValueError, 'No free step component'):
            assign_steps(slots[:1], None, [('a', 'velocity'), ('b', 'velocity')])
        # Dropped machines hold nothing, and their entries stay as hints.
        machines, numbers = assign_numbers([fixture('probestep')], {'other': {'step': 'velocity'}}, fixed={})
        self.assertEqual(machines[0].identity, {'step': 'velocity'})
        self.assertEqual(numbers, {'other': {'step': 'velocity'}, 'probestep': {'step': 'velocity'}})
        machines, numbers = assign_numbers([fixture('probestep')], {'probestep': {'step': 'bend'}}, fixed={})
        self.assertEqual((machines[0].identity, machines[0].requested), ({'step': 'bend'}, {'step': 'velocity'}))
        check_layout_numbers(numbers, 'the test')
        for bad in ({'step': 3}, {'step': 'velocity', 'page': 1}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                check_layout_numbers({'probestep': bad}, 'the test')

    def test_services(self):
        machines, _ = assign_numbers([fixture('probestep'), fixture('probefx')], fixed={})
        self.assertEqual(steps.services(machines), {'step:velocity'})
        self.assertEqual(set(steps.assigned(machines)), {'velocity'})


if __name__ == '__main__':
    unittest.main()
