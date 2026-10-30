"""Execute native piano encoder mapping and entry viewport against boundary doubles."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from tooling.pipeline import ROOT
from test_sample_services import HOST_CXX

class PianoControls(unittest.TestCase):
    def test_piano_row_asset_contract(self):
        import xml.etree.ElementTree as ET
        asset = ET.fromstring((ROOT / 'sdk/platform/opxy-1.2.1/arranger.svg').read_text())
        nodes = {node.get('id'): node for node in asset.iter() if node.get('id')}
        for index in range(24):
            self.assertEqual(nodes['pianokey' + str(index)].get('fill'), '#181818')
        for index in range(8):
            self.assertIn('laneline' + str(index), nodes)

    @unittest.skipUnless(HOST_CXX, 'No host C++ compiler')
    def test_encoder_mapping_and_focus(self):
        ui = (ROOT / 'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        encoder = ui[ui.index('void piano_encoder('):ui.index('void begin_history(')]
        focus = ui[ui.index('    // Start on actual source notes'):ui.index('\n    announce("E1 CURSOR')]
        program = r'''
#include "arranger_ui_model.hpp"
#include "arranger_piano.hpp"
#include <cstdlib>
using namespace opxy;
namespace opxy { namespace platform { bool shift_held() { return false; } } }
bool fine_grid, piano_view;
u32 piano_time, piano_pitch=60, piano_low=56, piano_start, piano_window=7680, piano_rows=8;
i32 piano_selected=0;
struct FakePiano {
    platform::PianoNote value{960,480,72,100,0,0}; u32 writes=0; bool has=true;
    u32 span() { return 15360; }
    void breakGroup() {}
    bool first(platform::PianoNote& out) { out=value; return has; }
    bool note(u32, platform::PianoNote& out) { out=value; return true; }
    bool update(u32, platform::PianoNote value_, u32) { value=value_; ++writes; return true; }
} piano;
const u8* piano_source() { static u8 value; return &value; }
void piano_select() {}
bool piano_editable() { return true; }
void piano_changed(bool) {}
void require(bool value) { if (!value) std::abort(); }
''' + encoder + '\nvoid focus() {\n' + focus + '\n}\n' + r'''
int main() {
    piano.value.position=12000; piano.value.pitch=84; focus();
    require(piano_pitch==84 && piano_time==12000 && piano_view);
    require(piano_pitch>=piano_low && piano_pitch<piano_low+8);
    require(piano_time>=piano_start && piano_time<piano_start+piano_window);
    piano.value={960,480,72,100,0,0}; piano_time=960; piano_pitch=72; piano_start=0; piano_window=7680, piano_rows=8;
    piano_encoder(0,1); require(piano_time==1440 && !piano.writes); // Cursor only.
    piano_encoder(1,1); require(piano_window!=7680 && !piano.writes); // Zoom only.
    const u32 horizontal=piano_window;
    piano_encoder(5,-1); require(piano_rows==12 && piano_window==horizontal && !piano.writes);
    piano_encoder(5,1); require(piano_rows==8 && piano_window==horizontal && !piano.writes);
    piano_encoder(2,1); require(piano.value.pitch==73 && piano.value.duration==480);
    piano_encoder(3,1); require(piano.value.duration==960 && piano.value.pitch==73);
    piano_encoder(4,1); require(piano.value.position==961); // Shift E1 fine note move.
    const u32 writes=piano.writes;
    piano_encoder(6,1); require(piano_pitch==74 && piano.writes==writes); // Shift E3 navigation.
    piano_encoder(7,1); require(piano.value.velocity==101 && piano.value.duration==960);
    fine_grid=true; piano_encoder(3,-1); require(piano.value.duration==959);
    piano_selected=-1; piano_encoder(2,-1); require(piano_pitch==72);
    piano.has=false; piano_time=0; focus(); require(piano_pitch==60 && piano_start==0);
}
'''
        with tempfile.TemporaryDirectory(prefix='piano-controls-') as folder:
            source=Path(folder)/'test.cpp'; source.write_text(program)
            binary=Path(folder)/('test.exe' if os.name=='nt' else 'test')
            result=subprocess.run([HOST_CXX,'-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform/opxy-1.2.1'),str(source),'-o',str(binary)],capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
