"""Check machine and extension folders anywhere, as their repositories' CI does.

For each machine: its records and sources against this SDK, a browser preview
build of a synth, its own host tests (tests/*.cpp, compiled against the SDK
headers and run) and an estimate of its CPU cost next to Open-XY's core
machines'. For each extension: its extension.json and sources against the SDK's
registry and source rules, and its own host tests, which it must have.
Optionally a native compile of every folder together. No stock files and no device.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from . import cpu
from .formats import require
from .machines import machine_at
from .native import check_sources
from .pipeline import NO_WINDOW, ROOT


def host_tests(m, host_cxx, scratch):
    """Compiles and runs each tests/*.cpp of a machine or extension; its own checks."""
    tests = sorted((m.root / 'tests').glob('*.cpp'))
    for test in tests:
        binary = scratch / (m.id + '-' + test.stem + ('.exe' if os.name == 'nt' else ''))
        # The folder's include paths, as its preview and native builds use them.
        includes = [ROOT / 'sdk/include', m.root, *(m.root / extra for extra in getattr(m, 'includes', ()))]
        command = [host_cxx, '-std=c++11', '-O2', '-Wall', '-Werror', *('-I' + str(path) for path in includes),
                   str(test), '-o', str(binary)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=300, creationflags=NO_WINDOW)
        require(result.returncode == 0, f'{m.id}: {test.name} does not compile:\n{result.stdout}{result.stderr}')
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=300, creationflags=NO_WINDOW)
        require(result.returncode == 0, f'{m.id}: {test.name} failed:\n{result.stdout}{result.stderr}')
    return len(tests)


def main(argv=None):
    parser = argparse.ArgumentParser(prog='sdk.py test', description=__doc__)
    parser.add_argument('--machine', type=Path, action='append', default=[], help='machine folder; repeatable')
    parser.add_argument('--extension', type=Path, action='append', default=[], help='extension folder; repeatable')
    parser.add_argument('--cxx', default='clang++', help='Clang++ with wasm32/wasm-ld, for synth previews')
    parser.add_argument('--host-cxx', default='c++', help='host C++11 compiler for tests/*.cpp')
    parser.add_argument('--no-cpu', action='store_true', help='skip the CPU estimate')
    parser.add_argument('--firmware', action='store_true',
                        help='also compile these folders together with the pinned Blackfin tools')
    args = parser.parse_args(argv)
    if not args.machine and not args.extension:
        parser.error('pass --machine or --extension')
    try:
        from .extensions import check_collection, extension_at
        machines = [machine_at(folder) for folder in args.machine]
        extensions = check_collection([extension_at(folder) for folder in args.extension])
        require(len({m.id for m in machines}) == len(machines), 'Each machine folder needs its own ID')
        check_sources([m for m in machines if m.kind != 'synth'])  # a synth's preview build checks its own
        with tempfile.TemporaryDirectory(prefix='opxy-machine-test-') as scratch:
            scratch = Path(scratch)
            for m in machines:
                done = [f'{m.kind} {m.id} ({m.root})']
                if m.kind == 'synth':
                    from .preview import build
                    build(args.cxx, m, scratch / ('preview-' + m.id))
                    done.append('preview built')
                host_cxx = shutil.which(args.host_cxx) or args.host_cxx
                count = host_tests(m, host_cxx, scratch)
                done.append(f'{count} host test{"s" * (count != 1)} passed')
                print('PASS: ' + '; '.join(done))
                if not args.no_cpu:
                    print(cpu.summary(m, host_cxx, scratch))
            for e in extensions:
                host_cxx = shutil.which(args.host_cxx) or args.host_cxx
                count = host_tests(e, host_cxx, scratch)
                require(count, f'{e.id}: an extension carries host tests in tests/*.cpp')
                print(f'PASS: extension {e.id} ({e.root}); {count} host test{"s" * (count != 1)} passed')
            if args.firmware:
                command = [sys.executable, str(ROOT / 'sdk.py'), 'compile', '--out', str(scratch / 'module')]
                for m in machines:
                    command += ['--machine', str(m.root)]
                for e in extensions:
                    command += ['--extension', str(e.root)]
                subprocess.run(command, check=True, cwd=ROOT, timeout=1800)
        print('PASS: machine and extension checks; no stock files or device operations.')
        return 0
    except (ValueError, OSError, KeyError, TypeError, ET.ParseError, subprocess.SubprocessError) as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        return 1
