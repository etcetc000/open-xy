"""Generated startup glue: its inputs and how it reaches stock.

Its behavior runs on the host in test_sample_services.py (fixtures/startup.cpp).
"""
import re
import unittest

from tooling.native import DEFINITIONS
from tooling.startup import MAX_ENGINES, render


class Startup(unittest.TestCase):
    def test_stock_is_reached_only_through_link_time_definitions(self):
        # Browser kits relink this object, so it names stock and machine IDs as symbols.
        text = render(['probesynth', 'probesources', 'probesample'], ['probefx'], ['probesample'])
        self.assertIsNone(re.search(r'0x[0-9a-fA-F]+', text))
        stock = set(re.findall(r'__asm__\("(stock_\w+)"\)', text))
        self.assertEqual(stock, {'stock_catalog_insert', 'stock_gui_role'})
        self.assertLessEqual(stock, set(DEFINITIONS))
        for prefix in ('probesynth', 'probesources', 'probesample'):
            self.assertIn(f'__asm__("{prefix}_engine_id")', text)
        self.assertNotIn('probesample_screen_register', text)

    def test_step_components_alone_register_no_engines(self):
        text = render([])
        self.assertNotIn('_register()', text)
        self.assertNotIn('_name_matches(name)', text)

    def test_rejects_invalid_machine_lists(self):
        for prefixes in (['probesynth', 'probesynth'], ['Bad'], ['x%d' % i for i in range(MAX_ENGINES + 1)]):
            with self.subTest(prefixes=prefixes[:3]), self.assertRaises(ValueError):
                render(prefixes)
        for effects, stock_page in ((['probesynth'], ()), ((), ['other'])):
            with self.subTest(effects=effects), self.assertRaises(ValueError):
                render(['probesynth'], effects, stock_page)


if __name__ == '__main__':
    unittest.main()
