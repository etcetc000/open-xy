"""Authored loader tests for reviewed pointer and absolute-jump hooks."""
import copy
import unittest

from tooling.firmware import assemble, hook_instruction
from tooling.formats import header, ldr, sha
from tooling.pipeline import check_patch_boundaries


def fixture():
    payload = bytes(range(32))
    records = (header(4, 0x1000, len(payload), 0) + payload +
               header(0x104, 0x2000, 256, 0) + header(0x8004, 0, 0, 0))
    stock = header(0x5004, 0x1000, 0, len(records)) + records
    patches = []
    for offset, length, encoding in ((0, 4, 'pointer'), (8, 10, 'absolute_jump')):
        patches.append(dict(kind='hook', symbol='entry', call=False, encoding=encoding,
                            address=hex(0x1000 + offset), file_offset=hex(32 + offset),
                            before=payload[offset:offset + length].hex(), length=length,
                            service='surface'))
    profile = dict(stock_sha256=sha(stock), stock_bytes=len(stock), stock_records=4,
                   module_base=0x2000, reserved_bytes=256, heap_fill_base=0x2000,
                   heap_fill_bytes=256, patches=patches, services={'surface': ['extension']})
    return stock, profile


class HookEncodings(unittest.TestCase):
    def assemble(self, stock, original, active=True):
        profile = copy.deepcopy(original)
        for edit in profile['patches']:
            edit['after'] = hook_instruction(edit, 0x2000).hex() if active else edit['before']
        return assemble(stock, b'\0\x60\x10\0', profile, original, 2, {'surface'} if active else set())

    def test_exact_reconstruction_and_inactive_service(self):
        stock, profile = fixture()
        before = stock[:]
        for active in (False, True):
            candidate = self.assemble(stock, profile, active)
            self.assertEqual(len(ldr(candidate)), 5)
            self.assertEqual(candidate[-16:], stock[-16:])
            restored = bytearray(candidate[:80] + candidate[100:])
            restored[:16] = stock[:16]
            for edit in profile['patches']:
                at = int(edit['file_offset'], 16)
                actual = candidate[at:at + edit['length']]
                expected = hook_instruction(edit, 0x2000) if active else bytes.fromhex(edit['before'])
                self.assertEqual(actual, expected)
                restored[at:at + edit['length']] = bytes.fromhex(edit['before'])
            self.assertEqual(bytes(restored), stock)
        self.assertEqual(stock, before)

    def test_encoding_policy_cannot_change_during_assembly(self):
        stock, original = fixture()
        changed = copy.deepcopy(original)
        for edit in changed['patches']:
            edit['after'] = hook_instruction(edit, 0x2000).hex()
        changed['patches'][0].pop('encoding')
        with self.assertRaisesRegex(ValueError, 'Changed patch policy'):
            assemble(stock, b'\0\x60\x10\0', changed, original, 2, {'surface'})

    def test_inactive_service_still_requires_valid_encoding(self):
        stock, original = fixture()
        for changes in (dict(encoding='typo'), dict(call=True), dict(address='0x1002')):
            profile = copy.deepcopy(original)
            profile['patches'][0].update(changes)
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                check_patch_boundaries(profile, 2)
        profile = copy.deepcopy(original)
        profile['patches'][0].update(kind='table', after='01020304')
        with self.assertRaisesRegex(ValueError, 'Only hooks'):
            check_patch_boundaries(profile, 2)

    def test_outside_module_and_changed_preimage_are_rejected(self):
        stock, original = fixture()
        for target in (0x1ffe, 0x2004):
            profile = copy.deepcopy(original)
            for edit in profile['patches']:
                edit['after'] = hook_instruction(edit, target).hex()
            with self.assertRaisesRegex(ValueError, 'Hook does not target module'):
                assemble(stock, b'\0\x60\x10\0', profile, original, 2, {'surface'})
        original['patches'][0]['before'] = 'ffffffff'
        with self.assertRaisesRegex(ValueError, 'Patch preimage mismatch'):
            self.assemble(stock, original)


if __name__ == '__main__':
    unittest.main()
