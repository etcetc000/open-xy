import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from tooling import arrangement, extensions, native, screen
from tooling.pipeline import ROOT, config
from test_sample_services import HOST_CXX

PROBE = ROOT / 'tests/fixtures/arrangement_probe'

class ArrangementTests(unittest.TestCase):
    def test_exclusive_owner_and_generated_optional_descriptor(self):
        owner = extensions.extension_at(PROBE)
        self.assertTrue(owner.arrangement)
        other = extensions.Extension('other', PROBE, (), (), {}, True)
        with self.assertRaisesRegex(ValueError, 'multiple owners'):
            extensions.check_collection([owner, other])
        with tempfile.TemporaryDirectory() as folder:
            source = extensions.generate_collection(Path(folder), [owner]).read_text()
            self.assertIn('return sdk_extension_arrangement_probe_surface()', source)
            source = extensions.generate_extension(Path(folder), owner).read_text()
            self.assertIn('arrangement.cpp', source)
            self.assertIn('return &arrangement_descriptor', source)
            source = extensions.generate_collection(Path(folder), []).read_text()
            self.assertNotIn('_surface()', source)

    def test_owner_frame_is_not_an_unconditional_sdk_unit(self):
        self.assertNotIn('arrangement', native.SDK_UNITS)
        self.assertIn('arrangement_registry', native.SDK_UNITS)
        units = native.plan([])
        self.assertFalse(any(name.endswith('arrangement_hooks.o') for name, _, _ in units))
        self.assertTrue(any(name.endswith('arrangement_registry.o') for name, _, _ in units))

    def test_artwork_matches_fixed_frame(self):
        import xml.etree.ElementTree as ET
        root = ET.fromstring(screen.compose(arrangement.artwork()))
        ids = [node.get('id') for node in root.iter()]
        self.assertTrue(set(arrangement.ids()) <= set(ids))
        self.assertEqual(len(arrangement.ids()), 224)
        self.assertLess(len(screen.compose(arrangement.artwork())), 65536)
        with tempfile.TemporaryDirectory() as folder:
            header = arrangement.header(folder).read_text()
            self.assertIn('const char arrangement_svg[]', header)
            self.assertNotIn('extension-arrangement.svg', header)

    def test_continuations_are_only_the_reviewed_whole_prologues(self):
        profile = config()
        for name, (address, before) in arrangement.ENTRIES.items():
            patch = next(p for p in profile['patches'] if int(p['address'], 16) == address)
            self.assertEqual(patch['before'], before)
            self.assertEqual(arrangement.continuation(name)[:8], bytes.fromhex(before))
            self.assertEqual(len(arrangement.continuation(name)), 16)

    def test_actual_input_owner_and_surface_fixture(self):
        if not HOST_CXX: self.skipTest('No host C++ compiler')
        with tempfile.TemporaryDirectory() as folder:
            binary = str(Path(folder) / 'arrangement.exe')
            subprocess.run([HOST_CXX, '-std=c++11', '-I' + str(ROOT / 'sdk/include'),
                            '-I' + str(ROOT / 'sdk/platform'), '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'),
                            str(ROOT / 'tests/fixtures/arrangement_input.cpp'), '-o', binary],
                           check=True, capture_output=True, timeout=120)
            subprocess.run([binary], check=True, timeout=30)

    def test_controller_intent_and_reply_actions_are_owner_scoped(self):
        if not HOST_CXX: self.skipTest('No host C++ compiler')
        from test_extension_runtime import build_and_run
        with tempfile.TemporaryDirectory() as folder:
            build_and_run(self, ROOT / 'tests/fixtures/arrangement_runtime.cpp', folder)

    def test_bounded_mailboxes_reserve_releases_and_do_not_overwrite(self):
        if not HOST_CXX: self.skipTest('No host C++ compiler')
        from test_extension_runtime import build_and_run
        with tempfile.TemporaryDirectory() as folder:
            build_and_run(self, ROOT / 'tests/fixtures/arrangement_queue.cpp', folder)
