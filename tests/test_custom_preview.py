"""Public source and artifact boundaries for local custom-machine previews."""
from contextlib import redirect_stdout
import io
import json
import shutil
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

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
            custom.scaffold(folder, 'drive', 'Drive', 'effect', repository=True)
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

    def test_control_counts_share_unused_columns_and_require_the_new_sdk(self):
        from tooling.adapters import control_cycles
        default = machine_at(self.source).definition
        self.assertEqual(len(default['parameters']), 4)
        self.assertNotIn('press', default)
        for count in range(4, 9):
            with self.subTest(controls=count), redirect_stdout(io.StringIO()):
                folder = self.root / ('controls%d' % count)
                custom.scaffold(folder, 'counted', 'Counted', controls=count)
                m = machine_at(folder)
                self.assertEqual(len(m.definition['parameters']), count)
                self.assertEqual(m.definition.get('press'), 'page' if count > 4 else None)
                for preset in m.definition['presets'].values():
                    self.assertEqual(len(preset), count)
                if count > 4:
                    self.assertEqual(m.definition['resources']['display']['texts'], 16)
                    cycles = control_cycles(m.definition)
                    self.assertEqual(cycles[:4], [i + 4 if i + 4 < count else i for i in range(4)])
                    self.assertIn('page_header(', (folder / 'display.cpp').read_text())
                    record = json.loads((folder / 'machine.json').read_text())
                    record['sdk'] = '1.3'
                    (folder / 'machine.json').write_text(json.dumps(record))
                    if count < 8:
                        with self.assertRaisesRegex(ValueError, 'partially filled second page'):
                            machine_at(folder)
        with self.assertRaisesRegex(ValueError, 'effects use 4'):
            custom.scaffold(self.root / 'bad-effect', 'badfx', 'Bad FX', 'effect', controls=8)
        self.assertFalse((self.root / 'bad-effect').exists())

    def test_repository_cli_keeps_machine_at_root_and_adds_agent_and_ci_entry_points(self):
        for kind in custom.STARTERS:
            with self.subTest(kind=kind), redirect_stdout(io.StringIO()):
                plain, repo = self.root / (kind + '-plain'), self.root / (kind + '-repo')
                # The step picker shows at most ten characters.
                title = 'Own Step' if kind == 'step' else 'Own Machine'
                custom.scaffold(plain, 'ownmachine', title, kind)
                self.assertEqual(custom.main(['--repository', '--kind', kind, '--id', 'ownmachine',
                                              '--name', title, '--out', str(repo)]), 0)
                self.assertEqual(machine_at(repo).kind, kind)
                # Repository setup must preserve every authored source and sound test.
                for path in plain.rglob('*'):
                    if path.is_file():
                        self.assertEqual(path.read_bytes(), (repo / path.relative_to(plain)).read_bytes())
                self.assertFalse((plain / 'AGENTS.md').exists())
                self.assertFalse((repo / '.git').exists())
                for name in ('README.md', 'AGENTS.md', 'LICENSE', '.gitignore', '.github/workflows/machine.yml'):
                    self.assertTrue((repo / name).is_file(), name)
                for name in ('README.md', 'AGENTS.md'):
                    text = (repo / name).read_text()
                    self.assertIn('docs/profiling.md', text)
                    self.assertIn('docs/catalog.md#submit-a-machine', text)
                    self.assertIn('open-xy-core-machines', text)
                    self.assertNotIn('machine-template', text)
                    self.assertNotIn('@ID@', text)
                    self.assertNotIn('@AUDITION@', text)
                workflow = (repo / '.github/workflows/machine.yml').read_text()
                self.assertIn('OPEN_XY_REF: staging', workflow)
                self.assertIn('sdk.py test --machine machine', workflow)
                self.assertIn('sdk.py compile --machine machine', workflow)
                self.assertIn('${{ runner.temp }}/module', workflow)
                with self.assertRaisesRegex(ValueError, 'Output already exists'):
                    custom.scaffold(repo, 'ownmachine', title, kind, repository=True)

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
        # The device merges repeated points, even ones written differently.
        repeated = custom.square_outline(64).replace('M-32 -32 ', 'M-32 -32 L-32.0 -32 ', 1).replace(' L-28 -32', '', 1)
        bad = panel_root(f'<g><path id="local_node_0" d="{repeated}" fill="none"/></g>'.encode(), 'test')
        with self.assertRaisesRegex(ValueError, 'repeats a point; the device merges repeated points'):
            check_outlines({n.get('id'): n for n in bad.iter() if n.get('id')}, 'local', display)
        # Text slots are placed by their transform, never x or y.
        from tooling.native import check_bindings
        display = {'paths': 0, 'nodes': 1, 'texts': 1, 'palette': 1, 'points': 0}
        for text, error in (('<text id="local_text_0" transform="matrix(1 0 0 1 5 34)">bits</text>', None),
                            ('<text id="local_text_0" x="5" y="34">bits</text>', 'positioned by their transform')):
            root = panel_root(f'<g><circle id="local_node_0"/>{text}</g>'.encode(), 'test')
            nodes = {n.get('id'): n for n in root.iter() if n.get('id')}
            with self.subTest(text=text):
                if error is None:
                    self.assertEqual(check_bindings(nodes, 'local', display), ['local_palette_0'])
                else:
                    with self.assertRaisesRegex(ValueError, error): check_bindings(nodes, 'local', display)

    def test_tampered_artwork_is_rejected(self):
        out, _ = self.fixture()
        (out / 'panel.svg').write_text('<script/>')
        with self.assertRaisesRegex(ValueError, 'artifact identity'):
            preview_bundle(out)

    @unittest.skipUnless(shutil.which('clang++') and shutil.which('node'), 'Clang with wasm-ld and Node are required')
    def test_wavetable_larger_than_old_browser_allowance_builds_without_growth(self):
        # Retained constant data alone exceeds the old 512 KiB allowance.
        path = self.source / 'engine.cpp'
        source = path.read_text().replace('namespace example {',
            'namespace example {\nconst opxy::i32 table[160000] = {1};')
        source = source.replace('output[i] = opxy::q31::multiply(mixed, parameters[2]);',
                                'output[i] = table[voice.phase % 160000];')
        path.write_text(source)
        with patch('tooling.preview.MEMORY', 512 * 1024), self.assertRaisesRegex(ValueError, 'Preview compilation failed'):
            custom.build('clang++', self.source, self.root / 'old-memory-preview')
        # The real contract checker renders audio/display and refuses memory.grow.
        with redirect_stdout(io.StringIO()):
            custom.build('clang++', self.source, self.root / 'large-preview')
        self.assertTrue((self.root / 'large-preview/engine.wasm').is_file())

    def test_custom_artwork_cannot_load_external_resources(self):
        files = custom.source_files(self.source)
        files['panel.svg.fragment'] = files['panel.svg.fragment'].replace(
            b'fill="none"', b'fill="url(https://example.invalid/paint.svg)"', 1)
        with self.assertRaisesRegex(ValueError, 'Drawing colors'):
            custom.metadata(files)

    def test_custom_definition_cannot_reuse_curated_identity(self):
        definition, _ = custom.metadata(custom.source_files(self.source))
        # A machine with fixed numbers, and one the catalog holds the ID of.
        for identity in ('fm2op', 'karplus'):
            definition['id'] = identity
            (self.source / 'definition.json').write_text(json.dumps(definition))
            with self.subTest(identity=identity), self.assertRaisesRegex(ValueError, 'distinct local machine ID'):
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
