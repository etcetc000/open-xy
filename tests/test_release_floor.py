"""The release_floor capability: audited edits, its setting and applying it to an image."""
import copy
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest

from tooling import native, release_floor as floor, settings as image_settings
from tooling.extensions import capability_settings, extension_at, metadata, setting_edits
from tooling.firmware import active_services, patch_active
from tooling.formats import header, ldr
from tooling.pipeline import ROOT, check_patch_boundaries, config, document
from tooling.stock_tables import decode

REGISTRY = document(ROOT / 'profiles/extensions.json')['capabilities']


def floor_edits(profile):
    return [p for p in profile['patches'] if p.get('service') == floor.SERVICE]


def folder(test, **fields):
    root = Path(tempfile.mkdtemp())
    test.addCleanup(shutil.rmtree, root)
    data = {'id': 'idle', 'name': 'Idle threshold', 'description': 'Ends quiet release tails sooner.',
            'claims': [], 'actions': [], 'capabilities': ['release_floor'], **fields}
    (root / 'extension.json').write_text(json.dumps(data), encoding='utf-8')
    return root


class Profile(unittest.TestCase):
    def test_profile_holds_the_audited_edits(self):
        floor.check_profile(config(), REGISTRY)
        floor_edit, stage_edit = floor_edits(config())
        self.assertEqual((int(floor_edit['address'], 16), int(stage_edit['address'], 16)),
                         (floor.FLOOR_SITE, floor.STAGE_SITE))

    def test_every_value_has_reviewed_bytes(self):
        choices = floor_edits(config())[0]['choices']
        self.assertEqual(sorted(map(int, choices)), list(range(-66, -19)))
        self.assertEqual(choices['-66'], floor_edits(config())[0]['before'])  # stock exactly
        for db, raw in choices.items():
            opcode, value = decode('imm32', bytes.fromhex(raw))
            self.assertEqual((opcode, value), (0xda01, floor.floor(int(db))))
        # Stock's own 0.1 and 0.01 constants land on -20 and -40 dB.
        self.assertEqual((floor.floor(-20), floor.floor(-40)), (214748365, 21474836))

    def test_rejects_other_values_and_sites(self):
        profile = config()
        index = profile['patches'].index(floor_edits(profile)[0])
        for change in (dict(after=floor_edits(profile)[0]['choices']['-30']),
                       dict(choices={**floor_edits(profile)[0]['choices'], '-19': '01dacc0ccdcc0000'}),
                       dict(address='0x8d50e264')):
            altered = copy.deepcopy(profile)
            altered['patches'][index].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                floor.check_profile(altered, REGISTRY)
        altered = copy.deepcopy(profile)
        altered['patches'][index + 1]['after'] = '3860'  # stage 7 would start a stolen note
        with self.assertRaises(ValueError):
            floor.check_profile(altered, REGISTRY)
        for value in (-67, -19, -40.0, '-40'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                floor.floor(value)

    def test_registry_setting(self):
        self.assertEqual(capability_settings(), {'release_floor': floor.SPEC})
        self.assertEqual(setting_edits(config(), {'release_floor': -40}),
                         {'0x8d50e226': floor_edits(config())[0]['choices']['-40']})
        self.assertEqual(setting_edits(config(), {}), {'0x8d50e226': floor_edits(config())[0]['after']})

    def test_an_inactive_build_leaves_the_sites_stock(self):
        profile = copy.deepcopy(config())
        for patch in floor_edits(profile):
            patch['after'] = patch['before']
        check_patch_boundaries(profile, len(profile['patches']))

    def test_every_limit_edit_names_its_service(self):
        profile = copy.deepcopy(config())
        floor_edits(profile)[1].pop('service')
        with self.assertRaises(ValueError):
            check_patch_boundaries(profile, len(profile['patches']))


class Extension(unittest.TestCase):
    def test_settings_choose_a_value_in_range(self):
        extension = extension_at(folder(self, settings={'release_floor': -40}))
        self.assertEqual((extension.settings, extension.linked), ({'release_floor': -40}, False))
        row = metadata([extension], {'idle': 'personal'})[0]
        self.assertEqual(row['settings'], {'release_floor': dict(floor.SPEC, value=-40)})
        self.assertEqual(metadata([extension_at(folder(self))], {'idle': 'personal'})[0]['settings']['release_floor']['value'],
                         floor.DEFAULT)
        for settings in ({'release_floor': -10}, {'release_floor': -40.5}, {'long_samples': 1}, {}, []):
            with self.subTest(settings=settings), self.assertRaises(ValueError):
                extension_at(folder(self, settings=settings))

    def test_only_a_declaring_extension_activates_the_edits(self):
        class Machines(list):
            extensions = (extension_at(folder(self, settings={'release_floor': -30})),)
        services = active_services(config(), native.service_users(Machines()))
        self.assertEqual({patch_active(p, services) for p in floor_edits(config())}, {True})
        self.assertEqual(native.build_settings(Machines()), {'release_floor': -30})
        self.assertNotIn(floor.SERVICE, active_services(config(), native.service_users([])))

    def test_the_first_declaring_extension_chooses_as_the_builder_does(self):
        class Machines(list):
            extensions = (extension_at(folder(self)), extension_at(folder(self, settings={'release_floor': -30})))
        # The first declares no value, so its default wins, as builder/core.mjs chosenSettings.
        self.assertEqual(native.build_settings(Machines()), {'release_floor': floor.DEFAULT})


class Image(unittest.TestCase):
    """Applying the capability to an image: a synthetic loader holding the stock bytes at
    every capability site, standing in for the private OS."""

    def image(self):
        profile = config()
        edits = [p for p in profile['patches'] if p.get('service') in ('long_samples', 'release_floor')]
        records = [header(0x5004, 0, 0, 0)]
        for patch in edits:
            records.append(header(1, int(patch['address'], 16), patch['length'], 0) + bytes.fromhex(patch['before']))
        records.append(header(0x8004, 0, 0, 0))
        data = bytearray(b''.join(records))
        # Point each edit at its record and pad the image to the profile's sizes.
        fake = copy.deepcopy(profile)
        offset = 16
        for patch in fake['patches']:
            if patch in edits or any(patch['address'] == e['address'] for e in edits):
                patch['file_offset'] = hex(offset + 16)
                offset += 16 + patch['length']
        fake['stock_bytes'], fake['stock_records'] = len(data), len(records)
        return bytes(data), fake

    def test_apply_and_show(self):
        data, profile = self.image()
        self.assertEqual(image_settings.state(data, profile), {'long_samples': None, 'release_floor': None})
        extension = extension_at(folder(self, settings={'release_floor': -40}))
        result, values = image_settings.apply(data, [extension], {}, profile)
        self.assertEqual(values, {'release_floor': -40})
        self.assertEqual(image_settings.state(result, profile), {'long_samples': None, 'release_floor': -40})
        self.assertEqual(sum(a != b for a, b in zip(data, result)), 5)
        again, values = image_settings.apply(result, [extension], {'release_floor': -66}, profile)
        # -66 is stock's floor bytes; the stage edit tells it from stock.
        self.assertEqual(image_settings.state(again, profile)['release_floor'], -66)
        partial = bytearray(result)
        stage = floor_edits(profile)[1]
        partial[int(stage['file_offset'], 16):int(stage['file_offset'], 16) + 2] = bytes.fromhex(stage['before'])
        with self.assertRaises(ValueError):
            image_settings.state(bytes(partial), profile)  # floor raised, stage stock: partly applied
        with self.assertRaises(ValueError):
            image_settings.apply(data, [extension], {'release_floor': -10}, profile)
        with self.assertRaises(ValueError):
            image_settings.apply(data, [extension], {'long_samples': 1}, profile)
        corrupt = bytearray(data)
        site = int(floor_edits(profile)[1]['file_offset'], 16)
        corrupt[site] ^= 0xff
        with self.assertRaises(ValueError):
            image_settings.state(bytes(corrupt), profile)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_stock_holds_the_audited_preimages(self):
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(image_settings.state(stock), {'long_samples': None, 'release_floor': None})


if __name__ == '__main__':
    unittest.main()
