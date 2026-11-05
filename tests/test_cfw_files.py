"""Files under /open-xy: only known kinds, each checked by its own rule."""
import unittest

from tooling import build_module, cfw_files


def module(name):
    return build_module.pack(build_module.manifest(name, '1.0.0', 'synth'), bytes(8), 0, 0, [])


class CfwFiles(unittest.TestCase):
    def test_only_known_subfolders_and_names_are_admitted(self):
        for parts in (('open-xy', 'notes.txt'), ('open-xy', 'other', 'a.opx'), ('open-xy', 'modules', 'sub', 'a.opx'),
                      ('open-xy', 'modules', 'a b.opx'), ('open-xy', 'modules', 'a.oxc'), ('open-xy', 'modules', '.opx'),
                      ('open-xy', 'modules', 'x' * 16 + '.opx'), ('open-xy', 'modules', 'Karplus.opx'),
                      ('open-xy', 'modules', 'my-synth.opx')):
            with self.assertRaises(ValueError, msg=parts):
                cfw_files.check(parts, module('a'))

    def test_a_module_file_is_inspected(self):
        with self.assertRaises(ValueError):
            cfw_files.check(('open-xy', 'modules', 'karplus.opx'), b'not a module')

    def test_a_module_file_carries_its_own_name(self):
        cfw_files.check(('open-xy', 'modules', 'karplus.opx'), module('karplus'))
        with self.assertRaises(ValueError):
            cfw_files.check(('open-xy', 'modules', 'renamed.opx'), module('karplus'))


if __name__ == '__main__': unittest.main()
