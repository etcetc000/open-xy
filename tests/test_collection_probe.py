"""Cross-language parser attack tests; no firmware fixtures or device required."""
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tooling import collection_probe as probe, native
from tooling.pipeline import ROOT, config
from tooling.startup import render


class CollectionProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='opxy-collection-parser-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        cls.runner = cls.directory / ('inspect.exe' if os.name == 'nt' else 'inspect')
        compiler = os.environ.get('OPXY_TEST_HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler:
            raise unittest.SkipTest('No host C++ compiler (set OPXY_TEST_HOST_CXX)')
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if os.environ.get('OPEN_XY_COLLECTION_SANITIZERS') == '1' else []
        subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                        *flags,
                        '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/platform'),
                        str(ROOT / 'tests/fixtures/collection_probe.cpp'),
                        str(ROOT / 'sdk/platform/collection_probe.cpp'), '-o', str(cls.runner)],
                       check=True, capture_output=True, text=True)
        cls.startup_runner = cls.directory / ('startup.exe' if os.name == 'nt' else 'startup')
        cls.startup_platform = probe.platform_id({}, [], True, {})
        generated = probe.generate(cls.directory, {}, [], True, {}, 0x8d2d0380, 0x1fc000)
        (cls.directory / 'stock.hpp').write_text('''#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace platform {
using Address=__UINTPTR_TYPE__;
Address host_stock_address(u32);
template<class Function> Function* stock(u32 address) {
    return reinterpret_cast<Function*>(host_stock_address(address));
}
}}
''')
        subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I' + str(cls.directory), '-I' + str(ROOT / 'sdk/include'),
                        '-I' + str(ROOT / 'sdk/platform'),
                        str(ROOT / 'tests/fixtures/collection_startup.cpp'), str(generated),
                        str(ROOT / 'sdk/platform/collection_probe.cpp'), '-o', str(cls.startup_runner)],
                       check=True, capture_output=True, text=True)

    def options(self, size=4097):
        return dict(base=0x8d2d0380, capacity=0x1fc000, bss_offset=(size+3)&~3,
                    bss_bytes=128, text_bytes=min(size,512), platform=bytes(range(32)))

    def data(self, size=4097):
        payload = bytes((i*137+19)&255 for i in range(size))
        return probe.encode(payload, **self.options(size))

    def run_native(self, data, chunk=0, mode='inspect'):
        path = self.directory / 'input.bin'
        path.write_bytes(data)
        return subprocess.check_output([str(self.runner),mode,str(path),str(chunk)], text=True).strip()

    def resign(self, data):
        data = bytearray(data); data[72:104] = bytes(32)
        data[72:104] = hashlib.sha256(data).digest()
        return bytes(data)

    def test_sha256_against_hashlib_at_padding_and_stream_boundaries(self):
        for size in (0,1,3,55,56,57,63,64,65,127,128,129,4097,1000000):
            data = b'a'*size
            with self.subTest(size=size):
                self.assertEqual(self.run_native(data, 113, 'sha'), hashlib.sha256(data).hexdigest())

    def test_valid_and_fragmented_reads(self):
        for size,chunk in ((1,1),(55,3),(1024,0),(4097,7),(0x1fbf80,997)):
            with self.subTest(size=size,chunk=chunk):
                data = self.data(size)
                self.assertEqual(self.run_native(data, chunk), '0')
                opts=self.options(size)
                self.assertFalse(probe.inspect(data,base=opts['base'],capacity=opts['capacity'],platform=opts['platform'])['execution'])

    def test_every_truncated_header_and_payload_boundary(self):
        data = self.data()
        for size in (*range(128),128,129,1151,len(data)-1):
            with self.subTest(size=size):
                self.assertEqual(self.run_native(data[:size]), '1')
        self.assertEqual(self.run_native(data+b'x'), '6')

    def test_header_and_payload_tampering(self):
        data = self.data()
        for index in (0,8,12,36,104,127,40,71,72,103,128,len(data)-1):
            changed=bytearray(data); changed[index]^=1
            with self.subTest(index=index):
                self.assertNotEqual(self.run_native(changed), '0')
        changed=bytearray(data); changed[20]^=1
        self.assertNotEqual(self.run_native(changed), '0')

    def test_failed_final_read_is_not_accepted_as_eof(self):
        self.assertEqual(self.run_native(self.data(), mode='fail-at-end'), '8')

    def test_overflow_alignment_and_pool_attacks_even_with_recomputed_digest(self):
        for offset,value in ((16,0),(16,0xffffffff),(20,0),(20,0xffffffff),(24,0),
                             (24,0xffffffff),(24,4098),(28,0xffffffff),(28,127),
                             (32,0),(32,0xffffffff)):
            changed=bytearray(self.data());struct.pack_into('<I',changed,offset,value)
            with self.subTest(offset=offset,value=value):
                self.assertEqual(self.run_native(self.resign(changed)), '4')

    def test_deterministic_random_malformed_inputs_are_rejected(self):
        randomizer=random.Random(17)
        for _ in range(32):
            data=randomizer.randbytes(randomizer.randrange(0,8192))
            self.assertNotEqual(self.run_native(data), '0')

    def test_probe_is_opt_in_and_precedes_registration(self):
        ordinary=render(['example'])
        enabled=render(['example'],probe=True)
        self.assertNotIn('sdk_collection_probe',ordinary)
        self.assertLess(enabled.index('    sdk_collection_probe();'),enabled.index('    example_register();'))
        machines=native.Collection(probe=True)
        self.assertTrue(native.subset(machines,[]).collection_probe)
        normal={name for name,_,_ in native.plan([])}
        inspected={name for name,_,_ in native.plan(machines)}
        self.assertEqual(inspected-normal,{'sdk/collection_probe.o','sdk/collection_inspector.o'})

    def test_mtp_overlay_preserves_every_existing_rule_and_authenticates_one_call(self):
        original=config(); modified=probe.profile(original)
        self.assertEqual(modified['patches'][:-1],original['patches'])
        from tooling.firmware import decode_branch
        hook=modified['patches'][-1]
        self.assertEqual((hook['address'],hook['file_offset'],hook['length']),('0x8d5c1d9e','0x1f8ff6',4))
        self.assertEqual(decode_branch(int(hook['address'],16),bytes.fromhex(hook['before'])),(0x8d5c1bb0,True))
        self.assertEqual(hook['symbol'],'sdk_collection_mtp_match')
        with patch.object(probe,'read',return_value=b'{}'), self.assertRaisesRegex(ValueError,'Unreviewed'):
            probe.profile(original)

    def test_native_mtp_matcher_preserves_stock_matches_and_restricts_new_files(self):
        def match(path,other_rule=False):
            return subprocess.check_output([str(self.startup_runner),path,str(int(other_rule)),'mtp'],text=True).strip()=='1'
        for path in ('/projects/folder/song.xy','/projects/folder/song.oxy',
                     '/projects/openxy/collection.oxc','/projects/openxy/gui-status.oxc','/projects/openxy/audio-status.oxc'):
            self.assertTrue(match(path),path)
        for path in ('/os.ldr','/projects/openxy/a.exe','/projects/openxy/a.xc','/projects/other/a.oxc',
                     '/projects/openxy/a.oxyc','/projects/openxy/a.oxc/extra','/projects/openxy/.oxc',
                     '/projects/openxy/a b.oxc','/projects/openxy/../a.oxc'):
            self.assertFalse(match(path),path)
        self.assertTrue(match('/projects/workspace.xy',True))
        self.assertTrue(match('/projects/workspace.oxy',True))
        self.assertFalse(match('/projects/openxy/collection.oxc',True))

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'),'No private stock OS fixture')
    def test_stock_regex_compiler_treats_group_parentheses_as_literals(self):
        from tooling.formats import ldr, mapped_offset
        data=Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        blocks=ldr(data)
        # Compiler dispatch for ASCII '$' through '.': '(' and ')' take the
        # default path, which only special-cases '?' before emitting a literal.
        at=mapped_offset(blocks,0x84007914,11*4)
        targets=struct.unpack_from('<11I',data,at)
        for character in '()':
            self.assertEqual(targets[ord(character)-ord('$')],0x8c073fec)
        at=mapped_offset(blocks,0x8c073fec,8)
        self.assertEqual(data[at:at+8].hex(),'f86174ae01089513')
        # Above '[' the only special cases are backslash and '^'; '|' also
        # takes the literal path. Bind this semantic check to those instructions.
        at=mapped_offset(blocks,0x8c073f0e,18)
        self.assertEqual(data[at:at+18].hex(),'19080f1820e15e0074ae0108071874ae3860')

    def test_probe_refuses_collection_trimming_before_capability_fingerprints_can_diverge(self):
        with patch.object(native, 'link', return_value='overflow'), self.assertRaisesRegex(ValueError, 'every selected machine'):
            native.linked_machines(None,None,config(),native.Collection(probe=True),{})

    def test_platform_excludes_machine_selection_but_binds_capabilities(self):
        sources={'sdk/a':'a','external/example/a':'b','collection':'c'}
        first=probe.platform_id(sources,['samples'],True,{'compiler':'one'})
        sources['external/example/a']='different';sources['collection']='different'
        self.assertEqual(first,probe.platform_id(sources,['samples'],True,{'compiler':'one'}))
        self.assertNotEqual(first,probe.platform_id(sources,[],True,{'compiler':'one'}))
        self.assertNotEqual(first,probe.platform_id(sources,['samples'],False,{'compiler':'one'}))
        self.assertNotEqual(first,probe.platform_id(sources,['samples'],True,{'compiler':'two'}))

    def test_startup_uses_native_mount_and_reports_each_role_once(self):
        options=self.options();options['platform']=self.startup_platform
        payload=b'x'*4097
        data=probe.encode(payload,**options)
        path=self.directory/'startup-input.oxc'
        for gui in (0,1):
            for mode,contents,status in (('normal',data,'valid'),('normal',data[:-1],'truncated'),
                                         ('normal',data[:-1]+b'y','checksum'),('missing',data,'missing')):
                with self.subTest(gui=gui,mode=mode,status=status):
                    path.write_bytes(contents)
                    report=json.loads(subprocess.check_output([str(self.startup_runner),str(path),str(gui),mode],text=True))
                    self.assertEqual(report['status'],status)
                    self.assertFalse(report['execution'])
                    self.assertEqual(report['platform'],self.startup_platform.hex())
                    if status=='valid':
                        self.assertEqual(report['digest'],data[72:104].hex())
            report=json.loads(subprocess.check_output([str(self.startup_runner),str(path),str(gui),'report-failure'],text=True))
            self.assertEqual(report,{'report':'unavailable'})


if __name__=='__main__':
    unittest.main()
