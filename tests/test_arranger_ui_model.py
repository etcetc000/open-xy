"""Native arranger clipboard, cursor, zoom and clip operations."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.pipeline import ROOT
from test_sample_services import HOST_CXX


class ArrangerUiModelTests(unittest.TestCase):
    def test_lane_sync_precedes_entry_and_input_targets(self):
        source = (ROOT / 'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        sync = source[source.index('void sync_selected_lane(bool entering = false) {'):source.index('const u8* piano_source()')]
        self.assertIn('platform::selected_track()', sync)
        self.assertIn('select_clip(false)', sync)
        self.assertNotIn('0x8d585290', sync)  # Read selection; never move the stock instrument.
        for signature in ('void sdk_arranger_raw_event(const void* raw) {',
                          'void sdk_arranger_encoder(void* controller, u32 encoder, const void* raw) {'):
            self.assertTrue(source.split(signature, 1)[1].lstrip().startswith('sync_selected_lane();'))

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_actual_page_entry_before_controller_switch(self):
        source = (ROOT / 'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        sync = source[source.index('void sync_selected_lane(bool entering = false) {'):source.index('const u8* piano_source()')]
        page = source[source.index('u32 sdk_arranger_page(void*) {'):source.index('u32 sdk_arranger_active()', source.index('u32 sdk_arranger_page(void*) {'))]
        fixture = r'''#include "arranger_ui_model.hpp"
#include <cstdlib>
using namespace opxy;
bool editing, piano_view, busy, linear = true, ready = true, top;
u32 ui_track, selected_pattern = 7, page_id = 100, native_track = 5, selections;
i32 resizing_slot = 9;
struct { bool pending; } pending_add;
struct { void breakGroup() {} } history;
namespace opxy { namespace platform { u32 selected_track() { return native_track; } }}
bool view_active() { return top; }
bool sdk_arranger_bounce_busy() { return busy; }
bool sdk_arranger_project_linear() { return linear; }
bool registered() { return ready; }
void select_clip(bool) { ++selections; selected_pattern = ui_track + 1; }
void require(bool ok) { if (!ok) std::abort(); }
''' + sync + page + r'''
int main() {
    require(sdk_arranger_page(nullptr) == 100 && ui_track == 5 && selected_pattern == 6 && selections == 1);
    native_track = 2; pending_add.pending = true;
    require(sdk_arranger_page(nullptr) == 100 && ui_track == 5 && selections == 1);
    pending_add.pending = false; native_track = 14;
    require(sdk_arranger_page(nullptr) == 100 && ui_track == 5);
    native_track = 2; linear = false;
    require(sdk_arranger_page(nullptr) == 47 && ui_track == 5);
    linear = true; editing = true;
    require(sdk_arranger_page(nullptr) == 47 && ui_track == 5);
    editing = false;
    require(sdk_arranger_page(nullptr) == 100 && ui_track == 2 && selections == 2);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory); cpp = folder / 'entry.cpp'; binary = folder / 'entry'
            cpp.write_text(fixture)
            result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'),
                str(cpp), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(subprocess.run([str(binary)]).returncode, 0)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_native_timeline_operations(self):
        hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        with tempfile.TemporaryDirectory(prefix='arranger-edit-') as directory:
            binary = Path(directory) / ('resolver.exe' if os.name == 'nt' else 'resolver')
            result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT / 'sdk/include'), str(ROOT / 'tests/fixtures/arranger_ui_model.cpp'), '-o', str(binary)],
                capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
