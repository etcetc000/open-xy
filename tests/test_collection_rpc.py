"""Startup identity RPC probe: ownership, isolated task notifications and timeout latching."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tooling import collection_rpc
from tooling.pipeline import ROOT, config


class RpcProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary=tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.folder=Path(cls.temporary.name)
        compiler=os.environ.get('OPXY_TEST_HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler: raise unittest.SkipTest('No host C++ compiler (set OPXY_TEST_HOST_CXX)')
        (cls.folder/'stock.hpp').write_text('''#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace platform {
using Address=__UINTPTR_TYPE__;
Address host_stock_address(u32); u32 word(u32);
template<class Function> Function* stock(u32 address) {
return reinterpret_cast<Function*>(host_stock_address(address));
}
}}
''')
        source=collection_rpc.generate(cls.folder,{},[],True,{})
        cls.runner=cls.folder/('rpc.exe' if os.name=='nt' else 'rpc')
        flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if os.environ.get('OPEN_XY_COLLECTION_SANITIZERS')=='1' else []
        subprocess.run([compiler,'-std=c++11','-Wall','-Wextra','-Werror',*flags,
                        '-I'+str(cls.folder),'-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform'),
                        str(ROOT/'tests/fixtures/collection_rpc.cpp'),str(source),
                        str(ROOT/'sdk/platform/collection_gate.cpp'),'-o',str(cls.runner)],
                       check=True,capture_output=True)

    def run_probe(self,mode,gui):
        from tooling.collection_probe import platform_id
        identity=b''.join(collection_rpc.probe_identities(platform_id({},[],True,{}))).hex()
        return json.loads(subprocess.check_output([str(self.runner),mode,str(int(gui)),identity],text=True))

    def test_gui_requests_roundtrip_through_stock_wire_factory_and_audio_only_listens(self):
        report=self.run_probe('normal',True)
        self.assertEqual(report['report']['result'],'hold-roundtrip-ok')
        self.assertEqual(report['report']['role'],'gui')
        self.assertFalse(report['report']['execution'])
        self.assertTrue(report['report']['initializer-held'])
        self.assertTrue(report['report']['freeze-seen'])
        self.assertEqual(report['report']['destructive-requests-refused'],'00000002')
        self.assertEqual((report['creates'],report['writes'],report['delays']),(1,1,0))
        report=self.run_probe('normal',False)
        self.assertEqual(report['report']['result'],'hold-cancelled')
        self.assertEqual(report['report']['destructive-requests-refused'],'00000002')
        self.assertEqual((report['creates'],report['writes']),(0,1))
        self.assertLess(report['delays'],1000)

    def test_timeout_latches_and_late_reply_cannot_rewrite_boot_result(self):
        for mode in ('timeout','late'):
            report=self.run_probe(mode,True)
            self.assertEqual(report['report']['result'],'timeout')
            self.assertFalse(report['report']['freeze-seen'])
            self.assertEqual((report['creates'],report['writes'],report['delays']),(1,1,1000))
        report=self.run_probe('timeout',False)
        self.assertEqual(report['report']['result'],'hold-timeout')
        self.assertEqual(report['delays'],1000)

    def test_create_failure_and_bad_reply_remain_inspection_failures(self):
        for mode in ('create-failed','bad-reply','busy'):
            report=self.run_probe(mode,True)
            self.assertEqual(report['report']['result'],'create-failed' if mode=='create-failed' else 'hold-failed')
            self.assertFalse(report['report']['execution'])

    def test_dispatcher_forwards_every_unowned_signature(self):
        for gui in (False,True):
            def dispatch(change):
                return tuple(map(int,subprocess.check_output([str(self.runner),'dispatch',str(int(gui)),str(change)],text=True).split()))
            good=dispatch(-1)
            if gui:
                self.assertEqual((good[0],good[3],good[4]),(0xd00d,0,1))
            else:
                self.assertEqual(good,(0xc001,0,0,1,0))
            for changed in (8,12,13,14,15,28,32,33):
                result=dispatch(changed)
                self.assertEqual((result[0],result[3],result[4]),(0xd00d,0,1))

    def test_profile_adds_only_authenticated_dispatcher_entry(self):
        base=config();modified=collection_rpc.profile(base)
        self.assertEqual(modified['patches'][:-2],base['patches'])
        self.assertEqual(modified['patches'][-2]['address'],'0x8c0c236c')
        self.assertEqual(modified['patches'][-1]['address'],'0x8d5067d4')
        hook=modified['patches'][-2]
        from tooling.firmware import hook_instruction,hook_target
        encoded=hook_instruction(hook,0x8d2d0380)
        self.assertEqual(hook_target(hook,encoded),(0x8d2d0380,False))
        self.assertTrue(collection_rpc.thunk_ok(bytes.fromhex('00e80000f505383000dc0c8c74230000'),0,16))
        self.assertFalse(collection_rpc.thunk_ok(bytes.fromhex('00e80000f505383000dc0c8c70230000'),0,16))
        from tooling.firmware import branch_instruction
        low=0x8d2d0380
        symbols={name:dict(value=low+offset) for name,offset in (
            ('sdk_collection_rpc_entry_hook',0),('sdk_hook_save',100),
            ('sdk_collection_rpc_entry',200),('sdk_hook_restore',300))}
        symbols['sdk_collection_rpc_entry_hook']['size']=36
        code=(bytes.fromhex('00e80000')+branch_instruction(low+4,low+100,True)+
              branch_instruction(low+8,low+200,True)+branch_instruction(low+12,low+300,True)+
              bytes.fromhex('01e8000000e80000fb05466f00dc508ddc670000'))
        self.assertTrue(collection_rpc.entry_ok(code,low,symbols))
        for offset in range(36):
            changed=bytearray(code);changed[offset]^=1
            self.assertFalse(collection_rpc.entry_ok(changed,low,symbols))

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'),'No private stock OS fixture')
    def test_stock_factory_admits_close_request_and_rejects_new_unknown_kind(self):
        import struct
        from tooling.formats import ldr,mapped_offset
        data=Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes();blocks=ldr(data)
        at=mapped_offset(blocks,0x840104c0+44*4,4)
        self.assertEqual(struct.unpack_from('<I',data,at)[0],0x8c0c4668)
        # Bind the modeled wire boundary to stock's subtract-one, count54 and
        # unsigned comparison, plus its signed-negative descriptor rejection.
        at=mapped_offset(blocks,0x8c0c45ee,8)
        self.assertEqual(data[at:at+8].hex(),'f867b16108328809')
        at=mapped_offset(blocks,0x8c0bcb20,4)
        self.assertEqual(data[at:at+4].hex(),'800c0c1c')
        at=mapped_offset(blocks,0x8d5067d4,8)
        self.assertEqual(data[at:at+8].hex(),'00e80000fb05466f')

    def test_probe_is_optional_and_runs_before_file_inspection_and_registration(self):
        from tooling import native,startup
        normal=startup.render(['example'],probe=True)
        source=startup.render(['example'],probe=True,rpc_probe=True)
        self.assertNotIn('sdk_collection_rpc',normal)
        self.assertLess(source.index('    sdk_collection_rpc_probe();'),source.index('    sdk_collection_probe();'))
        self.assertLess(source.index('    sdk_collection_probe();'),source.index('    example_register();'))
        machines=native.Collection(probe=True,rpc_probe=True)
        self.assertTrue(native.subset(machines,[]).collection_rpc_probe)
        self.assertEqual({name for name,_,_ in native.plan(machines)}-{name for name,_,_ in native.plan(native.Collection(probe=True))},
                         {'sdk/collection_rpc.o','sdk/collection_rpc_stock.o',
                          'sdk/collection_gate.o','sdk/collection_rpc_entry.o'})


if __name__=='__main__': unittest.main()
