"""A subset's effect IDs must come from its own Python allocation."""
from dataclasses import replace
import json
import unittest

from tests.machine_fixtures import fixture
from tooling.adapters import catalog_entry
from tooling.machines import assign_numbers
from tools.check_browser_firmware import selection_catalog


class BrowserFirmwareCatalogTests(unittest.TestCase):
    def test_effect_collision_is_renumbered_for_subset(self):
        template = fixture('probefx')
        # Find two authored IDs whose preferred effect number collides. This
        # exercises the allocation contract without hard-coding today's hashes.
        seen = {}
        pair = None
        for index in range(100):
            machine = replace(template, id='catalogprobe' + str(index),
                              identity={'effect': None, 'page': 21}, requested=None)
            alone, _ = assign_numbers([machine])
            number = alone[0].identity['effect']
            if number in seen:
                pair = [seen[number], machine]
                break
            seen[number] = machine
        self.assertIsNotNone(pair)
        full, _ = assign_numbers(pair)
        singles = {m.id: assign_numbers([m])[0][0] for m in pair}
        changed = next(m for m in full if m.identity != singles[m.id].identity)
        stock = {'engine_types': [], 'fx_types': [{'type': 'stock', 'id': 0}]}
        actual = selection_catalog(full, [changed.id], stock)
        self.assertEqual(actual['fx_types'][-1], json.loads(catalog_entry(singles[changed.id]).strip().lstrip(',')))
        self.assertNotEqual(actual['fx_types'][-1]['id'], changed.identity['effect'])
        self.assertEqual(stock['fx_types'], [{'type': 'stock', 'id': 0}])

    def test_full_selection_preserves_catalog_order_and_defaults(self):
        machines, _ = assign_numbers([fixture('probesynth'), fixture('probefx')])
        expected = {'engine_types': [], 'fx_types': []}
        for m in machines:
            expected['fx_types' if m.kind == 'effect' else 'engine_types'].append(
                json.loads(catalog_entry(m).strip().lstrip(',')))
        self.assertEqual(selection_catalog(machines, [m.id for m in reversed(machines)],
                                           {'engine_types': [], 'fx_types': []}), expected)
