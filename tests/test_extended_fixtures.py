"""The extended-drawing fixtures (tests/fixtures/machines/probecanvas, probecurves, probelarge,
probekitcanvas, probeplayer, probescope, probemeter, probestagemeter): counted, open, segment,
quadratic and cubic paths, RGBA and palette nodes, encoder activity on a synth, an effect, a drum kit
and a note player, a whole-screen kit, the last control touched, a glyph atlas, a synth's, an
effect's and a track stage's own audio and stock's output levels. Release verification builds them
for the device, so the extended renderer, the player's page, the control capture and the audio
transport keep compiling and linking there."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from tooling import adapters, native, preview
from tooling.definitions import extended_display
from tooling.pipeline import ROOT
from machine_fixtures import FOLDER, fixture
from test_divide import blackfin_compiler
from test_sample_services import HOST_CXX

EXTENDED = ('probecanvas', 'probecurves', 'probelarge', 'probekitcanvas', 'probeplayer', 'probescope', 'probemeter',
            'probestagemeter')
# The unit each draws in: a player's page is in its one unit.
SCREEN_UNIT = dict(probeplayer='player_object.o')
HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}


class ExtendedFixtures(unittest.TestCase):
    def test_each_fixture_is_extended_and_generates_the_extended_renderer(self):
        kinds = {}
        for name in EXTENDED:
            m = fixture(name)
            kinds[name] = m.kind
            display = m.definition['resources']['display']
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                self.assertTrue(extended_display(display))
                native.validate_panel(m)
                if m.kind == 'player':
                    unit = adapters.generate_player_object(Path(temp), m).read_text(encoding='utf-8')
                    self.assertIn('ExtendedRetained', unit)
                    continue
                adapters.generate_display(Path(temp), machine=m)
                binding = (Path(temp) / 'display_binding.hpp').read_text(encoding='utf-8')
                self.assertIn('ExtendedFrame', binding)
                screen = adapters.generate_screen(Path(temp), m).read_text(encoding='utf-8')
                self.assertIn('extended_drawing', screen)
        self.assertEqual(kinds, dict(probecanvas='synth', probecurves='effect', probelarge='synth',
                                     probekitcanvas='drum', probeplayer='player', probescope='synth',
                                     probemeter='effect', probestagemeter='synth'))
        # Between them: every path kind the renderer retains, both node kinds, and both inputs.
        displays = {name: fixture(name).definition['resources']['display'] for name in EXTENDED}
        self.assertTrue(displays['probecanvas']['countedPaths'] and displays['probecanvas']['openPaths']
                        and displays['probecanvas']['segmentPaths'] and displays['probecanvas']['rgba'])
        self.assertTrue(displays['probecurves']['quadratic'] and displays['probecurves']['encoderActivity'])
        self.assertTrue(displays['probelarge']['cubic'] and not displays['probelarge'].get('affine'))
        self.assertTrue(displays['probekitcanvas']['fullCanvas'] and displays['probekitcanvas']['encoderActivity'])
        self.assertTrue(displays['probeplayer']['cubic'] and displays['probeplayer']['rgba']
                        and displays['probeplayer']['encoderActivity'] and not displays['probeplayer'].get('fullCanvas'))
        self.assertTrue(native.includes_header([fixture('probecanvas')], 'control_interaction'))
        # The SDK 1.27 keys: a glyph atlas, a synth's wide snapshot with its reset, an effect's
        # snapshot, both reading stock's levels, and a track stage publishing its synth's words.
        self.assertTrue(displays['probescope']['glyphs'] and displays['probescope']['audioReset'] and
                        displays['probescope']['outputLevels'] and displays['probemeter']['audioTransport'] and
                        displays['probemeter']['outputLevels'] and displays['probestagemeter']['audioReset'] and
                        fixture('probestagemeter').definition['resources']['trackStage']['audio'])

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_each_display_compiles_against_its_generated_input(self):
        # Effects have no browser preview: this is where an effect page's display meets the
        # input type its declaration generates, before a device build.
        # A player's page is its own unit, compiled in test_extended_screens.PlayerPage.
        for name in (name for name in EXTENDED if SCREEN_UNIT.get(name) is None):
            m = fixture(name)
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                adapters.generate_display(Path(temp), machine=m)
                command = [HOST_CXX, '-std=c++11', '-fsyntax-only', '-Wall', '-Werror', '-I' + str(ROOT / 'sdk/include'),
                           '-I' + str(m.root), '-I' + temp, str(Path(temp) / 'display_binding.cpp')]
                result = subprocess.run(command, capture_output=True, text=True, timeout=120, **HIDDEN)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(shutil.which('clang++'), 'WASM compiler required')
    def test_the_synth_fixtures_preview(self):
        for name in ('probecanvas', 'probelarge', 'probescope', 'probestagemeter'):
            m = fixture(name)
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                preview.compile_preview('clang++', m.definition, Path(temp), m.root, m.sources)
                self.assertIn(b'opxy_display_extension', (Path(temp) / 'engine.wasm').read_bytes())

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS') and blackfin_compiler(),
                         'Release verification builds the fixtures (set OPXY_TEST_STOCK_OS and OPXY_TEST_COMPILER)')
    def test_the_device_build_links_every_fixture(self):
        compiler = blackfin_compiler().parent.parent
        with tempfile.TemporaryDirectory() as temp:
            command = [sys.executable, ROOT / 'sdk.py', 'build', '--toolchain', compiler / 'bin', '--compiler', compiler,
                       '--stock-os', os.environ['OPXY_TEST_STOCK_OS'], '--out', Path(temp) / 'package']
            for name in EXTENDED:
                command += ['--machine', FOLDER / name]
            result = subprocess.run([str(c) for c in command], cwd=ROOT, capture_output=True, text=True,
                                    timeout=1200, **HIDDEN)
            self.assertEqual(result.returncode, 0, result.stdout[-4000:] + result.stderr[-4000:])
            for name in EXTENDED:
                self.assertIn(f'"{name}"', result.stdout)
                self.assertTrue((Path(temp) / 'package/analysis' / name / SCREEN_UNIT.get(name, 'screen.o')).is_file(), name)
            # The three declaring fixtures link the audio transport's units, and the track
            # stage's runtime and table for the one with a stage.
            report = (Path(temp) / 'package/analysis/compile-report.json').read_text()
            for unit in ('telemetry_protocol', 'telemetry_native', 'telemetry_hooks', 'telemetry_registry', 'track_stage'):
                self.assertIn(unit, report)
            # The registry still tests each owner's number for 0 (a machine a builder selection
            # leaves out): the compiler may not assume a symbol's address is never null.
            registry = (Path(temp) / 'package/analysis/sdk/telemetry_registry.s').read_text()
            self.assertGreaterEqual(registry.count('==0;'), 3, 'owner numbers are not tested for 0')


if __name__ == '__main__':
    unittest.main()
