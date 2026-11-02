"""Execute the UI's real transform/hide functions against host leaf doubles."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from tooling.pipeline import ROOT
from test_sample_services import HOST_CXX


class ArrangerMenuVisibility(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_hide_clear_and_reopen_menu_rows(self):
        source = (ROOT / 'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        position = source[source.index('void text_position('):source.index('void clip_paint(')]
        hide = source[source.index('void hide_menu('):source.index('struct Renderer {')]
        program = r'''
#include <cstdlib>
#include <cstring>
using u32 = unsigned; using i32 = int;
struct Leaf { i32 matrix[9]; char text[80]; };
struct Object { Leaf *menu_background, *menu_highlight, *menu_rows[8], *menu_disabled[8], *menu_selected; };
template<unsigned N> void set_words(Leaf* leaf, const u32 (&at)[N], const i32 (&values)[N]) {
    for (unsigned i = 0; i < N; ++i) leaf->matrix[at[i]] = values[i];
}
void set_text(Leaf* leaf, const char* text) { std::strcpy(leaf->text, text); }
void rectangle(Leaf*, u32, u32, u32, u32) {}
void require(bool value) { if (!value) std::abort(); }
''' + position + hide + r'''
int main() {
    Leaf rows[16]{}, selected{}; Object object{}; object.menu_selected = &selected;
    for (unsigned i = 0; i < 8; ++i) {
        object.menu_rows[i] = &rows[i]; object.menu_disabled[i] = &rows[i + 8];
        set_text(&rows[i], "Split at cursor"); set_text(&rows[i + 8], "Undo unavailable");
        text_position(&rows[i], 16, 42 + i * 21, true);
    }
    hide_menu(&object);
    for (const Leaf& row : rows) {
        require(!row.text[0]);
        require(row.matrix[0] == 1024 && row.matrix[4] == 1024);
        require(row.matrix[6] == 640 * 1024 && row.matrix[7] == 320 * 1024);
    }
    set_text(&rows[0], "Split at cursor"); text_position(&rows[0], 16, 42, true);
    require(rows[0].matrix[0] == 1024 && rows[0].matrix[4] == 1024);
    require(rows[0].matrix[6] == 16 * 1024 && rows[0].matrix[7] == 42 * 1024);
    require(std::strcmp(rows[0].text, "Split at cursor") == 0);
}
'''
        with tempfile.TemporaryDirectory(prefix='menu-visibility-') as folder:
            cpp = Path(folder) / 'test.cpp'
            binary = Path(folder) / ('test.exe' if os.name == 'nt' else 'test')
            cpp.write_text(program)
            result = subprocess.run([HOST_CXX, '-std=c++11', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
