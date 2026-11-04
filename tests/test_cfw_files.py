"""Files under /open-xy: only known kinds, each checked by its own rule."""
import unittest

from tooling import cfw_files


class CfwFiles(unittest.TestCase):
    def test_only_known_subfolders_and_names_are_admitted(self):
        for parts in (('open-xy', 'notes.txt'), ('open-xy', 'other', 'a.opx'), ('open-xy', 'modules', 'sub', 'a.opx'),
                      ('open-xy', 'modules', 'a b.opx'), ('open-xy', 'modules', 'a.oxc'), ('open-xy', 'modules', '.opx')):
            with self.assertRaises(ValueError, msg=parts):
                cfw_files.check(parts, b'')

    def test_a_module_file_is_inspected(self):
        with self.assertRaises(ValueError):
            cfw_files.check(('open-xy', 'modules', 'karplus.opx'), b'not a module')


if __name__ == '__main__': unittest.main()
