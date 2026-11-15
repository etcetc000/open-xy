"""The release_floor capability: audited edits, its setting and applying it to an image."""
import contextlib
import copy
import dataclasses
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

from tooling import extensions, native, release_floor as floor, settings as image_settings
from tooling.extensions import capabilities, capability_settings, check_collection, extension_at, metadata, setting_edits
from tooling.firmware import active_services, check_edit, patch_active
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

    def test_a_capability_needs_a_fixed_edit_that_marks_it_on(self):
        # A setting's bytes may equal stock's (-66 dB), so on/off is read from the fixed
        # stage edit; a capability with only setting edits could not be read.
        self.assertEqual(capabilities(), ('long_samples', 'release_floor'))
        real = extensions.document

        def without_stage(path):
            data = real(path)
            if 'patches' in data:
                data = copy.deepcopy(data)
                data['patches'] = [p for p in data['patches'] if int(p['address'], 16) != floor.STAGE_SITE]
            return data
        with mock.patch.object(extensions, 'document', without_stage):
            with self.assertRaisesRegex(ValueError, 'fixed edit that marks it on'):
                capabilities()

    def test_an_inactive_build_leaves_the_sites_stock(self):
        profile = copy.deepcopy(config())
        for patch in floor_edits(profile):
            patch['after'] = patch['before']
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

    def test_two_extensions_may_not_choose_one_setting(self):
        quiet = extension_at(folder(self, id='quiet', settings={'release_floor': -40}))
        quieter = extension_at(folder(self, id='quieter', settings={'release_floor': -30}))
        with self.assertRaisesRegex(ValueError, 'release_floor is declared by two extensions'):
            check_collection([quiet, quieter])
        check_collection([quiet])
        # Declaring the capability without a value counts too, as in the builder: one chooses
        # -40, the other the default, and both would write the floor site.
        plain = extension_at(folder(self, id='plainfloor'))
        for pair in ([quiet, plain], [plain, extension_at(folder(self, id='plainer'))]):
            with self.subTest(pair=[e.id for e in pair]):
                with self.assertRaisesRegex(ValueError, 'release_floor is declared by two extensions'):
                    check_collection(pair)
        # A capability without a setting may be declared by several.
        longer = [extension_at(folder(self, id=i, capabilities=['long_samples'])) for i in ('long1', 'long2')]
        check_collection([quiet, *longer])

    def test_a_build_writes_a_non_default_setting_and_assemble_accepts_only_reviewed_bytes(self):
        # check_module's bytes for each capability edit (native.fixed_after), checked as
        # firmware.assemble checks every edit (firmware.check_edit).
        class Machines(list):
            extensions = (extension_at(folder(self, settings={'release_floor': -40})),)
        profile = config()
        services = active_services(profile, native.service_users(Machines()))
        chosen = setting_edits(profile, native.build_settings(Machines()))
        floor_edit, stage_edit = floor_edits(profile)
        built = dict(floor_edit, after=native.fixed_after(floor_edit, services, True, chosen))
        self.assertEqual(built['after'], floor_edit['choices']['-40'])
        self.assertNotEqual(built['after'], floor_edit['after'])  # not the default
        self.assertIsNone(native.fixed_after(stage_edit, services, True, chosen))  # keeps its reviewed after
        check_edit(built, floor_edit, services, True, 0, 1)
        check_edit(stage_edit, stage_edit, services, True, 0, 1)
        with self.assertRaisesRegex(ValueError, 'Unreviewed capability setting bytes'):
            check_edit(dict(floor_edit, after='01da0000ffff0000'), floor_edit, services, True, 0, 1)
        with self.assertRaisesRegex(ValueError, 'Changed heap reservation or table edit'):
            check_edit(dict(stage_edit, after=stage_edit['before']), stage_edit, services, True, 0, 1)
        # Without the extension both sites stay stock.
        idle = active_services(profile, native.service_users([]))
        self.assertEqual([native.fixed_after(p, idle, True, {}) for p in (floor_edit, stage_edit)],
                         [floor_edit['before'], stage_edit['before']])
        with self.assertRaisesRegex(ValueError, 'inactive hook changed stock'):
            check_edit(built, floor_edit, idle, True, 0, 1)

    def test_only_a_declaring_extension_activates_the_edits(self):
        class Machines(list):
            extensions = (extension_at(folder(self, settings={'release_floor': -30})),)
        services = active_services(config(), native.service_users(Machines()))
        self.assertEqual({patch_active(p, services) for p in floor_edits(config())}, {True})
        self.assertEqual(native.build_settings(Machines()), {'release_floor': -30})
        self.assertNotIn(floor.SERVICE, active_services(config(), native.service_users([])))


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
        self.assertEqual(image_settings.state(again, profile)['release_floor'], -66)
        with self.assertRaises(ValueError):
            image_settings.apply(data, [extension], {'release_floor': -10}, profile)
        with self.assertRaises(ValueError):
            image_settings.apply(data, [extension], {'long_samples': 1}, profile)
        corrupt = bytearray(data)
        site = int(floor_edits(profile)[1]['file_offset'], 16)
        corrupt[site] ^= 0xff
        with self.assertRaises(ValueError):
            image_settings.state(bytes(corrupt), profile)

    def test_the_minimum_reads_as_on_and_a_half_applied_image_is_rejected(self):
        data, profile = self.image()
        floor_edit, _ = floor_edits(profile)
        at = int(floor_edit['file_offset'], 16)
        minimum, _ = image_settings.apply(data, [extension_at(folder(self))], {'release_floor': -66}, profile)
        self.assertEqual(minimum[at:at + 8], data[at:at + 8])  # stock's bytes at the floor site
        self.assertEqual(image_settings.state(minimum, profile)['release_floor'], -66)
        half = bytearray(data)  # the floor raised, the stage edit stock
        half[at:at + 8] = bytes.fromhex(floor_edit['choices']['-40'])
        with self.assertRaisesRegex(ValueError, 'partly applied'):
            image_settings.state(bytes(half), profile)

    def test_apply_takes_only_settings_extensions(self):
        data, profile = self.image()
        coded = dataclasses.replace(extension_at(folder(self)), linked=True)
        with self.assertRaisesRegex(ValueError, 'Only settings extensions'):
            image_settings.apply(data, [coded], {}, profile)

    def test_settings_command(self):
        data, profile = self.image()
        work = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, work)
        (work / 'os.ldr').write_bytes(data)
        extension = folder(self, settings={'release_floor': -40})

        def run(*argv):
            out, err = io.StringIO(), io.StringIO()
            with mock.patch.object(image_settings, 'config', lambda: profile), \
                    contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                try:
                    code = image_settings.main([str(a) for a in argv])
                except SystemExit as exit:
                    code = exit.code
            return code, out.getvalue(), err.getvalue()
        code, text, _ = run('show', '--image', work / 'os.ldr')
        self.assertEqual((code, json.loads(text)), (0, {'long_samples': None, 'release_floor': None}))
        code, _, _ = run('apply', '--image', work / 'os.ldr', '--extension', extension,
                         '--set', 'release_floor=-30', '--out', work / 'out')
        report = json.loads((work / 'out/settings.json').read_text(encoding='utf-8'))
        self.assertEqual((code, report['capabilities'], report['extensions'], report['changed_bytes']),
                         (0, {'release_floor': -30}, ['idle'], 5))
        written = (work / 'out/os.ldr').read_bytes()
        self.assertEqual(report['sha256'], hashlib.sha256(written).hexdigest())
        self.assertEqual(image_settings.state(written, profile)['release_floor'], -30)
        # An existing folder needs --overwrite; a bad setting or a folder in the checkout
        # fails without writing anything.
        for argv, message in ((('--out', work / 'out'), 'already exists'),
                              (('--out', work / 'bad', '--set', 'release_floor'), 'A setting is'),
                              (('--out', work / 'bad', '--set', 'release_floor=-10'), 'release_floor is -66..-20'),
                              (('--out', ROOT / 'build-settings-test'), 'outside the checkout')):
            with self.subTest(argv=argv):
                code, _, err = run('apply', '--image', work / 'os.ldr', '--extension', extension, *argv)
                self.assertEqual(code, 1)
                self.assertIn(message, err)
        self.assertFalse((work / 'bad').exists())
        self.assertFalse((ROOT / 'build-settings-test').exists())

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_stock_holds_the_audited_preimages(self):
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(image_settings.state(stock), {'long_samples': None, 'release_floor': None})


if __name__ == '__main__':
    unittest.main()
