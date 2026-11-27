"""Auditions pin source identity and never contact a device or prepare an install."""
import base64
import copy
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch

from machine_fixtures import fixture, FOLDER
from tooling import manager, manager_audition, manager_releases
from tooling.formats import sha
from tooling.pipeline import ROOT, save_json


class Sources(unittest.TestCase):
    def setUp(self):
        self.root = Path(self.enterContext(tempfile.TemporaryDirectory())).resolve()
        self.m = manager.Manager.__new__(manager.Manager)
        self.m.home = self.root / 'manager'; self.m.home.mkdir()
        self.m.config = {}; self.m.local = {'probefx':dict(path=str(FOLDER / 'probefx'))}
        self.m.connect = Mock(side_effect=RuntimeError('Auditions must never open USB'))
        self.m.prepared = {'keep':'reviewed plan'}
        previous = self.root / 'previous'; (previous / 'analysis').mkdir(parents=True)
        save_json(previous / 'collection.json', dict(collection=[dict(path=str(FOLDER / 'probefx'))]))
        self.hashes = {k:sha(p.read_bytes()) for k,p in fixture('probefx').files().items()}
        save_json(previous / 'analysis/compile-report.json', dict(sources=self.hashes))
        self.m.known = {'saved':str(previous)}
        self.m.current = dict(selection=dict(digest='saved'))
        self.m.releases = manager_releases.Releases(ROOT, set())
        self.m.releases.active = dict(channel='stable',revision='a'*40,machines=[dict(id='probefx',tag='v2',commit='b'*40)])
        self.build = self.enterContext(patch.object(manager.manager_audition, 'build', return_value=dict(id='probefx',kind='effect')))
        self.fetch = self.enterContext(patch.object(manager.catalog, 'fetch',return_value=fixture('probefx')))

    def test_all_origins_snapshot_the_exact_source_without_selection_or_device_changes(self):
        before = copy.deepcopy(self.m.current)
        for source in ('installed','catalog','local'):
            result = self.m.audition(dict(source=source,id='probefx',channel='stable',catalog_revision='a'*40))
            root = self.build.call_args.args[0]
            self.assertNotEqual(root, str(FOLDER / 'probefx'))
            self.assertEqual((Path(root) / 'fx.cpp').read_bytes(), (FOLDER / 'probefx/fx.cpp').read_bytes())
            self.assertEqual(result['source'],source)
        self.assertEqual(self.m.current,before); self.assertEqual(self.m.prepared, {'keep':'reviewed plan'})
        self.m.connect.assert_not_called(); self.fetch.assert_called_once()

    def test_stale_catalog_unknown_paths_and_modified_installed_source_are_refused(self):
        for payload in (dict(source='catalog',id='probefx',channel='staging',catalog_revision='a'*40),
                        dict(source='local',id='/arbitrary/path'),dict(source='path',id='probefx')):
            with self.assertRaises(ValueError): self.m.audition(payload)
        report = Path(self.m.known['saved']) / 'analysis/compile-report.json'
        save_json(report, dict(sources={**self.hashes,'external/probefx/fx.cpp':'0'*64}))
        with self.assertRaisesRegex(ValueError,'source changed'): self.m.audition(dict(source='installed',id='probefx'))
        self.build.assert_not_called(); self.fetch.assert_not_called(); self.m.connect.assert_not_called()


@unittest.skipUnless(shutil.which('clang++') and shutil.which('node'), 'Clang with wasm-ld and Node are required')
class Compilation(unittest.TestCase):
    def setUp(self):
        self.root = Path(self.enterContext(tempfile.TemporaryDirectory())).resolve()

    def build(self, source, name):
        out = self.root / name; out.mkdir()
        result = manager_audition.build(source, out)
        self.assertEqual(sha(base64.b64decode(result['wasm'])), result['wasm_sha256'])
        return result, out

    def test_synth_and_effect_run_real_wasm_contracts_and_effect_bypass(self):
        result, _ = self.build(FOLDER / 'probesynth','synth')
        self.assertEqual(result['kind'],'synth')
        result, out = self.build(FOLDER / 'probefx','effect')
        self.assertEqual(result['dry_parameter'],3)
        check = subprocess.run(['node',str(ROOT / 'tests/fixtures/effect_audition.mjs'),str(out / 'engine.wasm')],
                               capture_output=True,text=True,timeout=60)
        self.assertEqual(check.returncode,0,check.stdout+check.stderr)

    def test_buffered_and_tempo_aware_effect_signatures(self):
        for buffered, transport in ((True,False),(False,True),(True,True)):
            name = f'fx-{buffered}-{transport}'
            source = self.root / name; shutil.copytree(FOLDER / 'probefx',source)
            definition = json.loads((source / 'definition.json').read_text())
            definition['resources'] = dict(bufferBytes=524288) if buffered else {}
            if transport: definition['resources']['transport'] = 1
            save_json(source / 'definition.json',definition)
            extra = ('opxy::Buffer buffer, ' if buffered else '') + ('const opxy::Transport& transport, ' if transport else '')
            for file in ('core.h','fx.cpp'):
                path = source / file
                path.write_text(path.read_text().replace('State& state, opxy::Channel', 'State& state, '+extra+'opxy::Channel'))
            metadata = json.loads((source/'machine.json').read_text());metadata['sdk']='1.8';metadata.pop('identity',None);save_json(source/'machine.json',metadata)
            result, _ = self.build(source,name+'-preview')
            self.assertEqual(result['kind'],'effect')
