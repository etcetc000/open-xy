"""Boot lease state and task-mailbox admission, without device execution."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tooling.pipeline import ROOT


class StartupGate(unittest.TestCase):
    def test_lease_epoch_deadlines_late_commands_and_copy_failure(self):
        compiler=os.environ.get('OPXY_TEST_HOST_CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler: raise unittest.SkipTest('No host C++ compiler (set OPXY_TEST_HOST_CXX)')
        with tempfile.TemporaryDirectory() as temp:
            runner=Path(temp)/('gate.exe' if os.name=='nt' else 'gate')
            flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if os.environ.get('OPEN_XY_COLLECTION_SANITIZERS')=='1' else []
            subprocess.run([compiler,'-std=c++11','-Wall','-Wextra','-Werror',*flags,
                            '-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform'),
                            str(ROOT/'tests/fixtures/collection_gate.cpp'),
                            str(ROOT/'sdk/platform/collection_gate.cpp'),'-o',str(runner)],
                           check=True,capture_output=True)
            subprocess.run([str(runner)],check=True)


if __name__=='__main__': unittest.main()
