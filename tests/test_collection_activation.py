"""Executable bundle admission and register-preserving resident ABI, without a device."""
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from tooling import collection_activation as activation
from tooling.pipeline import ROOT, config


class Activation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary=tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.folder=Path(cls.temporary.name)
        cls.runner=cls.folder/('activation.exe' if os.name=='nt' else 'activation')
        compiler=os.environ.get('OPXY_TEST_HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler: raise unittest.SkipTest('No host C++ compiler (set OPXY_TEST_HOST_CXX)')
        flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if os.environ.get('OPEN_XY_COLLECTION_SANITIZERS')=='1' else []
        subprocess.run([compiler,'-std=c++11','-Wall','-Wextra','-Werror',*flags,
                        '-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform'),
                        str(ROOT/'tests/fixtures/collection_activation.cpp'),
                        str(ROOT/'sdk/platform/collection_activation.cpp'),
                        str(ROOT/'sdk/platform/collection_handoff.cpp'),
                        str(ROOT/'sdk/platform/collection_probe.cpp'),'-o',str(cls.runner)],check=True,capture_output=True)

    def bundle(self,size=4097):
        base=0x8d2d4380
        payload=bytearray((i*137+19)&255 for i in range(size))
        for i,target in enumerate((12,16,20)):
            payload[i*4:i*4+4]=activation.entry_instruction(base+i*4,base+target)
        return activation.encode(payload,base=base,capacity=0x1f8000,bss_offset=(size+3)&~3,
                                 bss_bytes=128,text_bytes=min(size,1024)&~1,entries=3,
                                 platform=bytes(range(32)),abi=bytes(reversed(range(32))))

    def stage(self,data,chunk=0,mode='normal'):
        path=self.folder/'input.oxc';path.write_bytes(data)
        return tuple(map(int,subprocess.check_output([str(self.runner),str(path),str(chunk),mode],text=True).split()))

    def resign(self,data):
        data=bytearray(data);data[112:144]=bytes(32)
        data[112:144]=hashlib.sha256(data).digest()
        return data

    def resource_bundle(self):
        original=self.bundle(4096)
        payload=bytearray(original[160:])
        for i,kind in enumerate((1,2)):
            at=1024+i*40
            payload[at:at+32]=hashlib.sha256(bytes([kind])).digest()
            struct.pack_into('<2I',payload,at+32,1234,kind)
        return activation.encode(payload,base=0x8d2d4380,capacity=0x1f8000,bss_offset=4096,
                                 bss_bytes=128,text_bytes=1024,entries=3,platform=bytes(range(32)),
                                 abi=bytes(reversed(range(32))),resources_offset=1024,resources_count=2)

    def test_complete_candidate_is_staged_without_executable_writes(self):
        for size,chunk in ((25,1),(4097,7),(1000000,997)):
            data=self.bundle(size)
            self.assertEqual(self.stage(data,chunk),(0,1,1,len(data)))

    def test_header_rejection_never_allocates(self):
        data=self.bundle()
        for size in range(160): self.assertEqual(self.stage(data[:size])[:3],(1,0,0))
        for offset in (0,8,12,40,44,48,80,144,159):
            bad=bytearray(data);bad[offset]^=1
            self.assertNotEqual(self.stage(bad)[0],0)
            self.assertEqual(self.stage(bad)[1:3],(0,0))

    def test_payload_failures_release_staging_allocation(self):
        data=self.bundle()
        self.assertEqual(self.stage(data[:-1])[:3],(1,1,1))
        self.assertEqual(self.stage(data+b'x')[:3],(6,1,1))
        self.assertEqual(self.stage(data,mode='io-error')[:3],(7,1,1))
        bad=bytearray(data);bad[-1]^=1
        self.assertEqual(self.stage(bad)[:3],(5,1,1))
        self.assertEqual(self.stage(data,mode='oom')[:3],(8,1,0))

    def test_v2_resources_are_bound_to_payload_and_require_one_catalog(self):
        data=self.resource_bundle()
        self.assertEqual(self.stage(data,7),(0,1,1,len(data)))
        resources=activation.check_resources(data[160:],1024,1024,2,readonly=(1024,80))
        self.assertEqual([r['kind'] for r in resources],[1,2])
        self.assertTrue(resources[0]['path'].startswith('asset-'))
        self.assertTrue(resources[1]['path'].startswith('catalog-'))
        for boundary in ((1028,76),(1024,79),(0,1104)):
            with self.assertRaises(ValueError): activation.check_resources(data[160:],1024,1024,2,readonly=boundary)

    def test_handoff_checks_commit_identity_overlap_and_full_partition_boundaries(self):
        data=self.resource_bundle()
        for mode in ('success','no-permit','corrupt','metadata','overlap','cache-failure'):
            with self.subTest(mode=mode):
                self.assertEqual(self.stage(data,mode='handoff-'+mode),(0,1,1,len(data)))

    def test_resigned_invalid_resource_tables_fail_before_allocation_or_release_it(self):
        original=self.resource_bundle()
        for offset,value in ((144,0),(144,1025),(144,0xffffffff),(148,0),(148,65),
                             (148,0xffffffff),(152,0),(152,41),(156,1)):
            data=bytearray(original);struct.pack_into('<I',data,offset,value)
            result=self.stage(self.resign(data))
            self.assertNotEqual(result[0],0);self.assertEqual(result[1:3],(0,0))
        for resource_offset,value in ((32,0),(32,262145),(36,0),(36,3),(40+32,65537),(40+36,1)):
            data=bytearray(original);struct.pack_into('<I',data,160+1024+resource_offset,value)
            self.assertEqual(self.stage(self.resign(data))[:3],(10,1,1))
        data=bytearray(original);data[160+1024:160+1056]=bytes(32)
        self.assertEqual(self.stage(self.resign(data))[:3],(10,1,1))
        # Two copies of one catalog are ambiguous even with a valid whole-image hash.
        data=bytearray(original);data[160+1024:160+1064]=data[160+1064:160+1104]
        self.assertEqual(self.stage(self.resign(data))[:3],(10,1,1))
        data=bytearray(original);struct.pack_into('<I',data,148,9)
        for i in range(9):
            at=160+1024+i*40;data[at:at+32]=hashlib.sha256(bytes([i])).digest()
            struct.pack_into('<2I',data,at+32,65536 if not i else 262144,2 if not i else 1)
        self.assertEqual(self.stage(self.resign(data))[:3],(10,1,1))

    def test_v1_reserved_fields_cannot_be_reinterpreted_as_resource_metadata(self):
        data=bytearray(self.resource_bundle());struct.pack_into('<I',data,8,1)
        self.assertEqual(self.stage(self.resign(data))[:3],(2,0,0))

    def test_outside_pool_and_wrong_slot_contract_are_refused_even_if_resigned(self):
        for offset,value in ((16,0),(20,0xffffffff),(24,0),(24,0xffffffff),(28,0xffffffff),
                             (32,12),(32,1023),(32,0xffffffff),(36,0),(36,129)):
            bad=bytearray(self.bundle());struct.pack_into('<I',bad,offset,value)
            result=self.stage(self.resign(bad))
            self.assertNotEqual(result[0],0)
            self.assertEqual(result[1:3],(0,0))

    def test_resigned_code_entries_must_jump_past_prefix_into_initialized_text(self):
        for instruction in (b'\0'*4,bytes.fromhex('00e30000'),
                            activation.entry_instruction(0x8d2d4380,0x8d2d4380),
                            activation.entry_instruction(0x8d2d4380,0x8d2d4380+1024)):
            bad=bytearray(self.bundle());bad[160:164]=instruction
            self.assertEqual(self.stage(self.resign(bad))[:3],(9,1,1))

    def test_bypass_pending_attempt_and_peer_disagreement_keep_embedded_collection(self):
        subprocess.run([str(self.runner),'policy'],check=True)

    def test_control_records_fail_closed_on_torn_writes_corruption_and_foreign_platforms(self):
        def record(phase=1,slot=1):
            return activation.control(phase=phase,generation=17,slot=slot,digest=bytes(range(32)) if phase else bytes(32),
                                      platform=bytes(range(32)),abi=bytes(reversed(range(32))))
        for phase in range(5):
            data=record(phase,1 if phase else 0)
            self.assertEqual(self.stage(data,mode='control'),(1,phase,17,1 if phase else 0))
        data=record()
        for end in range(160): self.assertEqual(self.stage(data[:end],mode='control'),(0,0,0,0))
        for offset in range(160):
            bad=bytearray(data);bad[offset]^=1
            self.assertEqual(self.stage(bad,mode='control'),(0,0,0,0))
        for offset,value in ((12,5),(16,0),(20,2),(24,1),(32,1),(64,1)):
            bad=bytearray(data);struct.pack_into('<I',bad,offset,value)
            bad[128:160]=bytes(32);bad[128:160]=hashlib.sha256(bad).digest()
            self.assertEqual(self.stage(bad,mode='control'),(0,0,0,0))
        self.assertEqual(self.stage(data+b'x',mode='control'),(0,0,0,0))

    def test_layout_preserves_reserved_memory_and_hook_policy(self):
        profile=config();abi=activation.interface(profile,{'samples','drums','extensions'},True)
        layout=abi['geometry']
        self.assertEqual(layout['reserved_end'],profile['module_base']+profile['reserved_bytes']-profile['table_bytes'])
        self.assertEqual(layout['payload_base'],profile['module_base']+0x4000)
        self.assertEqual(len(activation.fingerprint(abi)),32)
        self.assertNotIn('sdk_collection_mtp_match',[e['symbol'] for e in abi['entries']])
        self.assertEqual(activation.payload_entries(abi).count('JUMP.L'),len(abi['entries']))
        self.assertEqual(activation.resident_veneers(abi).count('.short 0xdc00'),len(abi['entries']))
        data=b''.join(struct.pack('<4H',0xdc00,(layout['payload_base']+i*4)>>16,
                                 (layout['payload_base']+i*4)&65535,0) for i in range(len(abi['entries'])))
        activation.check_veneers(data,abi)
        for at in (0,2,4,6,len(data)-1):
            bad=bytearray(data);bad[at]^=1
            with self.assertRaises(ValueError): activation.check_veneers(bad,abi)

    def test_payload_linker_moves_only_the_base_and_pins_entry_slots_first(self):
        source=(ROOT/'linker/opxy-1.2.1.ld').read_text()
        changed=activation.payload_linker(source,config())
        self.assertIn('ORIGIN = 0x8d2d4380, LENGTH = 0x1f8000',changed)
        self.assertIn('KEEP(collection_entries.o(.text.collection_entries))',changed)
        self.assertIn('ASSERT(__extensions_reserved_end == __stock_effect_records',changed)
        with self.assertRaises(ValueError): activation.payload_linker(source+'\n',config())


if __name__=='__main__': unittest.main()
