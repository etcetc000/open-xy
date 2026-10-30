"""Deferred add-clip request ownership and stale-context rejection."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.pipeline import ROOT
from test_sample_services import HOST_CXX


class ArrangerPendingAddTests(unittest.TestCase):
    def test_native_ui_runs_request_only_after_project_poll(self):
        source = (ROOT / 'sdk/platform/opxy-1.2.1/arranger_ui.cpp').read_text()
        key = source[source.index('void timeline_key('):source.index('const char* menu_unavailable(')]
        self.assertLess(key.index('add_clip(); return;'), key.index('sdk_arranger_project_poll();'))
        request = source[source.index('void add_clip() {'):source.index('void process_pending_add()')]
        self.assertIn('pending_add.enqueue', request)
        self.assertNotIn('arranger_add_empty_clip', request)
        poll = source[source.index('void sdk_arranger_poll() {'):source.index('void sdk_arranger_project_restored(', source.index('void sdk_arranger_poll() {'))]
        self.assertLess(poll.index('stock_arranger_poll();'), poll.index('sdk_arranger_project_poll();'))
        self.assertLess(poll.index('sdk_arranger_project_poll();'), poll.index('process_pending_add();'))
        execute = source[source.index('void process_pending_add()'):source.index('void timeline_key(')]
        self.assertLess(execute.index('pending_add.take('), execute.index('begin_history();'))
        self.assertLess(execute.index('begin_history();'), execute.index('arranger_add_empty_clip('))
        self.assertLess(execute.index('arranger_add_empty_clip('), execute.index('commit_edit('))
        restored = source[source.index('void sdk_arranger_project_restored('):]
        self.assertIn('pending_add.cancel();', restored)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_request_lifetime(self):
        hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
        with tempfile.TemporaryDirectory(prefix='arranger-edit-') as directory:
            binary = Path(directory) / ('resolver.exe' if os.name == 'nt' else 'resolver')
            result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT / 'sdk/include'), str(ROOT / 'tests/fixtures/arranger_pending_add.cpp'), '-o', str(binary)],
                capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
