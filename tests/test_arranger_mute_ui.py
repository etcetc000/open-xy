"""Actual per-instance mute action and Stop chord ownership."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from tooling.pipeline import ROOT
from test_sample_services import HOST_CXX

class ArrangerMuteUiTests(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_toggle_and_stop_release(self):
        source=(ROOT/'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        toggle=source[source.index('void toggle_selected_mute() {'):source.index('const char* menu_unavailable')]
        start=source.index('    if (down && key == 10 && view_active() && stop_gesture.pending')
        route=source[start:source.index('    if (down && view_active() && ((key >= 14',start)]
        fixture=r'''
#include "arranger_ui_model.hpp"
#include "arranger_edit_guard.hpp"
#include <cstdlib>
#include <cstring>
using namespace opxy;
arranger::Model model;
platform::ArrangerStopGesture stop_gesture{};
platform::ArrangerRawEditGate raw_edit_gate{};
i32 selected_slot=0; bool ready=true;u32 commits,history,stock_events;const char* message="";
void sdk_arranger_project_poll(){}
bool sdk_arranger_project_edit_ready(){return ready;}
bool view_active(){return true;}
arranger::Model& sdk_arranger_model(){return model;}
void announce(const char* value){message=value;}
void begin_history(){++history;}
bool commit_edit(bool changed,const char* text){if(changed){++commits;announce(text);}return changed;}
bool held(u32){return false;}
void require(bool value){if(!value)std::abort();}
'''+toggle+r'''
void event(u32 key,bool down){
 if(key<72&&raw_edit_gate.blocked[key]){raw_edit_gate.consume(key,down,false,true,false);return;}
'''+route+r'''
 ++stock_events;
}
int main(){
 require(model.insert({0,0,0,7680,0,30720})==0);
 require(model.insert({0,0,7680,7680,0,30720})==1);
 stop_gesture.begin();event(10,true);event(10,true);event(10,false);
 require(model.clip(0)->muted&&!model.clip(1)->muted&&commits==1&&history==1&&!stock_events);
 require(!stop_gesture.release()&&!std::strcmp(message,"MUTED"));
 stop_gesture.begin();event(10,true);require(!stop_gesture.release());event(10,false);
 require(!model.clip(0)->muted&&commits==2&&!std::strcmp(message,"UNMUTED"));
 selected_slot=-1;stop_gesture.begin();event(10,true);event(10,false);require(!stop_gesture.release()&&commits==2);
 selected_slot=0;ready=false;toggle_selected_mute();require(!model.clip(0)->muted&&commits==2);
}
'''
        with tempfile.TemporaryDirectory(prefix='arranger-mute-ui-') as d:
            cpp=Path(d)/'test.cpp';cpp.write_text(fixture);binary=Path(d)/'test'
            run=subprocess.run([HOST_CXX,'-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform/opxy-1.2.1'),str(cpp),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertEqual(subprocess.run([str(binary)]).returncode,0)

    def test_muted_paint_preserves_labels_selection_and_piano(self):
        source=(ROOT/'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        self.assertIn('"Toggle Mute Clip (Stop + click E1)"',source)
        self.assertIn('clip_paint(o, shown, c->muted)',source)
        self.assertIn('clip_paint(o, shown, false)',source)
        self.assertIn('rectangle(o->selection, 35 + x',source)
        self.assertIn("static_cast<char>('A' + c->pattern)",source)
        self.assertIn('id="mutedclipstyle"',(ROOT/'sdk/platform/opxy-1.2.1/arranger.svg').read_text())
