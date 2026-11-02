"""Arrange button tap/hold arbitration with wrapping clock."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.pipeline import ROOT
from test_sample_services import HOST_CXX


class ArrangerMenuTests(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_tap_cancel_encoder_clicks_and_adjustment_actual_routes(self):
        source=(ROOT/'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        raw=source[source.index('void sdk_arranger_raw_event(const void* raw) {'):]
        owned=raw[raw.index('    if (key == 4 && arrange_owned) {'):raw.index('    if (sdk_arranger_bounce_busy()')]
        modal=raw[raw.index('    if (menu_open || menu_tool) {'):raw.index('            // Existing shortcuts')]+"        }\n    }\n"
        start=raw.index('    if (key == 4 && down && view_active()')
        begin=raw[start:raw.index('    if (piano_view && !view_active())',start)]
        encoder=source[source.index('void sdk_arranger_encoder_dispatch(u32 encoder, const void* event) {'):]
        encoder=encoder[encoder.index('    if (menu_open || menu_tool) {'):encoder.index('    if (bounce_choice.open)')]
        fixture=r"""
#include "arranger_menu.hpp"
#include "arranger_edit_guard.hpp"
#include <cstdlib>
#include <cstdio>
using namespace opxy;
struct Event{u32 words[4];};
platform::ArrangerMenuGesture menu_gesture;
platform::ArrangerRawEditGate raw_edit_gate{};
bool arrange_owned,menu_open,piano_view;
u32 menu_tool, menu_index,menu_count=23, clock_now,opens,actions,stock_events,resets,turns;
struct{bool pending;}stop_gesture;
namespace opxy{namespace platform{bool shift_held(){return false;}}}
u32 ui_now(){return clock_now;}
bool held(u32){return false;}
bool view_active(){return true;}
void open_menu(){menu_open=true;++opens;}
void finish_menu_tool(bool){menu_tool=0;}
void run_menu_action(u32){menu_open=false;++actions;}
void menu_parameter_reset(){++resets;}
void menu_tool_turn(i32,bool){++turns;}
void stock_arranger_encoder_dispatch(u32,const void*){++stock_events;}
#define require(ok) do{if(!(ok)){std::fprintf(stderr,"line %d opens %u stock %u actions %u\n",__LINE__,opens,stock_events,actions);std::abort();}}while(false)
void event(u32 key,bool down){
 if(key<72 && raw_edit_gate.blocked[key]){raw_edit_gate.consume(key,down,false,true,false);return;}
"""+owned+modal+begin+r"""
 ++stock_events;
}
void turn(u32 encoder,const void* event){
"""+encoder+r"""
}
int main(){
 event(4,true);event(4,true);require(arrange_owned&&!opens&&!stock_events);
 clock_now=20;event(4,false);require(menu_open&&opens==1&&!arrange_owned&&!stock_events);
 event(4,true);event(4,true);require(!menu_open&&!stock_events);
 event(4,false);require(!raw_edit_gate.blocked[4]&&!stock_events);
 for(u32 key=10;key<=11;++key){menu_open=true;event(key,true);event(key,true);event(key,false);require(actions==key-9&&!menu_open&&!stock_events);}
 Event e{{0,1,0,0}};menu_open=true;menu_index=20;
 turn(0,&e);require(menu_index==21&&resets==1);
 turn(1,&e);require(turns==1&&menu_tool==0&&menu_open);
 turn(3,&e);require(turns==1&&stock_events==0);
 event(4,true);event(4,false);require(!menu_open&&!stock_events);
 clock_now=100;event(4,true);clock_now=900;event(4,false);require(menu_open&&opens==2&&!stock_events);
}
"""
        with tempfile.TemporaryDirectory(prefix='arranger-menu026-') as directory:
            cpp=Path(directory)/'test.cpp';cpp.write_text(fixture);binary=Path(directory)/'test'
            result=subprocess.run([HOST_CXX,'-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform/opxy-1.2.1'),str(cpp),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertEqual(subprocess.run([str(binary)]).returncode,0)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_actual_menu_click_executes_once(self):
        source=(ROOT/'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        start=source.index('void run_menu_action(u32 initiating_key = 13) {')
        action=source[start:source.index('// Own unit rectangles',start)]
        guard=source[source.index('    for (u32 key = 0; key < 72; ++key)', source.index('void open_edit(')):source.index('    return_track =', source.index('void open_edit('))]
        fixture=r"""
#include <opxy/types.hpp>
#include <cstdlib>
using namespace opxy;
u32 menu_index,menu_tool,menu_project,menu_slot,cursor, selected_slot=2, applies, last_tool, bounces;
bool menu_open=true,inside=true;
int menu_preview;
const char* menu_unavailable(u32){return nullptr;}
void announce(const char*){}
bool menu_context_valid(){return true;}
void finish_menu_tool(bool apply){if(!apply)std::abort();++applies;last_tool=menu_tool;menu_tool=0;}
u32 mute_actions;void toggle_selected_mute(){++mute_actions;}
void timeline_key(u32,bool){}
u32 editor_key; void open_edit(u32 key){editor_key=key;}
void open_piano(){}
u32 sdk_arranger_bounce_request(u32,u32,bool replace){if(replace)std::abort();++bounces;return 0;}
namespace platform{bool arranger_trim_clip_left(int&,u8*,u32,u32){return inside;}}
struct {void begin(){} void modify(){} void release(){}} stop_gesture;
void require(bool ok){if(!ok)std::abort();}
bool held_keys[72], accepted;
bool held(u32 key){return held_keys[key];}
"""+"void entry_guard(u32 initiating_key){\n"+guard+"accepted=true;\n}\n"+action+r"""
int main(){
 held_keys[10]=true;entry_guard(10);require(accepted);
 accepted=false;held_keys[11]=true;entry_guard(10);require(!accepted);
 held_keys[10]=false;held_keys[55]=true;entry_guard(11);require(accepted);
 const u32 rows[]={10,11,21,22};
 for(u32 row:rows){menu_open=true;menu_index=row;u32 before=applies;run_menu_action();require(applies==before+1&&last_tool==row+1&&!menu_tool&&!menu_open);}
 menu_index=8;run_menu_action();require(applies==5&&last_tool==9);
 inside=false;run_menu_action();require(applies==5);
 menu_index=12;run_menu_action(10);require(editor_key==10);run_menu_action(11);require(editor_key==11);
 menu_index=14;run_menu_action();menu_index=15;run_menu_action();require(bounces==2);
 menu_index=23;run_menu_action();require(mute_actions==1);
}
"""
        with tempfile.TemporaryDirectory(prefix='arranger-menu-click-') as directory:
            cpp=Path(directory)/'test.cpp';cpp.write_text(fixture);binary=Path(directory)/'test'
            result=subprocess.run([HOST_CXX,'-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'sdk/include'),str(cpp),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertEqual(subprocess.run([str(binary)]).returncode,0)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_actual_shift_arrange_entry_and_release(self):
        source = (ROOT / 'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        helper = source[source.index('bool enter_arranger_menu() {'):source.index('bool consume_play(bool shifted) {')]
        start = source.index('    if (key == 4 && down && held(55)')
        route = source[start:source.index('    if (key == 4 && down && view_active()', start)]
        fixture = r"""
#include "arranger_edit_guard.hpp"
#include <cstdlib>
using namespace opxy;
bool linear=true, editing=false, ready=true, piano_view=true, shift=true;
u32 controller=7, menu_count, refresh_count, synced, stock_events;
platform::ArrangerRawEditGate raw_edit_gate{};
bool sdk_arranger_project_linear(){return linear;}
bool registered(){return ready;}
bool held(u32 key){return key==55 && shift;}
void select_controller(u32 value){controller=value;}
void refresh(){++refresh_count;}
template<class Signature> Signature* stock(u32 address){return address==0x8c094178?reinterpret_cast<Signature*>(&select_controller):reinterpret_cast<Signature*>(&refresh);}
void sync_selected_lane(bool entering){if(entering)++synced;}
void open_menu(){if(controller!=40)std::abort();++menu_count;}
void require(bool value){if(!value)std::abort();}
""" + helper + r"""
void event(u32 key,bool down){
 if(key<72 && raw_edit_gate.blocked[key]){raw_edit_gate.consume(key,down,false,true,false);return;}
""" + route + r"""
 ++stock_events;
}
int main(){
 event(4,true);require(controller==40 && menu_count==1 && synced==1 && !piano_view && !stock_events);
 event(4,true);require(menu_count==1 && !stock_events);
 shift=false;event(55,false);require(stock_events==1); // Shift release remains stock-owned.
 event(4,false);require(!raw_edit_gate.blocked[4] && stock_events==1);
 shift=true;linear=false;event(4,true);event(4,false);require(stock_events==3 && menu_count==1);
 linear=true;ready=false;event(4,true);require(stock_events==4);
 ready=true;editing=true;event(4,true);require(stock_events==5 && menu_count==1);
 editing=false;controller=41;event(4,true);require(controller==40 && menu_count==2);
 linear=false;event(4,false);require(!raw_edit_gate.blocked[4] && stock_events==5);
}
"""
        with tempfile.TemporaryDirectory(prefix='arranger-shift-menu-') as directory:
            cpp=Path(directory)/'test.cpp'; cpp.write_text(fixture)
            binary=Path(directory)/'test'
            result=subprocess.run([HOST_CXX,'-std=c++11','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform/opxy-1.2.1'),str(cpp),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertEqual(subprocess.run([str(binary)]).returncode,0)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_arrange_tap_hold_gesture(self):
        hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        with tempfile.TemporaryDirectory(prefix='arranger-menu-') as directory:
            binary = Path(directory) / ('resolver.exe' if os.name == 'nt' else 'resolver')
            result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-D__builtin_bfin_ssync()=host_barrier()',
                '-I' + str(ROOT / 'sdk/include'), str(ROOT / 'tests/fixtures/arranger_menu.cpp'), '-o', str(binary)],
                capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
