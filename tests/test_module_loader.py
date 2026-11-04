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
