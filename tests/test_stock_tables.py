"""Relocated engine-record, screen-slot and effect-record tables: profile, layout and templates.

No Blackfin code is compiled here. These checks keep the profile edits, the
linker layout and the stock side of registration consistent with the audited
constants in tooling/stock_tables.py.
"""
import copy
import json
import os
from pathlib import Path
import tempfile
import unittest

from tooling import pipeline, projects, stock_tables as tables
from tooling.adapters import generate_registration_stock
from tooling.firmware import hooks
from tooling.formats import ldr, mapped_offset
from tooling.machines import FIXED_NUMBERS, number_pools
from tooling.pipeline import config

ROOT = Path(__file__).resolve().parents[1]
PROFILE = 'profiles/opxy-1.2.1.json'
LINKER = 'linker/opxy-1.2.1.ld'


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
        for index, value in ((0, '02da4c8d64ff0000'), (7, '22e1d00b'), (len(self.table) - 1, '20e18100')):
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
        for change in (dict(symbol='sdk_other'), dict(before='0f6420e180008708'), dict(address='0x8d5214c4')):
            altered = copy.deepcopy(hooked)
            next(p for p in altered if p['address'] == site).update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                tables.check_compositor(altered)

    def test_engine_bound_must_be_hooked(self):
        # imm7 cannot hold the relocated engine count: the validity check's bound is a call.
        hooked = [p for p in self.profile['patches'] if p['kind'] == 'hook']
        tables.check_engine_bound(hooked)
        self.assertNotIn(tables.ENGINE_BOUND, [site for site, _, _, _ in tables.EDITS])
        site = '%#x' % tables.ENGINE_BOUND
        for change in (dict(symbol='sdk_other'), dict(before='f9618808'), dict(length=8), dict(call=False),
                       dict(service='samples'), dict(profiling=False)):
            altered = copy.deepcopy(hooked)
            next(p for p in altered if p['address'] == site).update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                tables.check_engine_bound(altered)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_engine_bound_hook_sits_inside_a_framed_check(self):
        # The call replaces RETS; the check saves it with LINK first and restores it with
        # UNLINK on both exits, and only its own branches (to 0x8d50535c and 0x8d505364)
        # land after the site, never between the two hooked instructions.
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        start = 0x8d50534e
        expected = (
                         '800c' '00e80000' '0418' '0961' '8808' '0518'  # CC = R0 < 0; LINK 0; IF CC; R1 = 33; CC = R0 < R1; IF CC
                         '01e80000' '0060' '1000'                       # UNLINK; R0 = 0; RTS
                         '8161' '02da0308fc940000' 'c840' '0250' '0832'  # R1 = 48; R2 = records; R0 *= R1; R0 += R2; P1 = R0
                         '88e51000' '000c' 'f11b'                       # R0 = B[P1 + 16]; CC = R0 == 0; IF CC -> UNLINK above
                         '01e80000' '0860' '1000')                      # UNLINK; R0 = 1; RTS
        size = len(expected) // 2
        offset = mapped_offset(ldr(stock), start, size)
        self.assertEqual(stock[offset:offset + size].hex(), expected)
        self.assertEqual(tables.ENGINE_BOUND, start + 8)

    def test_effect_edits_cover_every_audited_reader(self):
        effects = [p for p in self.table if p['name'].startswith('IN-MEMORY effect')]
        self.assertEqual([int(p['address'], 16) for p in effects],
                         [*tables.EFFECTS.bases, *(site for site, _, _ in tables.EFFECTS.bounds)])
        # Stock reads a send track's effect ID as an engine: no stock engine ID or "none".
        self.assertEqual(tables.free_effects(), [1, 8, 11, 21, *range(23, 29), *range(34, 63)])
        self.assertEqual(sorted(tables.ENGINE_LOOKALIKES),
                         sorted(pipeline.config()['identities']['stock_engines'] + [tables.ENGINE_SENTINEL]))

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
        self.assertEqual(tables.EFFECT_RECORDS + tables.EFFECT_COUNT * tables.RECORD_BYTES, tables.ENGINE_RECORDS)
        self.assertEqual(tables.ENGINE_RECORDS + tables.ENGINE_COUNT * tables.RECORD_BYTES, tables.SCREEN_SLOTS)
        tables.check_layout()

    def test_overlapping_tables_are_rejected(self):
        moved = tables.EFFECTS._replace(address=tables.ENGINE_RECORDS - 32)
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
        # The engine validity hook compares against the relocated count.
        self.assertIn('R1 = %d (X);' % tables.ENGINE_COUNT, text)

    def test_a_companion_lists_every_machine_one_firmware_numbers(self):
        # Effects above 33 hold engine numbers too, so engines and effects together never
        # outnumber the engine pool plus the low effect numbers and the fixed engines.
        rules = config()['identities']
        pools = number_pools(rules)
        low_effects = [n for n in pools['effect'] if n not in pools['engine']]
        fixed_engines = [i['engine'] for i in FIXED_NUMBERS.values() if 'engine' in i and i['engine'] not in pools['engine']]
        self.assertLessEqual(len(pools['engine']) + len(low_effects) + len(fixed_engines), projects.MACHINE_LIMIT)

    def test_project_companions_map_every_engine_number(self):
        # Both companion readers hold a map entry for every engine record.
        self.assertEqual((projects.NUMBERS, projects.EFFECT_NUMBERS), (tables.ENGINE_COUNT, tables.EFFECT_COUNT))
        source = read_text('sdk/platform/opxy-1.2.1/project_companion.cpp')
        self.assertIn('const u32 kNumbers = %d;' % tables.ENGINE_COUNT, source)
        self.assertIn('const u32 kEffectNumbers = %d;' % tables.EFFECT_COUNT, source)


if __name__ == '__main__':
    unittest.main()
