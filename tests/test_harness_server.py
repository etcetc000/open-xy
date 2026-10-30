import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from tooling.formats import sha
from machine_fixtures import fixture
from tooling import preview
from tooling.preview import preview_bundle
FM = fixture('probesynth')
SOURCE = FM.root
source_hashes = lambda: preview.source_hashes(FM)
graphics_metadata = lambda: preview.graphics_metadata(FM)

spec = importlib.util.spec_from_file_location('serve_harness', Path(__file__).resolve().parents[1] / 'tools/serve_harness.py')
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)

class HarnessServerTests(unittest.TestCase):
    def preview_fixture(self, directory):
        module = b'\0asm\x01\0\0\0'
        definition = json.loads((SOURCE / 'definition.json').read_text(encoding='utf-8'))
        manifest = dict(schema=1, kind='cpp-browser-preview', engine=FM.id, root=str(SOURCE),
                        description=FM.description, definition=definition, sample_rate=44100,
                        wasm_sha256=sha(module), sources=source_hashes(), compiler={}, graphics=graphics_metadata())
        (directory / 'engine.wasm').write_bytes(module)
        (directory / 'panel.svg').write_bytes((SOURCE / 'panel.svg.fragment').read_bytes())
        (directory / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
        return manifest

    def test_allowlist_is_available(self):
        for url in server.ALLOWED:
            with self.subTest(url=url):
                self.assertIsNotNone(server.resolve_public_path(url))

    def test_private_and_traversal_paths_are_not_served(self):
        for url in ['/local/inputs/os.ldr', '/.git/config', '/harness/../local/inputs/os.ldr',
                    '/harness/%2e%2e/local/inputs/os.ldr', '/harness/%252e%252e/local/',
                    '/sdk/include/../../profiles/opxy-1.2.1.json',
                    '/harness/..%5clocal%5cinputs%5cos.ldr', '/README.md', '/tools/serve_harness.py']:
            with self.subTest(url=url):
                self.assertIsNone(server.resolve_public_path(url))

    def test_preview_rejects_stale_source_tampering_and_added_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            # Hosted Windows TEMP can use an 8.3 alias. Supply the canonical
            # fixture path without weakening the server's redirect rejection.
            directory = Path(temporary).resolve()
            manifest = self.preview_fixture(directory)
            self.assertEqual(set(preview_bundle(directory)), {'/preview/manifest.json', '/preview/engine.wasm', '/preview/panel.svg'})
            manifest['sources']['external/probesynth/engine.cpp'] = '0' * 64
            (directory / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'stale'): preview_bundle(directory)
            manifest = self.preview_fixture(directory)
            (directory / 'engine.wasm').write_bytes(b'\0asm\x01\0\0\0changed')
            with self.assertRaisesRegex(ValueError, 'identity'): preview_bundle(directory)
            manifest = self.preview_fixture(directory)
            (directory / 'panel.svg').write_bytes(b'<script/>')
            with self.assertRaisesRegex(ValueError, 'artwork identity'): preview_bundle(directory)
            manifest = self.preview_fixture(directory)
            manifest['files'] = {'/private': '../private.ldr'}
            (directory / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'fields'): preview_bundle(directory)
            # Every machine is a folder of its own: a preview names it by its absolute path.
            for root in (None, 'probesynth'):
                manifest = self.preview_fixture(directory)
                manifest['root'] = root
                (directory / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
                with self.subTest(root=root), self.assertRaisesRegex(ValueError, 'machine folder'):
                    preview_bundle(directory)

    def test_server_exposes_only_verified_snapshot_with_wasm_mime_and_csp(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary).resolve()
            self.preview_fixture(directory)
            bundle = preview_bundle(directory)
            # Snapshot prevents post-start replacement from being exposed.
            (directory / 'engine.wasm').write_bytes(b'changed')
            http = server.PreviewServer(('127.0.0.1', 0), server.Handler)
            http.preview = bundle
            thread = threading.Thread(target=http.serve_forever, daemon=True); thread.start()
            try:
                base = 'http://127.0.0.1:' + str(http.server_port)
                with urlopen(base + '/preview/engine.wasm') as result:
                    self.assertEqual(result.read(), b'\0asm\x01\0\0\0')
                    self.assertEqual(result.headers['Content-Type'], 'application/wasm')
                    self.assertIn("'wasm-unsafe-eval'", result.headers['Content-Security-Policy'])
                    self.assertNotIn("'unsafe-eval'", result.headers['Content-Security-Policy'])
                with urlopen(Request(base + '/preview/engine.wasm', method='HEAD')) as result:
                    self.assertEqual(result.read(), b''); self.assertEqual(result.headers['Content-Length'], '8')
                for name in ('/preview/../engine.wasm', '/preview/vectors.json', '/local/inputs/os.ldr'):
                    with self.assertRaises(HTTPError) as error: urlopen(base + name)
                    self.assertEqual(error.exception.code, 404)
                    error.exception.close()
            finally:
                http.shutdown(); http.server_close(); thread.join(timeout=5)

    def test_stale_preview_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary).resolve()
            manifest = self.preview_fixture(directory)
            self.assertEqual(len(preview_bundle(directory, require_reference=True)), 3)
            manifest['sources']['external/probesynth/engine.cpp'] = '0' * 64
            (directory / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'stale'):
                preview_bundle(directory)

if __name__ == '__main__':
    unittest.main()
