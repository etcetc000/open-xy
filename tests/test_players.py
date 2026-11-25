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
    """Controls.instrument (SDK 1.25): the kind of instrument a player's track holds, for a build
    with a player declaring 1.25; every other build keeps Controls and players.cpp as they were."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()

    def player(self, sdk):
        from machine_fixtures import FOLDER
        folder = self.root / 'probepads'
        shutil.copytree(FOLDER / 'probepads', folder)
        record = json.loads((folder / 'machine.json').read_text(encoding='utf-8'))
        record['sdk'] = sdk
        (folder / 'machine.json').write_text(json.dumps(record), encoding='utf-8')
        return machine_at(folder)

    def test_only_a_build_with_a_1_25_player_defines_it(self):
        from tooling.adapters import player_instrument
        from tooling.definitions import SDK_1_25
        from tooling.machines import SDK_VERSION
        from machine_fixtures import fixture
        self.assertEqual(SDK_1_25, (1, 25))
        self.assertGreaterEqual(SDK_VERSION, SDK_1_25)
        new, older = fixture('probepads'), self.player('1.24')
        self.assertEqual((new.sdk, player_instrument(new), player_instrument(older)), ((1, 25), True, False))
        self.assertEqual(native.sdk_defines([new]), (native.PLAYER_INSTRUMENT_DEFINE,))
        self.assertEqual(native.sdk_defines([older]), ())
        # Not a player: kits, synths and effects never ask for it, whatever they declare.
        self.assertEqual(native.sdk_defines([fixture('probesynth'), fixture('probefx'), fixture('probekit23')]), ())
        # The SDK units and the players of such a build see it; no other machine's units do.
        source = (ROOT / 'tooling/native.py').read_text(encoding='utf-8')
        self.assertIn("player_defines = tuple(d for d in defines if d == PLAYER_INSTRUMENT_DEFINE)", source)
        self.assertIn("unit_defines = defines if m is None else ()\n"
                      "        if m is not None and m.kind == 'player':\n"
                      "            unit_defines = player_defines\n", source)
        # Only the header member and the bridge's publication depend on it.
        self.assertEqual((ROOT / 'sdk/include/opxy/player.hpp').read_text(encoding='utf-8').count(
            '#if OPXY_PLAYER_INSTRUMENT'), 1)
        self.assertEqual((PLATFORM / 'players.cpp').read_text(encoding='utf-8').count('#if OPXY_PLAYER_INSTRUMENT'), 3)
        # A 1.25 player's generated adapter is the one an older player gets: only the define differs.
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            new_source = adapters.generate_player_object(a, new).read_text(encoding='utf-8')
            old_source = adapters.generate_player_object(b, older).read_text(encoding='utf-8')
        self.assertEqual(new_source, old_source)

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
        # The fixture player reads Controls.instrument: it compiles only where the build defines it.
        from machine_fixtures import FOLDER
        source = FOLDER / 'probepads/player.cpp'
        for defines, ok in (((), False), (('-DOPXY_PLAYER_INSTRUMENT=1',), True)):
            result = subprocess.run([HOST_CXX, '-std=c++11', '-fsyntax-only', '-I' + str(ROOT / 'sdk/include'),
                                     *defines, str(source)], capture_output=True, timeout=60, **flags)
            self.assertEqual(result.returncode == 0, ok, result.stderr.decode(errors='replace'))

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_instrument_kind_is_published_per_track(self):
        # The GUI publishes each instrument track's kind with Shift, from stock's engine getter
        # and its drum predicate; note callbacks read the published byte
        # (tests/fixtures/player_instrument.cpp).
        stock = (PLATFORM / 'stock.hpp').read_text(encoding='utf-8')
        self.assertEqual(stock.count(TRACK_ENGINE), 1)
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
        binary = self.root / ('instrument.exe' if os.name == 'nt' else 'instrument')
        flags = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-DOPXY_PLAYER_INSTRUMENT=1',
                        '-I'+str(self.root), '-I'+str(ROOT/'sdk/include'), '-I'+str(PLATFORM),
                        str(ROOT/'tests/fixtures/player_instrument.cpp'), '-o', str(binary)],
                       check=True, capture_output=True, timeout=120, **flags)
        result = subprocess.run([str(binary)], capture_output=True, timeout=30, **flags)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))


@unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Stock OS not supplied')
class StockPlayer(unittest.TestCase):
    def test_instrument_kind_stands_on_stock_engine_getter_and_drum_predicate(self):
        # Controls.instrument (SDK 1.25) calls, on the GUI, the engine getter the drum services
        # already call and stock's drum predicate, whose entry the drums service hooks.
        from tooling.formats import sha
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(data), config()['stock_sha256'])
        source = (PLATFORM / 'players.cpp').read_text(encoding='utf-8')
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
