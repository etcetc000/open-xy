"""Updates replace one source while retaining IDs, other machines and Capture."""
import copy
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from tooling import manager, manager_releases
from tooling.pipeline import save_json

ROOT = Path(__file__).resolve().parents[1]


class UpdatePlans(unittest.TestCase):
    def setUp(self):
        self.temp=self.enterContext(tempfile.TemporaryDirectory());self.root=Path(self.temp)
        self.old_sources={}
        for name in ('keep','change','newchange'):
            path=self.root/name;path.mkdir()
            save_json(path/'definition.json',dict(id='change' if name=='newchange' else name,name=name,parameters=[],resources={}))
            save_json(path/'machine.json',dict(kind='effect',sdk='1.8'))
            (path/'engine.cpp').write_text(name)
            self.old_sources[name]=path
        capture=self.root/'capture';capture.mkdir();save_json(capture/'extension.json',dict(id='capture'))
        self.old_entries=[dict(path=str(self.root/'keep')),dict(path=str(self.root/'change')),dict(extension=str(capture))]
        self.layout=dict(keep=dict(effect=10,page=20),change=dict(effect=11,page=21),removed=dict(effect=12,page=22))
        self.sources={'external/keep/engine.cpp':'keep', 'external/change/engine.cpp':'old', 'extension:capture/runtime.cpp':'capture'}
        self.previous=self.root/'previous';(self.previous/'analysis').mkdir(parents=True)
        save_json(self.previous/'collection.json',dict(collection=self.old_entries))
        save_json(self.previous/'layout.json',dict(machines=self.layout))
        save_json(self.previous/'analysis/compile-report.json',dict(sources=self.sources))
        self.description=dict(kernel='ab'*32,digest='cd'*32,linked=['keep','change'],collection=self.old_entries)
        entry=dict(id='change',kind='effect',maintainer='tester',status='published',repository='https://github.com/tester/change',
                   tag='v2.0',commit='a'*40,path='.',compatibility=dict(state='sdk-compatible',message='Build required'))
        self.store=manager_releases.Releases(ROOT,set())
        self.store.active=dict(channel='stable',revision='b'*40,machines=[entry])
        self.m=manager.Manager.__new__(manager.Manager)
        self.m.releases=self.store;self.m.offline=False;self.m.local={};self.m.prepared={};self.m.home=self.root/'builds';self.m.home.mkdir()
        self.m.config=dict(python='python',sdk=str(ROOT),kernel_package=str(self.previous),toolchain=str(ROOT),compiler=str(ROOT))
        self.m.known={'old':str(self.previous)}
        self.m.current=dict(can_manage=True,kernel='kernel',selection=dict(digest='old'),attempt=dict(state='confirmed'),inventory=dict(
            machines=[dict(id=n,name=n,kind='effect',identity=self.layout[n],source=dict(type='installed')) for n in ('keep','change')],extensions=['capture']))
        self.m.connect=Mock(side_effect=RuntimeError('No device access during preparation'))
        self.payload=dict(channel='stable',catalog_revision='b'*40,update=['change'])
        self.output=None;self.corrupt=None
        self.enterContext(patch.object(manager.catalog,'fetch',return_value=SimpleNamespace(root=self.root/'newchange',kind='effect')))
        self.build=self.enterContext(patch.object(manager.subprocess,'run',side_effect=self.compile))
        self.enterContext(patch.object(manager.collection_bundle,'read_bundle',side_effect=self.bundle))

    def compile(self,command,**kwargs):
        self.output=Path(command[command.index('--out')+1]);(self.output/'analysis').mkdir(parents=True)
        selection=json.loads(Path(command[command.index('--collection')+1]).read_text())
        entries=[dict(path=x['path']) for x in selection['machines']]+[dict(extension=x['path']) for x in selection['extensions']]
        sources=dict(self.sources,**{'external/change/engine.cpp':'new'})
        if self.corrupt=='keep':sources['external/keep/engine.cpp']='modified'
        if self.corrupt=='capture':sources['extension:capture/runtime.cpp']='modified'
        layout=copy.deepcopy(self.layout)
        if self.corrupt=='identity':layout['change']['effect']=13
        save_json(self.output/'layout.json',dict(machines=layout))
        save_json(self.output/'analysis/compile-report.json',dict(sources=sources,linked=['keep','change'],sizes=dict(allocated_span_bytes=123)))
        save_json(self.output/'collection.json',dict(self.description,collection=entries))
        return SimpleNamespace(returncode=0)

    def bundle(self,path):
        if Path(path)==self.previous:return self.description,{},b'',{}
        return json.loads((Path(path)/'collection.json').read_text()),{},b'',{}

    def test_update_is_one_plan_preserving_other_sources_capture_and_full_id_ledger(self):
        before=copy.deepcopy(self.m.current)
        result=self.m.prepare(self.payload)
        self.assertEqual([m['id'] for m in result['machines']],['keep','change'])
        self.assertEqual(result['extensions'],['capture'])
        self.assertEqual(result['update'],[dict(id='change',before='Saved build',after='v2.0',notes=[])])
        self.assertEqual(result['machines'][1]['source']['commit'],'a'*40)
        self.assertEqual(result['machines'][1]['source']['channel'],'stable')
        self.assertEqual(json.loads((self.output/'layout.json').read_text())['machines'],self.layout)
        self.assertEqual(self.m.current,before);self.m.connect.assert_not_called();self.build.assert_called_once()

    def test_changed_retained_source_capture_or_machine_numbers_refuse_the_plan(self):
        for field in ('keep','capture','identity'):
            self.corrupt=field
            with self.assertRaises(ValueError,msg=field):self.m.prepare(self.payload)
        self.assertFalse(self.m.prepared);self.m.connect.assert_not_called()

    def test_update_mapping_change_rejected_before_build_or_device_access(self):
        parameter=dict(id='tone',mapping=dict(kind='linear',minimum=0,maximum=1),label='Tone',display='percent',default=0)
        for name in ('change','newchange'):
            save_json(self.root/name/'definition.json',dict(id='change',name=name,resources={},parameters=[parameter]))
        changed=copy.deepcopy(parameter);changed['mapping']['maximum']=2
        save_json(self.root/'newchange/definition.json',dict(id='change',resources={},parameters=[changed]))
        with self.assertRaisesRegex(ValueError,'reinterpret saved controls'):self.m.prepare(self.payload)
        self.build.assert_not_called();self.m.connect.assert_not_called()

    def test_conflicting_update_or_stale_channel_never_builds(self):
        for extra in (dict(remove=['change']),dict(catalog_revision='e'*40),dict(channel='staging')):
            with self.assertRaises(ValueError):self.m.prepare(dict(self.payload,**extra))
        self.build.assert_not_called();self.m.connect.assert_not_called()

    def test_unavailable_release_is_rejected_and_stale_apply_never_opens_device(self):
        self.store.active['machines'][0]['compatibility']['state']='firmware-update-required'
        with self.assertRaises(ValueError):self.m.prepare(self.payload)
        self.build.assert_not_called()
        self.m.prepared['old']=dict(channel='stable',catalog_revision='e'*40)
        with self.assertRaisesRegex(ValueError,'Catalog changed'):self.m.apply(dict(plan='old'))
        self.m.connect.assert_not_called()
