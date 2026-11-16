"""RAM installation uses existing backup/readback transactions and selects last."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
from tooling import collection_activation as a,collection_kernel as k,collection_bundle as b,device
from tooling.pipeline import config,save_json
from tooling.formats import sha
from test_device import FakeDevice,candidate


class CollectionInstall(unittest.TestCase):
    def setUp(self):
        self.temp=self.enterContext(tempfile.TemporaryDirectory());self.root=Path(self.temp)
        self.enterContext(contextlib.redirect_stdout(io.StringIO()))
        self.interface=a.interface(config(),set(config()['services']),True,k.RESIDENT_BYTES)
        self.platform=bytes(range(32));self.abi=a.fingerprint(self.interface)
        self.info=k.information(dict(collection_platform=self.platform.hex(),collection_interface=self.interface,linked=['keep']),sha(b'OS'))
        base=self.interface['geometry']['payload_base'];count=len(self.interface['entries']);text=count*4+8
        payload=bytearray(text+80);resources={}
        for i in range(count):payload[i*4:i*4+4]=a.entry_instruction(base+i*4,base+count*4)
        for i,(kind,data) in enumerate(((1,b'<svg/>'),(2,b'{}'))):
            digest=hashlib.sha256(data).digest();at=text+i*40
            payload[at:at+32]=digest;struct.pack_into('<2I',payload,at+32,len(data),kind)
            resources[('asset-' if kind==1 else 'catalog-')+digest.hex()+'.oxc']=data
        self.image=a.encode(payload,base=base,capacity=self.interface['geometry']['payload_bytes'],
            bss_offset=len(payload),bss_bytes=64,text_bytes=text,entries=count,platform=self.platform,abi=self.abi,
            resources_offset=text,resources_count=2)
        self.bundle=self.root/'bundle';self.bundle.mkdir()
        (self.bundle/'bundle.oxc').write_bytes(self.image);(self.bundle/'kernel.oxc').write_bytes(self.info)
        candidate(self.bundle/'resources',resources)
        save_json(self.bundle/'collection.json',dict(format='openxy-executable-collection/1',kernel=sha(self.info),
            platform=self.platform.hex(),abi=self.abi.hex(),image_sha256=sha(self.image),digest=self.image[112:144].hex(),
            resources={n:sha(data) for n,data in resources.items()},linked=['keep'],interface=self.interface,collection=[]))
        self.device=FakeDevice({'/projects/openxy/kernel.oxc':self.info,'/os.ldr':b'OS',
                                '/content/display/keep.svg':b'unchanged','/projects/song.xy':b'music'})

    def read_control(self,name):
        return a.read_control(self.device.get('/projects/openxy/'+name+'.oxc'),platform=self.platform,abi=self.abi)

    def install(self,name='install',verify=False):
        return device.install_collection(self.device,self.bundle,self.root/name,verify)

    def replace_record(self,name,data):
        path='/projects/openxy/'+name+'.oxc'
        _,entry=device.entry_at(self.device,path);self.device.delete(entry);self.device.put(path,data)

    def test_first_install_stages_and_reads_back_before_selecting_then_uses_inactive_slot(self):
        result=self.install();self.assertEqual((result['generation'],result['slot']),(2,0))
        self.assertEqual(self.device.writes[-1],'activate.oxc')
        self.assertEqual(self.read_control('attempt')['generation'],1)
        self.assertEqual(self.read_control('activate')['phase'],1)
        self.assertEqual(self.device.get('/projects/openxy/slot0.oxc'),self.image)
        result=self.install('second');self.assertEqual((result['generation'],result['slot']),(3,1))
        self.assertEqual(self.device.get('/projects/openxy/slot0.oxc'),self.image)
        self.assertEqual(self.device.get('/projects/openxy/slot1.oxc'),self.image)
        self.assertEqual(self.device.get('/os.ldr'),b'OS')
        self.assertEqual(self.device.get('/content/display/keep.svg'),b'unchanged')
        self.assertEqual(self.device.get('/projects/song.xy'),b'music')

    def test_stage_failure_never_activates_and_existing_push_rolls_it_back(self):
        self.device.fail_on='slot0.oxc'
        with self.assertRaises(ValueError):self.install()
        self.assertIsNone(self.device.get('/projects/openxy/activate.oxc'))
        self.assertIsNone(self.device.get('/projects/openxy/attempt.oxc'))
        self.assertNotIn('activate.oxc',self.device.writes)

    def test_selector_readback_failure_restores_old_selector_and_retains_inactive_bytes(self):
        self.install();old=self.device.get('/projects/openxy/activate.oxc');self.device.corrupt='activate.oxc'
        with self.assertRaises(ValueError):self.install('second')
        self.assertEqual(self.device.get('/projects/openxy/activate.oxc'),old)
        self.assertEqual(self.device.get('/projects/openxy/slot1.oxc'),self.image)

    def test_verify_only_is_a_distinct_request_and_disable_keeps_all_firmware_content(self):
        self.install(verify=True);self.assertEqual(self.read_control('activate')['phase'],4)
        device.disable_collection(self.device,self.root/'disable')
        self.assertEqual(self.read_control('activate')['phase'],0)
        self.assertEqual(self.device.get('/os.ldr'),b'OS')
        self.assertEqual(self.device.get('/projects/openxy/slot0.oxc'),self.image)

    def test_missing_or_invalid_active_history_cannot_be_seeded_under_an_old_selector(self):
        self.install()
        _,entry=device.entry_at(self.device,'projects/openxy/attempt.oxc');self.device.delete(entry)
        before=list(self.device.writes)
        with self.assertRaisesRegex(ValueError,'history'):self.install('second')
        self.assertEqual(before,self.device.writes)
        device.disable_collection(self.device,self.root/'disable')
        self.install('after-disable');self.assertEqual(self.read_control('activate')['phase'],1)

    def test_corrupt_or_foreign_bundle_fails_before_writes(self):
        data=bytearray(self.image);data[-1]^=1;(self.bundle/'bundle.oxc').write_bytes(data)
        with self.assertRaises(ValueError):self.install()
        self.assertFalse(self.device.writes)

    def test_bundled_presets_require_explicit_omission_and_never_overwrite_existing_presets(self):
        preset=b'{"engine":{"type":"keep"}}\n'
        path='content/presets/keep/default.json'
        description=json.loads((self.bundle/'collection.json').read_text())
        description['presets']={path:sha(preset)}
        candidate(self.bundle/'presets',{path:preset})
        save_json(self.bundle/'collection.json',description)
        assets={'content/display/keep.svg':b'<svg/>','content/op1_factory.json':b'{}',path:preset}
        self.assertEqual(len(k.resource_files(assets)),2)
        self.device.put('/'+path,b'existing factory preset')
        self.device.put('/presets/user.json',b'existing user preset')
        with self.assertRaisesRegex(ValueError,'--skip-presets'):self.install()
        self.assertFalse(self.device.writes)
        result=device.install_collection(self.device,self.bundle,self.root/'without-presets',skip_presets=True)
        self.assertFalse(result['presets_transferred'])
        self.assertEqual(self.device.get('/'+path),b'existing factory preset')
        self.assertEqual(self.device.get('/presets/user.json'),b'existing user preset')
        self.assertEqual(self.device.writes[-1],'activate.oxc')
        (self.bundle/'presets'/path).write_bytes(b'{}')
        before=list(self.device.writes)
        with self.assertRaises(ValueError):self.install('bad-preset')
        self.assertEqual(self.device.writes,before)

    def test_generic_push_refuses_activation_mixed_with_slot_or_resource_files(self):
        selector=a.control(phase=1,generation=2,slot=0,digest=self.image[112:144],platform=self.platform,abi=self.abi)
        folder=candidate(self.root/'unsafe',{'projects/openxy/activate.oxc':selector,'projects/openxy/slot0.oxc':self.image})
        with self.assertRaisesRegex(ValueError,'separate verified transaction'):device.source_files(folder)

    def test_every_control_byte_is_checked(self):
        data=a.control(phase=1,generation=2,slot=0,digest=self.image[112:144],platform=self.platform,abi=self.abi)
        for at in range(len(data)):
            bad=bytearray(data);bad[at]^=1
            with self.assertRaises(ValueError):a.read_control(bad,platform=self.platform,abi=self.abi)

    def test_old_seed_replay_cannot_rearm_a_failed_active_generation(self):
        self.install()
        pending=a.control(phase=2,generation=2,slot=0,digest=self.image[112:144],platform=self.platform,abi=self.abi)
        self.replace_record('attempt',pending);before=list(self.device.writes)
        folder=self.root/'install/stage-input'
        with self.assertRaisesRegex(ValueError,'Attempt history'):device.source_files(folder)
        files,_=device.source_files(folder,allow_attempt=True)
        with self.assertRaisesRegex(ValueError,'active selector'):device.push(self.device,files,self.root/'replay')
        self.assertEqual(self.device.writes,before);self.assertEqual(self.read_control('attempt')['phase'],2)

    def test_selector_change_between_transactions_is_preserved(self):
        self.install();actual_push=device.push
        newer=a.control(phase=0,generation=99,slot=0,digest=bytes(32),platform=self.platform,abi=self.abi)
        def concurrent(dev,files,record,**kwargs):
            result=actual_push(dev,files,record,**kwargs)
            if record.name=='stage':self.replace_record('activate',newer)
            return result
        with mock.patch.object(device,'push',side_effect=concurrent):
            with self.assertRaisesRegex(ValueError,'records changed'):self.install('race')
        self.assertEqual(self.device.get('/projects/openxy/activate.oxc'),newer)

    def test_oversized_corrupt_records_can_be_disabled_with_backups(self):
        self.install()
        bad=self.device.get('/projects/openxy/activate.oxc')+b'!'
        self.replace_record('activate',bad);self.replace_record('attempt',bad)
        device.disable_collection(self.device,self.root/'disable')
        self.assertEqual(self.read_control('activate')['phase'],0)
        self.assertEqual((self.root/'disable/before/projects/openxy/activate.oxc').read_bytes(),bad)

    def test_resource_length_must_match_descriptor_before_installation(self):
        image=bytearray(self.image);offset=struct.unpack_from('<I',image,144)[0]
        struct.pack_into('<I',image,160+offset+32,7)
        image[112:144]=bytes(32);image[112:144]=hashlib.sha256(image).digest()
        (self.bundle/'bundle.oxc').write_bytes(image)
        description=json.loads((self.bundle/'collection.json').read_text())
        description['digest']=image[112:144].hex();description['image_sha256']=sha(image)
        save_json(self.bundle/'collection.json',description)
        with self.assertRaisesRegex(ValueError,'length'):b.read_bundle(self.bundle)
        self.assertFalse(self.device.writes)

    def test_summary_save_error_reports_that_activation_already_committed(self):
        write_text=Path.write_text
        def fail_summary(path,*args,**kwargs):
            if path.name=='collection-install.json':raise OSError('disk full')
            return write_text(path,*args,**kwargs)
        with mock.patch.object(Path,'write_text',fail_summary):
            with self.assertRaises(OSError) as caught:self.install()
        self.assertIn('selected and verified',caught.exception.device_state)
        self.assertEqual(self.read_control('activate')['phase'],1)
        self.assertTrue(json.loads((self.root/'install/activate/transfer.json').read_text())['complete'])

    def test_kernel_record_can_be_installed_over_normal_mtp_after_dev_os_installation(self):
        folder=candidate(self.root/'record-only',{'projects/openxy/kernel.oxc':self.info})
        files,_=device.source_files(folder)
        self.assertEqual(set(files),{('projects','openxy','kernel.oxc')})
        candidate(folder,{'os.ldr':b'different OS'})
        with self.assertRaisesRegex(ValueError,'differs from the supplied OS'):device.source_files(folder)

    def test_kernel_push_scopes_preserve_machine_assets_and_use_the_correct_filesystem(self):
        folder=candidate(self.root/'kernel',{'projects/openxy/kernel.oxc':self.info,'os.ldr':b'OS',
            'content/display/_openxy_loader_1.svg':b'fallback','content/display/keep.svg':b'replace-never',
            'content/op1_factory.json':b'replace-never'})
        device.main(['push',str(folder),'--kernel-boot','--record',str(self.root/'boot-record')],connect=lambda:self.device)
        self.assertEqual(self.device.get('/content/display/keep.svg'),b'unchanged')
        self.assertIsNone(self.device.get('/content/op1_factory.json'))
        self.assertEqual(self.device.get('/content/display/_openxy_loader_1.svg'),b'fallback')
        self.device.writes.clear()
        device.main(['push',str(folder),'--kernel-record','--record',str(self.root/'info-record')],connect=lambda:self.device)
        self.assertFalse(self.device.writes) # Already identical; no OS/content writes.

    def test_status_distinguishes_pending_confirmed_verify_and_disabled_without_writes(self):
        self.install(verify=True);self.device.writes.clear()
        self.assertEqual(device.collection_status(self.device)['next_boot'],'verify-only')
        for phase,expected in ((2,'embedded'),(3,'embedded')):
            record=a.control(phase=phase,generation=2,slot=0,digest=self.image[112:144],platform=self.platform,abi=self.abi)
            self.replace_record('attempt',record)
            status=device.collection_status(self.device)
            self.assertEqual(status['attempt']['state'],'pending' if phase==2 else 'confirmed')
            self.assertEqual(status['next_boot'],expected)
        self.assertFalse(self.device.writes)
        self.install('arm')
        self.assertEqual(device.collection_status(self.device)['next_boot'],'external')


if __name__=='__main__':unittest.main()
