"""Catalog channels, project-safe updates and device-free previews."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

from tooling import manager, manager_releases as releases
from tooling.formats import sha
import test_collection_install as install_fixture

ROOT = Path(__file__).resolve().parents[1]


class ChannelTests(unittest.TestCase):
    def entry(self, name='testfx', status='published'):
        return dict(id=name, kind='effect', maintainer='tester', status=status,
                    repository='https://github.com/tester/machines', tag='v1.2.0', commit='a'*40, path='fx')

    def store(self, sdk='1.8'):
        entries = [self.entry(), self.entry('draftfx','draft')]
        def read(url):
            if url.endswith('catalog/machines.json'):return dict(schema=1,machines=entries)
            return dict(schema=1,sdk=sdk,kind='effect')
        return releases.Releases(ROOT, {'players','drums','samples'}, read=read, resolve=lambda branch:('b' if branch=='main' else 'c')*40)

    def test_channels_use_pinned_revisions_and_stable_excludes_drafts(self):
        store=self.store()
        stable=store.select('stable');self.assertEqual(stable['revision'],'b'*40)
        self.assertEqual([m['id'] for m in stable['machines']],['testfx'])
        stage=store.select('staging');self.assertEqual(stage['revision'],'c'*40)
        self.assertEqual(len(stage['machines']),2)
        self.assertTrue(all(m['experimental'] for m in stage['machines']))
        with self.assertRaises(ValueError):store.selected(dict(channel='stable',catalog_revision='b'*40))
        self.assertEqual(len(store.selected(dict(channel='staging',catalog_revision='c'*40))),2)
        with self.assertRaises(ValueError):store.select('../other')

    def test_newer_sdk_and_missing_capabilities_are_blocked_without_claiming_build_acceptance(self):
        c=self.store('9.0').select('stable')['machines'][0]['compatibility']
        self.assertEqual(c['state'],'firmware-update-required')
        self.assertEqual(releases.compatibility(dict(sdk='1.8',kind='player'),(1,9),set())['state'],'firmware-update-required')
        c=self.store().select('stable')['machines'][0]['compatibility']
        self.assertEqual(c['state'],'sdk-compatible');self.assertIn('full build required',c['message'])

    def test_metadata_failures_are_unavailable_and_never_treated_as_compatible(self):
        store=self.store()
        read=store.read
        store.read=lambda url: read(url) if url.endswith('catalog/machines.json') else (_ for _ in ()).throw(OSError('offline'))
        self.assertEqual(store.select('stable')['machines'][0]['compatibility']['state'],'unavailable')

    def test_saved_commit_requires_clean_checkout_and_exact_build_source_hashes(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);(root/'engine.cpp').write_bytes(b'original')
            pins={'external/mine/engine.cpp':sha(b'original')}
            with patch.object(releases.catalog,'head',return_value='a'*40),patch.object(releases.catalog,'untouched',return_value=True):
                self.assertEqual(releases.saved_origin(root,'mine',pins)['commit'],'a'*40)
                (root/'engine.cpp').write_bytes(b'changed')
                self.assertNotIn('commit',releases.saved_origin(root,'mine',pins))
            (root/'engine.cpp').write_bytes(b'original')
            with patch.object(releases.catalog,'head',return_value='a'*40),patch.object(releases.catalog,'untouched',return_value=False):
                self.assertNotIn('commit',releases.saved_origin(root,'mine',pins))

    def test_control_reordering_and_sample_format_changes_refuse_updates(self):
        with tempfile.TemporaryDirectory() as temp:
            old,new=Path(temp)/'old',Path(temp)/'new';old.mkdir();new.mkdir()
            parameter=dict(id='level',mapping=dict(kind='linear',minimum=0,maximum=1),label='Level',display='percent',default=0)
            definition=dict(id='same',parameters=[parameter],resources={})
            for root in (old,new):
                (root/'definition.json').write_text(json.dumps(definition))
                (root/'machine.json').write_text(json.dumps(dict(kind='synth',sdk='1.8')))
            self.assertEqual(releases.unchanged_controls(old,new),[])
            changed=copy.deepcopy(definition);changed['parameters'][0]['mapping']['maximum']=2
            (new/'definition.json').write_text(json.dumps(changed))
            with self.assertRaisesRegex(ValueError,'reinterpret saved controls'):releases.unchanged_controls(old,new)
            (new/'definition.json').write_text(json.dumps(dict(definition,resources=dict(modelCount=5))))
            with self.assertRaisesRegex(ValueError,'saved drum/sample'):releases.unchanged_controls(old,new)


class OfflineTests(unittest.TestCase):
    def test_saved_preview_and_apply_refusal_never_open_the_device(self):
        fixture=install_fixture.CollectionInstall();fixture.setUp();self.addCleanup(fixture.doCleanups)
        description=json.loads((fixture.bundle/'collection.json').read_text())
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'config.json'
            p.write_text(json.dumps(dict(sdk=str(ROOT),kernel_package=str(fixture.bundle),current_collection=str(fixture.bundle),
                                        python=str(ROOT/'sdk.py'),toolchain=str(ROOT),compiler=str(ROOT))))
            connect=Mock(side_effect=RuntimeError('Device must never be opened'))
            store=Mock();store.version=(1,9);store.services=set()
            with patch.object(manager,'outside_checkouts',return_value=True),patch.object(manager.device,'private_root',return_value=Path(temp)):
                m=manager.Manager(p,connect=connect,offline=True,releases=store)
            m.describe_bundle=lambda _:dict(machines=[],extensions=['capture'])
            state=m.read_status()
            self.assertTrue(state['offline']);self.assertFalse(state['can_manage']);self.assertTrue(state['can_prepare'])
            self.assertEqual(state['selection']['digest'],description['digest'])
            self.assertEqual(state['attempt']['state'],'not read')
            with self.assertRaisesRegex(ValueError,'Offline preview'):m.apply(dict(plan='anything'))
            connect.assert_not_called()
