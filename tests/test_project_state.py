"""Actual native project-state adapter with native filesystem/controller boundaries doubled."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_projects import host_writer

class ProjectStateAdapter(unittest.TestCase):
    def test_project_lifecycle_source_binding_and_failure_ownership(self):
        with tempfile.TemporaryDirectory(prefix='project-state-') as directory:
            folder = Path(directory)
            binary = host_writer(folder, 'project_state.cpp')
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_native_lifecycle_hooks_remain_wired_with_other_extension_services(self):
        import json
        profile = json.loads((Path(__file__).resolve().parents[1] / "profiles/opxy-1.2.1.json").read_text())
        expected = [{'kind': 'hook', 'address': '0x8d4de0ac', 'file_offset': '0x115308', 'length': 4, 'before': '05e30258', 'symbol': 'sdk_project_load_begin', 'call': True}, {'kind': 'hook', 'address': '0x8d598b14', 'file_offset': '0x1cfd70', 'length': 4, 'before': 'ffe3ce82', 'symbol': 'sdk_project_load_begin', 'call': True}, {'kind': 'hook', 'address': '0x8d573074', 'file_offset': '0x1aa2d0', 'length': 4, 'before': '00e33496', 'symbol': 'sdk_project_new_commit', 'call': True}, {'kind': 'hook', 'address': '0x8d57309c', 'file_offset': '0x1aa2f8', 'length': 4, 'before': '00e32096', 'symbol': 'sdk_project_new_commit', 'call': True}, {'kind': 'hook', 'address': '0x8d598b6a', 'file_offset': '0x1cfdc6', 'length': 4, 'before': 'ffe3b968', 'symbol': 'sdk_project_new_commit', 'call': True}, {'kind': 'hook', 'address': '0x8d572966', 'file_offset': '0x1a9bc2', 'length': 8, 'before': '00de048c780c0000', 'symbol': 'sdk_project_rename', 'call': True}]
        for hook in expected:
            with self.subTest(address=hook["address"]):
                self.assertIn(hook, profile["patches"])
