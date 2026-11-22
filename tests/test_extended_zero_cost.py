"""Machines that use no extended key get the generated code, link and hooks they had without them;
and the extended renderer and preview keep in step with the plain ones they mirror."""
from pathlib import Path
import re
import shutil
import tempfile
import unittest
from tooling import adapters, native, preview
from tooling.definitions import extended_display
from tooling.pipeline import ROOT, config
from machine_fixtures import NAMES, fixture

PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'


def assignments(text, name):
    """Each member the renderer's animate gives its drawing input, with what it assigns."""
    body = text[text.index('static void animate('):]
    body = body[:body.index('\n    }\n')]
    return {m.group(1): m.group(2).strip() for m in re.finditer(r'\b' + name + r'\.(\w+)(?:\[i\])? = ([^;]+);', body)}


class ExtendedZeroCost(unittest.TestCase):
    def test_plain_fixtures_generate_no_extended_code(self):
        for name in NAMES:
            m = fixture(name)
            display = m.definition['resources'].get('display')
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                folder = Path(temp)
                if m.display == 'stock' or not display:
                    continue
                self.assertFalse(extended_display(display))
                adapters.generate_display(folder, machine=m)
                binding = (folder / 'display_binding.hpp').read_text(encoding='utf-8')
                self.assertIn('opxy::draw::Frame<', binding)
                for text in ('ExtendedFrame', 'encoder_activity', 'display_extension', 'full_canvas'):
                    self.assertNotIn(text, binding)
                screen = adapters.generate_screen(folder, m).read_text(encoding='utf-8')
                self.assertIn('#include "screen_drawing.hpp"', screen)
                self.assertIn('opxy::screen::Drawing<Machine>', screen)
                self.assertNotIn('extended_drawing', screen)

    def test_plain_collection_compiles_synth_controls_as_it_is(self):
        machines = [fixture(name) for name in NAMES]
        steps = [step for _name, _m, step in native.plan(machines, config())]
        self.assertIn('synth_controls', steps)
        self.assertFalse(native.includes_header(machines, 'control_interaction'))

    @unittest.skipUnless(shutil.which('clang++'), 'WASM compiler required')
    def test_plain_preview_exports_no_extension(self):
        m = fixture('probesynth')
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            preview.compile_preview('clang++', m.definition, out, m.root, m.sources)
            wasm = (out / 'engine.wasm').read_bytes()
        for export in preview.EXTENDED_EXPORTS:
            self.assertNotIn(export.encode(), wasm)
        self.assertIn(b'opxy_display_abi', wasm)

    def test_extended_renderer_fills_the_input_as_the_plain_one(self):
        # The extended renderer mirrors screen_drawing.hpp's input fill: a member a later SDK adds
        # there must be added here too, or extended screens would read zero for it.
        plain = assignments((PLATFORM / 'screen_drawing.hpp').read_text(encoding='utf-8'), 'display_input')
        extended = assignments((PLATFORM / 'extended_drawing.hpp').read_text(encoding='utf-8'), 'input')
        self.assertEqual(set(plain), set(extended))
        # Every right-hand side is the same, but for the note serial: a kit's selected pad
        # (display_note_serial) never applies to a synth's or an effect's extended screen.
        differing = {k for k in plain if plain[k] != extended[k]}
        self.assertEqual(differing, {'note_serial'})
        self.assertEqual(extended['note_serial'], 'controls.track < 8 ? note_serial(controls.track) : 0')

    def test_both_previews_report_one_drawing_abi(self):
        abi = re.compile(r'opxy_display_abi\(\) \{ return (\d+); \}')
        plain = abi.findall((ROOT / 'sdk/host/display_preview.cpp').read_text(encoding='utf-8'))
        extended = abi.findall((ROOT / 'sdk/host/display_preview_extended.cpp').read_text(encoding='utf-8'))
        self.assertEqual(len(plain), 1)
        self.assertEqual(plain, extended)


if __name__ == '__main__':
    unittest.main()
