import copy
import json
import tempfile
import unittest

from tooling import startup_label as label
from tooling.pipeline import ROOT, check_patch_boundaries


class StartupLabelTests(unittest.TestCase):
    def test_the_label_is_the_release_version(self):
        version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
        self.assertEqual(label.release_version(), version)
        self.assertEqual(label.label(version), f'OPEN {version}-1.2.1'.encode('ascii') + b'\0')
        with tempfile.TemporaryDirectory() as directory:
            source = label.generate(directory, '1.2.3').read_text(encoding='utf-8')
        self.assertIn('.asciz "OPEN 1.2.3-1.2.1"', source)
        self.assertNotIn('@LABEL@', source)
        for bad in ('v0.0.2', '1.0', '', None):
            with self.subTest(version=bad), self.assertRaises(ValueError):
                label.label(bad)

    def test_label_edit_is_exactly_the_reviewed_one(self):
        profile = json.loads((ROOT / f'profiles/{label.PROFILE}.json').read_text())
        count = len(profile['patches'])
        check_patch_boundaries(profile, count)
        indices = [i for i, patch in enumerate(profile['patches']) if patch['kind'] == 'startup']
        self.assertEqual(len(indices), 1)
        index = indices[0]
        edit = profile['patches'][index]
        self.assertTrue(label.allowed(profile, edit))
        for key, value in [('address', hex(label.SITE + 2)), ('before', '00' * 10),
                           ('length', 8), ('file_offset', '0xf04d6'), ('symbol', 'other')]:
            changed = copy.deepcopy(profile)
            changed['patches'][index][key] = value
            with self.assertRaises(ValueError):
                check_patch_boundaries(changed, count)
        profile['id'] = 'another-profile'
        with self.assertRaises(ValueError):
            check_patch_boundaries(profile, count)

    def test_absolute_hook_rejects_wrong_opcodes_padding_and_width(self):
        for address in (0x8d450380, 0x8d4cf37e):
            self.assertEqual(label.destination(label.jump(address)), address)
        for address in (-1, 1, 0x100000000):
            with self.assertRaises(ValueError):
                label.jump(address)
        valid = label.jump(0x8d450380)
        for data in (valid[:8], valid + b'\0', b'\0' * 10):
            with self.assertRaises(ValueError):
                label.destination(data)
        for index in (0, 1, 4, 6, 7, 8, 9):
            changed = bytearray(valid)
            changed[index] ^= 1
            with self.assertRaises(ValueError):
                label.destination(changed)


if __name__ == '__main__':
    unittest.main()
