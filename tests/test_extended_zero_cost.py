"""Machines that use no extended key get the generated code, link and hooks they had without them."""
from pathlib import Path
import tempfile
import unittest
from tooling import adapters, native, preview
from tooling.definitions import extended_display
from tooling.firmware import active_services, patch_active
from tooling.pipeline import ROOT, config
from machine_fixtures import NAMES, fixture


class ExtendedZeroCost(unittest.TestCase):
    def test_plain_fixtures_generate_no_extended_code(self):
        for name in NAMES:
            m = fixture(name)
            display = m.definition['resources'].get('display')
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                folder = Path(temp)
                if m.kind == 'synth':
                    for sweep in (False, True):
                        voice = adapters.generate_voice(folder, m, sweep).read_text(encoding='utf-8')
                        self.assertNotIn('AudioContext', voice)
                        self.assertNotIn('telemetry', voice)
                        self.assertNotIn('@AUDIO', voice)
                    hosted = adapters.generate_voice_preview(folder, m.definition).read_text(encoding='utf-8')
                    self.assertNotIn('opxy_take_audio', hosted)
                    self.assertNotIn('@AUDIO', hosted)
                if m.display == 'stock' or not display:
                    continue
                self.assertFalse(extended_display(display))
                adapters.generate_display(folder, machine=m)
                binding = (folder / 'display_binding.hpp').read_text(encoding='utf-8')
                self.assertIn('opxy::draw::Frame<', binding)
                for text in ('ExtendedFrame', 'audio_transport', 'encoder_activity', 'display_extension', 'full_canvas'):
                    self.assertNotIn(text, binding)
                screen = adapters.generate_screen(folder, m).read_text(encoding='utf-8')
                self.assertIn('#include "screen_drawing.hpp"', screen)
                self.assertIn('opxy::screen::Drawing<Machine>', screen)
                self.assertNotIn('extended_drawing', screen)

    def test_plain_collection_links_and_patches_nothing_extended(self):
        machines = [fixture(name) for name in NAMES]
        profile = config()
        services = active_services(profile, native.service_users(machines))
        self.assertNotIn('telemetry', services)
        self.assertFalse([p for p in profile['patches'] if p.get('service') == 'telemetry' and patch_active(p, services)])
        steps = [step for _name, _m, step in native.plan(machines, profile)]
        self.assertFalse([s for s in steps if s.startswith('telemetry_')])
        # synth_controls.cpp compiles as it is: no machine reads the last control touched.
        self.assertIn('synth_controls', steps)
        self.assertFalse(native.includes_header(machines, 'control_interaction'))

    def test_plain_previews_export_what_they_did(self):
        for name in ('probesynth', 'probesources', 'probeshift'):
            m = fixture(name)
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                out = Path(temp)
                adapters.generate_display(out, definition=m.definition)
                self.assertFalse(extended_display(m.definition['resources']['display']))
                for export in preview.EXTENDED_EXPORTS + preview.AUDIO_EXPORTS + preview.TRANSPORT_EXPORTS:
                    self.assertNotIn(export, preview.DISPLAY_EXPORTS + preview.VOICE_EXPORTS)
        # The plain host preview keeps drawing ABI 6 with no extension export.
        plain = (ROOT / 'sdk/host/display_preview.cpp').read_text(encoding='utf-8')
        self.assertNotIn('opxy_display_extension', plain)
        self.assertNotIn('control_interaction', plain)


if __name__ == '__main__':
    unittest.main()
