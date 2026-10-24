"""machine.json: kinds, displays and the stock identity each machine declares."""
import copy
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

from tooling import machines
from tooling.pipeline import config

RULES = config()['identities']


def identity(kind, **changes):
    value = {'synth': dict(engine=34, page=82), 'sample': dict(engine=35, page=2),
             'effect': dict(effect=11, page=21)}[kind]
    return dict(value, **changes)


class Identity(unittest.TestCase):
    def test_bundled_machines_declare_valid_identities(self):
        # Every bundled machine is valid and part of the collection, in its declared order.
        listed = json.loads(machines.COLLECTION.read_text(encoding='utf-8'))['machines']
        self.assertEqual({m.id for m in machines.discovered()}, set(listed))
        self.assertEqual([m.id for m in machines.collection()], listed)

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
        for name in ('a' * 16, 'Bad', 'two-op', '', 'drum', 'reverb', 'sdk', 'kit_probe', 'extensions'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                machines.machine(name)
        self.assertEqual((machines.machine('fm2op').native_name, machines.machine('fm2op').prefix), ('fm2op', 'fm2op'))
        with self.assertRaises(ValueError):
            machines.machine('2opfm')

    def test_collection_rejects_conflicting_identities(self):
        original = machines.machine

        def clash(name):
            m = original(name)
            return m if name != 'timestretch' else machines.Machine(**dict(m.__dict__, identity=dict(engine=34, page=2)))
        with mock.patch.object(machines, 'machine', clash), self.assertRaisesRegex(ValueError, 'Conflicting'):
            machines.collection()

    def test_unknown_fields_kinds_and_abi_combinations_are_rejected(self):
        path = machines.ROOT / 'machines/fm2op/machine.json'
        original = json.loads(path.read_text(encoding='utf-8'))
        for change in (dict(extra=1), dict(kind='instrument'), dict(display='segments'), dict(compiler='opxy'),
                       dict(sources=['../macroosc/engine.cpp'])):
            value = dict(copy.deepcopy(original), **change)
            text = json.dumps(value)
            real = Path.read_text
            with self.subTest(change=change), mock.patch.object(
                    Path, 'read_text', lambda self, *a, **k: text if self.name == 'machine.json' else real(self, *a, **k)):
                with self.assertRaises(ValueError):
                    machines.machine('fm2op')

    def test_dry_parameter_requires_a_normalized_effect_control(self):
        real = Path.read_text
        for identity, field in (('ringmod', 'missing'), ('ringmod', 3), ('ringmod', None),
                                ('ringmod', 'frequency'), ('ringmod', 'low_cut'), ('fm2op', 'dry')):
            path = machines.ROOT / 'machines' / identity / 'machine.json'
            value = dict(json.loads(real(path)), dryParameter=field)
            with self.subTest(identity=identity, field=field), mock.patch.object(
                    Path, 'read_text', lambda self, *a, **k: json.dumps(value) if self == path else real(self, *a, **k)):
                with self.assertRaisesRegex(ValueError, 'dryParameter'):
                    machines.machine(identity)
        self.assertEqual(machines.machine('ringmod').dry_parameter, 3)
        self.assertIsNone(machines.machine('fm2op').dry_parameter)
        path = machines.ROOT / 'machines/ringmod/machine.json'
        value = json.loads(real(path))
        del value['dryParameter']
        with mock.patch.object(Path, 'read_text',
                               lambda self, *a, **k: json.dumps(value) if self == path else real(self, *a, **k)):
            self.assertIsNone(machines.machine('ringmod').dry_parameter)


class OutsideMachines(unittest.TestCase):
    """A machine in a folder of its own, such as its own repository."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name).resolve() / 'opxy-fmcopy'
        shutil.copytree(machines.ROOT / 'machines/fm2op', self.folder)
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
        self.assertEqual((m.id, m.root, m.bundled), ('fmcopy', self.folder, False))
        self.assertEqual(m.source('engine.cpp'), self.folder.as_posix() + '/engine.cpp')
        self.assertIn('external/fmcopy/engine.cpp', m.files())
        bundled = machines.machine('fm2op')
        self.assertTrue(bundled.bundled)
        self.assertEqual(bundled.source('engine.cpp'), 'machines/fm2op/engine.cpp')
        self.assertIn('machines/fm2op/engine.cpp', bundled.files())

    def test_collections_list_bundled_ids_and_folders(self):
        collection = Path(self.temporary.name) / 'collection.json'
        collection.write_text(json.dumps({'schema': 1, 'machines': ['fm2op', {'path': 'opxy-fmcopy'}]}), encoding='utf-8')
        entries = machines.read_collection(collection)
        self.assertEqual(entries, ['fm2op', {'path': str(self.folder)}])
        self.assertEqual([m.id for m in machines.collection(entries)], ['fm2op', 'fmcopy'])
        extra = machines.read_collection(machines.COLLECTION, [self.folder])
        self.assertEqual([m.id for m in machines.collection(extra)][-1], 'fmcopy')
        self.rename('fmcopy', dict(self.free, engine=34))
        with self.assertRaisesRegex(ValueError, 'Conflicting'):
            machines.collection(entries)

    def test_sdk_version_and_bundled_names_are_checked(self):
        for version in ('0.9', '2.0', '1.1', '1', 'latest', None):
            with self.subTest(version=version):
                self.rename('fmcopy', self.free, version)
                with self.assertRaises(ValueError):
                    machines.machine_at(self.folder)
        self.rename('fm2op', self.free)
        with self.assertRaisesRegex(ValueError, 'bundled'):
            machines.machine_at(self.folder)


if __name__ == '__main__':
    unittest.main()
