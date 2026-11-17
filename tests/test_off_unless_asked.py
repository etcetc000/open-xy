"""Off unless asked, on the host: a build that selects no capability, no opt-in machine flag and no opted-in kit
patches the same sites, links the same SDK units in the same order, and generates the same adapter sources as the
same checkout with the CPU capabilities taken out of it (tests/off_path_code.py without_capabilities), for every
collection tests/off_path_code.py builds. No toolchain: the generated sources are compared as text, not compiled
(tests/test_off_path_code.py compares the linked modules where the Blackfin tools are). Nothing is stored, so an SDK
change that does not touch the capabilities cannot fail it. No exceptions."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from off_path_code import COLLECTIONS, EXTENSIONS, machine_folders, without_capabilities
from tooling.pipeline import ROOT

# Run in a checkout, with its own tooling: per collection, the active sites, the planned SDK units and the text of
# every generated adapter source.
SCRIPT = '''
import json, sys, tempfile
from pathlib import Path
from tooling import native
from tooling.firmware import active_services, patch_active
from tooling.machines import read_collection
plan = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))
out = {}
for name, folders in plan.items():
    entries = read_collection(None, [Path(f) for f, e in folders if not e], [Path(f) for f, e in folders if e])
    profile, machines, _definitions = native.configuration(entries)
    services = active_services(profile, native.service_users(machines))
    units = [n for n, m, _step in native.plan(machines, profile) if m is None]
    sources = {}
    with tempfile.TemporaryDirectory() as work:
        for n, m, step in native.plan(machines, profile):
            if m is None or ':' in step:
                continue
            folder = Path(work) / n.replace('/', '_')
            folder.mkdir(parents=True, exist_ok=True)
            path = native.adapter_source(folder, m, step, False, native.selected_services(profile, machines))
            sources[n] = Path(path).read_text(encoding='utf-8')
    for profiling in (False, True):
        sites = sorted((p['address'], p.get('after', p.get('symbol', ''))) for p in profile['patches']
                       if patch_active(p, services, profiling))
        out[name + (':profiling' if profiling else '')] = {'sites': sites, 'units': units, 'sources': sources}
print(json.dumps(out))
'''


def off_paths(root, folders):
    """{collection[:profiling] -> {'sites', 'units', 'sources'}} as the checkout at `root` plans them."""
    with tempfile.TemporaryDirectory() as work:
        plan = Path(work) / 'plan.json'
        plan.write_text(json.dumps({name: [(str(folders[n]), n in EXTENSIONS) for n in names]
                                    for name, names in COLLECTIONS.items()}), encoding='utf-8')
        run = subprocess.run([sys.executable, '-B', '-c', SCRIPT, str(plan)], cwd=root, capture_output=True,
                             text=True, encoding='utf-8', timeout=900,
                             env={**os.environ, 'PYTHONDONTWRITEBYTECODE': '1'})
    if run.returncode:
        raise AssertionError(f'planning in {root} failed:\n{run.stderr[-3000:]}')
    return json.loads(run.stdout)


class OffUnlessAsked(unittest.TestCase):
    def test_no_capability_means_the_sites_units_and_sources_of_the_checkout_without_them(self):
        with tempfile.TemporaryDirectory() as work:
            folders = machine_folders(Path(work))
            expected = off_paths(without_capabilities(ROOT, Path(work) / 'bare'), folders)
            actual = off_paths(ROOT, folders)
        self.assertEqual(sorted(actual), sorted(expected))
        self.assertEqual(len(actual), 2 * len(COLLECTIONS))
        for key in sorted(expected):
            with self.subTest(collection=key):
                self.assertEqual(actual[key]['sites'], expected[key]['sites'])
                self.assertEqual(actual[key]['units'], expected[key]['units'])
                self.assertEqual(sorted(actual[key]['sources']), sorted(expected[key]['sources']))
                for name in sorted(expected[key]['sources']):
                    self.assertEqual(actual[key]['sources'][name], expected[key]['sources'][name], name)
                self.assertTrue(actual[key]['sites'] and actual[key]['units'])
        # The bus-shift effect's adapter is among the sources compared (the generator path idle changes).
        self.assertTrue(any('probebus' in name for key in actual for name in actual[key]['sources']))
        # A piece the two trees would both generate alike, because the copy keeps the generator, is still idle's:
        # with nothing selected no adapter records into the send hook's record (send_bus::declare writes
        # sdk_send_bus, which only idle's send_fx.cpp defines) or calls the idle cut.
        for key in sorted(actual):
            for name, text in sorted(actual[key]['sources'].items()):
                for piece in ('send_bus::declare', 'idle_cut::'):
                    self.assertNotIn(piece, text, f'{key} {name}')


if __name__ == '__main__':
    unittest.main()
