"""BAR routing executes the real GUI binding and timing reconciliation."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from tooling.pipeline import ROOT
from test_sample_services import HOST_CXX


class ArrangerBarTimingTests(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_bar_changes_highlighted_b_not_scene_a(self):
        text = (ROOT / 'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        def section(start, end):
            return text[text.index(start):text.index(end, text.index(start))]
        functions = section('bool bar_binding_valid()', 'bool step_held()')
        functions += section('void reconcile_source_timing()', 'platform::ArrangerAddContext add_context()')
        functions += section('const u8* sdk_arranger_edit_source(u32 track, bool instrument) {', 'bool sdk_arranger_record_target')
        functions += section('u32 sdk_arranger_edit_index(const u8* address) {', 'void sdk_arranger_poll()')
        # Translate native absolute project address in the host fixture only.
        functions = functions.replace('reinterpret_cast<const u8*>(bar_project)', 'native_project')
        fixture = r'''
#define arranger_pattern_source real_pattern_source
#include "arranger_timing.hpp"
#undef arranger_pattern_source
#include <cstdio>
#include <cstdlib>
using namespace opxy;
u32 project_id=1,generation=1,timing_project,timing_generation,ui_track=4;
u8 timing_lengths[8][16],timing_scales[8][16],sources[8][16][9],native_project[1024];
u32 bar_project,bar_generation,bar_track,bar_pattern,bar_slot;
u32 edit_project,edit_track,edit_pattern;
bool bar_binding,active=true,editing,piano_view,menu_open,shifted,ready=true,top=true;
u32 menu_tool,saved,published,reset_count;
i32 selected_slot=0,resizing_slot;
struct { bool pending; } pending_add;
struct { bool open; } bounce_choice;
struct { void reset() { ++reset_count; } } history;
arranger::Model model;
namespace opxy { namespace platform {
u32 word(u32) { return project_id; }
u32 selected_track() { return ui_track; }
bool shift_held() { return shifted; }
const u8* arranger_pattern_source(const u8*,u32 track,u32 pattern,bool) { return sources[track][pattern][0]?sources[track][pattern]:nullptr; }
}}
u32 current_generation() { return generation; }
template<class T> T* stock(u32) { return reinterpret_cast<T*>(current_generation); }
bool stock_gui_role() { return true; }
bool sdk_arranger_bounce_busy() { return false; }
bool sdk_arranger_project_edit_ready() { return ready; }
bool view_active() { return top; }
bool edit_valid() { return false; }
const u8* sdk_arranger_bounce_destination_source(u32) { return nullptr; }
i32 sdk_arranger_bounce_destination_index(const u8*) { return -1; }
arranger::Model& sdk_arranger_model() { return model; }
bool sdk_arranger_project_changed(bool,u32) { ++saved; return true; }
void sdk_arranger_publish(bool,bool) { ++published; }
void select_clip(bool) {}
void announce(const char*) {}
void require(bool ok,const char* message) { if (!ok) { std::puts(message); std::abort(); } }
''' + functions + r'''
int main() {
    sources[4][0][0]=sources[4][1][0]=64;
    sources[4][0][8]=sources[4][1][8]=3;
    require(model.insert({4,1,0,7680,0,30720})==0,"highlight B on track five");
    require(model.insert({4,1,20000,7680,0,30720})==1,"linked B instance");
    const u8* index=native_project+0x98+4;
    require(sdk_arranger_edit_index(index)==0 && !sdk_arranger_edit_source(4,false),"normal GUI retains scene source");
    { BarTimingEvent event(25,true); }
    top=false; // Stock BAR popup becomes the top controller.
    require(sdk_arranger_edit_index(index)==1 && sdk_arranger_edit_source(4,false)==sources[4][1],"BAR getter and native setter bind highlighted B");
    require(sdk_arranger_edit_index(index-1)==0 && !sdk_arranger_edit_source(3,false),"other lanes stay stock");
    { BarTimingEvent event(0,false); sources[4][sdk_arranger_edit_index(index)][8]=5; }
    require(sources[4][0][8]==3 && sources[4][1][8]==5,"stock scale setter changes B only");
    require(model.clip(0)->length==15360 && model.clip(1)->length==15360 && saved==1 && published==1,
        "BAR one-to-two immediately stretches all linked B clips");
    { BarTimingEvent event(25,false); require(sdk_arranger_edit_index(index)==1,"release forwarded before binding clears"); }
    require(!bar_binding && sdk_arranger_edit_index(index)==0,"release restores normal stock routing");
    top=true; { BarTimingEvent event(25,true); } ++generation;
    require(!bar_binding_valid() && sdk_arranger_edit_index(index)==0,"project generation invalidates captured source");
    { BarTimingEvent event(0,false); } require(!bar_binding,"stale binding retired");
    shifted=true; { BarTimingEvent event(25,true); } require(!bar_binding,"shift BAR remains stock");
    shifted=false; ready=false; { BarTimingEvent event(25,true); } require(!bar_binding,"project transaction blocks binding");
}
'''
        with tempfile.TemporaryDirectory(prefix='arranger-bar-') as directory:
            cpp=Path(directory)/'bar.cpp'; binary=Path(directory)/'bar'
            cpp.write_text(fixture)
            result=subprocess.run([HOST_CXX,'-std=c++11','-O2','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform/opxy-1.2.1'),str(cpp),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        raw=section('void sdk_arranger_raw_event(const void* raw) {','void sdk_arranger_encoder(void* controller')
        self.assertIn('BarTimingEvent bar_event(key, down);',raw)
        self.assertIn('BarTimingEvent bar_event(0, false);',section('void sdk_arranger_encoder_dispatch(u32 encoder','const u8* sdk_arranger_edit_source(u32 track, bool instrument) {'))


if __name__ == '__main__':
    unittest.main()
