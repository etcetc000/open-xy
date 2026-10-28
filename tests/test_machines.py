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

from tooling import machines
from tooling.pipeline import config
from machine_fixtures import FOLDER, NAMES, fixture, fixture_entries

RULES = config()['identities']


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
        # A collection keeps its declared order; the SDK has no default collection.
        self.assertEqual([m.id for m in machines.collection(fixture_entries())], list(NAMES))
        for entries in (None, []):
            with self.subTest(entries=entries), self.assertRaisesRegex(ValueError, 'Collection needs machines'):
                machines.collection(entries)

    def test_stock_occupied_or_sentinel_ids_are_rejected(self):
        for kind, change in (('synth', dict(engine=33)), ('synth', dict(engine=2)), ('synth', dict(engine=63)),
                             ('synth', dict(page=81)), ('synth', dict(page=128)), ('synth', dict(engine=True)),
                             ('sample', dict(engine=26)), ('sample', dict(page=82)),
                             ('effect', dict(effect=0)), ('effect', dict(effect=5)), ('effect', dict(page=20))):
            with self.subTest(kind=kind, change=change), self.assertRaises(ValueError):
                machines.check_identity(RULES, kind, identity(kind, **change))

    def test_own_screen_pages_come_from_the_relocated_range(self):
        machines.check_identity(RULES, 'synth', identity('synth'))
        for page in (2, 21, 50, 81):
            with self.subTest(page=page), self.assertRaises(ValueError):
                machines.check_identity(RULES, 'synth', identity('synth', page=page))

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


class OutsideMachines(unittest.TestCase):
    """A machine in a folder of its own, such as its own repository."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name).resolve() / 'opxy-fmcopy'
        shutil.copytree(FOLDER / 'probesynth', self.folder)
        self.free = machines.free_synth_identity()
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
        # A machine asking for a fixed number is given the lowest free one.
        self.rename('fmcopy', dict(self.free, engine=34))
        fmcopy = machines.collection(entries)[-1]
        self.assertEqual((fmcopy.identity, fmcopy.requested), (self.free, dict(self.free, engine=34)))

    def test_sdk_version_and_fixed_names_are_checked(self):
        for version in ('1.1', '1.2', '1.3', '1.4', '1.5'):
            self.rename('fmcopy', self.free, version)
            self.assertEqual(machines.machine_at(self.folder).id, 'fmcopy')
        for version in ('0.9', '2.0', '1.6', '1', 'latest', None):
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
            with self.assertRaisesRegex(ValueError, 'effects have none'):
                machines.machine_at(FOLDER / 'probefx')


if __name__ == '__main__':
    unittest.main()
