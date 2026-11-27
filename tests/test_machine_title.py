"""A machine's builder title (SDK 1.31): machine.json `title`, the name the builder shows
where the OP-XY shows definition.json's `name`."""
import json
from pathlib import Path
import shutil
import tempfile
import unittest

from tooling import browser_kit, machines
from machine_fixtures import FOLDER

ROOT = Path(__file__).resolve().parents[1]


class MachineTitle(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.folder = Path(self.temporary.name).resolve() / 'probefx'
        shutil.copytree(FOLDER / 'probefx', self.folder)
        self.record = json.loads((self.folder / 'machine.json').read_text(encoding='utf-8'))
        self.record.pop('identity', None)   # from SDK 1.5 each build gives the numbers

    def tearDown(self):
        self.temporary.cleanup()

    def machine(self, **changes):
        record = dict(self.record, **changes)
        (self.folder / 'machine.json').write_text(json.dumps(record), encoding='utf-8')
        return machines.machine_at(self.folder)

    def test_without_a_title_the_builder_shows_the_name(self):
        m = machines.machine_at(self.folder)   # the fixture as it is
        self.assertEqual(m.title, m.name)

    def test_a_title_needs_sdk_1_31(self):
        with self.assertRaisesRegex(ValueError, r'title.*1\.31'):
            self.machine(sdk='1.30', title='Probe Effect')
        m = self.machine(sdk='1.31', title='Probe Effect')
        self.assertEqual(m.title, 'Probe Effect')
        self.assertNotEqual(m.name, m.title)   # the OP-XY's name is unchanged

    def test_a_title_is_short_printable_text(self):
        for title in ('', ' Padded', 'Padded ', 'x' * 41, 'Tab\there', 7):
            with self.assertRaisesRegex(ValueError, 'title'):
                self.machine(sdk='1.31', title=title)
        self.assertEqual(self.machine(sdk='1.31', title='x' * 40).title, 'x' * 40)

    def test_the_kit_lists_a_title_only_where_it_differs(self):
        profile = json.loads((ROOT / 'profiles/opxy-1.2.1.json').read_text(encoding='utf-8'))
        output = Path(self.temporary.name) / 'out'
        output.mkdir()
        plain = browser_kit.machine_entry(self.machine(sdk='1.31'), profile, {}, output, 'clang++')
        self.assertNotIn('title', plain)
        titled = browser_kit.machine_entry(self.machine(sdk='1.31', title='Probe Effect'), profile, {}, output, 'clang++')
        self.assertEqual((titled['name'], titled['title']), (plain['name'], 'Probe Effect'))
        same = browser_kit.machine_entry(self.machine(sdk='1.31', title=plain['name']), profile, {}, output, 'clang++')
        self.assertNotIn('title', same)


if __name__ == '__main__':
    unittest.main()
