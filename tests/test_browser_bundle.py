"""Public checks for the menu embedded in the browser export."""
import base64
import json
from pathlib import Path
import re
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

    def test_modified_menu_cannot_produce_an_export(self):
        kit = dict(profile={'stock_catalog_sha256': sha(b'original')}, notices='Test',
                   stockCatalog=base64.b64encode(b'changed').decode('ascii'), screenTemplate=template().decode('utf-8'))
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'Bundled stock catalog hash mismatch'):
                write_builder(kit, Path(directory))
            self.assertFalse((Path(directory) / 'builder.pending.html').exists())


if __name__ == '__main__':
    unittest.main()
