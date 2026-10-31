"""Public checks for the menu embedded in the browser export."""
import base64
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tooling.browser_kit import write_builder
from tooling.formats import sha
from tooling.screen import template


class BrowserBundleTests(unittest.TestCase):
    def test_embedded_menu_preserves_authenticated_bytes(self):
        # Authored fixture; no stock content is needed by public tests.
        menu = b'{"engine_types":[],"fx_types":[]}\n'
        kit = dict(profile={'stock_catalog_sha256': sha(menu)}, notices='Test',
                   stockCatalog=base64.b64encode(menu).decode('ascii'), screenTemplate=template().decode('utf-8'))
        with tempfile.TemporaryDirectory() as directory:
            write_builder(kit, Path(directory))
            html = (Path(directory) / 'builder.pending.html').read_text(encoding='utf-8')
            embedded = json.loads(re.search(
                r'<script id="kit-data" type="application/json">(.*?)</script>', html, re.S).group(1))
            self.assertEqual(base64.b64decode(embedded['stockCatalog']), menu)
            self.assertEqual(kit['notices'], 'Test')
            # The offline page must not depend on external visual modules.
            self.assertIn('const Visuals = (() => {', html)
            self.assertIn('const {presetFiles, sha256} = Core;', html)
            self.assertNotRegex(html, r"import \* as \w+ from")
            inline = re.search(r'<script type="module">(.*?)</script>', html, re.S).group(1)
            subprocess.run(['node', '--input-type=module', '--check'], input=inline, text=True,
                           check=True, capture_output=True, timeout=30)
            # Run the offline modules without mounting the UI. Their factories
            # must be present in file:// exports as well as the hosted builder.
            factories = inline.rsplit('await createBuilder({audition:Audition});', 1)[0]
            subprocess.run(['node', '--input-type=module'], input=factories + '''
const config = createGraphicsConfiguration();
const machine = {id:'offline', artwork:'<circle fill="#ff0000"/>'};
if (!config.setMachine(machine, 'grayscale') || !config.summary([machine]).enabled)
  throw new Error('Offline per-machine configuration differs');
if (!config.setAll([machine], 'colorful') || config.summary([machine]).enabled)
  throw new Error('Offline bulk configuration differs');
if (typeof createSnapshotService !== 'function' || typeof createGraphicsDialog !== 'function')
  throw new Error('Offline preview factories missing');
''', text=True, check=True, capture_output=True, timeout=30)
            # The file:// installer must expose the same converter its app and
            # previews call, rather than only including it inside Core's closure.
            core = html.split('const Graphics = (() => {', 1)[1].split(
                'const {editedShown, shownControls, inputWords}', 1)[0]
            core = 'const Graphics = (() => {' + core
            check = '''
const svg = '<circle fill="#ff0000" stroke="#0000ff"/>';
if (Core.artworkForGraphics(svg) !== svg) throw new Error('Colorful changed');
if (Core.artworkForGraphics(svg, 'grayscale') !== '<circle fill="#f8f8f8" stroke="#606060"/>')
  throw new Error('Offline grayscale differs');
const variant = '<circle fill="#bbbbbb" stroke="#777777"/>';
if (Core.selectArtwork({artwork:svg, grayscaleArtwork:variant}, 'grayscale') !== variant)
  throw new Error('Offline authored grayscale differs');
'''
            subprocess.run(['node', '--input-type=module'], input=core + check, text=True,
                           check=True, capture_output=True, timeout=30)

    def test_modified_menu_cannot_produce_an_export(self):
        kit = dict(profile={'stock_catalog_sha256': sha(b'original')}, notices='Test',
                   stockCatalog=base64.b64encode(b'changed').decode('ascii'), screenTemplate=template().decode('utf-8'))
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'Bundled stock catalog hash mismatch'):
                write_builder(kit, Path(directory))
            self.assertFalse((Path(directory) / 'builder.pending.html').exists())


if __name__ == '__main__':
    unittest.main()
