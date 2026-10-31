"""Native arranger project lifecycle with mocked stock file I/O and interleavings."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.pipeline import ROOT, config
from test_sample_services import HOST_CXX


class ArrangerProjectTests(unittest.TestCase):
    def test_named_project_rename_hook_uses_verified_native_call(self):
        hooks = [p for p in config()['patches'] if p.get('symbol') == 'sdk_arranger_project_rename']
        self.assertEqual([(p['address'], p['file_offset'], p['length'], p['before'], p['call']) for p in hooks],
                         [('0x8d572966', '0x1a9bc2', 8, '00de048c780c0000', True)])

    def test_mtp_extension_uses_stock_file_allowlist_matcher(self):
        hooks = [p for p in config()['patches'] if p.get('symbol') == 'sdk_arranger_mtp_match']
        self.assertEqual([(p['address'], p['file_offset'], p['length'], p['before'], p['call']) for p in hooks],
                         [('0x8d5c1d9e', '0x1f8ff6', 4, 'ffe309ff', True)])

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_snapshot_binding_restore_and_io_failures(self):
        hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        with tempfile.TemporaryDirectory(prefix='arranger-project-') as directory:
            binary = Path(directory) / ('project.exe' if os.name == 'nt' else 'project')
            result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-D__builtin_bfin_ssync()=host_barrier()', '-I' + str(ROOT / 'sdk/include'),
                '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'),
                str(ROOT / 'tests/fixtures/arranger_project.cpp'), '-o', str(binary)],
                capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
