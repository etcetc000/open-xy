"""Commit guard checks use authored bytes in an isolated temporary repository."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'tools/check_private_files.py'
spec = importlib.util.spec_from_file_location('private_guard', SCRIPT)
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)


class PrivateFilesTests(unittest.TestCase):
    def test_private_paths_include_case_variants(self):
        for name in ('image.LDR', 'image.tfw', 'local/report.json', 'dist/kit.json', '.env.local', 'copy.zip',
                     'machines/probe/vendor/data/table.bin'):
            self.assertIsNotNone(guard.path_problem(name), name)
        for name in ('builder/core.mjs', 'docs/build.md', 'machines/probe/vendor/library/resources.cc'):
            self.assertIsNone(guard.path_problem(name), name)

    def test_renamed_firmware_and_archives_are_detected(self):
        header = bytearray(16)
        header[2] = header[3] = 0xad  # Authored header, valid XOR; no firmware content.
        self.assertEqual(guard.content_problem(bytes(header)), 'Blackfin firmware container')
        for prefix in (b'PK\x03\x04', b'\x7fELF', b'MZ', b'\x1f\x8b'):
            self.assertIsNotNone(guard.content_problem(prefix))
        self.assertIsNone(guard.content_problem(b'// ordinary source\n'))

    def test_checks_staged_bytes_even_when_working_copy_is_replaced(self):
        with tempfile.TemporaryDirectory(prefix='opxy-guard-') as folder:
            root = Path(folder)
            subprocess.run(['git', 'init', '-q', folder], check=True)
            target = root / 'innocent.txt'
            target.write_bytes(b'PK\x03\x04authored test archive signature')
            subprocess.run(['git', 'add', 'innocent.txt'], cwd=root, check=True)
            target.write_text('clean working copy')
            result = subprocess.run([sys.executable, str(SCRIPT), '--staged'], cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn('archive payload', result.stderr)
            subprocess.run(['git', 'add', 'innocent.txt'], cwd=root, check=True)
            clean = subprocess.run([sys.executable, str(SCRIPT), '--staged'], cwd=root, capture_output=True, text=True)
            self.assertEqual(clean.returncode, 0, clean.stdout + clean.stderr)
            # Even the now-unreachable test blob is found by the explicit audit.
            audit = subprocess.run([sys.executable, str(SCRIPT), '--history', '--all-objects'], cwd=root, capture_output=True, text=True)
            self.assertEqual(audit.returncode, 1, audit.stdout + audit.stderr)
