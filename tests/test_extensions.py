"""Extension folders, the claim and action registry, event sites, the extension catalog
and build provenance. Extensions live in their own repositories; the SDK's probe
extension (tests/fixtures/extension_probe) stands in for one here."""
import copy
import json
from pathlib import Path
import re
import shutil
import tempfile
import unittest
from unittest import mock
from tooling import catalog, extensions, native
from tooling.machines import read_collection
from tooling.pipeline import ROOT, config, document

PROBE = ROOT / 'tests/fixtures/extension_probe'


class Folder:
    """A copy of the probe extension to change."""

    def __enter__(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='extension-')
        self.root = Path(self.temporary.name) / 'probe'
        shutil.copytree(PROBE, self.root)
        return self

    def __exit__(self, *_):
        self.temporary.cleanup()

    def declare(self, **changes):
        path = self.root / 'extension.json'
        data = json.loads(path.read_text(encoding='utf-8'))
        data.update(changes)
        path.write_text(json.dumps({k: v for k, v in data.items() if v is not None}), encoding='utf-8')
        return self.root


class Extensions(unittest.TestCase):
    def test_a_folder_declares_itself_against_the_registry(self):
        probe = extensions.extension_at(PROBE)
        self.assertEqual((probe.id, probe.claims, probe.actions), ('probe', ('record_double_tap',), ('edit_notes', 'notify')))
        for changes in (
            dict(id='../probe'), dict(id='runtime'), dict(id='delete'),  # paths, SDK objects, C++ keywords
            dict(claims=[]),  # edit_notes needs its claim
            dict(claims=['shift_hold']),  # not in the registry
            dict(actions=['edit_notes', 'edit_notes']),
            dict(extra=True), dict(origin='official'),  # origin comes from the catalog, not the folder
            dict(description='x' * 141), dict(note=''), dict(name=None),
        ):
            with self.subTest(**{k: str(v) for k, v in changes.items()}), Folder() as folder, self.assertRaises(ValueError):
                extensions.extension_at(folder.declare(**changes))
        with Folder() as folder, self.assertRaisesRegex(ValueError, 'conditional compilation'):
            (folder.root / 'extension.cpp').write_text('#ifdef X\n#endif\n', encoding='utf-8')
            extensions.extension_at(folder.root)

    def test_a_collection_holds_each_extension_and_claim_once(self):
        probe = extensions.extension_at(PROBE)
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            extensions.check_collection([probe, probe])
        other = extensions.Extension('other', PROBE, ('record_double_tap',), (), {})
        with self.assertRaisesRegex(ValueError, 'multiple owners'):
            extensions.check_collection([probe, other])

    def test_the_registry_is_claims_and_actions_only(self):
        for mutate in (lambda d: d['claims'].update(bad={'label': ''}),
                       lambda d: d['actions'].update(other={'requires': 'missing'}),
                       lambda d: d.update(extensions=[]), lambda d: d.update(schema=2)):
            data = document(ROOT / extensions.PROFILE)
            mutate(data)
            with mock.patch.object(extensions, 'document', return_value=data), self.assertRaises(ValueError):
                extensions.registry()

    def test_actions_match_the_runtime_bits_and_claims_are_data(self):
        # An action is SDK code: one registry entry and one runtime constant, in
        # registry order. A claim is data: a registry entry naming its key.
        header = (ROOT / extensions.RUNTIME).read_text(encoding='utf-8')
        claims, actions = extensions.registry()
        declared = re.findall(r'const u32 action_(\w+) = 1u << (\d+);', header)
        self.assertEqual(declared, [(name, str(i)) for i, name in enumerate(actions)])
        self.assertNotRegex(header, r'const u32 claim_')
        self.assertTrue(all(type(c['key']) is int for c in claims.values()))

    def test_every_extension_hook_names_its_events_and_every_event_has_a_site(self):
        profile = config()
        extensions.check_profile(profile)
        self.assertEqual(extensions.events(), ['context', 'recordable_note_on', 'selected_note_off',
                                               'recording_tick', 'input_activity', 'queued_control'])
        hook = lambda p: next(x for x in p['patches'] if x.get('events'))
        for mutate in (
            lambda p: hook(p).pop('events'),
            lambda p: hook(p).update(events=['note_on']),
            lambda p: hook(p).update(events=[]),
            lambda p: next(x for x in p['patches'] if not x.get('service')).update(events=['context']),
            lambda p: [x.update(events=['context']) for x in p['patches'] if x.get('events')],
        ):
            changed = copy.deepcopy(profile)
            mutate(changed)
            with self.assertRaises(ValueError):
                extensions.check_profile(changed)

    def test_the_kit_carries_text_claims_labels_and_origin(self):
        probe = extensions.extension_at(PROBE)
        [entry] = extensions.metadata([probe], {'probe': 'personal'})
        self.assertEqual(entry['origin'], 'personal')
        self.assertEqual(entry['claim_labels'], {'record_double_tap': 'Record double-tap'})
        self.assertLessEqual(len(entry['description']), extensions.TEXT_LIMITS['description'])

    def test_extensions_reach_a_build_through_its_collection(self):
        with tempfile.TemporaryDirectory() as folder:
            listing = Path(folder) / 'collection.json'
            listing.write_text(json.dumps({'schema': 1, 'machines': [], 'extensions': [{'path': str(PROBE)}]}))
            self.assertEqual(read_collection(listing), [{'extension': str(PROBE.resolve())}])
            self.assertEqual(read_collection(None, (), [PROBE]), [{'extension': str(PROBE.resolve())}])
        probe = extensions.extension_at(PROBE)
        machines = native.Collection([], [probe])
        sources = native.provenance(machines)
        self.assertIn('profiles/extensions.json', sources)
        self.assertIn('extension:probe/extension.cpp', sources)
        self.assertIn(('sdk/extension_probe.o', None, 'extension:probe'), native.plan(machines))
        self.assertIn('extensions', native.active_services(config(), native.service_users(machines)))
        # A subset of the machines keeps the collection's extensions.
        self.assertEqual(native.subset(machines, []).extensions, (probe,))

    def test_no_extensions_means_no_extension_objects_or_service(self):
        self.assertFalse(any(step.startswith('extension:') for _, _, step in native.plan([])))
        self.assertFalse(any(name.startswith('extension:') for name in native.provenance([])))
        self.assertNotIn('extensions', native.active_services(config(), native.service_users([])))


class ExtensionCatalog(unittest.TestCase):
    def entry(self, **changes):
        entry = dict(id='capture', kind='extension', maintainer='etcetc000', status='published',
                     repository='https://github.com/etcetc000/open-xy-core-machines', tag='v1.6.0',
                     commit='a' * 40, path='extensions/capture')
        entry.update(changes)
        return {k: v for k, v in entry.items() if v is not None}

    def test_entries_have_the_machine_catalog_format(self):
        self.assertEqual(catalog.validate({'schema': 1, 'extensions': [self.entry()]}, 'extensions')[0]['id'], 'capture')
        for bad in (dict(kind='synth'), dict(id='runtime'), dict(cpu={}), dict(commit=None), dict(path='../x')):
            with self.subTest(**{k: str(v) for k, v in bad.items()}), self.assertRaises(ValueError):
                catalog.validate({'schema': 1, 'extensions': [self.entry(**bad)]}, 'extensions')
        with self.assertRaises(ValueError):  # extensions belong in their own catalog
            catalog.validate({'schema': 1, 'machines': [self.entry()]})

    def test_updates_keep_entries_and_need_no_cpu(self):
        before = [self.entry()]
        catalog.changes(before, [self.entry(commit='b' * 40, tag='v1.7.0')])
        with self.assertRaises(ValueError):
            catalog.changes(before, [])
        with self.assertRaises(ValueError):
            catalog.changes(before, [self.entry(repository='https://github.com/someone/else')])

    def test_official_extensions_are_listed_by_id(self):
        self.assertTrue(catalog.official(self.entry()))
        self.assertFalse(catalog.official(self.entry(id='echo')))
        self.assertEqual([e['id'] for e in catalog.load_extensions() if catalog.official(e)], ['capture'])
