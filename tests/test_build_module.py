"""Module files: the relocation list reproduces a second link, and the loader accepts the result."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
from tooling import build_module
from tooling.pipeline import ROOT


class Packing(unittest.TestCase):
    def test_apply_writes_every_kind_at_the_new_base(self):
        image = bytes(16)
        relocs = [(build_module.ABS32 << 28 | 0, 8), (build_module.HI16 << 28 | 4, 12), (build_module.LO16 << 28 | 6, 12)]
        moved = build_module.apply(image, relocs, 0x01234560)
        self.assertEqual(struct.unpack_from('<I', moved, 0)[0], 0x01234568)
        self.assertEqual(struct.unpack_from('<HH', moved, 4), (0x0123, 0x456c))
        self.assertEqual(build_module.apply(image, [], 0x01234560), image)

    def test_trailer_is_last_and_covers_image_and_relocs(self):
        image = bytes(range(8))
        data = build_module.pack(image, 4, 2, [(build_module.ABS32 << 28, 4)])
        fields = build_module.TRAILER.unpack(data[-48:])
        self.assertEqual(fields[-1], b'OPXYMOD\0')
        self.assertEqual(fields[1:6], (0, 8, 4, 8, 1))
        self.assertEqual(fields[6], 2)
        self.assertEqual(fields[7], zlib.crc32(data[8:16], zlib.crc32(image)))
        self.assertEqual(fields[8:11], (build_module.OS_1_2_1, 0, 48))

    def test_unknown_relocation_types_are_refused(self):
        with self.assertRaises(ValueError):
            build_module.relocations([('.text', 0, 0x05, 0)], bytes(8), 0)   # R_BFIN_RIMM16
        self.assertEqual(build_module.relocations([('.text', 0, 0x0a, 0)], bytes(8), 0), [])   # PCREL24


class Probe(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('OPXY_TEST_TOOLCHAIN'), 'No pinned Blackfin tools fixture')
    def test_probe_module_builds_and_stages(self):
        tools = Path(os.environ['OPXY_TEST_TOOLCHAIN'])
        compiler = tools.parent
        if not (compiler / 'bin' / ('bfin-elf-g++' + ('.exe' if os.name == 'nt' else ''))).is_file():
            raise unittest.SkipTest('Compiler not beside the tools')
        host = os.environ.get('OPXY_TEST_HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
        if not host: raise unittest.SkipTest('No host C++ compiler (set OPXY_TEST_HOST_CXX)')
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / 'probe.opx'
            result = build_module.build(ROOT / 'tests/fixtures/modules/probe', out, tools, compiler)
            self.assertGreater(result['relocs'], 0)
            data = out.read_bytes()
            kinds = {struct.unpack_from('<I', data, result['image'] + 8 * i)[0] >> 28 for i in range(result['relocs'])}
            self.assertEqual(kinds, {build_module.ABS32, build_module.HI16, build_module.LO16})
            runner = Path(temp) / ('loader.exe' if os.name == 'nt' else 'loader')
            subprocess.run([host, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/platform'),
                            str(ROOT / 'tests/fixtures/module_loader.cpp'),
                            str(ROOT / 'sdk/platform/module_loader.cpp'), '-o', str(runner)],
                           check=True, capture_output=True)
            subprocess.run([str(runner), str(out)], check=True)


if __name__ == '__main__': unittest.main()
