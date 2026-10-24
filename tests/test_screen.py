"""Platform SVG contract; host structure checks are not hardware rendering."""
import re
import unittest
import xml.etree.ElementTree as ET

from tooling import native, screen
from tooling.pipeline import ROOT


class PlatformScreenTests(unittest.TestCase):
    def test_sdk_header_and_machine_geometry_compose_without_stock_artwork(self):
        fragment = b'<g id="machine"><rect x="0" y="25" width="480" height="195" fill="#000000"/></g>'
        root = ET.fromstring(screen.compose(fragment))
        self.assertEqual([n.get('id') for n in root], ['bg', 'bar', 'values', 'titles', 'machine'])
        for name in ('titles', 'values'):
            group = next(n for n in root if n.get('id') == name)
            self.assertEqual(len(group), 4)
            self.assertTrue(all(len(n) == 1 and n[0].tag == screen.NS + 'text' for n in group))

    def test_missing_control_and_wrong_dimensions_fail_closed(self):
        original = screen.template()
        root = ET.fromstring(original)
        values = next(n for n in root if n.get('id') == 'values')
        values.remove(values[0])
        with self.assertRaisesRegex(ValueError, 'four values'):
            screen.validate_template(ET.tostring(root))
        root = ET.fromstring(original)
        bar = next(n for n in root if n.get('id') == 'bar')
        bar.remove(bar[0])
        with self.assertRaisesRegex(ValueError, 'header bar'):
            screen.validate_template(ET.tostring(root))
        with self.assertRaisesRegex(ValueError, 'dimensions'):
            screen.validate_template(original.replace(b'0 0 480 220', b'0 0 480 224'))

    def test_fragment_cannot_shadow_platform_controls(self):
        with self.assertRaisesRegex(ValueError, 'Duplicate SVG binding IDs'):
            screen.compose(b'<g id="titles"/>')


class BrowserSubset(unittest.TestCase):
    """The browser preview copies the elements the firmware subset admits and parses its numbers alike."""

    def test_display_preview_matches_the_authored_subset(self):
        source = (ROOT / 'sdk/browser/display.mjs').read_text(encoding='utf-8')
        elements = re.search(r"const elements = new Set\(\[([^\]]*)\]\)", source).group(1)
        self.assertEqual(set(re.findall(r"'([a-z]+)'", elements)), native.SVG_ELEMENTS)
