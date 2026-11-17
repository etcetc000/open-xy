"""Off unless asked: a build that selects no capability, no opt-in machine flag and no opted-in kit patches the same
sites and links the same SDK units, in the same order, as the same checkout with the CPU capabilities taken out of
it (tests/off_path_code.py without_capabilities). Host only: no toolchain; nothing is stored, so an SDK change that
does not touch the capabilities cannot fail it. No exceptions."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from off_path_code import without_capabilities
from tooling.pipeline import ROOT

# The plain fixture collections (tests/fixtures/machines), each with profiling off and on.
COLLECTIONS = ['probesynth', 'probefx', 'probesample', 'probeshift', 'probesources',
               'probesynth+probefx+probesample+probeshift+probesources']

# Run in a checkout, with its own tooling: the active sites and the planned SDK units of every collection.
SCRIPT = '''
import json, sys
from pathlib import Path
from tooling import native
from tooling.firmware import active_services, patch_active
fixtures, collections = Path(sys.argv[1]), sys.argv[2:]
out = {}
for names in collections:
    entries = [{'path': str(fixtures / n)} for n in names.split('+')]
    profile, machines, _definitions = native.configuration(entries)
    services = active_services(profile, native.service_users(machines))
    for profiling in (False, True):
        sites = sorted((p['address'], p.get('after', p.get('symbol', ''))) for p in profile['patches']
                       if patch_active(p, services, profiling))
        units = [n for n, m, _step in native.plan(machines, profile) if m is None]
        out[names + (':profiling' if profiling else '')] = {'sites': sites, 'units': units}
print(json.dumps(out))
'''


def off_paths(root):
    """{collection[:profiling] -> {'sites', 'units'}} as the checkout at `root` plans them."""
    run = subprocess.run([sys.executable, '-B', '-c', SCRIPT, str(ROOT / 'tests/fixtures/machines'), *COLLECTIONS],
                         cwd=root, capture_output=True, text=True, timeout=600,
                         env={**os.environ, 'PYTHONDONTWRITEBYTECODE': '1'})
    if run.returncode:
        raise AssertionError(f'planning in {root} failed:\n{run.stderr[-3000:]}')
    return json.loads(run.stdout)


class OffUnlessAsked(unittest.TestCase):
    def test_no_capability_means_the_sites_and_units_of_the_checkout_without_them(self):
        with tempfile.TemporaryDirectory() as work:
            expected = off_paths(without_capabilities(ROOT, Path(work) / 'bare'))
        actual = off_paths(ROOT)
        self.assertEqual(sorted(actual), sorted(expected))
        self.assertEqual(len(actual), 2 * len(COLLECTIONS))
        for key in sorted(expected):
            with self.subTest(collection=key):
                self.assertEqual(actual[key]['sites'], expected[key]['sites'])
                self.assertEqual(actual[key]['units'], expected[key]['units'])
                self.assertTrue(actual[key]['sites'] and actual[key]['units'])


if __name__ == '__main__':
    unittest.main()
