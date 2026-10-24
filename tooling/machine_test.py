"""Check machine folders anywhere, as a machine repository's CI does.

For each folder: the machine's records and sources against this SDK, a browser
preview build of a synth, the machine's own host tests (tests/*.cpp, compiled
against the SDK headers and run), and optionally a native compile of the
bundled collection plus every folder. No stock files and no device.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from .formats import require
from .machines import machine_at
from .native import check_sources, validate_panel
from .pipeline import ROOT

HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}


def host_tests(m, host_cxx, scratch):
    """Compiles and runs each tests/*.cpp of the machine; its own sound and behaviour checks."""
    tests = sorted((m.root / 'tests').glob('*.cpp'))
    for test in tests:
        binary = scratch / (m.id + '-' + test.stem + ('.exe' if os.name == 'nt' else ''))
        command = [host_cxx, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(ROOT / 'sdk/include'),
                   '-I' + str(m.root), str(test), '-o', str(binary)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=300, **HIDDEN)
        require(result.returncode == 0, f'{m.id}: {test.name} does not compile:\n{result.stdout}{result.stderr}')
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=300, **HIDDEN)
        require(result.returncode == 0, f'{m.id}: {test.name} failed:\n{result.stdout}{result.stderr}')
    return len(tests)


def main(argv=None):
    parser = argparse.ArgumentParser(prog='sdk.py test', description=__doc__)
    parser.add_argument('--machine', type=Path, action='append', required=True, help='machine folder; repeatable')
    parser.add_argument('--cxx', default='clang++', help='Clang++ with wasm32/wasm-ld, for synth previews')
    parser.add_argument('--host-cxx', default='c++', help='host C++11 compiler for tests/*.cpp')
    parser.add_argument('--firmware', action='store_true',
                        help='also compile the bundled collection plus these machines with the pinned Blackfin tools')
    args = parser.parse_args(argv)
    try:
        machines = [machine_at(folder) for folder in args.machine]
        require(len({m.id for m in machines}) == len(machines), 'Each machine folder needs its own ID')
        check_sources(machines)
        with tempfile.TemporaryDirectory(prefix='opxy-machine-test-') as scratch:
            scratch = Path(scratch)
            for m in machines:
                done = [f'{m.kind} {m.id} ({m.root})']
                if m.kind == 'synth':
                    from .preview import build
                    validate_panel(m)
                    build(args.cxx, m, scratch / ('preview-' + m.id))
                    done.append('preview built')
                count = host_tests(m, shutil.which(args.host_cxx) or args.host_cxx, scratch)
                done.append(f'{count} host test{"s" * (count != 1)} passed')
                print('PASS: ' + '; '.join(done))
            if args.firmware:
                command = [sys.executable, str(ROOT / 'sdk.py'), 'compile', '--out', str(scratch / 'module')]
                for folder in args.machine:
                    command += ['--machine', str(folder)]
                subprocess.run(command, check=True, cwd=ROOT, timeout=1800)
        print('PASS: machine checks; no stock files or device operations.')
        return 0
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        return 1
