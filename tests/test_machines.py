"""machine.json: kinds, displays and the stock identity each machine declares."""
import copy
from dataclasses import replace
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

from tooling import adapters, machines
from tooling.pipeline import config
from machine_fixtures import FOLDER, NAMES, fixture, fixture_entries

RULES = config()['identities']


def own(name):
    """The numbers a synth asking for none gets beside the fixed machines alone: its own places."""
    return machines.number_layout(RULES, [(n, None, i) for n, i in machines.FIXED_NUMBERS.items()], None,
                                  [(name, 'synth', {'engine': None, 'page': None})])[name]


# Synth numbers no fixed machine holds, for machines that ask for numbers.
FREE = own('free')


def identity(kind, **changes):
    value = {'synth': dict(engine=34, page=82), 'sample': dict(engine=35, page=2),
             'effect': dict(effect=11, page=21)}[kind]
    return dict(value, **changes)


class Identity(unittest.TestCase):
    def test_fixed_numbers_are_valid_and_distinct(self):
        # Open-XY's first machines keep the numbers its first firmware gave them, in every layout.
        kinds = {'fm2op': 'synth', 'macroosc': 'synth', 'ringmod': 'effect', 'timestretch': 'sample'}
        self.assertEqual(set(machines.FIXED_NUMBERS), set(kinds))
        for name, numbers in machines.FIXED_NUMBERS.items():
            with self.subTest(name=name):
                self.assertEqual(machines.check_identity(RULES, kinds[name], numbers), numbers)
        self.assertEqual(machines.assign_numbers([])[1], machines.FIXED_NUMBERS)
        # Each sample base is one of the profile's stock sample pages.
        self.assertEqual(sorted(machines.SAMPLE_BASES.values()), RULES['sample_pages'])
        # A collection keeps its declared order; the SDK has no default collection.
        self.assertEqual([m.id for m in machines.collection(fixture_entries())], list(NAMES))
        for entries in (None, []):
            with self.subTest(entries=entries), self.assertRaisesRegex(ValueError, 'Collection needs machines'):
                machines.collection(entries)

    def test_stock_occupied_or_sentinel_ids_are_rejected(self):
        for kind, change in (('synth', dict(engine=33)), ('synth', dict(engine=2)), ('synth', dict(engine=256)),
                             ('synth', dict(page=81)), ('synth', dict(page=256)), ('synth', dict(engine=True)),
                             ('sample', dict(engine=26)), ('sample', dict(page=81)), ('sample', dict(page=256)),
                             ('effect', dict(effect=0)), ('effect', dict(effect=5)), ('effect', dict(page=20))):
            with self.subTest(kind=kind, change=change), self.assertRaises(ValueError):
                machines.check_identity(RULES, kind, identity(kind, **change))

    def test_own_screen_pages_come_from_the_relocated_range(self):
        machines.check_identity(RULES, 'synth', identity('synth'))
        for page in (2, 21, 50, 81):
            with self.subTest(page=page), self.assertRaises(ValueError):
                machines.check_identity(RULES, 'synth', identity('synth', page=page))

    def test_a_sample_engine_drawing_its_own_screen_holds_a_relocated_page(self):
        own = identity('sample', page=90)
        self.assertEqual(machines.check_identity(RULES, 'sample', own), own)
        self.assertEqual(machines.check_identity(RULES, 'sample', own, 'drawing'), own)
        self.assertIn(('page', 90), machines.occupied(RULES, own))
        self.assertNotIn(('page', 2), machines.occupied(RULES, identity('sample', page=2)))
        for value, display in ((own, 'stock'), (identity('sample', page=2), 'drawing'), (identity('sample', page=81), None),
                               (identity('sample', page=256), None), (identity('sample', screen=90), None)):
            with self.subTest(value=value, display=display), self.assertRaises(ValueError):
                machines.check_identity(RULES, 'sample', value, display)

    def test_names_and_fields_are_checked(self):
        # The folder name is the stock name, so identity holds only IDs.
        for kind, value in (('synth', identity('synth', name='probe')), ('synth', identity('synth', prefix='probe')),
                            ('effect', dict(identity('effect'), engine=34))):
            with self.subTest(value=value), self.assertRaises(ValueError):
                machines.check_identity(RULES, kind, value)
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary).resolve() / 'renamed'
            shutil.copytree(FOLDER / 'probesynth', folder)
            definition = json.loads((folder / 'definition.json').read_text(encoding='utf-8'))
            for name in ('a' * 16, 'Bad', 'two-op', '', 'drum', 'reverb', 'sdk', 'kit_probe', 'extensions'):
                (folder / 'definition.json').write_text(json.dumps(dict(definition, id=name)), encoding='utf-8')
                with self.subTest(name=name), self.assertRaises(ValueError):
                    machines.machine_at(folder)
            with self.assertRaises(ValueError):
                machines.machine_at(Path(temporary) / 'missing')
        m = fixture('probesynth')
        self.assertEqual((m.native_name, m.prefix), ('probesynth', 'probesynth'))

    def test_collection_rejects_conflicting_identities(self):
        # Fixed numbers belong to their own machines, so two may never share one.
        clash = dict(machines.FIXED_NUMBERS, copy=dict(engine=34, page=2))
        with mock.patch.object(machines, 'FIXED_NUMBERS', clash), self.assertRaisesRegex(ValueError, 'both hold engine 34'):
            machines.collection(fixture_entries())
        # A machine with fixed numbers must ask for them, and then keeps them.
        synth = fixture('probesynth')
        with self.assertRaisesRegex(ValueError, 'fm2op has fixed numbers'):
            machines.assign_numbers([replace(synth, id='fm2op')])
        fixed = replace(synth, id='fm2op', identity=machines.FIXED_NUMBERS['fm2op'])
        self.assertEqual(machines.assign_numbers([fixed])[0], [fixed])

    def test_unknown_fields_kinds_and_abi_combinations_are_rejected(self):
        path = FOLDER / 'probesynth/machine.json'
        original = json.loads(path.read_text(encoding='utf-8'))
        for change in (dict(extra=1), dict(kind='instrument'), dict(display='segments'), dict(compiler='opxy'),
                       dict(sources=['../probesources/engine.cpp'])):
            value = dict(copy.deepcopy(original), **change)
            text = json.dumps(value)
            real = Path.read_text
            with self.subTest(change=change), mock.patch.object(
                    Path, 'read_text', lambda self, *a, **k: text if self.name == 'machine.json' else real(self, *a, **k)):
                with self.assertRaises(ValueError):
                    machines.machine_at(FOLDER / 'probesynth')

    def test_dry_parameter_requires_a_normalized_effect_control(self):
        real = Path.read_text
        # probefx's Drive is linear but not from 0 to 1, and its Tone is a power mapping.
        for name, field in (('probefx', 'missing'), ('probefx', 3), ('probefx', None),
                            ('probefx', 'drive'), ('probefx', 'tone'), ('probesynth', 'level')):
            path = FOLDER / name / 'machine.json'
            value = dict(json.loads(real(path)), dryParameter=field)
            with self.subTest(name=name, field=field), mock.patch.object(
                    Path, 'read_text', lambda self, *a, **k: json.dumps(value) if self == path else real(self, *a, **k)):
                with self.assertRaisesRegex(ValueError, 'dryParameter'):
                    machines.machine_at(FOLDER / name)
        self.assertEqual(fixture('probefx').dry_parameter, 3)
        self.assertIsNone(fixture('probesynth').dry_parameter)
        path = FOLDER / 'probefx/machine.json'
        value = json.loads(real(path))
        del value['dryParameter']
        with mock.patch.object(Path, 'read_text',
                               lambda self, *a, **k: json.dumps(value) if self == path else real(self, *a, **k)):
            self.assertIsNone(fixture('probefx').dry_parameter)

    def test_dry_level_fixes_the_source_dry_instead(self):
        real = Path.read_text
        path = FOLDER / 'probefx/machine.json'
        plain = {k: v for k, v in json.loads(real(path)).items() if k not in ('dryParameter', 'identity')}
        cases = ((dict(plain, sdk='1.7', dryLevel=0), 0), (dict(plain, sdk='1.7', dryLevel=0.5), 0x40000000),
                 (dict(plain, sdk='1.7', dryLevel=1.5), 'dryLevel'), (dict(plain, sdk='1.7', dryLevel='0'), 'dryLevel'),
                 (dict(plain, sdk='1.7', dryLevel=0, dryParameter='dry'), 'dryLevel'),
                 (dict(plain, sdk='1.6', dryLevel=0), '"sdk": "1.7"'))
        for value, expected in cases:
            with self.subTest(value=value), mock.patch.object(
                    Path, 'read_text', lambda self, *a, **k: json.dumps(value) if self == path else real(self, *a, **k)):
                if isinstance(expected, str):
                    with self.assertRaisesRegex(ValueError, expected):
                        fixture('probefx')
                else:
                    self.assertEqual(fixture('probefx').dry_level, expected)

    def test_more_than_sixteen_text_slots_need_sdk_1_7(self):
        real = Path.read_text
        root = FOLDER / 'probesynth'
        plain = {k: v for k, v in json.loads(real(root / 'machine.json')).items() if k != 'identity'}
        definition = json.loads(real(root / 'definition.json'))
        for version, texts, expected in (('1.6', 16, None), ('1.6', 17, '"sdk": "1.7"'), ('1.7', 32, None),
                                         ('1.7', 33, 'Display texts must be 0..32')):
            files = {'machine.json': json.dumps(dict(plain, sdk=version)),
                     'definition.json': json.dumps(dict(definition, resources=dict(definition['resources'], display=dict(
                         definition['resources']['display'], texts=texts))))}
            with self.subTest(version=version, texts=texts), mock.patch.object(
                    Path, 'read_text',
                    lambda self, *a, **k: files[self.name] if self.parent == root and self.name in files
                    else real(self, *a, **k)):
                if expected:
                    with self.assertRaisesRegex(ValueError, expected):
                        machines.machine_at(root)
                else:
                    self.assertEqual(machines.machine_at(root).id, 'probesynth')


class OutsideMachines(unittest.TestCase):
    """A machine in a folder of its own, such as its own repository."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name).resolve() / 'opxy-fmcopy'
        shutil.copytree(FOLDER / 'probesynth', self.folder)
        self.free = FREE
        self.rename('fmcopy', self.free)

    def rename(self, identity, ids, sdk='1.0'):
        definition = json.loads((self.folder / 'definition.json').read_text(encoding='utf-8'))
        definition['id'] = identity
        (self.folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
        record = json.loads((self.folder / 'machine.json').read_text(encoding='utf-8'))
        record.update(identity=ids, sdk=sdk)
        (self.folder / 'machine.json').write_text(json.dumps(record), encoding='utf-8')

    def test_a_folder_anywhere_is_a_machine(self):
        m = machines.machine_at(self.folder)
        self.assertEqual((m.id, m.root), ('fmcopy', self.folder))
        # Every machine's sources are named by its absolute folder, and its provenance by external/<id>/.
        self.assertEqual(m.source(), self.folder.as_posix())
        self.assertEqual(m.source('engine.cpp'), self.folder.as_posix() + '/engine.cpp')
        self.assertIn('external/fmcopy/engine.cpp', m.files())
        self.assertTrue(all(name.startswith('external/fmcopy/') for name in m.files()))
        # A relative path through .. names the same folder; nothing under .git is source.
        dotted = self.folder.parent / 'elsewhere' / '..' / self.folder.name
        self.assertEqual(machines.machine_at(dotted).root, self.folder)
        (self.folder / '.git').mkdir()
        (self.folder / '.git' / 'HEAD').write_text('ref', encoding='utf-8')
        self.assertFalse(any('.git' in name for name in m.files()))
        self.assertTrue(m.contains(self.folder / 'out') and not m.contains(self.folder.parent / 'out'))

    def test_slice_clock_requires_its_public_sdk_version(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary).resolve() / 'sample'
            shutil.copytree(FOLDER / 'probesample', folder)
            definition = json.loads((folder / 'definition.json').read_text(encoding='utf-8'))
            definition['resources'].update(padSettings=1, drumControls=1, sliceTiming=1)
            (folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
            record = json.loads((folder / 'machine.json').read_text(encoding='utf-8'))
            record.pop('identity', None)
            record['base'] = 'drumSampler'
            for version in ('1.5', '1.6', '1.7'):
                record['sdk'] = version
                (folder / 'machine.json').write_text(json.dumps(record), encoding='utf-8')
                if version != '1.7':
                    with self.assertRaisesRegex(ValueError, 'sliceTiming.*1.7'):
                        machines.machine_at(folder)
                else:
                    self.assertEqual(machines.machine_at(folder).definition['resources']['sliceTiming'], 1)

    def test_collections_list_machine_folders(self):
        # Paths are relative to the collection's folder; both stay on its drive, as relpath needs on Windows.
        probefx = self.folder.parent / 'shelf' / 'probefx'
        shutil.copytree(FOLDER / 'probefx', probefx)
        collection = self.folder.parent / 'lists' / 'collection.json'
        collection.parent.mkdir()
        collection.write_text(json.dumps({'schema': 1, 'machines': [{'path': '../shelf/probefx'},
                                                                    {'path': '../opxy-fmcopy'}]}), encoding='utf-8')
        entries = machines.read_collection(collection)
        self.assertEqual(entries, [{'path': str(probefx)}, {'path': str(self.folder)}])
        self.assertEqual([m.id for m in machines.collection(entries)], ['probefx', 'fmcopy'])
        extra = machines.read_collection(collection, [FOLDER / 'probesynth'])
        self.assertEqual([m.id for m in machines.collection(extra)], ['probefx', 'fmcopy', 'probesynth'])
        self.assertEqual(machines.read_collection(None, [self.folder]), [{'path': str(self.folder)}])
        # The SDK holds no machines of its own, so a collection lists folders only.
        for listed in (['fm2op'], [{'id': 'fm2op'}], [{'path': 'opxy-fmcopy', 'id': 'fmcopy'}]):
            collection.write_text(json.dumps({'schema': 1, 'machines': listed}), encoding='utf-8')
            with self.subTest(listed=listed), self.assertRaisesRegex(ValueError, 'Collection entries are'):
                machines.read_collection(collection)
        # A machine asking for a fixed number is given its own place instead.
        self.rename('fmcopy', dict(self.free, engine=34))
        fmcopy = machines.collection(entries)[-1]
        self.assertEqual((fmcopy.identity, fmcopy.requested),
                         (dict(self.free, engine=own('fmcopy')['engine']), dict(self.free, engine=34)))

    def test_sdk_version_and_fixed_names_are_checked(self):
        for version in ('1.1', '1.2', '1.3', '1.4'):
            self.rename('fmcopy', self.free, version)
            self.assertEqual(machines.machine_at(self.folder).id, 'fmcopy')
        for version in ('1.5', '1.6', '1.7', '1.8', '1.9', '1.10'):
            self.numberless(self.folder, sdk=version)
            self.assertEqual(machines.machine_at(self.folder).id, 'fmcopy')
        future = '%d.%d' % (machines.SDK_VERSION[0], machines.SDK_VERSION[1] + 1)
        for version in ('0.9', '2.0', future, '1', 'latest', None):
            with self.subTest(version=version):
                self.rename('fmcopy', self.free, version)
                with self.assertRaises(ValueError):
                    machines.machine_at(self.folder)
        # A folder may hold a machine named like one with fixed numbers, but it must ask for them.
        self.rename('fm2op', self.free)
        with self.assertRaisesRegex(ValueError, 'fm2op has fixed numbers'):
            machines.collection(machines.read_collection(None, [self.folder]))
        self.rename('fm2op', machines.FIXED_NUMBERS['fm2op'])
        self.assertEqual(machines.collection(machines.read_collection(None, [self.folder]))[0].identity,
                         machines.FIXED_NUMBERS['fm2op'])

    def test_track_control_needs_the_sdk_that_adds_it(self):
        (self.folder / 'control.cpp').write_text('', encoding='utf-8')
        definition = json.loads((self.folder / 'definition.json').read_text(encoding='utf-8'))
        definition['resources']['trackControl'] = {'stateBytes': 8, 'stateAlignment': 4, 'unique': True, 'aux': [12]}
        (self.folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
        self.numberless(self.folder, sdk='1.8')
        with self.assertRaisesRegex(ValueError, 'Track control'):
            machines.machine_at(self.folder)
        self.numberless(self.folder, sdk='1.9')
        self.assertEqual(machines.machine_at(self.folder).id, 'fmcopy')

    def test_a_drum_kits_pad_mix_needs_the_sdk_that_adds_it(self):
        path = self.folder / 'definition.json'
        definition = json.loads(path.read_text(encoding='utf-8'))
        definition['presets'] = {}
        for control, parameter in enumerate(json.loads(json.dumps(definition['parameters']))):
            parameter.update(id=parameter['id'] + '_alt', label='Alt ' + str(control), control=control)
            definition['parameters'].append(parameter)
        definition['resources'].update(modelCount=12, padMix=1)
        path.write_text(json.dumps(definition), encoding='utf-8')
        self.numberless(self.folder, kind='drum', sdk='1.9')
        with self.assertRaisesRegex(ValueError, 'per-key mixer'):
            machines.machine_at(self.folder)
        self.numberless(self.folder, kind='drum', sdk='1.10')
        self.assertEqual(machines.machine_at(self.folder).id, 'fmcopy')
        definition['resources']['padMix'] = 0
        path.write_text(json.dumps(definition), encoding='utf-8')
        self.numberless(self.folder, kind='drum', sdk='1.9')
        self.assertEqual(machines.machine_at(self.folder).id, 'fmcopy')

    def test_hidden_pages_need_the_sdk_that_adds_them(self):
        definition = json.loads((self.folder / 'definition.json').read_text(encoding='utf-8'))
        definition['hidePages'] = ['envelope']
        (self.folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
        self.numberless(self.folder, sdk='1.8')
        with self.assertRaisesRegex(ValueError, 'Hidden stock pages'):
            machines.machine_at(self.folder)
        self.numberless(self.folder, sdk='1.9')
        self.assertEqual(machines.machine_at(self.folder).id, 'fmcopy')

    def numberless(self, folder, **changes):
        """A machine.json in SDK 1.5 form: no identity; `changes` adds or (None) removes fields."""
        record = json.loads((folder / 'machine.json').read_text(encoding='utf-8'))
        record.pop('identity', None)
        record['sdk'] = '1.5'
        for key, value in changes.items():
            if value is None:
                record.pop(key, None)
            else:
                record[key] = value
        (folder / 'machine.json').write_text(json.dumps(record), encoding='utf-8')
        return folder

    def copy(self, name):
        folder = self.folder.parent / name
        shutil.copytree(FOLDER / name, folder)
        return folder

    def test_more_than_256_display_nodes_need_sdk_1_8(self):
        path = self.folder / 'definition.json'
        definition = json.loads(path.read_text(encoding='utf-8'))
        definition['resources']['display']['nodes'] = 300
        path.write_text(json.dumps(definition), encoding='utf-8')
        self.numberless(self.folder, sdk='1.7')
        with self.assertRaisesRegex(ValueError, 'More than 256 display nodes'):
            machines.machine_at(self.folder)
        self.numberless(self.folder, sdk='1.8')
        self.assertEqual(machines.machine_at(self.folder).id, 'fmcopy')

    def test_sdk_1_5_machines_name_no_numbers(self):
        self.numberless(self.folder)
        m = machines.machine_at(self.folder)
        self.assertEqual(m.identity, {'engine': None, 'page': None})
        # The build gives the machine its own places, the same each time, and records the request.
        self.assertNotEqual(own('fmcopy'), FREE)
        for _ in range(2):
            built = machines.collection(machines.read_collection(None, [self.folder]))[0]
            self.assertEqual((built.identity, built.requested), (own('fmcopy'), m.identity))
        # A layout keeps the numbers it gave; dropped machines' stay held.
        layout = {'fmcopy': {'engine': 50, 'page': 100}, 'gone': dict(FREE)}
        built = machines.collection(machines.read_collection(None, [self.folder]), layout)[0]
        self.assertEqual(built.identity, {'engine': 50, 'page': 100})
        built = machines.collection(machines.read_collection(None, [self.folder]), {'gone': own('fmcopy')})[0]
        self.assertEqual(built.identity, {'engine': own('fmcopy')['engine'] + 1, 'page': own('fmcopy')['page'] + 1})
        # A machine named like one with fixed numbers gets them.
        self.rename('fm2op', None, '1.5')
        self.numberless(self.folder)
        self.assertEqual(machines.collection(machines.read_collection(None, [self.folder]))[0].identity,
                         machines.FIXED_NUMBERS['fm2op'])

    def test_sdk_1_5_refuses_numbers_and_misplaced_bases(self):
        cases = {'identity': (dict(identity=FREE), 'no identity.*"base": "sampler" or "drumSampler"'),
                 'synth base': (dict(base='sampler'), 'Only a sample engine names a base')}
        for case, (changes, error) in cases.items():
            self.numberless(self.folder, **changes)
            with self.subTest(case=case), self.assertRaisesRegex(ValueError, error):
                machines.machine_at(self.folder)
        effect = self.numberless(self.copy('probefx'), base='drumSampler')
        with self.assertRaisesRegex(ValueError, 'Only a sample engine names a base'):
            machines.machine_at(effect)
        self.assertEqual(machines.machine_at(self.numberless(effect, base=None)).identity, {'effect': None, 'page': 21})
        # Before 1.5 a machine lists numbers and names no base.
        self.rename('fmcopy', self.free, '1.4')
        record = json.loads((self.folder / 'machine.json').read_text(encoding='utf-8'))
        (self.folder / 'machine.json').write_text(json.dumps(dict(record, base='sampler')), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'needs "sdk": "1.5"'):
            machines.machine_at(self.folder)

    def test_a_sample_engine_names_the_stock_sampler_it_forks(self):
        folder = self.copy('probesample')
        for base, error in ((None, 'names the stock sampler it forks'), ('piano', 'names the stock sampler'),
                            (7, 'names the stock sampler'), (['sampler'], 'names the stock sampler'),
                            ({'sampler': 2}, 'names the stock sampler'),
                            ('sampler', 'Pad settings and drum controls need the drum sampler base')):
            self.numberless(folder, base=base)
            with self.subTest(base=base), self.assertRaisesRegex(ValueError, error):
                machines.machine_at(folder)
        self.numberless(folder, base='drumSampler')
        self.assertEqual(machines.machine_at(folder).identity, {'engine': None, 'page': 7})
        built = machines.collection(machines.read_collection(None, [folder]))[0]
        self.assertEqual(built.identity, {'engine': 201, 'page': 7})  # its own place in the engine pool
        # Without pad settings it may fork the keyboard sampler; a 1.4 machine still picks it by number.
        definition = json.loads((folder / 'definition.json').read_text(encoding='utf-8'))
        definition['resources'].pop('padSettings')
        definition['resources'].pop('drumControls')
        (folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
        self.numberless(folder, base='sampler')
        self.assertEqual(machines.machine_at(folder).identity, {'engine': None, 'page': 2})
        self.numberless(folder, base=None, sdk='1.4', identity={'engine': 47, 'page': 2})
        self.assertEqual(machines.machine_at(folder).identity, {'engine': 47, 'page': 2})

    def test_a_sample_engine_forking_the_sampler_may_draw_its_own_screen(self):
        folder = self.copy('probesample')
        definition = json.loads((folder / 'definition.json').read_text(encoding='utf-8'))
        for name in ('padSettings', 'drumControls'):
            definition['resources'].pop(name)
        synth = json.loads((FOLDER / 'probesynth/definition.json').read_text(encoding='utf-8'))
        definition['resources']['display'] = synth['resources']['display']
        (folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
        self.numberless(folder, base='drumSampler', sdk='1.8')
        with self.assertRaisesRegex(ValueError, 'Only a sample engine forking the sampler'):
            machines.machine_at(folder)
        # Every released SDK that builds an earlier version refuses a sample engine's
        # display, so a machine declaring one must name the version that first has it.
        for version in ('1.5', '1.6', '1.7'):
            self.numberless(folder, base='sampler', sdk=version)
            with self.subTest(version=version), self.assertRaisesRegex(
                    ValueError, r'''A sample engine's own screen \(resources.display\) needs "sdk": "1.8"'''):
                machines.machine_at(folder)
        # The form before SDK 1.5 names no version that has the feature, and gives no page.
        self.numberless(folder, base=None, sdk='1.4', identity={'engine': 47, 'page': 2, 'screen': 90})
        with self.assertRaisesRegex(ValueError, 'needs "sdk": "1.8"'):
            machines.machine_at(folder)
        self.numberless(folder, base=None, sdk='1.8', identity={'engine': 47, 'page': 2, 'screen': 90})
        with self.assertRaisesRegex(ValueError, 'Remove "identity"'):
            machines.machine_at(folder)
        self.numberless(folder, base='sampler', sdk='1.8')
        m = machines.machine_at(folder)
        self.assertEqual((m.display, m.identity), ('drawing', {'engine': None, 'page': None}))
        # The layout gives the screen a page from its own place, as it gives a synth's,
        # and the engine stays on the sampler's page.
        built = machines.collection(machines.read_collection(None, [folder]))[0]
        self.assertIn(built.identity['page'], machines.number_pools(RULES)['page'])
        self.assertEqual(built.sample_page, 2)
        # Without a display it keeps the stock page it forks, and may not ask for another.
        definition['resources'].pop('display')
        (folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
        self.numberless(folder, base='sampler', sdk='1.8')
        self.assertEqual(machines.machine_at(folder).identity, {'engine': None, 'page': 2})
        self.numberless(folder, base=None, sdk='1.4', identity={'engine': 47, 'page': 90})
        with self.assertRaisesRegex(ValueError, 'stock sample page'):
            machines.machine_at(folder)

    def test_shift_on_a_sample_engines_own_screen_may_go_to_the_stock_sampler(self):
        folder = self.copy('probesample')
        definition = json.loads((folder / 'definition.json').read_text(encoding='utf-8'))
        for name in ('padSettings', 'drumControls'):
            definition['resources'].pop(name)

        def declare(stock_shift, display=True, sdk='1.10'):
            resources = {k: v for k, v in definition['resources'].items() if k not in ('display', 'stockShift')}
            if display:
                synth = json.loads((FOLDER / 'probesynth/definition.json').read_text(encoding='utf-8'))
                resources['display'] = synth['resources']['display']
            if stock_shift is not None:
                resources['stockShift'] = stock_shift
            (folder / 'definition.json').write_text(json.dumps(dict(definition, resources=resources)), encoding='utf-8')
            self.numberless(folder, base='sampler', sdk=sdk)

        def table():
            with tempfile.TemporaryDirectory() as out:
                adapters.generate_synth_controls(out, [machines.machine_at(folder)])
                return (Path(out) / 'synth_control_table.cpp').read_text(encoding='utf-8')
        # Shift's stock controls belong to an own screen, which took the sampler page's encoders.
        declare(['start'], display=False)
        with self.assertRaisesRegex(ValueError, 'stockShift .* needs resources.display'):
            machines.machine_at(folder)
        declare(['start'], sdk='1.9')
        with self.assertRaisesRegex(ValueError, r'''\(stockShift\) needs "sdk": "1.10"'''):
            machines.machine_at(folder)
        for bad, message in ((['pitch'], 'stockShift names "pitch"'), (['crossfade'], "can't name crossfade"),
                             ([], 'at most four names'),
                             (['', '', '', '', 'gain'], 'at most four names'), ('gain', 'at most four names'),
                             (['', ''], 'names no stock control'), (['gain', '', 'gain'], 'twice')):
            declare(bad)
            with self.assertRaisesRegex(ValueError, message):
                machines.machine_at(folder)
        # Each encoder takes any one of the stock page's eight turns or a window;
        # an empty name, or one past the list, keeps Shift's fine turn.
        declare(['end', 'loopWindow', '', 'start'])
        self.assertIn(', 0, {1, 1, 1, 1}, nullptr, {4, 10, 0, 1}},', table())
        declare(['direction', 'tune', '', 'gain'])
        self.assertIn('nullptr, {5, 6, 0, 8}},', table())
        declare(['window', 'loopStart', 'loopEnd'])
        self.assertIn('nullptr, {9, 2, 3, 0}},', table())
        # Without it the row leaves every code zero, and Shift turns stay fine steps.
        declare(None)
        self.assertNotIn('{1, 1, 1, 1}, nullptr', table())

    def test_only_a_sample_engines_own_screen_reads_the_overview_from_sdk_1_8(self):
        include = '#include <opxy/sample_overview.hpp>\n'
        # A synth at its own version, or one the header doesn't yet exist in.
        synth = self.copy('probesynth')
        source = synth / 'display.cpp'
        source.write_text(include + source.read_text(encoding='utf-8'), encoding='utf-8')
        self.numberless(synth, sdk='1.7')
        with self.assertRaisesRegex(ValueError, r'A sample overview \(opxy/sample_overview.hpp\) needs "sdk": "1.8"'):
            machines.machine_at(synth)
        self.numberless(synth, sdk='1.8')
        with self.assertRaisesRegex(ValueError, "Only a sample engine's own screen"):
            machines.machine_at(synth)
        # In its tests or tools it's the machine's own business.
        (synth / 'tests').mkdir(exist_ok=True)
        (synth / 'tests/screen.cpp').write_text(include, encoding='utf-8')
        source.write_text(source.read_text(encoding='utf-8').replace(include, ''), encoding='utf-8')
        self.assertEqual(machines.machine_at(synth).id, synth.name)
        # A sample engine without a display of its own has none to read it from.
        folder = self.copy('probesample')
        definition = json.loads((folder / 'definition.json').read_text(encoding='utf-8'))
        definition['resources'].pop('padSettings')
        definition['resources'].pop('drumControls')
        (folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
        (folder / 'display.cpp').write_text(include, encoding='utf-8')
        self.numberless(folder, base='sampler', sdk='1.8')
        with self.assertRaisesRegex(ValueError, "Only a sample engine's own screen"):
            machines.machine_at(folder)
        # With one, it may.
        synth_definition = json.loads((FOLDER / 'probesynth/definition.json').read_text(encoding='utf-8'))
        definition['resources']['display'] = synth_definition['resources']['display']
        (folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
        self.assertEqual(machines.machine_at(folder).display, 'drawing')

    def sampler(self, samples, presets, sdk='1.10', folder='samples'):
        """probesample as an SDK 1.10 sample engine forking the sampler, with its own samples and presets."""
        root = self.copy('probesample')
        definition = json.loads((root / 'definition.json').read_text(encoding='utf-8'))
        definition['resources'].pop('padSettings')
        definition['resources'].pop('drumControls')
        (root / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
        self.numberless(root, base='sampler', sdk=sdk, samples=folder, presets='presets')
        for name, files in (('samples', samples), ('presets', presets)):
            (root / name).mkdir()
            for file, value in files.items():
                data = value if isinstance(value, bytes) else json.dumps(value).encode()
                (root / name / file).write_bytes(data)
        return root

    @staticmethod
    def wav(frames=100, channels=1, rate=44100, width=2):
        import io, wave
        out = io.BytesIO()
        with wave.open(out, 'wb') as w:
            w.setnchannels(channels); w.setsampwidth(width); w.setframerate(rate)
            w.writeframes(bytes(frames * channels * width))
        return out.getvalue()

    def region(self, **changes):
        value = {'sample': 'content/samples/probesample/pad.wav', 'framecount': 100, 'sample.start': 0,
                 'sample.end': 100, 'pitch.keycenter': 48, 'hikey': 127, 'gain': 0, 'tune': 0, 'reverse': False,
                 'loop.start': 0, 'loop.end': 100, 'loop.crossfade': 0, 'loop.onrelease': False}
        return {key: v for key, v in {**value, **changes}.items() if v is not None}

    @staticmethod
    def riff(fmt, data, extra=b''):
        """A WAV file from its fmt and data chunk bodies, with `extra` chunks between them."""
        chunk = lambda tag, body: tag + len(body).to_bytes(4, 'little') + body + b'\0' * (len(body) & 1)
        body = b'WAVE' + chunk(b'fmt ', fmt) + extra + chunk(b'data', data)
        return b'RIFF' + len(body).to_bytes(4, 'little') + body

    @staticmethod
    def fmt(tag=1, channels=1, rate=44100, bits=16, extensible=False):
        align = channels * bits // 8
        value = b''.join(n.to_bytes(size, 'little') for n, size in (
            (0xfffe if extensible else tag, 2), (channels, 2), (rate, 4), (rate * align, 4), (align, 2), (bits, 2)))
        if extensible:  # cbSize, valid bits, channel mask and the PCM subformat GUID
            value += (22).to_bytes(2, 'little') + bits.to_bytes(2, 'little') + (4).to_bytes(4, 'little') + \
                     bytes.fromhex('0100000000001000800000aa00389b71')
        return value

    def test_a_sample_engine_ships_its_own_samples_for_its_presets(self):
        sample = self.preset(type='probesample', regions=[self.region()])
        stereo = self.preset(type='probesample', regions=[self.region(
            sample='content/samples/probesample/loop-2.wav', framecount=50, **{'sample.end': 50, 'loop.end': 50})])
        root = self.sampler({'pad.wav': self.wav(), 'loop-2.wav': self.wav(50, 2), 'README.md': b'# Samples'},
                            {'default.json': sample, 'empty.json': self.preset(type='probesample'), 'wide.json': stereo})
        m = machines.machine_at(root)
        self.assertEqual({name: (s.frames, s.channels) for name, s in m.samples.items()},
                         {'loop-2': (50, 2), 'pad': (100, 1)})
        self.assertEqual(m.presets['default']['regions'], [self.region()])
        self.assertEqual(machines.sample_path(m.id, 'pad'), 'content/samples/probesample/pad.wav')
        # The build installs each sample once, as it is, beside the presets naming it.
        from tooling import native
        from tooling.pipeline import config
        catalog = (machines.ROOT / 'profiles/opxy-1.2.1/content/op1_factory.json').read_bytes()
        outputs = native.prepare_assets(catalog, config(), [m])
        self.assertEqual(outputs['content/samples/probesample/pad.wav'], self.wav())
        self.assertEqual(sorted(p for p in outputs if p.startswith('content/samples/')),
                         ['content/samples/probesample/loop-2.wav', 'content/samples/probesample/pad.wav'])
        self.assertEqual(json.loads(outputs['content/presets/probesample/default.json'])['regions'], [self.region()])

    def test_a_sample_is_read_from_its_wav_chunks(self):
        # Chunks before the data, an odd-sized one padded, are skipped: the frames are the data's.
        listed = self.riff(self.fmt(channels=2), bytes(40), extra=b'LIST' + (3).to_bytes(4, 'little') + b'abc\0')
        root = self.sampler({'pad.wav': listed}, {'a.json': self.preset(type='probesample', regions=[
            self.region(framecount=10, **{'sample.end': 10, 'loop.end': 10})])})
        self.assertEqual((machines.machine_at(root).samples['pad'].frames, machines.machine_at(root).samples['pad'].channels),
                         (10, 2))
        self.assertEqual(machines.wav_format(self.wav(7, 2)), (1, 2, 44100, 16, 28, 4))

    def test_a_sample_engines_own_samples_are_checked(self):
        ok = {'pad.wav': self.wav()}
        preset = lambda *regions: self.preset(type='probesample', regions=list(regions))
        used = {'a.json': preset(self.region())}
        cases = {
            'old sdk': (ok, {'a.json': preset()}, '1.9', 'samples\\) needs "sdk": "1.10"'),
            'empty': ({}, {'a.json': preset()}, '1.10', '1 to 4 samples'),
            'too many': ({f's{i}.wav': self.wav() for i in range(5)}, {'a.json': preset()}, '1.10', '1 to 4 samples'),
            'name': ({'Pad 1.wav': self.wav()}, {'a.json': preset()}, '1.10', 'Invalid sample file'),
            'suffix': ({'pad.aif': self.wav()}, {'a.json': preset()}, '1.10', 'Invalid sample file'),
            'not wav': ({'pad.wav': b'RIFF'}, {'a.json': preset()}, '1.10', 'not a PCM WAV'),
            'rate': ({'pad.wav': self.wav(rate=48000)}, {'a.json': preset()}, '1.10', '16-bit PCM at 44100'),
            'width': ({'pad.wav': self.wav(width=3)}, {'a.json': preset()}, '1.10', '16-bit PCM at 44100'),
            'size': ({'pad.wav': self.wav(600000, 2)}, {'a.json': preset()}, '1.10', 'at most 2097152 bytes'),
            # Python's wave module reads extensible WAVs from 3.12 only: refused on every version.
            'extensible': ({'pad.wav': self.riff(self.fmt(extensible=True), bytes(200))}, used, '1.10',
                           'WAVE_FORMAT_EXTENSIBLE'),
            'float': ({'pad.wav': self.riff(self.fmt(tag=3, bits=32), bytes(400))}, used, '1.10', '16-bit PCM'),
            'truncated': ({'pad.wav': self.wav()[:-10]}, used, '1.10', 'not a PCM WAV.*past the end'),
            'odd data': ({'pad.wav': self.riff(self.fmt(channels=2), bytes(6))}, used, '1.10', '16-bit PCM'),
            'no data': ({'pad.wav': self.wav(0)}, used, '1.10', '16-bit PCM'),
            'no presets': (ok, None, '1.10', 'loaded by one of its presets.*pad'),
            'unused': ({'pad.wav': self.wav(), 'tail.wav': self.wav()}, used, '1.10', 'none loads tail'),
            'other sample': (ok, {'a.json': preset(self.region(sample='content/samples/probesample/kick.wav'))},
                             '1.10', "machine's own samples"),
            'user sample': (ok, {'a.json': preset(self.region(sample='/fat32/samples/user/a.wav'))}, '1.10',
                            "machine's own samples"),
            'framecount': (ok, {'a.json': preset(self.region(framecount=99))}, '1.10', 'framecount'),
            'range': (ok, {'a.json': preset(self.region(**{'sample.end': 101}))}, '1.10', 'framecount'),
            'loop': (ok, {'a.json': preset(self.region(**{'loop.end': 101}))}, '1.10', 'loop lies inside'),
            'loop past end': (ok, {'a.json': preset(self.region(**{'sample.end': 80}))}, '1.10', 'loop lies inside'),
            'loop before start': (ok, {'a.json': preset(self.region(**{'sample.start': 10}))}, '1.10',
                                  'loop lies inside its sample.start..sample.end \\(10..100\\)'),
            'gain high': (ok, {'a.json': preset(self.region(gain=21))}, '1.10', 'gain is -30..20'),
            'gain low': (ok, {'a.json': preset(self.region(gain=-31))}, '1.10', 'gain is -30..20'),
            'tune': (ok, {'a.json': preset(self.region(tune=12))}, '1.10', 'tune, pan.*at 0'),
            'pan': (ok, {'a.json': preset(self.region(pan=-20))}, '1.10', 'tune, pan.*at 0'),
            'crossfade': (ok, {'a.json': preset(self.region(**{'loop.crossfade': 100}))}, '1.10', 'at 0'),
            'key': (ok, {'a.json': preset(self.region(hikey=128))}, '1.10', '0..127'),
            'field': (ok, {'a.json': preset(self.region(slices=[]))}, '1.10', 'stock region fields'),
            'value': (ok, {'a.json': preset(self.region(reverse=0))}, '1.10', 'region values'),
            'missing': (ok, {'a.json': preset(self.region(framecount=None))}, '1.10', 'stock region fields'),
            'two regions': (ok, {'a.json': preset(self.region(), self.region())}, '1.10', 'at most 1 sample region'),
        }
        for case, (samples, presets, sdk, error) in cases.items():
            with self.subTest(case=case), self.assertRaisesRegex(ValueError, error):
                root = self.sampler(samples, presets or {}, sdk)
                if presets is None:
                    self.numberless(root, base='sampler', sdk=sdk, samples='samples', presets=None)
                machines.machine_at(root)
            shutil.rmtree(self.folder.parent / 'probesample')
        # Gain at its ends is kept.
        for gain in (-30, 20):
            root = self.sampler(ok, {'a.json': preset(self.region(gain=gain))})
            self.assertEqual(machines.machine_at(root).presets['a']['regions'][0]['gain'], gain)
            shutil.rmtree(root)
        # Only a sample engine forking the sampler ships samples: not a synth, and not the
        # drum sampler, whose pad regions save fields these checks don't cover yet.
        drum = self.copy('probesample')
        (drum / 'samples').mkdir()
        (drum / 'samples/pad.wav').write_bytes(self.wav())
        self.numberless(drum, base='drumSampler', sdk='1.10', samples='samples')
        with self.assertRaisesRegex(ValueError, 'forking the sampler'):
            machines.machine_at(drum)
        (self.folder / 'samples').mkdir()
        (self.folder / 'samples/pad.wav').write_bytes(self.wav())
        self.numberless(self.folder, sdk='1.10', samples='samples')
        with self.assertRaisesRegex(ValueError, 'forking the sampler'):
            machines.machine_at(self.folder)

    def test_a_kit_offers_a_sample_engine_without_its_own_samples(self):
        """The builder doesn't install a machine's own samples yet: a kit made from a collection
        holding such a machine still lists it and its presets, which load no sample there."""
        import contextlib, io
        from tooling import browser_kit
        sample = self.preset(type='probesample', regions=[self.region()])
        empty = self.preset(type='probesample')
        self.sampler({'pad.wav': self.wav()}, {'default.json': sample, 'tape.json': sample, 'empty.json': empty})
        self.copy('probefx')
        listed = self.folder.parent / 'collection.json'
        listed.write_text(json.dumps({'schema': 1, 'machines': [{'path': 'probefx'}, {'path': 'probesample'}]}),
                          encoding='utf-8')
        found = machines.collection(machines.read_collection(listed))
        self.assertEqual([m.id for m in found if m.samples], ['probesample'])
        notes = io.StringIO()
        with contextlib.redirect_stderr(notes):
            entries = {m.id: browser_kit.machine_entry(m, config(), {}, self.folder.parent / 'kit', 'clang++')
                       for m in found}
        self.assertEqual(set(entries), {'probefx', 'probesample'})
        # Every preset, its own default included, with no sample region (core.mjs presetFiles).
        self.assertEqual(entries['probesample']['presets'], {'default': empty, 'tape': empty, 'empty': empty})
        self.assertIn('probesample: the kit leaves out its own samples; its presets default, tape load no sample',
                      notes.getvalue())
        # The machine's own presets are left as they are, for sdk.py build.
        m = next(m for m in found if m.id == 'probesample')
        self.assertEqual(m.presets['tape']['regions'], [self.region()])
        self.assertEqual(browser_kit.kit_presets(replace(m, presets={'empty': empty})), {'empty': empty})

    def preset(self, **changes):
        value = {'engine': {'params': [0, 16384, 32767, 0, 0, 0, 0, 0], 'volume': 16384}, 'platform': 'OP-XY',
                 'regions': [], 'type': 'fmcopy', 'version': 4}
        return {**value, **changes}

    def with_presets(self, files, folder='presets', sdk='1.3'):
        self.rename('fmcopy', self.free, sdk)
        record = json.loads((self.folder / 'machine.json').read_text(encoding='utf-8'))
        record['presets'] = folder
        (self.folder / 'machine.json').write_text(json.dumps(record), encoding='utf-8')
        shutil.rmtree(self.folder / 'presets', ignore_errors=True)
        (self.folder / 'presets').mkdir()
        for name, value in files.items():
            data = value if isinstance(value, bytes) else json.dumps(value).encode()
            (self.folder / 'presets' / name).write_bytes(data)
        return lambda: machines.machine_at(self.folder)

    def test_a_machine_ships_its_own_presets(self):
        # Device-saved fields beyond the checked ones are kept as they are; a README is not a preset.
        load = self.with_presets({'warm-pad.json': self.preset(), 'bass_2.json': self.preset(octave=-1),
                                  'README.md': b'# Presets', '.DS_Store': b'\0'})
        m = load()
        self.assertEqual(sorted(m.presets), ['bass_2', 'warm-pad'])
        self.assertEqual(m.presets['bass_2']['octave'], -1)
        self.assertIn('external/fmcopy/presets/warm-pad.json', m.files())
        # Its own default.json replaces the generated init preset.
        self.assertEqual(sorted(self.with_presets({'default.json': self.preset()})().presets), ['default'])
        self.assertIsNone(machines.machine_at(FOLDER / 'probesynth').presets)

    def test_machine_presets_are_checked(self):
        cases = {
            'old sdk': ({'a.json': self.preset()}, 'presets', '1.2', 'needs "sdk": "1.3"'),
            'missing folder': ({'a.json': self.preset()}, 'elsewhere', '1.3', 'Missing machine path'),
            'outside': ({'a.json': self.preset()}, '..', '1.3', 'Invalid machine path|Missing machine path'),
            'empty': ({}, 'presets', '1.3', '1 to 16 presets'),
            'too many': ({f'p{i}.json': self.preset() for i in range(17)}, 'presets', '1.3', '1 to 16 presets'),
            'name': ({'Warm Pad.json': self.preset()}, 'presets', '1.3', 'Invalid preset file'),
            'suffix': ({'a.txt': b'{}'}, 'presets', '1.3', 'Invalid preset file'),
            'json': ({'a.json': b'{'}, 'presets', '1.3', 'invalid JSON'),
            'nan': ({'a.json': json.dumps(self.preset(gain=float('nan'))).encode()}, 'presets', '1.3', 'NaN is not JSON'),
            'size': ({'a.json': self.preset(pad='x' * 16384)}, 'presets', '1.3', 'at most 16384 bytes'),
            'machine': ({'a.json': self.preset(type='fm2op')}, 'presets', '1.3', 'version 4 OP-XY preset'),
            'version': ({'a.json': self.preset(version=3)}, 'presets', '1.3', 'version 4 OP-XY preset'),
            'samples': ({'a.json': self.preset(regions=[{'sample': 'kick.wav'}])}, 'presets', '1.3', 'sample regions'),
            'params': ({'a.json': self.preset(engine={'params': [0] * 7})}, 'presets', '1.3', 'eight engine params'),
            'range': ({'a.json': self.preset(engine={'params': [32768] + [0] * 7})}, 'presets', '1.3', 'eight engine'),
        }
        for case, (files, folder, sdk, error) in cases.items():
            with self.subTest(case=case), self.assertRaisesRegex(ValueError, error):
                self.with_presets(files, folder, sdk)()
        # Effects have no presets.
        path = FOLDER / 'probefx' / 'machine.json'
        real = Path.read_text
        value = dict(json.loads(real(path)), sdk='1.3', presets='.')
        with mock.patch.object(Path, 'read_text',
                               lambda self, *a, **k: json.dumps(value) if self == path else real(self, *a, **k)):
            with self.assertRaisesRegex(ValueError, 'instrument preset folder'):
                machines.machine_at(FOLDER / 'probefx')


if __name__ == '__main__':
    unittest.main()
