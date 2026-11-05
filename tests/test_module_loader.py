"""Module files are checked, relocated and synchronized before any code can run."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tooling.pipeline import ROOT


class ModuleLoader(unittest.TestCase):
    def test_stage_relocates_valid_modules_and_refuses_everything_else(self):
        compiler=os.environ.get('OPXY_TEST_HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler: raise unittest.SkipTest('No host C++ compiler (set OPXY_TEST_HOST_CXX)')
        with tempfile.TemporaryDirectory() as temp:
            runner=Path(temp)/('loader.exe' if os.name=='nt' else 'loader')
            flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if os.environ.get('OPEN_XY_COLLECTION_SANITIZERS')=='1' else []
            subprocess.run([compiler,'-std=c++11','-Wall','-Wextra','-Werror',*flags,
                            '-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform'),
                            str(ROOT/'tests/fixtures/module_loader.cpp'),
                            str(ROOT/'sdk/platform/module_loader.cpp'),'-o',str(runner)],
                           check=True,capture_output=True)
            subprocess.run([str(runner)],check=True)
            self.catalog_splices(runner, Path(temp))
            self.numbers_match_a_build(runner)

    def numbers_match_a_build(self, runner):
        """A module gets the numbers a build without a layout gives the same machine."""
        from tooling.machines import config, number_pools, preferred
        rules = config()['identities']
        pools = number_pools(rules)
        for slot, first in (('engine', rules['engine_none'] + 1), ('page', rules['page_none'] + 1)):
            self.assertEqual(pools[slot], list(range(first, 256)), slot)
            for name in ('karplus', 'macroosc', 'a', 'troupe', 'my_synth_2', 'x' * 15):
                got = int(subprocess.check_output([str(runner), 'preferred', name, str(first), '256'], text=True))
                self.assertEqual(got, preferred(pools[slot], name)[0], (slot, name))

    def test_module_presets_appear_where_the_preset_walk_looks_and_nowhere_else(self):
        compiler=os.environ.get('OPXY_TEST_HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler: raise unittest.SkipTest('No host C++ compiler (set OPXY_TEST_HOST_CXX)')
        with tempfile.TemporaryDirectory() as temp:
            runner=Path(temp)/('presets.exe' if os.name=='nt' else 'presets')
            subprocess.run([compiler,'-std=c++11','-Wall','-Wextra','-Werror',
                            '-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform'),
                            str(ROOT/'tests/fixtures/module_presets.cpp'),
                            str(ROOT/'sdk/platform/module_presets.cpp'),'-o',str(runner)],
                           check=True,capture_output=True)
            result=subprocess.run([str(runner)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_module_numbers_follow_the_profile_and_the_fixed_machines(self):
        """module_synths.cpp holds what a build holds: the profile's pools, the
        reserved last page and the fixed machines' numbers."""
        import re
        from tooling.machines import FIXED_NUMBERS, config
        rules = config()['identities']
        text = (ROOT/'sdk/platform/opxy-1.2.1/module_synths.cpp').read_text(encoding='utf-8')
        found = re.search(r'engine_first = (\d+), engine_end = (\d+), page_first = (\d+), page_end = (\d+), '
                          r'page_reserved = (\d+);', text)
        self.assertIsNotNone(found)
        self.assertEqual(tuple(map(int, found.groups())),
                         (rules['engine_none'] + 1, rules['engines'], rules['page_none'] + 1, rules['pages'],
                          rules['pages'] - 1))
        self.assertTrue(all(n <= rules['engine_none'] for n in rules['stock_engines']), 'stock engines inside the pool')
        fixed = {name: (int(e), int(p)) for name, e, p in re.findall(r'\{"(\w+)", (\d+), (\d+)\}', text)}
        self.assertEqual(fixed, {name: (identity.get('engine', identity.get('effect')), identity['page'])
                                 for name, identity in FIXED_NUMBERS.items()})

    def catalog_splices(self, runner, temp):
        """A module entry lands at the end of engine_types and nowhere else, in the
        stock catalog and in one a build has already extended."""
        import json
        from tooling import native
        stock = (ROOT/'profiles/opxy-1.2.1/content/op1_factory.json').read_bytes()
        catalogs = [stock]
        try:
            entries = [{'path': str((ROOT/'../open-xy-core-machines/machines/karplus').resolve())}]
            profile, machines, _ = native.configuration(entries, None)
            catalogs.append(native.extend_catalog(stock, profile, machines))
        except (ValueError, OSError, KeyError):
            pass  # no core-machines checkout beside this one: the stock catalog alone
        entry = '{"type": "modulesynth", "default_params": [1, 2, 3, 4, 5, 6, 7, 8]}'
        for n, catalog in enumerate(catalogs):
            path = temp/f'catalog-{n}.json'
            path.write_bytes(catalog)
            at = int(subprocess.check_output([str(runner), 'array_end', str(path)], text=True))
            self.assertEqual(catalog[at:at+1], b']')
            spliced = json.loads(catalog[:at] + b',' + entry.encode() + catalog[at:])
            base = json.loads(catalog)
            self.assertEqual(spliced, dict(base, engine_types=base['engine_types'] + [json.loads(entry)]))


if __name__=='__main__': unittest.main()
