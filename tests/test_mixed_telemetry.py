import json
import os
from pathlib import Path
import tempfile
import unittest
from tooling import definitions, preview
from test_audio_transport import write_fixture
from test_sample_services import HOST_CXX
import test_display_extension as display_fixture
ROOT=Path(__file__).resolve().parents[1]

class MixedTelemetry(unittest.TestCase):
    run_command=display_fixture.DisplayExtension.run_command
    def test_mode2_requires_accumulator_and_snapshot_tail(self):
        with tempfile.TemporaryDirectory() as t:
            machine=write_fixture(Path(t),33,2,1)
            definitions.validate_definition(machine.definition,'synth')
            machine.definition['resources']['display']['audioWords']=1
            with self.assertRaises(ValueError):definitions.validate_definition(machine.definition,'synth')
            d=machine.definition['resources']['display'];d.update(audioWords=60,audioAccumulators=24)
            definitions.validate_definition(machine.definition,'synth')
            for changes in ({'audioAccumulators':25},{'audioAccumulators':0},{'audioWords':24},{'audioMode':0}):
                previous=d.copy();d.update(changes)
                with self.assertRaises(ValueError):definitions.validate_definition(machine.definition,'synth')
                d.clear();d.update(previous)
    @unittest.skipUnless(HOST_CXX,'Host C++ compiler required')
    def test_packet_loss_reset_and_prefix_tail_semantics(self):
        with tempfile.TemporaryDirectory() as t:
            for level in ('-O0','-O2'):
                binary=Path(t)/('mixed'+level[1:]+('.exe' if os.name=='nt' else ''))
                self.run_command([HOST_CXX,'-std=c++11',level,'-Wall','-Wextra','-Werror',
                    ROOT/'tests/mixed_telemetry.cpp',ROOT/'sdk/platform/opxy-1.2.1/telemetry/protocol.cpp','-o',binary])
                self.run_command([binary])

    def test_actual_mixed_wasm_consumes_prefix_and_retains_wave(self):
        with tempfile.TemporaryDirectory() as t:
            base=Path(t);machine=write_fixture(base/'machine',33,2,1);out=base/'preview';out.mkdir()
            p=machine.root/'engine.cpp';s=p.read_text()
            s=s.replace('for (opxy::u32 i=0; i<33; ++i) context.publish_word(i, 0x10203040u+i);',
                'context.add_packed16(0,0x00020003u);\n    for (opxy::u32 i=1; i<33; ++i) context.publish_word(i, 0x10203040u+i);')
            p.write_text(s)
            preview.compile_preview('clang++',machine.definition,out,machine.root)
            self.run_command(['node',ROOT/'tools/check_mixed_telemetry.mjs',out/'engine.wasm'])

    def test_actual_multislot_wasm_and_targeted_reset(self):
        with tempfile.TemporaryDirectory() as t:
            base=Path(t);machine=write_fixture(base/'machine',60,2,1);out=base/'preview';out.mkdir()
            machine.definition['resources']['display']['audioAccumulators']=24
            p=machine.root/'engine.cpp';s=p.read_text()
            s=s.replace('for (opxy::u32 i=0; i<60; ++i) context.publish_word(i, 0x10203040u+i);',
                'for(opxy::u32 i=0;i<24;++i)context.add_packed16(i,i+1);\n'
                '    for(opxy::u32 i=24;i<60;++i)context.publish_word(i,1000+i);\n'
                '    if(context.pending_command())context.publish_word(59,context.pending_target());')
            p.write_text(s)
            p=machine.root/'display.cpp';p.write_text(p.read_text().replace('input.request_audio_reset();','input.request_audio_reset(0xfedcba98u);'))
            preview.compile_preview('clang++',machine.definition,out,machine.root)
            self.run_command(['node',ROOT/'tools/check_multislot_telemetry.mjs',out/'engine.wasm'])
