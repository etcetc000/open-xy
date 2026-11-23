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


def input_fill(text):
    """Every statement of a renderer's animate from the first input member it fills to the
    render call (assignments, loops, calls that take the input), the input named input."""
    body = text[text.index('static void animate('):]
    body = body[body.index('for (u32 i = 0; i < page_controls; ++i) {'):body.index('binding::display_render(')]
    body = re.sub(r'//[^\n]*', '', body)
    body = re.sub(r'\b(display|extended)_input\b', 'input', body)
    statements, depth, current = [], 0, ''
    for c in body:
        depth += (c == '(') - (c == ')')
        if depth == 0 and c in ';{}':
            statements.append(current)
            current = ''
        else:
            current += c
    return [s for s in (' '.join(s.split()) for s in statements) if s]


# What the extended renderer does differently, and why:
PLAIN_ONLY = {
    # A sample engine's screen reads its sample; sample engines have no extended screen.
    'if (Machine::sample) sdk_sample_overview_update(controls.track)',
    # A kit before SDK 1.21 is struck by its selected pad only; an extended kit declares 1.26,
    # whose serial is the track's (track_note_serial, as the extended renderer's own line).
    'if (Machine::selected_pad)',
    'input.note_serial = selected_pad_serial(controls)',
    'else',
    'input.note_serial = track_note_serial(controls)',
}
EXTENDED_ONLY = {
    'input.note_serial = controls.track < 8 ? note_serial(controls.track) : 0',
    # The encoder record follows the input (encoderActivity).
    'binding::snapshot_encoders(input)',
}


def filled_early(plain):
    """Whether the plain renderer's animate touches its input before the first page_controls
    loop, where input_fill does not look."""
    body = plain[plain.index('static void animate('):]
    return 'display_input' in body[:body.index('for (u32 i = 0; i < page_controls; ++i) {')]


def unexpected(plain, extended):
    """The statements one renderer has and the other lacks, beyond the intended differences;
    a plain fill before the compared range counts as one the extended renderer lacks."""
    a, b = input_fill(plain), input_fill(extended)
    early = ['display_input before the page_controls loop'] if filled_early(plain) else []
    return early + sorted(set(a) - set(b) - PLAIN_ONLY), sorted(set(b) - set(a) - EXTENDED_ONLY)


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
        # The extended renderer mirrors screen_drawing.hpp's input fill. A statement a later SDK adds
        # there (a member, a loop, a call taking the input) must be added here too, or extended
        # screens would read zero for it; only the differences listed above are allowed.
        plain = (PLATFORM / 'screen_drawing.hpp').read_text(encoding='utf-8')
        extended = (PLATFORM / 'extended_drawing.hpp').read_text(encoding='utf-8')
        self.assertEqual(unexpected(plain, extended), ([], []))
        a, b = input_fill(plain), input_fill(extended)
        self.assertTrue(PLAIN_ONLY <= set(a) and EXTENDED_ONLY <= set(b), 'An allowed difference is stale')
        self.assertEqual([s for s in a if s not in PLAIN_ONLY], [s for s in b if s not in EXTENDED_ONLY],
                         'The shared statements run in the same order')
        # The test sees what it is meant to: lines open changes add to the plain renderer, and a deleted call.
        render = '        binding::display_render(&display_state'
        for change in ('display_input.bpm_x10 = platform::bpm_x10();',
                       'display_input.level_in = platform::level_in(controls.track);',
                       'for (u32 k = 0; k < 4; ++k) display_input.trigger_controls[k] = 0;',
                       'played_pad_snapshot(controls.track, display_input);'):
            with self.subTest(added=change):
                changed = plain.replace(render, '        ' + change + '\n' + render, 1)
                self.assertNotEqual(unexpected(changed, extended), ([], []))
        # A fill line inserted before the compared range is caught too.
        loop = '        for (u32 i = 0; i < page_controls; ++i) {\n            display_input.parameters'
        self.assertEqual(plain.count(loop), 1)
        early = plain.replace(loop, '        display_input.bpm_x10 = platform::bpm_x10();\n' + loop, 1)
        self.assertNotEqual(unexpected(early, extended), ([], []))
        call = 'platform::amp_envelope(controls.track, input.amp);'
        self.assertIn(call, extended)
        self.assertNotEqual(unexpected(plain, extended.replace(call, '', 1)), ([], []))

    def test_both_previews_report_one_drawing_abi(self):
        abi = re.compile(r'opxy_display_abi\(\) \{ return (\d+); \}')
        plain = abi.findall((ROOT / 'sdk/host/display_preview.cpp').read_text(encoding='utf-8'))
        extended = abi.findall((ROOT / 'sdk/host/display_preview_extended.cpp').read_text(encoding='utf-8'))
        self.assertEqual(len(plain), 1)
        self.assertEqual(plain, extended)


if __name__ == '__main__':
    unittest.main()
