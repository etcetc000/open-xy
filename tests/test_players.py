"""Player ABI and integration contracts; musical tests live with each player."""
from dataclasses import replace
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import shutil
import unittest

from tooling import adapters, native, records
from tooling.formats import ldr, mapped_offset
from tooling.machines import machine_at, assign_numbers, number_layout
from tooling.pipeline import config
from test_sample_services import HOST_CXX
from test_divide import blackfin_compiler

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'


def vtable():
    source = (PLATFORM / 'player_object.cpp.in').read_text(encoding='utf-8')
    start = source.index('_player_vtable[16] = {')
    body = source[start:source.index('};', start)].split('{', 1)[1]
    return [entry.strip() for entry in body.split(',') if entry.strip()]


class PlayerNativeLink(unittest.TestCase):
    @unittest.skipUnless(blackfin_compiler(), 'No Blackfin compiler in local/toolchain (set OPXY_TEST_COMPILER)')
    def test_two_player_keyboards_link_together(self):
        compiler = blackfin_compiler()
        linker = compiler.with_name('bfin-elf-ld' + compiler.suffix)
        assembler = compiler.with_name('bfin-elf-as' + compiler.suffix)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            objects = []
            for name in ('first', 'second'):
                source, obj = root / (name + '.cpp'), root / (name + '.o')
                source.write_text('#include <player_activity.hpp>\n'
                                  'opxy::u32 ' + name + '(opxy::u32 note) {\n'
                                  '    return opxy::platform::player_white(note);\n}\n')
                native.compile_cxx(compiler, assembler, source, obj, native.includes())
                # Match firmware/browser objects: ordinary ELF linking would merge the weak table.
                obj.write_bytes(native.normalize_object(obj.read_bytes()))
                objects.append(str(obj))
            result = subprocess.run([str(linker), '-r', *objects, '-o', str(root / 'players.o')],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class PlayerContracts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        (self.root / 'machine.json').write_text(json.dumps(dict(schema=1, sdk='1.8', kind='player',
                                                               description='Player contract fixture.')))
        definition = dict(schema=1, id='probeplayer', name='Player', resources={}, presets={},
                          parameters=[dict(id='p'+str(i), label='P'+str(i), default=0,
                                           mapping=dict(kind='linear', minimum=0, maximum=1), display='percent')
                                      for i in range(4)])
        (self.root / 'definition.json').write_text(json.dumps(definition))
        self.machine = assign_numbers([machine_at(self.root)])[0][0]

    def test_player_requires_its_own_sdk_revision(self):
        path = self.root / 'machine.json'
        value = json.loads(path.read_text(encoding='utf-8'))
        value['sdk'] = '1.7'
        path.write_text(json.dumps(value), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Note players needs.*1.8'):
            machine_at(self.root)

    def test_press_pages_a_player_with_more_than_four_parameters(self):
        path = self.root / 'definition.json'
        definition = json.loads(path.read_text(encoding='utf-8'))
        definition['parameters'] += [
            dict(id='p4', label='P4', default=0, mapping=dict(kind='enum', labels=['a', 'b', 'c']), display='enum'),
            dict(id='p5', label='P5', default=0, mapping=dict(kind='linear', minimum=0, maximum=1), display='percent')]
        path.write_text(json.dumps(definition), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'more than four parameters pages them'):
            machine_at(self.root)
        definition['press'] = 'page'
        path.write_text(json.dumps(definition), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'press switches the page.*1.16'):
            machine_at(self.root)
        machine = json.loads((self.root / 'machine.json').read_text(encoding='utf-8'))
        machine['sdk'] = '1.16'
        (self.root / 'machine.json').write_text(json.dumps(machine), encoding='utf-8')
        paged = assign_numbers([machine_at(self.root)])[0][0]
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_player_object(directory, paged).read_text(encoding='utf-8')
        # The second page's missing slots are absent: one choice, an empty title.
        self.assertIn('{0, 0, 0, 0, 3, 0, 1, 1}, true, false};', text)
        self.assertIn('titles[8] = {"p0", "p1", "p2", "p3", "p4", "p5", "", ""};', text)
        # Its page also binds the two-row header's eight bars and sixteen texts.
        self.assertIn('static const bool paged = true;', text)
        self.assertIn('Page::ids[305] = {', text)
        self.assertLess(text.index('"probeplayer_played_127"'), text.index('"probeplayer_header_node_0"'))
        self.assertIn('"probeplayer_header_text_15"', text)
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_player_object(directory, self.machine).read_text(encoding='utf-8')
        self.assertIn('{0, 0, 0, 0, 1, 1, 1, 1}, false, false};', text)
        self.assertIn('static const bool paged = false;', text)
        self.assertIn('Page::ids[281] = {', text)
        self.assertNotIn('_header_', text)

    def test_press_click_keeps_encoder_4_a_click_of_a_one_page_player(self):
        # "press": "click": a player of up to four parameters whose presses are only
        # its own clicks, so encoder 4's click skips stock's toggle of parameter 3
        # (players.cpp, tests/fixtures/player_shift.cpp).
        path = self.root / 'definition.json'
        definition = json.loads(path.read_text(encoding='utf-8'))
        definition['press'] = 'click'
        path.write_text(json.dumps(definition), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, r'presses are only its clicks.*1.16'):
            machine_at(self.root)
        machine = json.loads((self.root / 'machine.json').read_text(encoding='utf-8'))
        machine['sdk'] = '1.16'
        (self.root / 'machine.json').write_text(json.dumps(machine), encoding='utf-8')
        clicking = assign_numbers([machine_at(self.root)])[0][0]
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_player_object(directory, clicking).read_text(encoding='utf-8')
        self.assertIn('{0, 0, 0, 0, 1, 1, 1, 1}, false, true};', text)
        self.assertIn('static const bool paged = false;', text)
        # Not with a second page (other kinds: test_definitions), and no other press value.
        definition['parameters'].append(dict(id='p4', label='P4', default=0,
                                             mapping=dict(kind='linear', minimum=0, maximum=1), display='percent'))
        path.write_text(json.dumps(definition), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, r'"press": "click" is for a note player of up to four parameters'):
            machine_at(self.root)
        from tooling.definitions import validate_definition
        with self.assertRaisesRegex(ValueError, r'"press": "page" needs'):
            validate_definition(dict(definition, press='turn'), 'player')

    def test_paged_player_artwork_gains_the_two_row_header(self):
        nodes = ''.join(f'<circle id="probeplayer_dot_{i}" transform="matrix(1 0 0 1 20 80)" '
                        'r="4" fill="#000000" stroke="#000000" stroke-width="1"/>' for i in range(24))
        nodes += ('<text id="probeplayer_text" transform="matrix(1 0 0 1 240 215)" '
                  'fill="#FFFFFF" font-family="UniversNextPro-Light" font-size="22">chords</text>')
        fragment = '<g id="probeplayer_panel">' + nodes + '</g>'
        (self.root / 'panel.svg.fragment').write_text(fragment, encoding='utf-8')
        self.assertEqual(native.page_artwork(self.machine), fragment.encode('utf-8'))
        paged = replace(self.machine, definition=dict(self.machine.definition, press='page'))
        artwork = native.page_artwork(paged).decode('utf-8')
        # First in the root group: one root still, and the player's own nodes paint over it.
        self.assertTrue(artwork.startswith('<g id="probeplayer_panel">\n<!-- SDK two-page header'))
        self.assertTrue(artwork.endswith('</text>' + nodes + '</g>'))
        for name in native.player_header_ids('probeplayer_header'):
            self.assertEqual(artwork.count(f'id="{name}"'), 1)
        self.assertIn(artwork.encode('utf-8'), native.screen.compose(artwork.encode('utf-8')))

    def test_layout_keeps_player_types_and_shares_page_pool(self):
        m = self.machine
        rules = config()['identities']
        self.assertIn(m.identity['player'], rules['players'])
        requested = dict(player=None, page=None)
        layout = {m.id: m.identity}
        numbered = number_layout(rules, [], layout, [(m.id, 'player', requested), ('other', 'player', requested)])
        self.assertEqual(numbered[m.id], m.identity)
        self.assertNotEqual(numbered['other']['player'], m.identity['player'])
        self.assertNotEqual(numbered['other']['page'], m.identity['page'])
        for invalid in (0, 1, 2, 3, 4, 5, 32):
            with self.assertRaises(ValueError):
                number_layout(rules, [], {m.id: dict(player=invalid, page=90)}, [(m.id, 'player', requested)])

    def test_adapter_is_independent_of_numbers_and_binds_output_keyboard(self):
        m = self.machine
        other = replace(m, identity=dict(player=31, page=255))
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            first = adapters.generate_player_object(a, m).read_text(encoding='utf-8')
            self.assertEqual(first, adapters.generate_player_object(b, other).read_text(encoding='utf-8'))
        self.assertNotIn('@', first)
        self.assertIn('probeplayer_key_127', first)
        self.assertIn('probeplayer_played_127', first)
        self.assertIn('player_output(object, shown)', first)
        source = (PLATFORM / 'players.cpp').read_text(encoding='utf-8')
        self.assertLess(source.index('refresh_keyboard_octaves()'), source.index('if (selected != kMaestroPage)'))
        self.assertEqual(len(vtable()), 16)
        for i, entry in enumerate(vtable()):
            self.assertEqual('0x' not in entry, i in {2, 3, 5, 6, 7, 10, 13})

    def test_catalog_player_is_not_an_instrument(self):
        profile = config()
        catalog = (native.STOCK_CONTENT / 'op1_factory.json').read_bytes()
        before = json.loads(catalog)
        after = json.loads(native.extend_catalog(catalog, profile, [self.machine]))
        self.assertEqual(after['engine_types'], before['engine_types'])
        self.assertEqual(after['fx_types'], before['fx_types'])
        self.assertEqual(after['seq_types'][-1]['id'], self.machine.identity['player'] + 4)
        self.assertEqual(after['seq_types'][-1]['type'], 'probeplayer')

    def test_player_panel_can_omit_the_entire_output_keyboard(self):
        nodes = [f'<circle id="probeplayer_dot_{i}" transform="matrix(1 0 0 1 20 80)" '
                 'r="4" fill="#000000" stroke="#000000" stroke-width="1"/>' for i in range(24)]
        nodes.append('<text id="probeplayer_text" transform="matrix(1 0 0 1 240 215)" '
                     'fill="#FFFFFF" font-family="UniversNextPro-Light" font-size="22">chords</text>')
        native.validate_panel(self.machine, '<g>' + ''.join(nodes) + '</g>')
        nodes.append('<path id="probeplayer_key_0" transform="matrix(1 0 0 1 20 174)" '
                     'd="M0 0 H10 V44 H0 Z" fill="#FFFFFF" stroke="#000000" stroke-width="1"/>')
        with self.assertRaisesRegex(ValueError, 'all 128 keys and activity dots, or none'):
            native.validate_panel(self.machine, '<g>' + ''.join(nodes) + '</g>')

    def test_player_code_and_octave_hook_are_selection_gated(self):
        m = self.machine
        from tooling.firmware import active_services
        profile = config()
        self.assertEqual(active_services(profile, ['player']), {'players', 'octaves'})
        self.assertEqual(active_services(profile, ['synth']), set())
        # Its record (tooling/records.py) names its page and its player; no shared object does.
        record = records.render(m)
        self.assertIn('probeplayer_screen_register, nullptr', record)
        self.assertIn('const opxy::platform::PlayerEntry player_row = {probeplayer_player};', record)
        self.assertEqual([step for _, machine, step in native.plan([m]) if machine], ['player_object', 'record'])

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_adapter_controls_do_not_enable_stock_playback_mode(self):
        adapters.generate_player_object(self.root, replace(self.machine, display='stock'))
        (self.root / 'player.cpp').write_text('''#include <opxy/player.hpp>
namespace probeplayer {
struct State { bool modifier; };
bool press(State& s, const opxy::player::Controls& c, opxy::u32 note,
           opxy::u32 velocity, opxy::player::Plan& plan) {
    plan.count = 0;
    if (note == 65) { s.modifier = true; return true; }
    plan.count = 1;
    plan.tones[0] = {static_cast<opxy::u8>(note + (c.value[3] ? 12 : 0)),
                     static_cast<opxy::u8>(velocity), 0};
    return true;
}
bool release(State& s, opxy::u32 note) {
    if (note != 65) return false;
    s.modifier = false; return true;
}
}
''', encoding='utf-8')
        (self.root / 'stock.hpp').write_text('''#include <opxy/types.hpp>
const void* stock_address(opxy::u32);
namespace opxy { namespace platform {
template<class Function> Function* stock(u32 address) { return reinterpret_cast<Function*>(const_cast<void*>(stock_address(address))); }
}}
''', encoding='utf-8')
        binary = self.root / ('adapter.exe' if os.name == 'nt' else 'adapter')
        flags = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I'+str(self.root),
                        '-I'+str(ROOT/'sdk/include'), '-I'+str(PLATFORM),
                        str(ROOT/'tests/fixtures/player_adapter.cpp'), '-o', str(binary)],
                       check=True, capture_output=True, timeout=120, **flags)
        subprocess.run([str(binary)], check=True, capture_output=True, timeout=30, **flags)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_live_plan_edits_preserve_queues_and_root_ownership(self):
        binary = self.root / ('plans.exe' if os.name == 'nt' else 'plans')
        subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I'+str(ROOT/'sdk/include'),
                        '-I'+str(PLATFORM), str(ROOT/'tests/fixtures/player_plans.cpp'), '-o', str(binary)],
                       check=True, capture_output=True, timeout=120)
        subprocess.run([str(binary)], check=True, capture_output=True, timeout=30)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_player_enum_detents(self):
        binary = self.root / ('controls.exe' if os.name == 'nt' else 'controls')
        subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I'+str(ROOT/'sdk/include'),
                        '-I'+str(PLATFORM), str(ROOT/'tests/fixtures/player_controls.cpp'), '-o', str(binary)],
                       check=True, capture_output=True, timeout=120)
        subprocess.run([str(binary)], check=True, capture_output=True, timeout=30)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_owned_turns_never_enter_stock_hold_mode(self):
        (self.root / 'players_under_test.cpp').write_bytes((PLATFORM / 'players.cpp').read_bytes())
        (self.root / 'keyboard_octaves.hpp').write_text('''#pragma once
#include <opxy/types.hpp>
extern opxy::u32 fake_track;
extern volatile opxy::i32 sdk_track_octaves[8];
namespace opxy { namespace platform {
const u32 kLowestKey=53, kTracks=8;
inline u32 selected_track() { return fake_track; }
inline void refresh_keyboard_octaves() {}
}}
''', encoding='utf-8')
        (self.root / 'registration.hpp').write_text('''#pragma once
#include "players.hpp"
const void* stock_address(opxy::u32);
extern int fake_register;
namespace opxy { namespace platform {
using Address=__UINTPTR_TYPE__;
template<class F> F* stock(u32 a) { return reinterpret_cast<F*>(const_cast<void*>(stock_address(a))); }
struct Record {}; struct Screen {};
inline int sdk_register_screen(const Record*,const Screen*) { return fake_register; }
inline u32 audio_bpm_x10() { return 1200; }
inline bool shift_held() { return false; }
namespace registry { struct StockString { char text[16]; u32 length,capacity; }; }
}}
''', encoding='utf-8')
        binary = self.root / ('turn.exe' if os.name == 'nt' else 'turn')
        subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                        '-I'+str(self.root), '-I'+str(ROOT/'sdk/include'), '-I'+str(PLATFORM),
                        str(ROOT/'tests/fixtures/player_turn.cpp'), '-o', str(binary)],
                       check=True, capture_output=True, timeout=120)
        subprocess.run([str(binary)], check=True, capture_output=True, timeout=30)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_optional_update_and_reset_callbacks(self):
        adapters.generate_player_object(self.root, replace(self.machine, display='stock'))
        (self.root / 'player.cpp').write_text('''#include <opxy/player.hpp>
namespace probeplayer {
struct State { unsigned pending, option; };
bool press(State& s, const opxy::player::Controls&, opxy::u32 note,
           opxy::u32, opxy::player::Plan& plan) {
    plan.count=0; if(note==60) s.pending=2; return true;
}
bool release(State& s, opxy::u32) { s.pending=1; return false; }
bool update(State& s, const opxy::player::Controls&, opxy::player::Update& u) {
    if(!s.pending) return false;
    --s.pending; u.root=60; u.mode=opxy::player::Add; u.plan.count=0; return true;
}
void reset(State& s) { s.pending=0; }
}
''', encoding='utf-8')
        (self.root / 'stock.hpp').write_text('''#include <opxy/types.hpp>
const void* stock_address(opxy::u32);
namespace opxy { namespace platform {
template<class F> F* stock(u32 address) { return reinterpret_cast<F*>(const_cast<void*>(stock_address(address))); }
}}
''', encoding='utf-8')
        binary = self.root / ('updates.exe' if os.name == 'nt' else 'updates')
        subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I'+str(self.root),
                        '-I'+str(ROOT/'sdk/include'), '-I'+str(PLATFORM),
                        str(ROOT/'tests/fixtures/player_updates.cpp'), '-o', str(binary)],
                       check=True, capture_output=True, timeout=120)
        subprocess.run([str(binary)], check=True, capture_output=True, timeout=30)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_activity_and_keyboard_range(self):
        binary = self.root / ('activity.exe' if os.name == 'nt' else 'activity')
        flags = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I'+str(ROOT/'sdk/include'),
                        '-I'+str(PLATFORM), str(ROOT/'tests/fixtures/player_activity.cpp'), '-o', str(binary)],
                       check=True, capture_output=True, timeout=120, **flags)
        subprocess.run([str(binary)], check=True, capture_output=True, timeout=30, **flags)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_real_renderer_draws_either_header_and_fits_output_notes(self):
        shutil.copy(PLATFORM / 'screen_player.hpp', self.root)
        shutil.copy(ROOT / 'tests/fixtures/screen_player_host.hpp', self.root / 'screen.hpp')
        binary = self.root / ('screen.exe' if os.name == 'nt' else 'screen')
        flags = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I'+str(self.root),
                        '-I'+str(ROOT/'sdk/include'), '-I'+str(PLATFORM),
                        str(ROOT/'tests/fixtures/screen_player.cpp'), '-o', str(binary)],
                       check=True, capture_output=True, timeout=120, **flags)
        subprocess.run([str(binary)], check=True, capture_output=True, timeout=30, **flags)


# A player's shift row (SDK 1.16).
PROBE_SHIFT = [dict(id='size', label='Size', default=0.5, display='enum',
                    mapping=dict(kind='enum', labels=['S', 'M', 'L']))]


class PlayerShiftRow(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'probeplayer'
        self.root.mkdir()

    def machine(self, sdk='1.16', shift=None, parameters=4):
        (self.root / 'machine.json').write_text(json.dumps(dict(schema=1, sdk=sdk, kind='player',
                                                               description='Player page fixture.')))
        definition = dict(schema=1, id='probeplayer', name='Player', presets={},
                          resources={},
                          parameters=[dict(id='p'+str(i), label='P'+str(i), default=0,
                                           mapping=dict(kind='linear', minimum=0, maximum=1), display='percent')
                                      for i in range(parameters)])
        if parameters > 4:
            definition['press'] = 'page'
        if shift is not None:
            definition['shift'] = shift
        (self.root / 'definition.json').write_text(json.dumps(definition))
        return assign_numbers([machine_at(self.root)])[0][0]

    def test_versions_and_limits(self):
        with self.assertRaisesRegex(ValueError, "player's shift row.*1.16"):
            self.machine(sdk='1.15', shift=PROBE_SHIFT)
        wide = [dict(PROBE_SHIFT[0], mapping=dict(kind='enum', labels=[str(i) for i in range(256)]))]
        with self.assertRaisesRegex(ValueError, 'at most 255 choices'):
            self.machine(shift=wide)
        with self.assertRaisesRegex(ValueError, 'one to four'):
            self.machine(shift=[dict(PROBE_SHIFT[0], id='s%d' % i) for i in range(5)])
        # Four controls of 255 choices each take no packing limit.
        self.machine(shift=[dict(PROBE_SHIFT[0], id='s%d' % i,
                                 mapping=dict(kind='enum', labels=[str(n) for n in range(255)]))
                            for i in range(4)])
        # As on a synth, a paged player's row has an entry under each page's encoders: up to eight.
        self.machine(parameters=6, shift=[dict(PROBE_SHIFT[0], id='s%d' % i) for i in range(8)])
        with self.assertRaisesRegex(ValueError, 'one to eight'):
            self.machine(parameters=6, shift=[dict(PROBE_SHIFT[0], id='s%d' % i) for i in range(9)])

    def test_a_paged_players_row_is_generated_in_entry_order(self):
        m = self.machine(parameters=5, shift=[dict(PROBE_SHIFT[0], id='s%d' % i, default=0) for i in range(6)])
        with tempfile.TemporaryDirectory() as folder:
            generated = adapters.generate_player_object(folder, m).read_text(encoding='utf-8')
        centre = 0x7fffffff // 3 // 2
        self.assertIn('static const opxy::platform::PlayerShift player_shift = {6, {3, 3, 3, 3, 3, 3, 0, 0}, '
                      '{%s, 0, 0}};' % ', '.join([str(centre)] * 6), generated)
        self.assertIn('{0, 0, 0, 0, 0, 1, 1, 1}, true, false, &player_shift};', generated)

    def test_exactly_one_click_function_the_hook_calls(self):
        # The encoder 4 click hook calls sdk_player_click from assembly and reads R0
        # as "skip stock's toggle". Exactly one C++ function may own that name, and it
        # must return a u32 and take nothing; the press dispatch is another name.
        declarations = []
        for path in sorted((ROOT / 'sdk').rglob('*')):
            if path.is_file() and path.suffix in ('.cpp', '.hpp', '.in', '.h'):
                for line in path.read_text(encoding='utf-8').splitlines():
                    if '"sdk_player_click"' in line:
                        declarations.append((path.name, line.strip()))
        self.assertEqual(declarations,
                         [('players.cpp', 'opxy::u32 sdk_player_click() __asm__("sdk_player_click");')])
        source = (PLATFORM / 'players.cpp').read_text(encoding='utf-8')
        self.assertEqual(re.findall(r'^\S.*\bsdk_player_click\(.*\)\s*\{', source, re.M),
                         ['u32 sdk_player_click() {'])
        hook = (PLATFORM / 'player_hooks.s').read_text(encoding='utf-8')
        self.assertRegex(hook, r'CALL sdk_player_click;\s*\n\s*CC = R0 == 0;')
        # One press dispatch: the press handler calls the players service once, on the
        # player controller only, with the encoder.
        press = (PLATFORM / 'synth_controls.cpp').read_text(encoding='utf-8')
        self.assertEqual(re.findall(r'.*\bsdk_player_press\(.*', press),
                         ['        sdk_player_press(encoder);'])
        self.assertIn('} else if (controller == kPlayerController && sdk_player_press) {\n'
                      '        sdk_player_press(encoder);', press)

    def test_player_without_shift_generates_as_before(self):
        # The source is the template's expansion with the SDK 1.16 placeholders left out.
        m = self.machine(sdk='1.8')
        template = (PLATFORM / 'player_object.cpp.in').read_text(encoding='utf-8')
        before = template.replace('@SHIFT_TABLE@', '').replace('@SHIFT@', '')
        with tempfile.TemporaryDirectory() as folder:
            generated = adapters.generate_player_object(folder, m).read_text(encoding='utf-8')
        values = dict(PREFIX=m.prefix, NAMESPACE='probeplayer', NAME=m.native_name,
                      STEPS='0, 0, 0, 0, 1, 1, 1, 1', PAGED='false', CLICKS='false',
                      PAGE=f'{m.prefix}_page_id', PAGE_READY=f'{m.prefix}_screen_ready')
        for key, value in values.items():
            before = before.replace('@' + key + '@', value)
        self.assertEqual(generated[:generated.index('extern const char probeplayer_page_id[]')],
                         before[:before.index('@PAGE_DECLARATIONS@')])
        tail = before[before.index('@PAGE_DECLARATIONS@') + len('@PAGE_DECLARATIONS@'):before.index('@PAGE_UNIT@')]
        self.assertIn(tail, generated)
        self.assertIn('opxy::screen::PlayerPage<Page>', generated)
        self.assertNotIn('player_shift', generated)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_clock_ticks_keeps_another_resolution_on_steps(self):
        # clock_ticks: after n steps a clock of t ticks per quarter has ticked
        # ceil(n * t / 240) times, so it never drifts, and never more than
        # ceil(t / 240) times on one step; a 96-tick clock ticks on steps 0, 2, 5, 7, 10.
        (self.root / 'clock.cpp').write_text(r'''
#include <opxy/player.hpp>
using opxy::u32;
int main() {
    const u32 rates[] = {24, 96, 240, 480, 1000, 1920};
    for (u32 t : rates) {
        opxy::u16 phase = 0; u32 total = 0;
        const u32 most = (t + opxy::player::steps_per_quarter - 1) / opxy::player::steps_per_quarter;
        for (u32 n = 1; n <= 1000; ++n) {
            const u32 ticks = opxy::player::clock_ticks(phase, t);
            if (ticks > most) return 1;
            total += ticks;
            if (total != (n * t + opxy::player::steps_per_quarter - 1) / opxy::player::steps_per_quarter) return 2;
        }
    }
    opxy::u16 phase = 0; u32 seen = 0;
    for (u32 step = 0; step < 11; ++step)
        if (opxy::player::clock_ticks(phase, 96)) seen |= 1u << step;
    return seen == ((1u << 0) | (1u << 2) | (1u << 5) | (1u << 7) | (1u << 10)) ? 0 : 3;
}
''', encoding='utf-8')
        binary = self.root / ('clock.exe' if os.name == 'nt' else 'clock')
        flags = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I'+str(ROOT/'sdk/include'),
                        str(self.root / 'clock.cpp'), '-o', str(binary)], check=True, capture_output=True, timeout=120, **flags)
        self.assertEqual(subprocess.run([str(binary)], timeout=30, **flags).returncode, 0)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_shift_layer_turns_flag_and_clicks(self):
        (self.root / 'players_under_test.cpp').write_bytes((PLATFORM / 'players.cpp').read_bytes())
        (self.root / 'keyboard_octaves.hpp').write_text('''#pragma once
#include <opxy/types.hpp>
extern opxy::u32 fake_track;
extern volatile opxy::i32 sdk_track_octaves[8];
namespace opxy { namespace platform {
const u32 kLowestKey=53, kTracks=8;
inline u32 selected_track() { return fake_track; }
inline void refresh_keyboard_octaves() {}
}}
''', encoding='utf-8')
        (self.root / 'registration.hpp').write_text('''#pragma once
#include "players.hpp"
const void* stock_address(opxy::u32);
extern bool fake_shift;
extern opxy::u32 fake_bpm;
extern int fake_register;
namespace opxy { namespace platform {
using Address=__UINTPTR_TYPE__;
template<class F> F* stock(u32 a) { return reinterpret_cast<F*>(const_cast<void*>(stock_address(a))); }
struct Record {}; struct Screen {};
inline int sdk_register_screen(const Record*,const Screen*) { return fake_register; }
inline u32 audio_bpm_x10() { return fake_bpm; }
inline bool shift_held() { return fake_shift; }
namespace registry { struct StockString { char text[16]; u32 length,capacity; }; }
}}
''', encoding='utf-8')
        binary = self.root / ('shift.exe' if os.name == 'nt' else 'shift')
        flags = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                        '-I'+str(self.root), '-I'+str(ROOT/'sdk/include'), '-I'+str(PLATFORM),
                        str(ROOT/'tests/fixtures/player_shift.cpp'), '-o', str(binary)],
                       check=True, capture_output=True, timeout=120, **flags)
        result = subprocess.run([str(binary)], capture_output=True, timeout=30, **flags)
        if result.returncode == 77:
            self.skipTest(result.stdout.decode(errors='replace').strip())
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))

# stock.hpp's track engine getter, which the host doubles of registration.hpp repeat word for
# word (PlayerInstrument checks it).
TRACK_ENGINE = 'inline u32 track_engine(u32 track) { return stock<u32(u32)>(0x8d58781c)(track); }'


class PlayerInstrument(unittest.TestCase):
    """Controls.instrument (resources.trackInstrument, SDK 1.25): the kind of instrument a player's
    track holds, for a player declaring it. Its own units compile with the define and its build links
    the track_instrument service; every other unit, and every build without such a player, stays as
    it was."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()

    def player(self, sdk='1.25', declared=1, name='probepads', source=None):
        """The probepads fixture, declaring SDK `sdk` and trackInstrument `declared` (None: no
        such key), its player.cpp replaced by `source` when given."""
        from machine_fixtures import FOLDER
        folder = self.root / name
        shutil.copytree(FOLDER / 'probepads', folder)
        record = json.loads((folder / 'machine.json').read_text(encoding='utf-8'))
        record['sdk'] = sdk
        (folder / 'machine.json').write_text(json.dumps(record), encoding='utf-8')
        definition = json.loads((folder / 'definition.json').read_text(encoding='utf-8'))
        definition['id'] = name
        definition['resources'].pop('trackInstrument', None)
        if declared is not None:
            definition['resources']['trackInstrument'] = declared
        (folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
        text = (folder / 'player.cpp').read_text(encoding='utf-8') if source is None else source
        (folder / 'player.cpp').write_text(text.replace('namespace probepads', 'namespace ' + name), encoding='utf-8')
        return machine_at(folder)

    @staticmethod
    def unread():
        """The fixture player's source, reading nothing of its track's instrument."""
        return ((ROOT / 'tests/fixtures/machines/probepads/player.cpp').read_text(encoding='utf-8')
                .replace('.instrument', '.lowest_key').replace('opxy::player::DrumInstrument', '1'))

    def test_only_a_player_declaring_it_reads_the_instrument(self):
        from tooling.adapters import player_instrument
        from tooling.definitions import SDK_1_25
        from tooling.machines import SDK_VERSION
        from machine_fixtures import fixture
        self.assertEqual(SDK_1_25, (1, 25))
        self.assertGreaterEqual(SDK_VERSION, SDK_1_25)
        pads = fixture('probepads')
        self.assertEqual(pads.definition['resources'], {'trackInstrument': 1})
        self.assertTrue(player_instrument(pads))
        # A 1.25 player that does not declare it, one declaring 0, and every other kind do not.
        from off_path_code import player_folder
        player_folder(self.root / 'probeplayer')
        plain = machine_at(self.root / 'probeplayer')
        self.assertFalse(player_instrument(plain))
        self.assertFalse(player_instrument(self.player(declared=0, name='padszero', source=self.unread())))
        for m in (fixture('probesynth'), fixture('probefx'), fixture('probekit23'), fixture('probekit25')):
            self.assertFalse(player_instrument(m))
        # No SDK unit compiles with the define: only the declaring player's own units do.
        every = [pads, plain, fixture('probesynth'), fixture('probekit23'), fixture('probekit25')]
        self.assertNotIn(native.PLAYER_INSTRUMENT_DEFINE, native.sdk_unit_defines(config(), every))
        self.assertEqual(native.own_unit_defines(pads), (native.PLAYER_INSTRUMENT_DEFINE,))
        for m in every[1:]:
            self.assertEqual(native.own_unit_defines(m), ())
        source = (ROOT / 'tooling/native.py').read_text(encoding='utf-8')
        self.assertIn("unit_defines = defines if m is None else own_unit_defines(m)\n", source)
        # The build links the track instrument's units only with a declaring player.
        services = native.active_services(config(), native.service_users([pads, plain]))
        self.assertIn('track_instrument', services)
        self.assertNotIn('track_instrument', native.active_services(config(), native.service_users([plain])))
        names = [name for name, _m, _step in native.plan([pads, plain], config())]
        self.assertIn('sdk/track_instrument.o', names)
        self.assertIn('sdk/track_instrument_hooks.o', names)
        self.assertEqual([n for n in native.plan([plain], config()) if 'track_instrument' in n[0]], [])
        # Only the header member and the declaring player's controls depend on the define.
        self.assertEqual((ROOT / 'sdk/include/opxy/player.hpp').read_text(encoding='utf-8').count(
            '#if OPXY_PLAYER_INSTRUMENT'), 1)
        self.assertEqual((PLATFORM / 'players.hpp').read_text(encoding='utf-8').count('#if OPXY_PLAYER_INSTRUMENT'), 1)
        self.assertNotIn('OPXY_PLAYER_INSTRUMENT', (PLATFORM / 'players.cpp').read_text(encoding='utf-8'))
        # A declaring player's generated adapter is the one any other player gets: only the define differs.
        undeclared = self.player(declared=None, name='padsplain', source=self.unread())
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            new_source = adapters.generate_player_object(a, pads).read_text(encoding='utf-8')
            old_source = adapters.generate_player_object(b, undeclared).read_text(encoding='utf-8')
        self.assertEqual(new_source, old_source.replace('padsplain', 'probepads'))

    def test_the_declaration_is_refused_where_it_does_not_apply(self):
        from tooling.definitions import validate_definition
        from machine_fixtures import fixture
        # 0 or 1, a player's alone, from SDK 1.25.
        for value in (2, -1, True, '1'):
            with self.assertRaisesRegex(Exception, 'trackInstrument must be 0 or 1|Numeric resource'):
                self.player(declared=value, name='padsbad')
            shutil.rmtree(self.root / 'padsbad')
        with self.assertRaisesRegex(Exception, r"track instrument \(trackInstrument\)"):
            self.player(sdk='1.24', name='padsold')
        for m in (fixture('probesynth'), fixture('probekit25')):
            definition = json.loads(json.dumps(m.definition))
            definition['resources']['trackInstrument'] = 1
            with self.assertRaisesRegex(Exception, 'resources'):
                validate_definition(definition, m.kind)
        # Reading Controls.instrument, or naming its kinds, without declaring it is refused.
        source = ((ROOT / 'tests/fixtures/machines/probepads') / 'player.cpp').read_text(encoding='utf-8')
        for declared in (None, 0):
            with self.assertRaisesRegex(Exception, 'declares resources.trackInstrument'):
                self.player(declared=declared, name='padsundeclared')
            shutil.rmtree(self.root / 'padsundeclared')
        for text in ('u32 k(const Controls& c) { return c.instrument; }', 'u32 k(const Controls* c) { return c->instrument; }',
                     'const u32 k = opxy::player::DrumInstrument;', 'const u32 k = OtherInstrument;'):
            with self.assertRaisesRegex(Exception, 'declares resources.trackInstrument'):
                self.player(declared=None, name='padsread', source=source.replace('namespace probepads {',
                                                                                  'namespace probepads {\n' + text, 1))
            shutil.rmtree(self.root / 'padsread')
        # Its own tests and tools may name it.
        m = self.player(declared=None, name='padstests', source=self.unread())
        (Path(m.root) / 'tests').mkdir()
        (Path(m.root) / 'tests/check.cpp').write_text('int k(int instrument) { return DrumInstrument; }\n', encoding='utf-8')
        machine_at(m.root)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_controls_keeps_its_size_without_the_define_and_a_player_needs_it_to_read_the_kind(self):
        check = self.root / 'size.cpp'
        check.write_text('#include <opxy/player.hpp>\n'
                         'static_assert(sizeof(opxy::player::Controls) == SIZE, "Controls size");\n', encoding='utf-8')
        flags = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        for defines, size in (((), 112), (('-DOPXY_PLAYER_INSTRUMENT=1',), 116)):
            result = subprocess.run([HOST_CXX, '-std=c++11', '-fsyntax-only', '-I' + str(ROOT / 'sdk/include'),
                                     *defines, '-DSIZE=%d' % size, str(check)], capture_output=True, timeout=60, **flags)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        # The fixture player reads Controls.instrument: it compiles only with the define.
        source = (ROOT / 'tests/fixtures/machines/probepads') / 'player.cpp'
        for defines, ok in (((), False), (('-DOPXY_PLAYER_INSTRUMENT=1',), True)):
            result = subprocess.run([HOST_CXX, '-std=c++11', '-fsyntax-only', '-I' + str(ROOT / 'sdk/include'),
                                     *defines, str(source)], capture_output=True, timeout=60, **flags)
            self.assertEqual(result.returncode == 0, ok, result.stderr.decode(errors='replace'))

    def test_the_compositor_enters_the_track_instrument_only_with_its_service(self):
        # The loop advance's hook enters sdk_instrument_advance in a build with the service,
        # sdk_screen_advance in any other; the two handlers differ in the C++ they call alone.
        from tooling.firmware import hook_symbol
        patch = next(p for p in config()['patches'] if p.get('symbol') == 'sdk_screen_advance')
        self.assertEqual(patch.get('symbols'), {'track_instrument': 'sdk_instrument_advance'})
        self.assertNotIn('service', patch)
        self.assertEqual(hook_symbol(patch, {'players', 'octaves'}), 'sdk_screen_advance')
        self.assertEqual(hook_symbol(patch, {'players', 'octaves', 'track_instrument'}), 'sdk_instrument_advance')
        self.assertEqual(config()['services']['track_instrument'], ['trackInstrument'])

        def body(text, name):
            start = text.index(name + ':\n')
            return text[start:text.index('.size ' + name, start)].replace(name, 'HANDLER').splitlines()
        screen = body((PLATFORM / 'screen_hooks.s').read_text(encoding='utf-8'), 'sdk_screen_advance')
        instrument = body((PLATFORM / 'track_instrument_hooks.s').read_text(encoding='utf-8'), 'sdk_instrument_advance')
        self.assertEqual([line for line in screen if 'CALL sdk_screen_next' not in line],
                         [line for line in instrument if 'CALL sdk_instrument_next' not in line])
        self.assertEqual(sum('CALL sdk_screen_next' in line for line in screen), 1)
        self.assertEqual(sum('CALL sdk_instrument_next' in line for line in instrument), 1)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_instrument_kind_is_published_per_track(self):
        # The GUI publishes each instrument track's kind at a frame's first slot, from stock's
        # engine getter and its drum predicate; a declaring player's callbacks read the
        # published byte (tests/fixtures/player_instrument.cpp), beside players.cpp compiled
        # as every build compiles it.
        stock = (PLATFORM / 'stock.hpp').read_text(encoding='utf-8')
        self.assertEqual(stock.count(TRACK_ENGINE), 1)
        (self.root / 'players_under_test.cpp').write_bytes((PLATFORM / 'players.cpp').read_bytes())
        (self.root / 'track_instrument_under_test.cpp').write_bytes((PLATFORM / 'track_instrument.cpp').read_bytes())
        (self.root / 'keyboard_octaves.hpp').write_text('''#pragma once
#include <opxy/types.hpp>
extern opxy::u32 fake_track;
extern volatile opxy::i32 sdk_track_octaves[8];
namespace opxy { namespace platform {
const u32 kLowestKey=53, kTracks=8;
inline u32 selected_track() { return fake_track; }
inline void refresh_keyboard_octaves() {}
}}
''', encoding='utf-8')
        (self.root / 'registration.hpp').write_text('''#pragma once
#include "players.hpp"
const void* stock_address(opxy::u32);
extern bool fake_shift;
extern int fake_register;
namespace opxy { namespace platform {
using Address=__UINTPTR_TYPE__;
template<class F> F* stock(u32 a) { return reinterpret_cast<F*>(const_cast<void*>(stock_address(a))); }
''' + TRACK_ENGINE + '''
struct Record {}; struct Screen {};
inline int sdk_register_screen(const Record*,const Screen*) { return fake_register; }
inline u32 audio_bpm_x10() { return 1200; }
inline bool shift_held() { return fake_shift; }
namespace registry { struct StockString { char text[16]; u32 length,capacity; }; }
}}
''', encoding='utf-8')
        # The track instrument's unit sees this stock.hpp, which its copy here finds first.
        (self.root / 'stock.hpp').write_text('''#pragma once
#include <opxy/types.hpp>
const void* stock_address(opxy::u32);
namespace opxy { namespace platform {
const u32 kTracks=8;
template<class F> F* stock(u32 a) { return reinterpret_cast<F*>(const_cast<void*>(stock_address(a))); }
''' + TRACK_ENGINE + '''
}}
''', encoding='utf-8')
        flags = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        common = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(self.root), '-I' + str(ROOT / 'sdk/include'),
                  '-I' + str(PLATFORM)]
        objects = []
        for source, defines in ((self.root / 'players_under_test.cpp', ()),
                                (self.root / 'track_instrument_under_test.cpp', ()),
                                (ROOT / 'tests/fixtures/player_instrument.cpp', (native.PLAYER_INSTRUMENT_DEFINE,))):
            obj = self.root / (source.stem + '.o')
            built = subprocess.run([*common, *defines, '-c', str(source), '-o', str(obj)], capture_output=True,
                                   timeout=120, **flags)
            self.assertEqual(built.returncode, 0, built.stderr.decode(errors='replace'))
            objects.append(str(obj))
        binary = self.root / ('instrument.exe' if os.name == 'nt' else 'instrument')
        subprocess.run([HOST_CXX, *objects, '-o', str(binary)], check=True, capture_output=True, timeout=120, **flags)
        result = subprocess.run([str(binary)], capture_output=True, timeout=30, **flags)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))


def player_trace(m, folder, defines=()):
    """Everything player `m` decides over tests/fixtures/player_parity.cpp's seeded sequence, its own
    source compiled for the host with `defines`."""
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    wrapper = folder / 'trace.cpp'
    wrapper.write_text(f'#include "{m.source("player.cpp")}"' + chr(10) + f'namespace subject = {m.definition["id"]};' + chr(10) +
                       f'#include "{(ROOT / "tests/fixtures/player_parity.cpp").as_posix()}"' + chr(10), encoding='utf-8')
    binary = folder / ('trace.exe' if os.name == 'nt' else 'trace')
    flags = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
    subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-I' + str(ROOT / 'sdk/include'), '-I' + m.source(),
                    *('-I' + m.source(extra) for extra in m.includes), *defines, str(wrapper), '-o', str(binary)],
                   check=True, capture_output=True, timeout=300, **flags)
    return subprocess.run([str(binary)], check=True, capture_output=True, timeout=120, **flags).stdout


class PlayerInstrumentParity(unittest.TestCase):
    """Controls.instrument changes nothing for a player that does not read it: compiled with
    OPXY_PLAYER_INSTRUMENT or without it, as it is in every build (native.own_unit_defines), it
    decides exactly the same, whatever the instrument."""

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_a_player_not_reading_the_instrument_plays_the_same_with_the_define(self):
        from off_path_code import player_folder
        from machine_fixtures import fixture
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            player_folder(root / 'probeplayer')
            m = machine_at(root / 'probeplayer')
            plain = player_trace(m, root / 'plain')
            self.assertGreater(plain.count(b'press '), 10000)
            self.assertIn(b'-> 1 2:', plain)  # it plays chords
            for kind in (0, 1, 2):
                self.assertEqual(player_trace(m, root / str(kind), (native.PLAYER_INSTRUMENT_DEFINE, f'-DINSTRUMENT={kind}')),
                                 plain, kind)
            # The sequence reaches the kind: the fixture that reads it plays otherwise on a drum track.
            pads = fixture('probepads')
            other = player_trace(pads, root / 'other', (native.PLAYER_INSTRUMENT_DEFINE, '-DINSTRUMENT=0'))
            drum = player_trace(pads, root / 'drum', (native.PLAYER_INSTRUMENT_DEFINE, '-DINSTRUMENT=1'))
            self.assertNotEqual(other, drum)


@unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Stock OS not supplied')
class StockPlayer(unittest.TestCase):
    def test_instrument_kind_stands_on_stock_engine_getter_and_drum_predicate(self):
        # Controls.instrument (SDK 1.25) calls, on the GUI, the engine getter the drum services
        # already call and stock's drum predicate, whose entry the drums service hooks.
        from tooling.formats import sha
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(data), config()['stock_sha256'])
        source = (PLATFORM / 'track_instrument.cpp').read_text(encoding='utf-8')
        self.assertIn('stock<u32(u32)>(kDrumPredicate)(track_engine(track))', source)
        self.assertIn('const u32 kDrumPredicate = 0x8d5b79f0;', source)
        image = ldr(data)
        at = mapped_offset(image, 0x8d58781c, 18)
        # LINK 0xc; CALL 0x8d587708 (the track's object); P1 = R0; R0 = [P1 + 0x18]; UNLINK; RTS
        self.assertEqual(data[at:at+18].hex(), '00e80300ffe374ff083288a101e800001000')
        at = mapped_offset(image, 0x8d5b79f0, 36)
        # CC = R0 < 3 (IU); LINK 0; IF CC JUMP no; CC = R0 == 3; IF CC JUMP yes; R1 = 4;
        # CC = R0 == R1; IF !CC JUMP no; yes: UNLINK; R0 = 1; RTS; NOP; no: UNLINK; R0 = 0; RTS
        self.assertEqual(data[at:at+36].hex(),
                         '980d00e800000b18180c041821600808061001e8000008601000000001e8000000601000')
        # The drums service hooks the predicate's first two instructions, aliasing an SDK kit to 4
        # before repeating them (drum_hooks.s), and returns to the third.
        hook = [p for p in config()['patches'] if p.get('symbol') == 'extensions_drum_classify']
        self.assertEqual([(p['address'], p['before'], p['service']) for p in hook],
                         [('0x8d5b79f0', '980d00e80000', 'drums')])
        asm = (PLATFORM / 'drum_hooks.s').read_text(encoding='utf-8')
        self.assertIn('extensions_drum_classify:\n    DRUM_ALIAS_R0\n    CC = R0 < 3 (IU);\n    LINK 0;\n'
                      '    JUMP.L stock_drum_classify_tail;', asm)

    def test_enum_turn_hook_matches_stock_frame_and_continuations(self):
        from tooling.formats import sha
        from tooling.firmware import decode_branch
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(data), config()['stock_sha256'])
        blocks = ldr(data)

        def read_at(address, size):
            offset = mapped_offset(blocks, address, size)
            return data[offset:offset + size]

        patch = next(p for p in config()['patches'] if p.get('symbol') == 'extensions_player_turn')
        self.assertTrue(patch['call'])
        self.assertEqual(patch['length'], 8)
        site = int(patch['address'], 16)
        self.assertEqual(site, 0x8c084d18)
        # Existing LINK frame, saved registers, index in R5, and outgoing space.
        self.assertEqual(read_at(0x8c084d0c, 12), bytes.fromhex('00e80000e50529302030a66f'))
        self.assertEqual(read_at(site, 4), bytes.fromhex('05301930'))  # R0=R5; R3=R1
        self.assertEqual(decode_branch(site + 4, read_at(site + 4, 4)), (0x8c084afc, True))
        # Handled enums skip stock's mode-transition helper and return through
        # the original frame: SP+=12, restore registers, UNLINK, RTS.
        self.assertEqual(read_at(0x8c084d46, 10), bytes.fromhex('666ca50501e800001000'))

    def test_click_hook_replaces_the_hold_mode_query_of_an_encoder_4_click(self):
        from tooling.formats import sha
        from tooling.firmware import absolute_target, decode_branch
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(data), config()['stock_sha256'])
        blocks = ldr(data)

        def read_at(address, size):
            offset = mapped_offset(blocks, address, size)
            return data[offset:offset + size]

        patch = next(p for p in config()['patches'] if p.get('symbol') == 'extensions_player_click')
        self.assertTrue(patch['call'])
        self.assertEqual(patch['length'], 8)
        site = int(patch['address'], 16)
        self.assertEqual(site, 0x8c084e14)
        # The call before it leaves the track's player in R0 for the query.
        self.assertEqual(decode_branch(site - 4, read_at(site - 4, 4)), (0x8c084bc8, True))
        self.assertEqual(absolute_target(read_at(site, 8)), 0x8d57d478)
        # A paged player returns through the handler's own exit: R0 = 0,
        # restore R7, UNLINK, RTS.
        self.assertEqual(read_at(0x8c084e44, 14), bytes.fromhex('04cc0040f7a0000001e800001000'))

    def test_selecting_a_type_stores_eight_catalog_defaults(self):
        from tooling.formats import sha
        from tooling.firmware import absolute_target
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(data), config()['stock_sha256'])
        blocks = ldr(data)

        def read_at(address, size):
            offset = mapped_offset(blocks, address, size)
            return data[offset:offset + size]

        # The player controller's selection loop: R4 = 8 bounds it, and each
        # pass calls the parameter setter with (track, index, default << 16).
        self.assertEqual(read_at(0x8c0af366, 2), bytes.fromhex('4460'))
        self.assertEqual(absolute_target(read_at(0x8c0af380, 8)), 0x8d586474)
        self.assertEqual(read_at(0x8c0af388, 4), bytes.fromhex('a508ef1f'))

    def test_effective_keyboard_octave_scales_the_stored_value(self):
        from tooling.formats import sha
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(data), config()['stock_sha256'])
        source = (PLATFORM / 'keyboard_octaves.cpp').read_text(encoding='utf-8')
        self.assertIn('stock<i32(u32)>(0x8d58f65c)(track)', source)
        at = mapped_offset(ldr(data), 0x8d58f67c, 10)
        # The stock getter selects a multiplier of one or two by instrument
        # type, then multiplies the stored octave before returning it.
        self.assertEqual(data[at:at+10], struct.pack('<5H', 0x6012, 0x6009, 0x060a, 0x40c8, 0x0010))

    def test_chord_storage_matches_stock_step_and_release(self):
        from tooling.formats import sha
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(data), config()['stock_sha256'])
        records, held, limit = (int(value, 0) for value in re.search(
            r'kRecords = (0x[0-9a-f]+), kHeld = (0x[0-9a-f]+), kMaxHeld = (\d+)',
            (PLATFORM / 'players.cpp').read_text(encoding='utf-8')).groups())
        self.assertEqual(held - records, limit * 72)

        def instruction(address, opcode, immediate):
            at = mapped_offset(ldr(data), address, 4)
            self.assertEqual(data[at:at+4], struct.pack('<HH', opcode, immediate),
                             f'Player chord layout disagrees with stock at {address:#x}')

        # Decode the SDK's layout against both stock consumers, not against
        # another copy of the assumed offsets. OS 1.2.1 moved these fields.
        instruction(0x8d57d8d4, 0xe121, records)  # step: R1 = record-array offset
        instruction(0x8d57d8e0, 0xe428, held // 4)  # step: R0 = [P5 + held]
        instruction(0x8d57d8dc, 0xe125, 72)  # step: chord stride
        instruction(0x8d57d74c, 0xe122, records)  # release: R2 = record-array offset
        instruction(0x8d57d758, 0xe428, held // 4)  # release: R0 = [P5 + held]
        instruction(0x8d57d754, 0xe124, 72)  # release: chord stride

    def test_kept_methods_are_authenticated_stock_maestro(self):
        from tooling.formats import sha
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(data), config()['stock_sha256'])
        at = mapped_offset(ldr(data), 0x8409f6d0, 64)
        words = struct.unpack('<16I', data[at:at+64])
        for i, entry in enumerate(vtable()):
            if i not in {2, 3, 5, 6, 7, 10, 13}:
                self.assertEqual(int(entry.split('(')[1].rstrip(')'), 16), words[i])
        header = (PLATFORM / 'players.hpp').read_text(encoding='utf-8')
        for index, name in ((6, 'Release'), (7, 'Reset'), (10, 'AllOff'), (13, 'Step')):
            address = int(re.search(r'kMaestro'+name+r' = (0x[0-9a-f]+)', header)[1], 16)
            self.assertEqual(address, words[index])
