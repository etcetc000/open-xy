"""The extended-drawing fixtures (tests/fixtures/machines/probecanvas, probecurves, probelarge,
probekitcanvas, probeplayer): counted, open, segment, quadratic and cubic paths, RGBA and palette
nodes, encoder activity on a synth, an effect, a drum kit and a note player, a whole-screen kit, and
the last control touched. Release verification builds them for the device, so the extended renderer,
the player's page and the control capture keep compiling and linking there."""
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

EXTENDED = ('probecanvas', 'probecurves', 'probelarge', 'probekitcanvas', 'probeplayer')
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
                                     probekitcanvas='drum', probeplayer='player'))
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

    @unittest.skipUnless(shutil.which('clang++'), 'WASM compiler required')
    def test_the_synth_fixtures_preview(self):
        for name in ('probecanvas', 'probelarge'):
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


if __name__ == '__main__':
    unittest.main()
