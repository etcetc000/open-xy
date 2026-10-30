"""The stock instrument catalog kept for development is the one the profile authenticates."""
import json
import unittest

from tooling.formats import sha
from tooling.native import STOCK_CONTENT
from tooling.pipeline import config, read


class StockCatalog(unittest.TestCase):
    def test_the_repository_copy_is_the_profiles_catalog(self):
        data = read(STOCK_CONTENT / 'op1_factory.json')
        self.assertEqual(sha(data), config()['stock_catalog_sha256'])
        self.assertIn('engine_types', json.loads(data))

    def test_the_folder_holds_the_catalog_alone(self):
        # Firmware and every other stock file come from the user's device.
        self.assertEqual(sorted(p.name for p in STOCK_CONTENT.iterdir()), ['op1_factory.json'])


if __name__ == '__main__':
    unittest.main()
