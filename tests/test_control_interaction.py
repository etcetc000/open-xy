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

STOCK=r'''#pragma once
#include <opxy/types.hpp>
namespace opxy {namespace platform {
using Address=__UINTPTR_TYPE__;
Address host_function(u32);u32 word(u32);u32 byte(u32);u32 track_engine(u32);u32 selected_track();bool shift_held();u32 set_parameter(u32,u32,u32,i32);
template<class T>T* stock(u32 address){return reinterpret_cast<T*>(host_function(address));}
const u32 kTracks=8,kSelectedTrack=0x840a0534;
}}
'''
CALLBACK=r'''#include <cstdio>
#include <cstdlib>
#include "synth_controls.cpp"
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace opxy;
namespace {
u32 current=0,page=0,controller=0x8400dff8,engine[8]={73,73,73,73,73,73,73,73};
u32 role=1,press_calls=0,turn_calls=0,last_slot=0,last_event=0,reads=0;
u32 project_pointer=0x90000000u,pattern_count[8]={1,1,1,1,1,1,1,1};
u32 pattern_mask=0,scene=0,direct_pattern[8]={},scene_pattern[8]={},engine_reads=0;
const u32 project_base=0x90000000u;
i32 value=123;bool shift=false,clamp=false;
int ready(){return 1;}bool format(u32,i32,char*){return false;}
const u32* top(){return &controller;}
void press(u32){++press_calls;}
u32 turn(u32 project,u32 track,u32 slot,u32 event){CHECK(project==0x86bd027c&&track<8);++turn_calls;last_slot=slot;last_event=event;if(!clamp)++value;return 0x12345678;}
i32 get(u32 project,u32 track,u32 slot){CHECK(project==0x86bd027c&&track<8&&slot<8);++reads;return value;}
}
const platform::SynthControls sdk_synth_controls[]={{reinterpret_cast<const char*>(73),ready,{4,1,2,3,0,5,6,7},0,{},format,false,false,0,0,{},{}}};
const u32 sdk_synth_control_count=1;
namespace opxy {namespace platform {
u32 set_parameter(u32,u32,u32,i32){CHECK(false);return 0;}
Address host_function(u32 a){switch(a){case 0x8c0944b0:return reinterpret_cast<Address>(top);case 0x8d51f1fc:return reinterpret_cast<Address>(press);case 0x8d5ad5bc:return reinterpret_cast<Address>(turn);case 0x8d5ac9dc:return reinterpret_cast<Address>(get);default:CHECK(false);return 0;}}
u32 word(u32 a){
 if(a==0x080238bc)return page;
 if(a==0x840a0524)return project_pointer;
 for(u32 t=0;t<8;++t)if(a==project_base+0x54cbcu+t*0x53f44u)return pattern_count[t];
 CHECK(false);return 0;
}
u32 byte(u32 a){
 if(a==0x86c05ca4)return role;
 if(a==kSelectedTrack)return current;
 if(a==project_base+0xc)return pattern_mask&255;
 if(a==project_base+0xd)return pattern_mask>>8;
 if(a==project_base+6)return scene;
 for(u32 t=0;t<8;++t){
  if(a==project_base+0xeu+t)return direct_pattern[t];
  if(a==project_base+0x98+33*scene+t)return scene_pattern[t];
 }
 CHECK(false);return 0;
}
u32 track_engine(u32 t){CHECK(t<8);++engine_reads;return engine[t];}u32 selected_track(){return current;}bool shift_held(){return shift;}
}}
int main(){
using control_interaction::detail::native_latest;
Track* missing = &tracks[0];
CHECK(machine(8, missing) == nullptr && missing == nullptr);
missing = &sends[0];
CHECK(send_effect(0, missing) == nullptr && missing == nullptr);
auto rejected=[&](u32 track){const u32 before=engine_reads;CHECK(!native_latest(track).available);CHECK(engine_reads==before);};
// SVG reset requests a baseline before project/pattern initialization. No
// unsafe stock engine getter may be reached for any failed precondition.
project_pointer=0;rejected(0);rejected(7);
project_pointer=project_base;pattern_count[0]=0;rejected(0);
pattern_count[0]=2;scene=7;scene_pattern[0]=2;rejected(0);
scene_pattern[0]=255;rejected(0);scene_pattern[0]=1;
CHECK(native_latest(0).available==1); // scene branch, valid last pattern
pattern_mask=1;direct_pattern[0]=2;rejected(0);
direct_pattern[0]=255;rejected(0);direct_pattern[0]=1;scene_pattern[0]=255;
CHECK(native_latest(0).available==1); // direct branch ignores invalid scene pattern
pattern_mask=0x80;direct_pattern[7]=1;rejected(7);
pattern_count[7]=2;CHECK(native_latest(7).available==1);
pattern_mask=0x100;scene_pattern[0]=1;direct_pattern[0]=255;
CHECK(native_latest(0).available==1); // unrelated high mask bit cannot select track 0
rejected(8);role=0;rejected(0);role=1;
pattern_mask=0;scene=0;scene_pattern[0]=0;pattern_count[0]=1;
auto e=native_latest(0);CHECK(e.available==1&&e.serial==0);
CHECK(sdk_synth_turn(0x86bd027c,0,2,99)==0x12345678);e=native_latest(0);
CHECK(e.serial==1&&e.physical_knob==2&&e.parameter_index==2&&e.normalized_value_at_event==124&&reads==1);
clamp=true;sdk_synth_turn(0x86bd027c,0,2,99);e=native_latest(0);CHECK(e.serial==2&&e.normalized_value_at_event==124);
value=999;CHECK(native_latest(0).normalized_value_at_event==124); // automation is not a physical event
project_pointer=0;rejected(0);project_pointer=project_base;
pattern_count[0]=0;rejected(0);pattern_count[0]=1;
CHECK(native_latest(0).serial==2&&native_latest(0).normalized_value_at_event==124); // guard does not erase events
sdk_synth_press(0);e=native_latest(0);CHECK(e.serial==3&&e.parameter_index==4&&e.physical_knob==0&&e.normalized_value_at_event==999);
sdk_synth_press(0);CHECK(native_latest(0).serial==4&&native_latest(0).parameter_index==0);
sdk_synth_turn(0x86bd027c,1,3,17);CHECK(native_latest(1).serial==5&&native_latest(0).serial==4);
CHECK(last_event==17&&last_slot==3&&turn_calls==3&&press_calls==2);
page=1;sdk_synth_turn(0x86bd027c,0,2,1);sdk_synth_press(0);CHECK(native_latest(0).serial==4);
page=0;controller=0;sdk_synth_press(0);CHECK(native_latest(0).serial==4);
controller=0x8400dff8;engine[0]=74;sdk_synth_turn(0x86bd027c,0,1,1);sdk_synth_press(1);CHECK(!native_latest(0).available);
role=0;CHECK(!native_latest(1).available);CHECK(!native_latest(8).available);
std::puts("physical callback ordering, clamp, press, automation and startup pattern guards passed");
}
'''
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
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);platform=ROOT/'sdk/platform/opxy-1.2.1'
            for name in ('synth_controls.cpp','synth_controls.hpp','control_interaction_native.inc','shift_row.hpp'):
                shutil.copyfile(platform/name,folder/name)
            (folder/'stock.hpp').write_text(STOCK);(folder/'test.cpp').write_text(CALLBACK)
            exe=folder/('test.exe' if os.name=='nt' else 'test')
            for opt in ('-O0','-O2'):
                self.run_command([HOST_CXX,'-std=c++11',opt,'-Wall','-Wextra','-Werror','-I'+str(ROOT/'sdk/include'),folder/'test.cpp','-o',exe]);self.run_command([exe])
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
