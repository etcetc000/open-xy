"""Execute the production GUI timing reconciliation with isolated native sources."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.pipeline import ROOT
from test_sample_services import HOST_CXX


class ArrangerTimingUiTests(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_live_timing_reconciliation(self):
        source = (ROOT / 'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        function = source[source.index('void reconcile_source_timing() {'):source.index('struct BarTimingEvent {')]
        fixture = r'''
#define arranger_pattern_source real_pattern_source
#include "arranger_timing.hpp"
#undef arranger_pattern_source
#include "arranger_history.hpp"
#include <cstdio>
#include <cstdlib>
using namespace opxy;
u32 project_id=1, generation=1, timing_project, timing_generation, ui_track;
u32 saved,published,reset_count;
u8 timing_lengths[8][16], timing_scales[8][16], sources[8][16][9];
bool active=true, ready=true, busy=false, menu_open=false;
u32 menu_tool;
i32 resizing_slot=1;
struct { bool pending; } pending_add;
struct { bool open; } bounce_choice;
struct { void reset() { ++reset_count; } } history;
arranger::Model model;
namespace opxy { namespace platform {
u32 word(u32) { return project_id; }
const u8* arranger_pattern_source(const u8*,u32 track,u32 pattern,bool) {
    return sources[track][pattern][0] ? sources[track][pattern] : nullptr;
}
}}
u32 current_generation() { return generation; }
template<class T> T* stock(u32) { return reinterpret_cast<T*>(current_generation); }
bool sdk_arranger_bounce_busy() { return busy; }
bool sdk_arranger_project_edit_ready() { return ready; }
arranger::Model& sdk_arranger_model() { return model; }
bool sdk_arranger_project_changed(bool,u32) { ++saved; return true; }
void sdk_arranger_publish(bool,bool) { ++published; }
void select_clip(bool) {}
void announce(const char*) {}
void require(bool ok,const char* message) { if (!ok) { std::puts(message); std::abort(); } }
''' + function + r'''
int main() {
    sources[0][0][0]=64; sources[0][0][8]=3;
    require(model.insert({0,0,0,7680,30000,30720,1679})==0,"first instance");
    require(model.insert({0,0,10000,7680,100,30720})==1,"linked instance");
    reconcile_source_timing();
    require(!saved && model.clip(0)->length==7680,"load baseline preserves saved geometry");
    sources[0][0][8]=7;
    menu_tool=1; reconcile_source_timing(); require(!saved,"active tool defers reconciliation");
    menu_tool=0; busy=true; reconcile_source_timing(); require(!saved,"bounce defers reconciliation");
    busy=false; pending_add.pending=true; reconcile_source_timing(); require(!saved,"pending add defers reconciliation");
    pending_add.pending=false; ready=false; reconcile_source_timing(); require(!saved,"project transaction defers reconciliation");
    ready=true; reconcile_source_timing();
    require(saved==1 && published==1 && reset_count==1,"native change persists and invalidates stale history");
    require(model.clip(0)->length==10000 && model.clip(1)->length==30720,"all instances scale with neighbor collision cap");
    sources[0][0][0]=4; reconcile_source_timing();
    require(model.clip(0)->length==7680 && model.clip(1)->length==7680,"short native source caps both instances");
    require(model.clip(0)->source_length==1920 && model.clip(0)->offset==1200 &&
        model.clip(0)->phase_remainder==1679,"short source normalizes raw phase without losing fraction");
    const u32 previous_saved=saved; reconcile_source_timing(); require(saved==previous_saved,"no redundant save");
    ++generation; sources[0][0][8]=3; reconcile_source_timing();
    require(saved==previous_saved && model.clip(0)->length==7680,"same address new generation establishes baseline");
    timing_project=0; sources[0][0][8]=5; reconcile_source_timing();
    require(saved==previous_saved && model.clip(0)->length==7680,"restore reset preserves loaded clips");
}
'''
        with tempfile.TemporaryDirectory(prefix='arranger-timing-ui-') as directory:
            cpp = Path(directory) / 'timing.cpp'
            binary = Path(directory) / 'timing'
            cpp.write_text(fixture)
            result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'),
                str(cpp), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
