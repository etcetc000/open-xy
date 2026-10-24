"""Public source and artifact boundaries for local custom-machine previews."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import unittest

from tooling import custom_preview as custom
from tooling.formats import sha
from tooling.machines import machine_at
from tooling.preview import preview_bundle
from test_sample_services import HOST_CXX


class CustomPreviewTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.source = self.root / 'machine'
        with redirect_stdout(io.StringIO()):
            custom.scaffold(self.source, 'example', 'Local Example')

    def fixture(self):
        files = custom.source_files(self.source)
        definition, graphics = custom.metadata(files)
        out = self.root / 'preview'
        out.mkdir()
        module = b'\0asm\x01\0\0\0'
        manifest = dict(schema=1, kind=custom.KIND, engine=definition['id'], sample_rate=44100,
                        source_root=str(self.source), definition=definition, graphics=graphics,
                        sources=custom.source_hashes(files), wasm_sha256=sha(module), compiler={})
        (out / 'manifest.json').write_text(json.dumps(manifest))
        (out / 'engine.wasm').write_bytes(module)
        (out / 'panel.svg').write_bytes(files['panel.svg.fragment'])
        return out, manifest

    def test_local_snapshot_is_bounded_and_cannot_qualify_a_collection(self):
        out, manifest = self.fixture()
        self.assertEqual(set(preview_bundle(out)), {'/preview/manifest.json', '/preview/engine.wasm', '/preview/panel.svg'})
        with self.assertRaisesRegex(ValueError, 'not qualified collection'):
            preview_bundle(out, require_reference=True)
        manifest['definition']['name'] = 'Other Name'
        (out / 'manifest.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'metadata differs'):
            preview_bundle(out)

    def test_source_edits_invalidate_the_snapshot(self):
        out, _ = self.fixture()
        with (self.source / 'engine.cpp').open('a') as file:
            file.write('\n// An intentional edit.\n')
        with self.assertRaisesRegex(ValueError, 'stale'):
            preview_bundle(out)

    def test_starters_are_machines_whose_own_checks_pass(self):
        folder = self.root / 'effect'
        with redirect_stdout(io.StringIO()):
            custom.scaffold(folder, 'drive', 'Drive', 'effect')
        m = machine_at(folder)
        self.assertEqual((m.kind, m.dry_parameter, m.identity['page']), ('effect', 2, 21))
        with self.assertRaises(ValueError):
            custom.scaffold(self.root / 'other', 'drive', 'Drive', 'player')
        self.assertTrue((folder / 'tests/sound.cpp').is_file() and (self.source / 'tests/sound.cpp').is_file())
        if not HOST_CXX:
            self.skipTest('No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
        # Each starter's own sound checks pass as generated.
        from tooling.machine_test import host_tests
        for m in (machine_at(folder), machine_at(self.source)):
            self.assertEqual(host_tests(m, HOST_CXX, self.root), 1)

    def test_scaffold_artwork_meets_the_native_rules(self):
        # One root element and outlines with the declared point count, as the firmware build requires.
        from tooling.native import check_outlines, panel_root
        with self.assertRaisesRegex(ValueError, 'must be one SVG element'):
            panel_root(b'<path id="a"/>\n<path id="b"/>', 'panel.svg.fragment')
        for points in (4, 7, 64):
            outline = custom.square_outline(points)
            self.assertEqual(outline.count('L'), points - 1)
            self.assertTrue(outline.startswith('M') and outline.endswith(' Z') and 'e' not in outline)
        display = {'paths': 1, 'nodes': 1, 'texts': 0, 'palette': 0, 'points': 64}
        good = panel_root(f'<g><path id="local_node_0" d="{custom.square_outline(64)}" fill="none"/></g>'.encode(), 'test')
        check_outlines({n.get('id'): n for n in good.iter() if n.get('id')}, 'local', display)
        bad = panel_root(f'<g><path id="local_node_0" d="{custom.square_outline(4)}" fill="none"/></g>'.encode(), 'test')
        with self.assertRaisesRegex(ValueError, 'exactly M, 63 L and Z'):
            check_outlines({n.get('id'): n for n in bad.iter() if n.get('id')}, 'local', display)

    def test_tampered_artwork_is_rejected(self):
        out, _ = self.fixture()
        (out / 'panel.svg').write_text('<script/>')
        with self.assertRaisesRegex(ValueError, 'artifact identity'):
            preview_bundle(out)

    def test_custom_artwork_cannot_load_external_resources(self):
        files = custom.source_files(self.source)
        files['panel.svg.fragment'] = files['panel.svg.fragment'].replace(
            b'fill="none"', b'fill="url(https://example.invalid/paint.svg)"', 1)
        with self.assertRaisesRegex(ValueError, 'Drawing colors'):
            custom.metadata(files)

    def test_custom_definition_cannot_reuse_curated_identity(self):
        definition, _ = custom.metadata(custom.source_files(self.source))
        definition['id'] = 'fm2op'
        (self.source / 'definition.json').write_text(json.dumps(definition))
        with self.assertRaisesRegex(ValueError, 'distinct local machine ID'):
            custom.metadata(custom.source_files(self.source))

    def test_includes_cannot_escape_source_or_use_an_untracked_header(self):
        path = self.source / 'engine.cpp'
        for include in ('"../outside.h"', '"missing.h"', 'HEADER_MACRO', '<windows.h>'):
            with self.subTest(include=include):
                path.write_text('#include ' + include + '\n')
                with self.assertRaises(ValueError):
                    custom.source_files(self.source)

    def test_output_cannot_overwrite_or_nest_in_source(self):
        rejected = self.root / 'rejected'
        with self.assertRaisesRegex(ValueError, 'distinct local machine ID'):
            custom.scaffold(rejected, 'fm2op', 'My Machine')
        self.assertFalse(rejected.exists())
        with self.assertRaisesRegex(ValueError, 'Output already exists'):
            custom.scaffold(self.source, 'another', 'Another')
        with self.assertRaisesRegex(ValueError, 'outside the machine source'):
            custom.build('missing-compiler', self.source, self.source / 'preview')
