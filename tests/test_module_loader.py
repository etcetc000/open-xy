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


if __name__=='__main__': unittest.main()
