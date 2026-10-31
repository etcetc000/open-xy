"""Developer grayscale declarations, bindings and authenticated preview snapshots."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tooling import custom_preview, preview
from tooling.formats import sha
from tooling.machines import machine_at


class GrayscaleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.source = self.root / 'machine'
        with redirect_stdout(io.StringIO()):
            custom_preview.scaffold(self.source, 'grayprobe', 'Gray Probe', repository=True)
        original = (self.source / 'panel.svg.fragment').read_bytes()
        self.gray = original.replace(b'#ffffff', b'#bbbbbb')
        (self.source / 'panel.svg.fragment').write_bytes(original.replace(b'#ffffff', b'#ff0000'))
        self.variant = self.source / 'panel.grayscale.svg.fragment'
        self.variant.write_bytes(self.gray)
        self.record = json.loads((self.source / 'machine.json').read_bytes())
        self.record['graphics'] = {'grayscale': self.variant.name}
        self.save()

    def save(self):
        (self.source / 'machine.json').write_text(json.dumps(self.record))

    def test_declaration_and_custom_preview_use_the_same_validated_variant(self):
        m = machine_at(self.source)
        self.assertEqual(m.grayscale, self.variant.name)
        graphics = preview.graphics_metadata(m)
        self.assertEqual(graphics['grayscale_sha256'], sha(self.gray))
        _, custom = custom_preview.metadata(custom_preview.source_files(self.source))
        self.assertEqual(custom, graphics)
        self.assertTrue(any(path == self.variant for path in m.files().values()))
        for name in ('AGENTS.md', 'README.md'):
            self.assertIn('panel.grayscale.svg.fragment', (self.source / name).read_text())

    def test_schema_paths_and_version_are_strict(self):
        for graphics in (None, {}, [], {'colorful': 'x'}, {'grayscale': '../x.svg.fragment'},
                         {'grayscale': '/x.svg.fragment'}, {'grayscale': 'panel.svg.fragment'},
                         {'grayscale': 'missing.svg.fragment'}, {'grayscale': 'engine.cpp'}):
            with self.subTest(graphics=graphics):
                self.record['graphics'] = graphics; self.save()
                with self.assertRaises(ValueError):
                    machine_at(self.source)
        self.record['graphics'] = {'grayscale': self.variant.name}
        self.record['sdk'] = '1.4'; self.save()
        with self.assertRaisesRegex(ValueError, 'needs "sdk": "1.5"'):
            machine_at(self.source)
        self.record['sdk'] = '1.5'; self.save()
        resolve = Path.resolve
        def redirected(path, *args, **kwargs):
            return resolve(path.with_name('other.svg.fragment') if path == self.variant else path, *args, **kwargs)
        with patch.object(Path, 'resolve', redirected), self.assertRaisesRegex(ValueError, 'redirected'):
            machine_at(self.source)

    def test_colored_decorations_palettes_and_incompatible_bindings_are_rejected(self):
        cases = [self.gray.replace(b'</g>', b'<circle r="2" fill="#ff0000"/></g>'),
                 self.gray.replace(b'fill="#000000"', b'fill="#00ff00"', 1),
                 self.gray.replace(b'grayprobe_node_0', b'missing_node_0'),
                 self.gray.replace(b'id="grayprobe_node_0"', b'id="grayprobe_node_0" transform="matrix(1 0 0 1 1 0)"'),
                 self.gray.replace(b'<g id=', b'<g transform="matrix(1 0 0 1 2 0)" id=', 1),
                 self.gray.replace(b'M-32 -32', b'M-31.12345 -32', 1),
                 self.gray.replace(b'M-32 -32', b'M-28 -32', 1),
                 self.gray.replace(b'</g>', b'<path id="titles" fill="#ffffff"/></g>')]
        for variant in cases:
            with self.subTest(variant=variant[:90]):
                self.variant.write_bytes(variant)
                with self.assertRaises(ValueError):
                    machine_at(self.source)
                with self.assertRaises(ValueError):
                    custom_preview.metadata(custom_preview.source_files(self.source))

    def test_preview_hashes_variant_and_rejects_tampering_and_stale_sources(self):
        m = machine_at(self.source)
        out = self.root / 'preview'; out.mkdir()
        wasm = b'\0asm\x01\0\0\0'
        manifest = dict(schema=1, kind='cpp-browser-preview', engine=m.id, root=str(m.root),
                        description=m.description, definition=m.definition, sample_rate=44100,
                        wasm_sha256=sha(wasm), sources=preview.source_hashes(m), compiler={},
                        graphics=preview.graphics_metadata(m))
        (out / 'manifest.json').write_text(json.dumps(manifest))
        (out / 'engine.wasm').write_bytes(wasm)
        (out / 'panel.svg').write_bytes((self.source / 'panel.svg.fragment').read_bytes())
        (out / 'panel.grayscale.svg').write_bytes(self.gray)
        bundle = preview.preview_bundle(out)
        self.assertEqual(bundle['/preview/panel.grayscale.svg'][1], self.gray)
        self.assertEqual(len(bundle), 4)
        (out / 'panel.grayscale.svg').write_bytes(self.gray + b' ')
        with self.assertRaisesRegex(ValueError, 'Grayscale preview artwork identity'):
            preview.preview_bundle(out)
        (out / 'panel.grayscale.svg').write_bytes(self.gray)
        self.variant.write_bytes(self.gray + b' ')
        with self.assertRaisesRegex(ValueError, 'stale'):
            preview.preview_bundle(out)
