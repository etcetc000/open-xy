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
    def test_actual_menu_click_executes_once(self):
        source=(ROOT/'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        start=source.index('void run_menu_action() {')
        action=source[start:source.index('// Own unit rectangles',start)]
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
void timeline_key(u32,bool){}
void open_edit(){}
void open_piano(){}
u32 sdk_arranger_bounce_request(u32,u32,bool replace){if(replace)std::abort();++bounces;return 0;}
namespace platform{bool arranger_trim_clip_left(int&,u8*,u32,u32){return inside;}}
struct {void begin(){} void modify(){} void release(){}} stop_gesture;
void require(bool ok){if(!ok)std::abort();}
"""+action+r"""
int main(){
 const u32 rows[]={10,11,21,22};
 for(u32 row:rows){menu_open=true;menu_index=row;u32 before=applies;run_menu_action();require(applies==before+1&&last_tool==row+1&&!menu_tool&&!menu_open);}
 menu_index=8;run_menu_action();require(applies==5&&last_tool==9);
 inside=false;run_menu_action();require(applies==5);
 menu_index=14;run_menu_action();menu_index=15;run_menu_action();require(bounces==2);
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
