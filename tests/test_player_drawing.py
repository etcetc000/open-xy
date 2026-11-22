"""A note player's own drawn page (SDK 1.22): its contract, generated unit and renderer."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tooling import adapters, native
from tooling.machine_test import drawn_player
from tooling.machines import machine_at, assign_numbers
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
FLAGS = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}

DISPLAY = dict(stateBytes=8, stateAlignment=4, paths=0, points=0, nodes=1, texts=0, palette=1)
PLAYER = '''#include <opxy/player.hpp>
namespace probedraw {
struct State { opxy::u32 held, note; };
bool press(State& s, const opxy::player::Controls&, opxy::u32 note, opxy::u32, opxy::player::Plan& plan) {
    s.held = 1; s.note = note; plan.count = 0; return false;
}
bool release(State& s, opxy::u32) { s.held = 0; return false; }
}
'''
DISPLAY_H = '''#pragma once
#include <opxy/draw.hpp>
namespace probedraw {
using Frame = opxy::draw::Frame<0, 0, 1, 0, 1>;
struct DisplayState { opxy::u32 frames, held; };
}
'''
# A dot that grows while a key is held: it reads the player's State.
DISPLAY_CPP = '''#include "display.h"
namespace probedraw {
inline void display_reset(DisplayState& d) { d.frames = d.held = 0; }
inline void display(DisplayState& d, const State& s, const opxy::player::Controls&, const opxy::draw::Input&, Frame& f) {
    ++d.frames;
    d.held = s.held;
    opxy::draw::place(f, 0, 240 << 10, 120 << 10, s.held ? 2048 : 1024, 0, opxy::draw::fill_and_stroke);
}
}
'''
PANEL = '''<g id="probedraw_panel">
<circle id="probedraw_node_0" transform="matrix(1 0 0 1 240 120)" cx="0" cy="0" r="6" fill="#FFFFFF" stroke="#FFFFFF" stroke-width="0.5"/>
<circle id="probedraw_palette_0" cx="640" cy="0" r="1" fill="#FFFFFF" stroke="#FFFFFF" stroke-width="0.5"/>
</g>
'''


class PlayerDrawnPage(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve() / 'probedraw'
        self.root.mkdir()
        self.write_machine('1.22')
        self.write_definition(display=DISPLAY)
        for name, text in (('player.cpp', PLAYER), ('display.h', DISPLAY_H), ('display.cpp', DISPLAY_CPP),
                           ('panel.svg.fragment', PANEL)):
            (self.root / name).write_text(text, encoding='utf-8')

    def write_machine(self, sdk):
        (self.root / 'machine.json').write_text(json.dumps(dict(schema=1, sdk=sdk, kind='player',
                                                                description='Drawn player page fixture.')))

    def write_definition(self, display=None, paged=False):
        resources = {} if display is None else dict(display=display)
        parameters = [dict(id='p' + str(i), label='P' + str(i), default=0,
                           mapping=dict(kind='enum', labels=['a', 'b']), display='enum')
                      for i in range(6 if paged else 4)]
        definition = dict(schema=1, id='probedraw', name='Probe Draw', presets={}, resources=resources,
                          parameters=parameters)
        if paged:
            definition['press'] = 'page'
        (self.root / 'definition.json').write_text(json.dumps(definition))

    def machine(self):
        return assign_numbers([machine_at(self.root)])[0][0]

    def test_a_display_makes_the_page_the_players_own_from_sdk_1_22(self):
        self.assertEqual(self.machine().display, 'drawing')
        self.write_machine('1.21')
        with self.assertRaisesRegex(ValueError, r"note player's own drawn page.*1\.22"):
            machine_at(self.root)
        self.write_machine('1.22')
        (self.root / 'display.cpp').unlink()
        with self.assertRaisesRegex(ValueError, 'supplies display.h and display.cpp'):
            machine_at(self.root)

    def test_a_player_without_a_display_keeps_the_dot_page(self):
        self.write_definition()
        m = self.machine()
        self.assertEqual(m.display, 'player')
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_player_object(directory, m).read_text(encoding='utf-8')
        self.assertIn('opxy::screen::PlayerPage<Page>', text)
        self.assertNotIn('display.cpp', text)

    def test_only_plain_drawing(self):
        self.write_definition(display=dict(DISPLAY, affine=1))
        with self.assertRaisesRegex(ValueError, 'Extended drawing is for synth and effect screens'):
            machine_at(self.root)

    def test_generated_unit_draws_with_state(self):
        m = self.machine()
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_player_object(directory, m).read_text(encoding='utf-8')
        # display.cpp after player.cpp, in the one unit a player compiles.
        self.assertLess(text.index('#include "player.cpp"'), text.index('#include "display.cpp"'))
        self.assertIn('opxy::screen::PlayerDrawing<Page>', text)
        self.assertIn('typedef opxy::draw::Frame<0, 0, 1, 0, 1> Frame;', text)
        self.assertIn('Page::ids[2] = {\n    "probedraw_node_0",\n    "probedraw_palette_0",\n};', text)
        self.assertIn('static_cast<const State&>(state(object))', text)
        self.assertIn(f'"{m.native_name}.svg", {adapters.RELOCATED["PAGE_LIMIT"]}, {adapters.screen_bytes(m)})', text)
        self.assertNotIn('_dot_', text)
        self.assertIn('static const bool paged = false;', text)

    def test_generated_unit_compiles(self):
        clang = shutil.which('clang++')
        if not clang:
            self.skipTest('No clang++ for a freestanding 32-bit syntax check')
        for paged in (False, True):
            self.write_definition(display=DISPLAY, paged=paged)
            m = self.machine()
            with tempfile.TemporaryDirectory() as directory:
                unit = adapters.generate_player_object(directory, m)
                includes = [*(str((ROOT / path).resolve()) for path in ('.', 'sdk/include', 'sdk/platform',
                                                                          'sdk/platform/opxy-1.2.1')),
                            str(self.root), str(ROOT / 'sdk/platform/blackfin/freestanding')]
                result = subprocess.run([clang, '--target=i686-unknown-elf', '-fsyntax-only', '-O2', '-std=gnu++11',
                                         '-ffreestanding', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
                                         '-nostdinc++', '-nostdinc', '-Wall', '-Werror', '-Wno-unused-variable',
                                         *('-I' + path for path in includes), str(unit)],
                                        capture_output=True, text=True, timeout=120, **FLAGS)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_artwork_is_a_display_and_gains_no_dot_page_header(self):
        self.write_definition(display=DISPLAY, paged=True)
        m = self.machine()
        # A paged drawn player draws its own two-page header (opxy/page_header.hpp).
        self.assertEqual(native.page_artwork(m), PANEL.encode('utf-8'))
        (self.root / 'panel.svg.fragment').write_text(PANEL.replace('probedraw_node_0', 'probedraw_other'),
                                                      encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'missing path/circle probedraw_node_0'):
            native.validate_panel(m)

    def test_sdk_test_draws_a_held_keys_frames(self):
        m = self.machine()
        scratch = Path(self.temp.name) / 'scratch'
        scratch.mkdir()
        self.assertEqual(drawn_player(m, HOST_CXX, scratch), 720)
        # A display that leaves its node unplaced draws invalid frames, which the device skips.
        (self.root / 'display.cpp').write_text(DISPLAY_CPP.replace('opxy::draw::place(', 'if (s.held) opxy::draw::place('),
                                               encoding='utf-8')
        shutil.rmtree(scratch)
        scratch.mkdir()
        with self.assertRaisesRegex(ValueError, r'of its 720 frames are invalid'):
            drawn_player(m, HOST_CXX, scratch)

    def test_real_renderer_fills_the_input_and_the_header(self):
        host = Path(self.temp.name) / 'host'
        host.mkdir()
        double = ROOT / 'tests/fixtures/screen_player_drawing_host.hpp'
        for name in ('retained_drawing', 'note_activity', 'amp_envelope', 'players'):
            shutil.copy(double, host / (name + '.hpp'))
        shutil.copy(PLATFORM / 'screen_player_drawing.hpp', host)
        binary = host / ('screen.exe' if os.name == 'nt' else 'screen')
        subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(host),
                        '-I' + str(ROOT / 'sdk/include'), str(ROOT / 'tests/fixtures/screen_player_drawing.cpp'),
                        '-o', str(binary)], check=True, capture_output=True, timeout=120, **FLAGS)
        subprocess.run([str(binary)], check=True, capture_output=True, timeout=30, **FLAGS)


if __name__ == '__main__':
    unittest.main()
