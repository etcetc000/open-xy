"""Audio transport: explicit capability, exact hook sites, bounded host ABI4."""
import copy
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from tooling import audio_telemetry, definitions, native, preview
from tooling.machines import machine_at
import test_display_extension as fixture
from test_sample_services import HOST_CXX
ROOT = Path(__file__).resolve().parents[1]


def write_fixture(folder, words=256, mode=0, reset=1):
    fixture.write_fixture(folder)
    p=folder/'definition.json'; d=json.loads(p.read_text())
    d['resources']['display'].update(audioWords=words,audioTransport=1,audioMode=mode,audioReset=reset)
    p.write_text(json.dumps(d)+'\n')
    p=folder/'display.h'; p.write_text(p.read_text().replace('<opxy/draw.hpp>','<opxy/audio_transport.hpp>'))
    p=folder/'display.cpp'; s=p.read_text()
    if words>16 or reset:
        s=s.replace('draw::AudioInput','draw::TelemetryInput').replace('++state.frames;', '++state.frames;\n    input.request_audio_reset();')
    p.write_text(s)
    p=folder/'engine.cpp';s=p.read_text()
    s=s.replace('context.add_packed16(0, 0x01000001u);',
                'for (opxy::u32 i=0; i<'+str(words)+'; ++i) context.publish_word(i, 0x10203040u+i);\n'
                '    if (context.pending_command()) context.command_applied(context.pending_command());')
    p.write_text(s)
    return machine_at(folder)


class AudioTransport(unittest.TestCase):
    run_command=fixture.DisplayExtension.run_command

    @unittest.skipUnless(HOST_CXX,'No working host C++ compiler')
    def test_wire_loss_duplicates_switch_lease_versions_and_queue(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp)
            for optimization in ('-O0','-O2'):
                binary=folder/('protocol'+optimization[1:]+('.exe' if os.name=='nt' else ''))
                self.run_command([HOST_CXX,'-std=c++11',optimization,'-Wall','-Wextra','-Werror','-pedantic',
                                  ROOT/'sdk/platform/opxy-1.2.1/telemetry/protocol.cpp',
                                  ROOT/'tests/telemetry_protocol.cpp','-o',binary])
                self.run_command([binary])

    def test_explicit_contract_and_exact_two_hooks(self):
        with tempfile.TemporaryDirectory() as temp:
            machine=write_fixture(Path(temp))
            value=machine.definition
            for changes in ({'audioWords':257}, {'audioMode':1}, {'audioTransport':0}, {'audioReset':True}):
                d=copy.deepcopy(value);d['resources']['display'].update(changes)
                with self.assertRaises(ValueError): definitions.validate_definition(d,'synth')
            from tooling.pipeline import config
            original=config();result=audio_telemetry.extend_profile(original,[machine])
            self.assertEqual(original,config())
            self.assertEqual(result['patches'][:-2],original['patches'])
            self.assertEqual([(p['address'],p['before']) for p in result['patches'][-2:]],
                             [('0x8c0ba4f4','00de4d8d106a0000'),('0x8d5b1af4','f9e3d525')])
            self.assertEqual(len([s for _,_,s in native.plan([machine],result) if s.startswith('telemetry_')]),4)
            plain=copy.deepcopy(machine.definition);plain['resources']['display']={'audioWords':1}
            self.assertFalse(audio_telemetry.enabled(plain))

    @unittest.skipUnless(HOST_CXX,'No working host C++ compiler')
    def test_banks_bounds_tracks_persistence_accumulation_and_reset(self):
        source=r'''#include <opxy/audio_transport.hpp>
int main() {
 using namespace opxy;
 struct Guarded {u32 left; HostTelemetryBank<256,0,true> bank; u32 right;};
 Guarded a={}; a.left=0x12345678; a.right=0xabcdef01;
 struct Output {u32 left;u32 words[256];u32 right;} out={};out.left=17;out.right=29;
 auto c=a.bank.context(3);
 for(u32 i=0;i<257;++i)c.publish_word(i,i+100);
 if(a.bank.take(3,out.words,256)!=256 || out.words[255]!=355) return 1;
 if(a.bank.take(3,out.words,256)!=256 || out.words[0]!=100) return 2;
 if(a.bank.take(2,out.words,256)!=256 || out.words[0]!=0) return 3;
 if(a.bank.take(8,out.words,256)!=0 || a.bank.context(8).pending_command())return 4;
 u32 id=a.bank.request_reset(3);if(!id || c.pending_command()!=id || a.bank.request_reset(3)!=id)return 5;
 c.command_applied(id+1);if(c.pending_command()!=id)return 6;
 c.command_applied(id);if(c.pending_command() || a.bank.request_reset(2)==id)return 7;
 if(a.left!=0x12345678 || a.right!=0xabcdef01 || out.left!=17 || out.right!=29)return 8;
 HostTelemetryBank<1,1,false> phase={};u32 small[16]={};auto p=phase.context(0);
 p.add_packed16(0,0x70007000);p.add_packed16(0,0x70007000);
 if(phase.take(0,small,16)!=1 || small[0]!=0x7fff7fff || phase.request_reset(0))return 9;
 phase.take(0,small,16);if(small[0])return 10;
 draw::TelemetryInput input={};if(input.request_audio_reset())return 11;
 input.audio_reset_supported=1;if(!input.request_audio_reset())return 12;
 input.audio_requested_command=5;if(input.request_audio_reset())return 13;
 return 0;
}'''
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp); p=folder/'test.cpp';p.write_text(source)
            binary=folder/('test.exe' if os.name=='nt' else 'test')
            self.run_command([HOST_CXX,'-std=c++11','-O2','-Wall','-Werror','-I'+str(ROOT/'sdk/include'),p,'-o',binary])
            self.run_command([binary])

    @unittest.skipUnless(shutil.which('clang++') and shutil.which('node'),'WASM and Node required')
    def test_actual_256word_wasm_snapshot_and_reset_handshake(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);m=write_fixture(folder/'machine');out=folder/'preview';out.mkdir()
            preview.compile_preview('clang++',m.definition,out,m.root)
            self.run_command(['node',ROOT/'tools/check_audio_transport.mjs',out/'engine.wasm'])
