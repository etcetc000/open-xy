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
