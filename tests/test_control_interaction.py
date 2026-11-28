"""Physical control events preserve callback order and post-edit snapshots."""
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from test_sample_services import HOST_CXX
import test_display_extension as fixture
from tooling import preview
ROOT=Path(__file__).resolve().parents[1]

HOST=r'''#include <opxy/control_interaction.hpp>
int main(){using namespace opxy::control_interaction;
if(latest(0).available)return 1;configure_preview_source(true);
if(!latest(0).available||latest(0).serial)return 2;
if(!submit_preview(0,3,7,123)||!submit_preview(0,1,1,123))return 3;
auto e=latest(0);if(e.serial!=2||e.physical_knob!=1||e.parameter_index!=1||e.normalized_value_at_event!=123)return 4;
if(submit_preview(8,0,0,0)||submit_preview(0,4,0,0)||submit_preview(0,0,8,0)||submit_preview(0,0,0,-1))return 5;
if(latest(0).serial!=2||latest(1).serial||!latest(1).available)return 6;
detail::preview_bank().serial=0xffffffffu;submit_preview(0,0,0,0);if(latest(0).serial!=1)return 7;
configure_preview_source(false);if(latest(0).available||submit_preview(0,0,0,1))return 8;return 0;}
'''

class ControlInteraction(unittest.TestCase):
    run_command=fixture.DisplayExtension.run_command
    @unittest.skipUnless(HOST_CXX,'Host C++ compiler unavailable')
    def test_native_callback_boundaries(self):
        # The real adapter with capture compiled in (tests/fixtures/control_interaction.cpp),
        # substituting only the stock-address boundary, as test_synth_steps does.
        platform=ROOT/'sdk/platform/opxy-1.2.1'
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp)
            stock=(platform/'stock.hpp').read_text(encoding='utf-8')
            stock=stock.replace('typedef __UINTPTR_TYPE__ Address;','typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*','const volatile u32*','const volatile unsigned char*'):
                stock=stock.replace('reinterpret_cast<'+pointer+'>(address)','reinterpret_cast<'+pointer+'>(host_stock_address(address))')
            (folder/'stock.hpp').write_text(stock,encoding='utf-8')
            for name in ('synth_controls.cpp','drum_lock_store.hpp','control_interaction_native.inc'):
                (folder/name).write_bytes((platform/name).read_bytes())
            exe=folder/('test.exe' if os.name=='nt' else 'test')
            # -O0 too: every service synth_controls.cpp references must be in the fixture, whatever
            # a host compiler's inlining happens to drop at -O2.
            for opt in ('-O0', '-O2'):
                self.run_command([HOST_CXX,'-std=c++11',opt,'-Wall','-Werror','-I'+str(folder),'-I'+str(ROOT/'sdk/include'),
                                  '-I'+str(platform),ROOT/'tests/fixtures/control_interaction.cpp','-o',exe])
                self.run_command([exe])
    def test_default_build_has_no_capture(self):
        # Without a machine including the header, synth_controls.cpp compiles as before:
        # every capture is behind OPXY_CONTROL_INTERACTION, which only the generated wrapper defines.
        from tooling import native
        source=(ROOT/'sdk/platform/opxy-1.2.1/synth_controls.cpp').read_text(encoding='utf-8')
        import re
        plain=re.sub(r'#if OPXY_CONTROL_INTERACTION\n.*?(#else\n|#endif\n)','',source,flags=re.S)
        self.assertGreaterEqual(source.count('capture_interaction('),3)
        self.assertNotIn('interaction',plain)
        self.assertNotIn('#define OPXY_CONTROL_INTERACTION',source)
        with tempfile.TemporaryDirectory() as temp:
            wrapper=native.control_interaction_unit(Path(temp)).read_text(encoding='utf-8')
        self.assertIn('#define OPXY_CONTROL_INTERACTION 1\n#include "synth_controls.cpp"\n',wrapper)
    def test_capture_reads_the_value_as_the_pages_show_it(self):
        # The capture calls stock's displayed-value getter with the four arguments
        # synth_controls.cpp shown_value passes; a shorter call leaves the last one undefined.
        platform=ROOT/'sdk/platform/opxy-1.2.1'
        shown='stock<opxy::i32(u32, u32, u32, u32)>(0x8d5ac9dc)(kProject, track, slot, 1)'
        self.assertIn(shown,(platform/'synth_controls.cpp').read_text(encoding='utf-8'))
        inc=(platform/'control_interaction_native.inc').read_text(encoding='utf-8')
        self.assertEqual(inc.count('0x8d5ac9dc'),1)
        self.assertIn('stock<opxy::i32(opxy::u32,opxy::u32,opxy::u32,opxy::u32)>(0x8d5ac9dcu)(project,track,parameter,1)',inc)
    @unittest.skipUnless(HOST_CXX,'Host C++ compiler unavailable')
    def test_host_baselines_bounds_order_and_wrap(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);(folder/'test.cpp').write_text(HOST);exe=folder/('test.exe' if os.name=='nt' else 'test')
            self.run_command([HOST_CXX,'-std=c++11','-O2','-I'+str(ROOT/'sdk/include'),folder/'test.cpp','-o',exe]);self.run_command([exe])
    @unittest.skipUnless(shutil.which('clang++') and shutil.which('node'),'WASM and Node required')
    def test_actual_wasm_event_boundary(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);m=fixture.write_fixture(folder/'machine');out=folder/'out';out.mkdir()
            p=m.root/'display.cpp';s=p.read_text()
            s='#include <opxy/control_interaction.hpp>\n'+s
            s=s.replace('const opxy::i32 x = input.audio_count ? (input.audio_words[0] & 2047) : -1;',
                        'const auto e=opxy::control_interaction::latest(input.track);\n    const opxy::i32 x=e.available?e.serial:-1;')
            s=s.replace('512, x, 0','512, x, e.normalized_value_at_event')
            s=s.replace('0, 0, 0, 0, 1024','0, 0, 0, static_cast<opxy::i32>(e.physical_knob), static_cast<opxy::i32>(e.parameter_index)')
            p.write_text(s)
            preview.compile_preview('clang++',m.definition,out,m.root)
            self.run_command(['node',ROOT/'tools/check_control_interaction.mjs',out/'engine.wasm'])
