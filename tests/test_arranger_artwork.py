"""The stock text renderer uses transforms; SVG x/y left labels invisible on device."""
import unittest
import xml.etree.ElementTree as ET

from tooling.pipeline import ROOT


class ArrangerArtworkTests(unittest.TestCase):
    def test_every_label_uses_stock_text_positioning(self):
        root = ET.parse(ROOT / 'sdk/platform/opxy-1.2.1/arranger.svg').getroot()
        texts = list(root.iter('{http://www.w3.org/2000/svg}text'))
        self.assertTrue({'position', 'controls', 'feedback'} <= {node.get('id') for node in texts})
        for node in texts:
            with self.subTest(label=node.get('id') or node.text):
                self.assertFalse({'x', 'y'} & set(node.attrib))
                if node.get('id', '').startswith(('cliplabel', 'menurow', 'menudisabled')):
                    # Dynamic labels and menu rows start hidden; the native frame sets their matrix.
                    self.assertEqual(node.get('transform'), 'matrix(0 0 0 0 0 0)')
                else:
                    self.assertRegex(node.get('transform', ''), r'^matrix\(1 0 0 1 \d+(?:\.\d+)? \d+(?:\.\d+)?\)$')
        self.assertEqual({node.get('id') for node in texts if node.get('id', '').startswith('cliplabel')},
                         {'cliplabel' + str(index) for index in range(64)})
