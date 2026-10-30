"""Deferred add-clip request ownership and stale-context rejection."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.pipeline import ROOT
from test_sample_services import HOST_CXX


class ArrangerPendingAddTests(unittest.TestCase):
    def test_native_ui_preserves_painted_stage_sequence(self):
        source = (ROOT / 'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        key = source[source.index('void timeline_key('):source.index('const char* menu_unavailable(')]
        self.assertLess(key.index('add_clip(); return;'), key.index('sdk_arranger_project_poll();'))
        request = source[source.index('void add_clip() {'):source.index('void process_pending_add()')]
        self.assertIn('pending_add.enqueue', request)
        self.assertNotIn('arranger_add_empty_clip', request)
        poll = source[source.index('void sdk_arranger_poll() {'):source.index('void sdk_arranger_project_restored(', source.index('void sdk_arranger_poll() {'))]
        self.assertIn('if (pending_add.pending) { process_pending_add(); return; }', poll)
        self.assertLess(poll.index('stock_arranger_poll();'), poll.index('process_pending_add();'))
        self.assertLess(poll.index('process_pending_add();'), poll.index('sdk_arranger_project_poll();'))
        execute = source[source.index('void process_pending_add()'):source.index('void timeline_key(')]
        self.assertLess(execute.index('pending_add.ready('), execute.index('switch (pending_add.stage)'))
        for stage in ('project_check', 'history', 'create', 'commit', 'select', 'draw'):
            self.assertIn('case A::' + stage + ':', execute)
        frame = source[source.index('static void frame(Object* o, i32)'):source.index('const VTable<Renderer>')]
        cached = frame[:frame.index('if (menu_open)')]
        self.assertIn('pending_add.pending && !pending_add.draw_requested', cached)
        self.assertIn('draw(&o->base); pending_add.acknowledge(ui_now()); return;', cached)
        self.assertLess(frame.rindex('draw(&o->base);'), frame.index('pending_add.cancel();'))
        press = source[source.index('void press(const void* raw, bool shifted) {'):source.index('void sdk_arranger_raw_event(const void* raw) {')]
        self.assertLess(press.index('if (pending_add.pending) return;'), press.index('const Event& event'))
        raw = source[source.index('void sdk_arranger_raw_event(const void* raw) {'):source.index('void sdk_arranger_encoder(void* controller')]
        self.assertIn('else if (!down) stock_arranger_raw_event(raw);', raw)
        self.assertIn('raw_edit_gate.consume(key, true, true, false, false)', raw)
        restored = source[source.index('void sdk_arranger_project_restored('):]
        self.assertIn('pending_add.cancel();', restored)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_modal_input_preserves_owned_releases(self):
        source = (ROOT / 'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        start = source.index('    if (pending_add.pending) {', source.index('void sdk_arranger_raw_event(const void* raw) {'))
        block = source[start:source.index('    // Consume new modal', start)]
        press_start = source.index('void press(const void* raw, bool shifted) {')
        press_gate = source[press_start:source.index('    const Event& event', press_start)]
        program = r"""
#include "arranger_pending_add.hpp"
#include "arranger_edit_guard.hpp"
#include <cstdlib>
using opxy::u32;
opxy::platform::ArrangerPendingAdd pending_add{};
opxy::platform::ArrangerRawEditGate raw_edit_gate{};
bool held_keys[72]{};
bool held(u32 key) { return key < 72 && held_keys[key]; }
u32 releases, actions;
void press(const void*, bool);
void stock_arranger_raw_event(const void* raw) { press(raw, false); ++releases; }
void require(bool value) { if (!value) std::abort(); }
void event(u32 key, bool down) { const void* raw = nullptr;
""" + block + "\n}\n" + press_gate + "    (void)raw; (void)shifted; ++actions;\n}\n" + r"""
int main() {
    pending_add.pending = true;
    held_keys[54] = true; held_keys[26] = true;
    event(54, true); require(!raw_edit_gate.blocked[54] && !releases);
    event(54, false); event(26, false); require(releases == 2 && actions == 0);
    for (unsigned stage = 1; stage <= 6; ++stage) {
        pending_add.stage = static_cast<opxy::platform::ArrangerPendingAdd::Stage>(stage);
        press(nullptr, false); press(nullptr, true); require(actions == 0);
    }
    event(51, true); event(51, true); require(raw_edit_gate.blocked[51] && releases == 2);
    event(51, false); require(!raw_edit_gate.blocked[51] && releases == 2);
    event(4, true); require(raw_edit_gate.blocked[4]);
    pending_add.cancel();
    require(raw_edit_gate.consume(4, false, false, true, false));
    require(!raw_edit_gate.blocked[4]); // Paired release after modal completes remains consumed.
}
"""
        with tempfile.TemporaryDirectory(prefix='arranger-add-input-') as directory:
            cpp = Path(directory) / 'test.cpp'; cpp.write_text(program)
            binary = Path(directory) / ('test.exe' if os.name == 'nt' else 'test')
            result = subprocess.run([HOST_CXX, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'),
                str(cpp), '-o', str(binary)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_request_lifetime(self):
        hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        with tempfile.TemporaryDirectory(prefix='arranger-edit-') as directory:
            binary = Path(directory) / ('resolver.exe' if os.name == 'nt' else 'resolver')
            result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT / 'sdk/include'), str(ROOT / 'tests/fixtures/arranger_pending_add.cpp'), '-o', str(binary)],
                capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
