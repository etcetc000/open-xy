"""Exercise the real send-page control bridge with host header boundaries."""
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
        native = (ROOT / 'sdk/platform/opxy-1.2.1/screen.hpp').read_text(encoding='utf-8')
        start = native.index('inline bool send_controls(')
        body = native[start:native.index('\n}\n', start) + 3]
        source = r'''#include "synth_controls.hpp"
#include <cstdlib>
namespace opxy { namespace screen {
struct Leaf {};
struct Base { Leaf* titles[4]; Leaf* values[4]; };
struct Controls { bool valid; u32 track; u8 shown[4]; i32 values[4], slots[8]; u32 shift; u8 choices[4]; };
static unsigned writes;
void set_text(Leaf* leaf, const char*) { if (!leaf) std::abort(); ++writes; }
''' + body + r'''
}}
bool sdk_send_view(opxy::u32 track, opxy::platform::SendView* view) {
    if (track != 14) return false;
    *view = {}; view->shift = true; view->choices[0] = 2; view->slots[7] = 123;
    for (unsigned i=0;i<4;++i) { view->shown[i]=i+4; view->values[i]=100+i; }
    return true;
}
int main() {
    using namespace opxy::screen;
    Base base={}; Controls controls={}; controls.track=14;
    if (!send_controls(&base,controls,true) || writes || !controls.valid || !controls.shift ||
        controls.choices[0]!=2 || controls.slots[7]!=123 || controls.shown[0]!=4 || controls.values[3]!=103) return 1;
    Leaf leaf; for(unsigned i=0;i<4;++i)base.titles[i]=base.values[i]=&leaf;
    if (!send_controls(&base,controls,false) || writes!=8) return 2;
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
