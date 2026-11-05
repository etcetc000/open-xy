"""The release_floor capability: audited edits, and applying it to an image."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

from tooling import native, release_floor as floor, settings as image_settings
from tooling.extensions import extension_at
from tooling.firmware import absolute_call, active_services, patch_active
from tooling.formats import header
from tooling.pipeline import check_patch_boundaries, config
from tooling.stock_tables import decode


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
        floor.check_profile(config())
        floor_edit, stage_edit = floor_edits(config())
        self.assertEqual((int(floor_edit['address'], 16), int(stage_edit['address'], 16)),
                         (floor.FLOOR_SITE, floor.STAGE_SITE))
        self.assertEqual(decode('imm32', bytes.fromhex(floor_edit['after'])), (0xda01, floor.FLOOR))
        # -40 dB is stock's own 0.01 constant, round(2^31 / 100).
        self.assertEqual(floor.FLOOR, round((1 << 31) / 100))

    def test_rejects_other_values_and_sites(self):
        profile = config()
        index = profile['patches'].index(floor_edits(profile)[0])
        for change in (dict(after='01dacc0ccdcc0000'),             # -20 dB: not the reviewed floor
                       dict(after='07' + floor_edits(profile)[0]['after'][2:]),  # another register
                       dict(address='0x8d50e264')):
            altered = copy.deepcopy(profile)
            altered['patches'][index].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                floor.check_profile(altered)
        altered = copy.deepcopy(profile)
        altered['patches'][index + 1]['after'] = '3860'  # stage 7 would start a stolen note
        with self.assertRaises(ValueError):
            floor.check_profile(altered)

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
    def test_only_a_declaring_extension_activates_the_edits(self):
        extension = extension_at(folder(self))
        self.assertEqual((extension.capabilities, extension.linked), (('release_floor',), False))

        class Machines(list):
            extensions = (extension,)
        services = active_services(config(), native.service_users(Machines()))
        self.assertEqual({patch_active(p, services) for p in floor_edits(config())}, {True})
        self.assertNotIn(floor.SERVICE, active_services(config(), native.service_users([])))
        with self.assertRaises(ValueError):
            extension_at(folder(self, settings={'release_floor': -40}))  # no settings field


class Image(unittest.TestCase):
    """Applying the capability to an image: a synthetic loader holding the stock bytes at
    every capability site, standing in for the private OS."""

    def image(self):
        profile = config()
        edits = [p for p in profile['patches'] if p.get('service') in ('long_samples', 'release_floor')]
        records = [header(0x5004, 0, 0, 0)]
        for patch in edits:
            records.append(header(1, int(patch['address'], 16), patch['length'], 0) + bytes.fromhex(patch['before']))
        records.append(header(1, 0x80000000, 128, 0) + bytes(range(128)))  # other stock code
        records.append(header(0x8004, 0, 0, 0))
        data = bytearray(b''.join(records))
        data[:16] = header(0x5004, 0, 0, len(data) - 16)  # FIRST holds the image size, as stock's does
        # Point each edit at its record and pad the image to the profile's sizes.
        fake = copy.deepcopy(profile)
        fake['patches'] = [p for p in fake['patches'] if p in edits]
        offset = 16
        for patch in fake['patches']:
            patch['file_offset'] = hex(offset + 16)
            offset += 16 + patch['length']
        fake['stock_bytes'], fake['stock_records'] = len(data), len(records)
        fake['stock_sha256'] = hashlib.sha256(data).hexdigest()
        return bytes(data), fake

    def test_apply_and_show(self):
        data, profile = self.image()
        self.assertEqual(image_settings.state(data, profile), {'long_samples': False, 'release_floor': False})
        extension = extension_at(folder(self))
        result, declared = image_settings.apply(data, [extension], profile)
        self.assertEqual(declared, ['release_floor'])
        self.assertEqual(image_settings.state(result, profile), {'long_samples': False, 'release_floor': True})
        self.assertEqual(sum(a != b for a, b in zip(data, result)), 5)
        self.assertEqual(image_settings.apply(result, [extension], profile)[0], result)  # applying twice changes nothing
        partial = bytearray(result)
        stage = floor_edits(profile)[1]
        partial[int(stage['file_offset'], 16):int(stage['file_offset'], 16) + 2] = bytes.fromhex(stage['before'])
        with self.assertRaises(ValueError):
            image_settings.state(bytes(partial), profile)  # floor raised, stage stock: partly applied
        corrupt = bytearray(data)
        site = int(floor_edits(profile)[1]['file_offset'], 16)
        corrupt[site] ^= 0xff
        with self.assertRaises(ValueError):
            image_settings.state(bytes(corrupt), profile)
        # Reviewed bytes at every site are not enough: the image must be made from stock.
        with self.assertRaisesRegex(ValueError, 'Not OP-XY OS 1.2.1'):
            image_settings.apply(data, [extension], dict(profile, stock_sha256='0' * 64))

    def test_an_open_xy_image_is_authenticated_and_applied(self):
        data, profile = self.image()
        module = header(4, 0x20000000, 4, 0) + bytes(4)
        built = bytearray(data[:-16] + module + data[-16:])
        built[:16] = header(0x5004, 0, 0, len(built) - 16)
        extension = extension_at(folder(self))
        result, _ = image_settings.apply(bytes(built), [extension], profile)
        self.assertEqual(image_settings.state(result, profile)['release_floor'], True)
        self.assertEqual(result[-36:], bytes(built[-36:]))  # the module record is untouched

    def test_hook_sites_hold_stock_or_a_branch_into_the_image_module(self):
        data, profile = self.image()
        code = len(data) - 16 - 128 + 64  # inside the other stock record's payload: a hook site here
        profile['patches'].append(dict(kind='hook', symbol='entry', call=True, length=8,
                                       address=hex(0x80000000), file_offset=hex(code), before=data[code:code + 8].hex()))
        profile['stock_sha256'] = hashlib.sha256(data).hexdigest()
        module = header(4, 0x30000000, 4, 0) + bytes(4)
        built = bytearray(data[:-16] + module + data[-16:])
        built[:16] = header(0x5004, 0, 0, len(built) - 16)
        self.assertEqual(image_settings.state(bytes(built), profile)['release_floor'], False)
        into = bytearray(built)
        into[code:code + 8] = absolute_call(0x30000000)
        self.assertEqual(image_settings.state(bytes(into), profile)['release_floor'], False)
        outside = bytearray(built)
        outside[code:code + 8] = absolute_call(0x30000100)
        with self.assertRaisesRegex(ValueError, 'leaves its module'):
            image_settings.state(bytes(outside), profile)
        bare = bytearray(data)  # a hook with no module record to land in
        bare[code:code + 8] = absolute_call(0x30000000)
        with self.assertRaisesRegex(ValueError, 'unexpected bytes'):
            image_settings.state(bytes(bare), profile)

    def test_an_image_from_another_sdk_needs_the_stock_os(self):
        # In the synthetic image, the capability relies on the first 64 bytes of the other record.
        patcher = mock.patch.dict(image_settings.DEPENDS, {floor.SERVICE: ((0x80000000, 64),)})
        patcher.start()
        self.addCleanup(patcher.stop)
        data, profile = self.image()
        module = header(4, 0x30000000, 4, 0) + bytes(4)
        other = bytearray(data[:-16] + module + data[-16:])
        other[:16] = header(0x5004, 0, 0, len(other) - 16)
        code = len(data) - 16 - 128  # the other stock record's payload
        other[code + 64] ^= 0x5a  # a hook of its own, far from any capability site
        extension = extension_at(folder(self))
        with self.assertRaisesRegex(ValueError, 'pass the stock OS with --stock-os'):
            image_settings.state(bytes(other), profile)
        self.assertEqual(image_settings.state(bytes(other), profile, data), {'long_samples': False, 'release_floor': False})
        result, _ = image_settings.apply(bytes(other), [extension], profile, data)
        self.assertEqual(image_settings.state(result, profile, data)['release_floor'], True)
        self.assertEqual(sum(a != b for a, b in zip(other, result)), 5)
        with self.assertRaisesRegex(ValueError, 'exact stock'):
            image_settings.state(bytes(other), profile, data[:-1] + b'\1')
        # Its edits must not adjoin a capability site, and stock's records must stay in place.
        site = int(floor_edits(profile)[0]['file_offset'], 16)
        near = bytearray(other)
        near[site - 1] ^= 0xff
        with self.assertRaises(ValueError):
            image_settings.state(bytes(near), profile, data)
        relied = bytearray(other)
        relied[code + 48] ^= 0x01  # inside the stock code the capability relies on, past the window
        with self.assertRaisesRegex(ValueError, 'relies on'):
            image_settings.state(bytes(relied), profile, data)
        # A build of this SDK (its module at this profile's base) that fails reconstruction is
        # corrupt: the stock OS does not admit it.
        own = bytearray(data[:-16] + header(4, profile['module_base'], 4, 0) + bytes(4) + data[-16:])
        own[:16] = header(0x5004, 0, 0, len(own) - 16)
        own[code + 64] ^= 0x5a
        with self.assertRaises(ValueError) as caught:
            image_settings.state(bytes(own), profile, data)
        self.assertNotIn('--stock-os', str(caught.exception))
        moved = bytearray(other)
        moved[code - 16:code] = header(1, 0x80000010, 128, 0)  # a valid record at another address
        with self.assertRaisesRegex(ValueError, 'records moved'):
            image_settings.state(bytes(moved), profile, data)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_stock_holds_the_audited_preimages(self):
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(image_settings.state(stock), {'long_samples': False, 'release_floor': False})


if __name__ == '__main__':
    unittest.main()
