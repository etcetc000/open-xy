"""The firmware's word-wise memset and memcpy match byte loops."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_sample_services import HOST_CXX, ROOT


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class Memory(unittest.TestCase):
    def test_word_paths_match_bytes_at_every_alignment(self):
        with tempfile.TemporaryDirectory(prefix='memory-') as folder:
            folder = Path(folder)
            binary = folder / ('memory.exe' if os.name == 'nt' else 'memory')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            runtime = folder / 'runtime.o'
            # Renamed, so the host's own memset and memcpy stay in place.
            command = [HOST_CXX, '-std=c++11', '-O2', '-ffreestanding', '-fno-builtin', '-Wall', '-Werror',
                       '-Dmemset=opxy_memset', '-Dmemcpy=opxy_memcpy', '-c',
                       str(ROOT / 'sdk/platform/blackfin/memory.cpp'), '-o', str(runtime)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(ROOT / 'sdk/include'),
                       str(ROOT / 'tests/fixtures/memory.cpp'), str(runtime), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
