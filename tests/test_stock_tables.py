"""Relocated engine-record, screen-slot and effect-record tables: profile, layout and templates.

No Blackfin code is compiled here. These checks keep the profile edits, the
linker layout and the stock side of registration consistent with the audited
constants in tooling/stock_tables.py.
"""
import copy
import json
from pathlib import Path
import tempfile
import unittest

from tooling import stock_tables as tables
from tooling.adapters import generate_registration_stock
from tooling.firmware import hooks
from tooling.pipeline import config

ROOT = Path(__file__).resolve().parents[1]
PROFILE = 'profiles/opxy-1.2.0.json'
LINKER = 'linker/opxy-1.2.0.ld'


def read_text(relative):
    return (ROOT / relative).read_text(encoding='utf-8')


class Profile(unittest.TestCase):
    def setUp(self):
        self.profile = config()
        self.table = [p for p in self.profile['patches'] if p['kind'] == 'table']

    def test_profile_agrees_with_the_audit(self):
        tables.check_profile(self.profile)

    def test_edits_change_only_immediates_outside_hooks(self):
        for patch, (site, kind, stock, relocated) in zip(self.table, tables.EDITS):
            self.assertEqual(tables.decode(kind, bytes.fromhex(patch['before']))[1], stock)
            self.assertEqual(tables.decode(kind, bytes.fromhex(patch['after']))[1], relocated)
            self.assertNotIn(site, hooks(self.profile))

    def test_altered_edit_or_bound_is_rejected(self):
        for index, value in ((0, '02da4c8d64ff0000'), (7, 'f161'), (len(self.table) - 1, '20e18100')):
            altered = copy.deepcopy(self.table)
            altered[index]['after'] = value
            with self.subTest(index=index), self.assertRaises(ValueError):
                tables.check_edits(altered)
        with self.assertRaises(ValueError):
            tables.check_edits(self.table[:-1])
        for key, value in (('engines', 64), ('engine_none', 32), ('pages', 127), ('page_none', 80),
                           ('effects', tables.free_effects() + [19]), ('effects', tables.free_effects() + [9])):
            altered = copy.deepcopy(self.profile)
            altered['identities'][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                tables.check_profile(altered)
        with self.assertRaises(ValueError):
            tables.check_profile(dict(self.profile, table_bytes=self.profile['table_bytes'] + 4))

    def test_compositor_bound_must_be_hooked(self):
        hooked = [p for p in self.profile['patches'] if p['kind'] == 'hook']
        tables.check_compositor(hooked)
        site = '%#x' % tables.COMPOSITOR_ADVANCE
        for change in (dict(symbol='sdk_other'), dict(before='0f6420e180008708'), dict(address='0x8d5208fc')):
            altered = copy.deepcopy(hooked)
            next(p for p in altered if p['address'] == site).update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                tables.check_compositor(altered)

    def test_effect_edits_cover_every_audited_reader(self):
        effects = [p for p in self.table if p['name'].startswith('IN-MEMORY effect')]
        self.assertEqual([int(p['address'], 16) for p in effects],
                         [*tables.EFFECTS.bases, *(site for site, _, _ in tables.EFFECTS.bounds)])
        self.assertEqual(tables.free_effects(), [1, 2, 3, 4, 6, 7, 8, 11, *range(20, 63)])

    def test_imm7_bound_is_the_largest_encodable(self):
        self.assertEqual(tables.decode('imm7', bytes.fromhex('f961')), (0x6001, 63))
        self.assertEqual(tables.decode('imm7', bytes.fromhex('0962')), (0x6001, -63))


class Layout(unittest.TestCase):
    def test_linker_places_tables_at_audited_addresses(self):
        linker = read_text(LINKER)
        for table in tables.TABLES:
            self.assertIn('__stock_%s = %#x;' % (table.name.replace(' ', '_'), table.address), linker)
        self.assertIn('LENGTH = %#x' % (tables.TABLES_START - config()['module_base']), linker)
        self.assertEqual(tables.EFFECT_RECORDS, tables.TABLES_START)
        self.assertEqual(tables.SCREEN_SLOTS + tables.SCREEN_COUNT * 4, tables.ENGINE_RECORDS)
        tables.check_layout()

    def test_overlapping_tables_are_rejected(self):
        moved = tables.EFFECTS._replace(address=tables.SCREEN_SLOTS - 48)
        original = tables.TABLES
        try:
            tables.TABLES = (tables.ENGINES, tables.SCREENS, moved)
            with self.assertRaises(ValueError):
                tables.check_layout()
        finally:
            tables.TABLES = original


class Registration(unittest.TestCase):
    def test_stock_side_names_only_relocated_tables(self):
        with tempfile.TemporaryDirectory() as directory:
            text = generate_registration_stock(directory).read_text(encoding='utf-8')
        self.assertIn('const Registry kEngines = {%#x, %d, %d,' % (tables.ENGINE_RECORDS, tables.ENGINE_COUNT,
                                                                     tables.ENGINE_SENTINEL), text)
        self.assertIn('const Registry kEffects = {%#x, %d, %d,' % (tables.EFFECT_RECORDS, tables.EFFECT_COUNT,
                                                                     tables.EFFECT_SENTINEL), text)
        self.assertIn('const Pages kPages = {%d,' % tables.SCREEN_COUNT, text)
        self.assertIn('%#x' % tables.SCREEN_SLOTS, text)
        self.assertNotIn('%#x' % tables.STOCK_ENGINE_RECORDS, text)
        self.assertNotIn('%#x' % tables.STOCK_SCREEN_SLOTS, text)
        self.assertNotIn('%#x' % tables.STOCK_EFFECT_RECORDS, text)


if __name__ == '__main__':
    unittest.main()
