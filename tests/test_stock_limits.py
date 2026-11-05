"""The sample-length limit edits: profile, audited sites and the SDK's mirror of the cap."""
import copy
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import unittest

from tooling import native, stock_limits as limits
from tooling.extensions import capabilities, extension_at, metadata
from tooling.firmware import active_services, patch_active
from tooling.formats import ldr, mapped_offset
from tooling.pipeline import check_patch_boundaries, config

ROOT = Path(__file__).resolve().parents[1]


def limit_edits(profile):
    return [p for p in profile['patches'] if p['kind'] == 'limit' and p.get('service') == limits.SERVICE]


class Profile(unittest.TestCase):
    def test_profile_raises_exactly_the_audited_sites(self):
        profile = config()
        limits.check_profile(profile)
        self.assertEqual([int(p['address'], 16) for p in limit_edits(profile)], [a for a, _ in limits.EDITS])

    def test_the_raised_cap_stays_below_the_sample_object_ceiling(self):
        self.assertEqual(limits.RAISED_FRAMES, (1 << 20) - 1)
        self.assertGreater(limits.RAISED_FRAMES, limits.STOCK_FRAMES)

    def test_rejects_other_values_registers_and_sites(self):
        profile = config()
        edit = next(i for i, p in enumerate(profile['patches']) if p['kind'] == 'limit')
        stock = profile['patches'][edit]['before']
        for change in (dict(after=stock[:4] + '1000' + '0000' + '0000'),   # 2^20: past the ceiling
                       dict(after='07' + profile['patches'][edit]['after'][2:]),  # another register
                       dict(before=stock[:4] + '0e00' + stock[8:]),        # a different stock value
                       dict(address='0x8d500000')):
            altered = copy.deepcopy(profile)
            altered['patches'][edit].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                limits.check_profile(altered)
        altered = copy.deepcopy(profile)
        altered['patches'] = [p for p in altered['patches'] if p.get('service') != limits.SERVICE]
        with self.assertRaises(ValueError):
            limits.check_profile(altered)

    def test_sdk_mirrors_the_raised_cap(self):
        text = (ROOT / 'sdk/platform/opxy-1.2.1/stock_sample.hpp').read_text(encoding='utf-8')
        self.assertRegex(text, r'const u32 kLoaderFrames = \(1u << 20\) - 1;')
        self.assertNotIn('882000', text)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_stock_holds_the_audited_preimages(self):
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        blocks = ldr(stock)
        for patch in limit_edits(config()):
            offset = mapped_offset(blocks, int(patch['address'], 16), patch['length'])
            self.assertEqual(int(patch['file_offset'], 16), offset)
            self.assertEqual(stock[offset:offset + patch['length']].hex(), patch['before'])
        # Every other stock load of 882,000 belongs to the recorder and stays stock.
        sites = {int(p['file_offset'], 16) for p in limit_edits(config())}
        # Any Dreg/Preg = imm32 form (opcodes 0xda00..0xda3f, as stock_tables.decode accepts).
        others = [m.start() for m in re.finditer(rb'[\x00-\x3f]\xda\x0d\x00\x50\x75\x00\x00', stock)
                  if m.start() not in sites]
        self.assertEqual(len(others), 3)
        # Nor is 882,000 held as a data word, split into half-word loads, or derived as a
        # byte count or off-by-one bound anywhere in stock.
        for value in (882000, 881999, 882001, 882000 * 2, 882000 * 4):
            self.assertNotIn(value.to_bytes(4, 'little'), stock)
        self.assertIsNone(re.search(rb'[\x00-\xff]\xe1\x50\x75', stock))  # Reg.L = 0x7550


class Service(unittest.TestCase):
    """The edits apply only with an extension that declares the long_samples capability."""

    def folder(self, data, files=()):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root)
        (root / 'ext').mkdir()
        (root / 'ext/extension.json').write_text(json.dumps(data), encoding='utf-8')
        for name in files:
            shutil.copy(ROOT / 'tests/fixtures/extension_probe' / name, root / 'ext' / name)
        return root / 'ext'

    def settings(self, **fields):
        return self.folder({'id': 'longer', 'name': 'Longer samples', 'description': 'Loads longer samples.',
                            'claims': [], 'actions': [], 'capabilities': ['long_samples'], **fields})

    def probe(self, **fields):
        data = json.loads((ROOT / 'tests/fixtures/extension_probe/extension.json').read_text(encoding='utf-8'))
        return self.folder({**data, **fields}, ['extension.cpp'])

    def services(self, *extensions_):
        class Machines(list):
            extensions = extensions_
        return Machines(), active_services(config(), native.service_users(Machines()))

    def test_only_a_declaring_extension_activates_the_edits(self):
        plain, settings = extension_at(self.probe()), extension_at(self.settings())
        self.assertEqual((plain.capabilities, plain.linked), ((), True))
        self.assertEqual((settings.capabilities, settings.linked), (('long_samples',), False))
        for extension, active in ((plain, False), (settings, True)):
            services = self.services(extension)[1]
            self.assertEqual({patch_active(p, services) for p in limit_edits(config())}, {active})
        row = metadata([settings], {'longer': 'personal'})[0]
        self.assertEqual((row['capabilities'], row['linked']), (['long_samples'], False))
        self.assertFalse({'capabilities', 'linked'} & set(metadata([plain], {'probe': 'personal'})[0]))

    def test_a_settings_extension_links_no_code_or_runtime(self):
        machines, services = self.services(extension_at(self.settings()))
        self.assertNotIn('extensions', services)
        self.assertEqual(native.linked_extensions(machines), ())
        # An extension with code may declare a capability too, and links as usual.
        machines, services = self.services(extension_at(self.probe(capabilities=['long_samples'])))
        self.assertTrue({'extensions', limits.SERVICE} <= services)
        self.assertEqual(len(native.linked_extensions(machines)), 1)

    def test_a_settings_extension_holds_only_capabilities(self):
        for change in (dict(capabilities=[]), dict(capabilities=['missing']), dict(capabilities=['long_samples'] * 2),
                       dict(capabilities='long_samples'), dict(claims=['record_double_tap'])):
            with self.subTest(change=change), self.assertRaises(ValueError):
                extension_at(self.settings(**change))
        stray = self.settings()
        (stray / 'helper.cpp').write_text('int x;', encoding='utf-8')
        with self.assertRaises(ValueError):
            extension_at(stray)
        without = self.settings()
        data = json.loads((without / 'extension.json').read_text(encoding='utf-8'))
        data.pop('capabilities')
        (without / 'extension.json').write_text(json.dumps(data), encoding='utf-8')
        with self.assertRaises(ValueError):
            extension_at(without)

    def test_capabilities_are_fixed_edit_services(self):
        self.assertEqual(capabilities(), (limits.SERVICE, 'release_floor'))

    def test_an_inactive_build_leaves_the_sites_stock(self):
        profile = copy.deepcopy(config())
        for patch in limit_edits(profile):
            patch['after'] = patch['before']
        check_patch_boundaries(profile, len(profile['patches']))
        untagged = copy.deepcopy(config())
        limit_edits(untagged)[0].pop('service')
        with self.assertRaises(ValueError):
            limits.check_profile(untagged)

if __name__ == '__main__':
    unittest.main()
