"""Extended drawing on a drum kit's screen and a note player's own page (SDK 1.28): the
declaration, the generated renderer and player unit, a whole-screen kit's display file
in the SDK and the builder, the kit renderer over host doubles, and the previews.
The fixtures are tests/fixtures/machines/probekitcanvas and probeplayer."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tooling import adapters, native, preview, screen
from tooling.definitions import extended_display
from tooling.machines import machine_at
from machine_fixtures import FOLDER, fixture
from test_sample_services import HOST_CXX
from test_stock_content import STOCK_MENU

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}


def copy_fixture(name, folder):
    target = Path(folder) / name
    shutil.copytree(FOLDER / name, target)
    return target


def set_sdk(root, version):
    record = json.loads((root / 'machine.json').read_text(encoding='utf-8'))
    record['sdk'] = version
    (root / 'machine.json').write_text(json.dumps(record), encoding='utf-8', newline='\n')


class Declaration(unittest.TestCase):
    def test_kits_and_players_declare_it_from_sdk_1_28(self):
        for name in ('probekitcanvas', 'probeplayer'):
            m = fixture(name)
            self.assertTrue(extended_display(m.definition['resources']['display']))
            self.assertEqual(m.display, 'drawing')
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                root = copy_fixture(name, temp)
                set_sdk(root, '1.27')
                with self.assertRaisesRegex(ValueError, r"drum kit's screen or a note player's page needs \"sdk\": \"1\.28\""):
                    machine_at(root)

    def test_the_last_control_touched_stays_a_synths(self):
        with tempfile.TemporaryDirectory() as temp:
            root = copy_fixture('probekitcanvas', temp)
            display = root / 'display.cpp'
            display.write_text('#include <opxy/control_interaction.hpp>\n' + display.read_text(encoding='utf-8'),
                               encoding='utf-8', newline='\n')
            with self.assertRaisesRegex(ValueError, "Only a synth's own screen reads opxy/control_interaction.hpp"):
                machine_at(root)


class KitScreen(unittest.TestCase):
    def test_an_extended_kit_takes_the_extended_renderer_with_its_kit_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            m = fixture('probekitcanvas')
            adapters.generate_display(Path(temp), machine=m)
            self.assertIn('const bool full_canvas = true;', (Path(temp) / 'display_binding.hpp').read_text(encoding='utf-8'))
            text = adapters.generate_screen(Path(temp), m).read_text(encoding='utf-8')
            self.assertIn('opxy::screen::ExtendedDrawing<Machine>', text)
            for flag in ('kit', 'played_pad', 'selected_key', 'kit_controls'):
                self.assertIn(f'static const bool {flag} = true;', text)
            # A synth's extended screen: none of a kit's inputs.
            text = adapters.generate_screen(Path(temp), fixture('probecanvas')).read_text(encoding='utf-8')
            for flag in ('kit', 'played_pad', 'selected_key', 'kit_controls'):
                self.assertIn(f'static const bool {flag} = false;', text)

    def test_a_whole_screen_kits_file_carries_the_header_after_its_drawing(self):
        m = fixture('probekitcanvas')
        data = native.prepare_assets(STOCK_MENU.read_bytes(), native.config(), [m])[f'content/display/{m.native_name}.svg']
        fragment = native.page_artwork(m)
        self.assertTrue(data.startswith(screen.CANVAS[:-6] + fragment))
        header = screen.kit_header(screen.template())
        self.assertTrue(data.endswith(header + b'</svg>'))
        # The bar as eight paths the renderer can hide; the values and titles the stock binder finds.
        for i in range(8):
            self.assertIn(f'<path id="bar_{i}"'.encode(), header)
        self.assertIn(b'<g id="values">', header)
        self.assertIn(b'<g id="titles">', header)
        # Another kind's whole-screen canvas has no header, and a kit without fullCanvas keeps the template's.
        self.assertEqual(screen.compose(b'<g id="x"/>', full_canvas=True), screen.CANVAS[:-6] + b'<g id="x"/></svg>')
        self.assertEqual(screen.compose(b'<g id="x"/>', kit=True), screen.template().replace(b'</svg>', b'<g id="x"/></svg>'))

    def test_the_builder_composes_the_same_file(self):
        if not shutil.which('node'):
            self.skipTest('Node.js required')
        script = ('import {canvasKitHeader} from ' + json.dumps((ROOT / 'builder/core.mjs').as_uri()) + ';\n'
                  'import {readFileSync} from "node:fs";\n'
                  'process.stdout.write(canvasKitHeader(readFileSync(process.argv[2], "utf8")));\n')
        with tempfile.TemporaryDirectory() as temp:
            probe = Path(temp) / 'header.mjs'
            probe.write_text(script, encoding='utf-8', newline='\n')
            result = subprocess.run(['node', str(probe), str(PLATFORM / 'screen.svg')], capture_output=True,
                                    timeout=60, **HIDDEN)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, screen.kit_header(screen.template()))
        # The builder's asset step adds it for a drum kit's whole screen only.
        core = (ROOT / 'builder/core.mjs').read_text(encoding='utf-8')
        self.assertIn("m.fullCanvas === 1 && m.kind === 'drum' ? canvasKitHeader(baseSvg) : ''", core)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_the_renderer_over_host_doubles(self):
        # A whole-screen kit, a kit with the stock header, and a synth's whole-screen canvas.
        for kit, canvas in ((1, 1), (1, 0), (0, 1)):
            with self.subTest(kit=kit, canvas=canvas), tempfile.TemporaryDirectory() as temp:
                folder = Path(temp)
                for name in ('extended_drawing.hpp', 'kit_screen.hpp'):
                    shutil.copy(PLATFORM / name, folder)
                for name in ('screen', 'screen_header', 'note_activity', 'amp_envelope', 'drum_send', 'extended_retained'):
                    shutil.copy(ROOT / 'tests/fixtures/extended_kit_host.hpp', folder / (name + '.hpp'))
                binary = folder / ('kit.exe' if os.name == 'nt' else 'kit')
                built = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', f'-DOPXY_TEST_KIT={kit}',
                                        f'-DOPXY_TEST_CANVAS={canvas}', '-I' + str(folder), '-I' + str(ROOT / 'sdk/include'),
                                        '-I' + str(PLATFORM), str(ROOT / 'tests/fixtures/extended_kit.cpp'), '-o', str(binary)],
                                       capture_output=True, text=True, timeout=120, **HIDDEN)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **HIDDEN)
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_the_renderer_frames_a_kit_as_the_plain_one(self):
        # The plain renderer's frame: kit_cells after refresh_controls, the strip after the page.
        plain = (PLATFORM / 'screen_drawing.hpp').read_text(encoding='utf-8')
        extended = (PLATFORM / 'extended_drawing.hpp').read_text(encoding='utf-8')
        for text in (plain, extended):
            frame = text[text.index('static void frame('):]
            self.assertLess(frame.index('kit_cells(&o->base, controls);'), frame.index('animate(o, controls);'))
            self.assertLess(frame.index('draw(&o->base);'), frame.index('draw_hold_strip(controls.track, elapsed);'))


class PlayerPage(unittest.TestCase):
    def test_the_generated_unit_takes_the_extended_frame_input_and_renderer(self):
        m = fixture('probeplayer')
        display = m.definition['resources']['display']
        with tempfile.TemporaryDirectory() as temp:
            text = adapters.generate_player_object(temp, m).read_text(encoding='utf-8')
        self.assertIn(f'typedef {adapters.extended_frame(display)} Frame;', text)
        self.assertIn('typedef opxy::draw::EncoderInput Input;', text)
        self.assertIn('typedef opxy::screen::ExtendedRetained<Frame> Retained;', text)
        self.assertIn('#include "extended_retained.hpp"\n#include "encoder_activity.hpp"\n#include "screen_player_drawing.hpp"', text)
        self.assertIn('input.encoders = opxy::platform::encoder_activity();', text)
        self.assertIn('static const bool full_canvas = false;', text)
        self.assertIn(f'"{m.native_name}.svg", {adapters.RELOCATED["PAGE_LIMIT"]}, {adapters.screen_bytes(m)})', text)

    def test_the_generated_unit_compiles(self):
        clang = shutil.which('clang++')
        if not clang:
            self.skipTest('No clang++ for a freestanding 32-bit syntax check')
        for canvas in (False, True):
            with self.subTest(canvas=canvas), tempfile.TemporaryDirectory() as temp:
                root = copy_fixture('probeplayer', temp)
                if canvas:
                    definition = json.loads((root / 'definition.json').read_text(encoding='utf-8'))
                    definition['resources']['display']['fullCanvas'] = 1
                    (root / 'definition.json').write_text(json.dumps(definition), encoding='utf-8', newline='\n')
                m = machine_at(root)
                unit = adapters.generate_player_object(Path(temp), m)
                self.assertIn(f'static const bool full_canvas = {"true" if canvas else "false"};', unit.read_text(encoding='utf-8'))
                includes = [ROOT, ROOT / 'sdk/include', ROOT / 'sdk/platform', PLATFORM, root,
                            ROOT / 'sdk/platform/blackfin/freestanding']
                result = subprocess.run([clang, '--target=i686-unknown-elf', '-fsyntax-only', '-O2', '-std=gnu++11',
                                         '-ffreestanding', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
                                         '-nostdinc++', '-nostdinc', '-Wall', '-Werror', '-Wno-unused-variable',
                                         '-Wno-unused-template', *('-I' + str(path) for path in includes), str(unit)],
                                        capture_output=True, text=True, timeout=120, **HIDDEN)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_plain_players_unit_is_as_before(self):
        # A plain display keeps draw::Frame, draw::Input and the retained renderer, and includes nothing more.
        with tempfile.TemporaryDirectory() as temp:
            root = copy_fixture('probeplayer', temp)
            definition = json.loads((root / 'definition.json').read_text(encoding='utf-8'))
            definition['resources']['display'] = dict(stateBytes=8, stateAlignment=4, paths=0, points=0, nodes=2,
                                                      texts=1, palette=1)
            (root / 'definition.json').write_text(json.dumps(definition), encoding='utf-8', newline='\n')
            text = adapters.generate_player_object(Path(temp), machine_at(root)).read_text(encoding='utf-8')
        self.assertIn('typedef opxy::draw::Frame<0, 0, 2, 1, 1> Frame;', text)
        self.assertIn('typedef opxy::draw::Input Input;', text)
        self.assertIn('typedef opxy::screen::RetainedDrawing<Frame> Retained;', text)
        self.assertIn('#include "display.cpp"\n#include "screen_player_drawing.hpp"', text)
        self.assertIn('static void snapshot(Input& input) { (void)input; }', text)
        self.assertNotIn('extended_retained', text)
        self.assertNotIn('encoder_activity', text)


class Previews(unittest.TestCase):
    def test_a_players_preview_reports_the_drawing_abi_of_the_others(self):
        import re
        abi = re.compile(r'opxy_display_abi\(\) \{ return (\d+); \}')
        with tempfile.TemporaryDirectory() as temp:
            player = abi.findall(adapters.generate_player_preview(temp, fixture('probeplayer')).read_text(encoding='utf-8'))
        self.assertEqual(player, abi.findall((ROOT / 'sdk/host/display_preview.cpp').read_text(encoding='utf-8')))

    @unittest.skipUnless(shutil.which('clang++'), 'WASM compiler required')
    def test_an_extended_kit_previews_with_its_extension(self):
        m = fixture('probekitcanvas')
        with tempfile.TemporaryDirectory() as temp:
            preview.compile_preview('clang++', m.definition, Path(temp), m.root, m.sources, kind='drum')
            wasm = (Path(temp) / 'engine.wasm').read_bytes()
        self.assertIn(b'opxy_display_extension', wasm)
        self.assertIn(b'opxy_drum_defaults', wasm)
        self.assertTrue(preview.drawing_bindings(native.display_ids(m.prefix, m.definition['resources']['display']),
                                                 m.definition['resources']['display'])['fullCanvas'])

    @unittest.skipUnless(shutil.which('clang++') and shutil.which('node'), 'WASM compiler and Node.js required')
    def test_a_drawing_players_page_previews_silently(self):
        # tools/check_player_preview.mjs runs inside the build: valid frames over a held key.
        m = fixture('probeplayer')
        self.assertTrue(preview.previewed(m))
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / 'preview'
            preview.build('clang++', m, out)
            manifest = json.loads((out / 'manifest.json').read_text(encoding='utf-8'))
            wasm = (out / 'engine.wasm').read_bytes()
            preview.preview_bundle(out, require_reference=True)
        self.assertEqual(manifest['graphics'], preview.graphics_metadata(m))
        for name in ('opxy_player_press', 'opxy_player_step', 'opxy_display_extension'):
            self.assertIn(name.encode(), wasm)
        self.assertNotIn(b'opxy_render', wasm)
        # A player with the SDK's dot page has none.
        with tempfile.TemporaryDirectory() as temp:
            root = copy_fixture('probeplayer', temp)
            definition = json.loads((root / 'definition.json').read_text(encoding='utf-8'))
            del definition['resources']['display']
            (root / 'definition.json').write_text(json.dumps(definition), encoding='utf-8', newline='\n')
            self.assertFalse(preview.previewed(machine_at(root)))


if __name__ == '__main__':
    unittest.main()
