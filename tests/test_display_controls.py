"""A whole-screen canvas reads its controls without writing the stock header it does not have."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]


class DisplayControls(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_full_canvas_reads_controls_without_stock_header_writes(self):
        native = (ROOT / 'sdk/platform/opxy-1.2.1/extended_drawing.hpp').read_text(encoding='utf-8')
        start = native.index('inline bool canvas_controls(')
        body = native[start:native.index('\n}\n', start) + 3]
        source = r'''#include "synth_controls.hpp"
#include "drum_storage.hpp"
#include <cstdlib>
namespace opxy { namespace screen {
const u32 page_controls = 4, parameter_slots = 8;
struct Base { void* svg; };
struct Parameters { u32 begin, end; };
struct Controls {
    u32 track; bool valid; u8 shown[page_controls]; i32 values[page_controls]; i32 slots[parameter_slots];
    u32 model, pad; bool choosing; u32 shift; u8 choices[4]; i32 pan, level; u32 line;
    i32 pads[platform::kDrumPadFields];
};
static unsigned track = 14, reads;
static Parameters vector;
u32 selected_track() { return track; }
const Parameters* parameters(u32) { return &vector; }
bool read_shown(const Parameters* p, Controls& controls, u32 (&)[parameter_slots]) {
    ++reads; for (u32 i = 0; i < page_controls; ++i) controls.values[i] = 200 + i; return p == &vector;
}
// A header write would need text leaves a whole-screen canvas does not have.
void set_text(void*, const char*) { std::abort(); }
''' + body + r'''
}}
bool sdk_send_view(opxy::u32 track, opxy::platform::SendView* view) {
    if (track != 14) return false;
    *view = {}; view->shift = true; view->choices[0] = 2; view->slots[7] = 123;
    for (unsigned i=0;i<4;++i) { view->shown[i]=i+4; view->values[i]=100+i; }
    return true;
}
void sdk_synth_shown(opxy::u32, opxy::u8* shown) { for (unsigned i=0;i<4;++i) shown[i]=i; }
int main() {
    using namespace opxy::screen;
    int svg; Base base = {&svg}; Controls controls = {};
    if (!canvas_controls(&base, controls) || reads || !controls.valid || !controls.shift ||
        controls.choices[0]!=2 || controls.slots[7]!=123 || controls.shown[0]!=4 || controls.values[3]!=103) return 1;
    track = 3; controls = {};
    if (!canvas_controls(&base, controls) || reads != 1 || !controls.valid || controls.shift ||
        controls.shown[3] != 3 || controls.values[0] != 200 || controls.line) return 2;
    base.svg = nullptr;
    if (canvas_controls(&base, controls)) return 3;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            path, binary = folder / 'test.cpp', folder / ('test.exe' if os.name == 'nt' else 'test')
            path.write_text(source, encoding='utf-8')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            for command in ([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                             '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'),
                             path, '-o', binary], [binary]):
                result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=120, **hidden)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_plain_screens_keep_the_stock_header_path(self):
        # The plain renderer and its control bridge are unchanged: no full-canvas branch.
        screen = (ROOT / 'sdk/platform/opxy-1.2.1/screen.hpp').read_text(encoding='utf-8')
        self.assertIn('inline bool send_controls(Base* o, Controls& controls) {', screen)
        self.assertNotIn('full_canvas', screen)
        self.assertNotIn('full_canvas', (ROOT / 'sdk/platform/opxy-1.2.1/screen_drawing.hpp').read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
