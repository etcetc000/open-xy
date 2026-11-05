"""The reviewed experimental 6 MiB layout the builder offers as an opt-in.

profiles/opxy-1.2.1.json `experimental_memory` moves only the module base and the
two heap immediates (builder/core.mjs expandedProfile). These checks hold it to
the same stock instruction forms and table placement as the standard layout.
"""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling import stock_tables
from tooling.machines import layout_document, read_layout
from tooling.pipeline import ROOT, config

BROWSER = """
import {expandedProfile} from %s;
let input = '';
for await (const chunk of process.stdin) input += chunk;
process.stdout.write(JSON.stringify(expandedProfile(JSON.parse(input))));
"""


class ExperimentalMemory(unittest.TestCase):
    def setUp(self):
        self.profile = config()
        self.layout = self.profile['experimental_memory']

    def test_six_mib_ends_where_the_reviewed_reservation_ends(self):
        p, x = self.profile, self.layout
        self.assertEqual(set(x), {'module_base', 'reserved_bytes', 'heap_after'})
        self.assertEqual(x['reserved_bytes'], 6 * 1024 * 1024)
        self.assertEqual(x['module_base'] + x['reserved_bytes'], stock_tables.RESERVATION_END)
        self.assertEqual(p['module_base'] + p['reserved_bytes'], stock_tables.RESERVATION_END)
        self.assertLessEqual(x['module_base'] + x['reserved_bytes'] - p['table_bytes'], stock_tables.TABLES_START)
        # Still inside the unchanged stock zero-fill, leaving 14 MiB of heap below it.
        self.assertEqual(x['module_base'] - p['heap_fill_base'], 14 * 1024 * 1024)

    def test_heap_edits_change_only_the_immediate(self):
        heaps = [e for e in self.profile['patches'] if e['kind'] == 'heap']
        self.assertEqual(len(heaps), 2)
        new_opcode, size = stock_tables.decode('imm32', bytes.fromhex(self.layout['heap_after']))
        for edit in heaps:
            opcode, original = stock_tables.decode('imm32', bytes.fromhex(edit['before']))
            self.assertEqual((new_opcode, size), (opcode, self.layout['module_base'] - self.profile['heap_fill_base']))
            self.assertEqual(original, self.profile['heap_fill_bytes'])

    def test_relative_hooks_reach_the_whole_larger_module(self):
        low, high = self.layout['module_base'], stock_tables.RESERVATION_END
        for edit in self.profile['patches']:
            if edit['kind'] == 'hook' and edit['length'] in (4, 6):
                site = int(edit['address'], 16)
                self.assertTrue(-(1 << 24) <= low - site and high - site < 1 << 24, edit['address'])

    def test_browser_applies_exactly_the_reviewed_values(self):
        script = BROWSER % json.dumps((ROOT / 'builder/core.mjs').as_uri())
        result = subprocess.run(['node', '--input-type=module', '-e', script], input=json.dumps(self.profile),
                                capture_output=True, text=True, cwd=ROOT, timeout=60, check=True)
        expanded = json.loads(result.stdout)
        self.assertEqual((expanded['module_base'], expanded['reserved_bytes']),
                         (self.layout['module_base'], self.layout['reserved_bytes']))
        for edit, reviewed in zip(expanded['patches'], self.profile['patches']):
            changed = {k for k in reviewed if edit[k] != reviewed[k]}
            self.assertEqual(changed, {'after', 'name'} if reviewed['kind'] == 'heap' else set())
            if reviewed['kind'] == 'heap':
                self.assertEqual(edit['after'], self.layout['heap_after'])
                self.assertEqual(edit['name'], 'IN-MEMORY application heap 0x1400000 -> 0xe00000 '
                                               '(6 MiB module reservation, experimental)')
        self.assertEqual({k: v for k, v in expanded.items() if k not in ('module_base', 'reserved_bytes', 'patches')},
                         {k: v for k, v in self.profile.items() if k not in ('module_base', 'reserved_bytes', 'patches')})

    def test_sdk_builds_refuse_a_layout_that_asks_for_it(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'layout.json'
            path.write_text(json.dumps(dict(layout_document({}), memory='expanded')), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'experimental memory'):
                read_layout(path, build=True)
            self.assertEqual(read_layout(path), {})  # project companions need only the numbers
            for memory in (None, 'large'):
                path.write_text(json.dumps(dict(layout_document({}), memory=memory)), encoding='utf-8')
                with self.assertRaisesRegex(ValueError, 'invalid memory'):
                    read_layout(path)
            path.write_text(json.dumps(layout_document({})), encoding='utf-8')
            self.assertEqual(read_layout(path, build=True), {})


if __name__ == '__main__':
    unittest.main()
