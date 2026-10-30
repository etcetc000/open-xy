"""Contributor checks. Public mode needs no firmware and never uses private fixtures."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
from .formats import require
from .pipeline import require_release_host, resolve_toolchain

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = {
    'stock_os': ('OPXY_TEST_STOCK_OS', None),
    'stock_content': ('OPXY_TEST_STOCK_CONTENT', 'op1_factory.json'),
    'native_package': ('OPXY_TEST_NATIVE_PACKAGE', 'analysis/module.elf'),
}


def fixture_environment(options, inherited):
    # Public CI and a developer's public check must mean the same thing, even
    # when the calling shell retains fixture paths from an earlier experiment.
    env = {k: v for k, v in inherited.items() if not k.startswith('OPXY_TEST_')}
    supplied = [getattr(options, key) for key in (*FIXTURES, 'toolchain')]
    if not any(supplied):
        return env, False
    if not all(supplied):
        raise ValueError('Native checks require --native-package, '
                         '--stock-os, --stock-content and --toolchain together.')
    for key, (variable, relative) in FIXTURES.items():
        path = Path(getattr(options, key)).resolve()
        expected = path / relative if relative else path
        if not expected.is_file():
            raise ValueError(f'Missing {key} input: {expected}')
        env[variable] = str(path)
    if not Path(options.toolchain).is_dir():
        raise ValueError(f'Missing toolchain directory: {options.toolchain}')
    env['OPXY_TEST_NATIVE_ELF'] = str(Path(env['OPXY_TEST_NATIVE_PACKAGE']) / 'analysis/module.elf')
    return env, True


def run_suite(require_all=False):
    suite = unittest.TestLoader().discover(str(ROOT / 'tests'))
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    if require_all and result.skipped:
        print('FAIL: private qualification may not skip tests.', file=sys.stderr)
        return 1
    return 0 if result.wasSuccessful() else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in (*FIXTURES, 'toolchain'):
        parser.add_argument('--' + name.replace('_', '-'), type=Path)
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--host-cxx', default='g++')
    args = parser.parse_args(argv)
    try:
        if any(getattr(args, key) for key in FIXTURES):  # tools default to the fetched toolchain
            args.toolchain, args.compiler = resolve_toolchain(args.toolchain, args.compiler)
        env, native = fixture_environment(args, os.environ)
        if native:
            require_release_host()
            env['OPXY_TEST_HOST_CXX'] = args.host_cxx
        if args.compiler and not native:
            raise ValueError('Compiler arguments require all native qualification inputs')
        def run(label, command):
            print(f'\n{label}', flush=True)
            subprocess.run([str(x) for x in command], cwd=ROOT, env=env,
                           check=True, timeout=1800)

        node_tests = sorted((ROOT / 'tests').glob('test_*.mjs'))
        if node_tests:
            run('Browser tests', ['node', '--test', *node_tests])
        if native:
            if not args.compiler:
                raise ValueError('Native verification needs --compiler')
            run('Independent native source rebuild and package verification',
                [sys.executable, ROOT / 'sdk.py', 'verify', '--stock-os', args.stock_os.resolve(),
                 '--stock-content', args.stock_content.resolve(), '--package', args.native_package.resolve(),
                 '--toolchain', args.toolchain.resolve(), '--compiler', args.compiler.resolve()])
        # One pass: SDK code checks with require(), never assert (test_checks), so
        # python -O would run the same checks again.
        run('Python checks', [sys.executable, '-m', 'tooling.checks', '_suite', *(['--require-all'] if native else [])])
        print('\nPASS: ' + ('native package verified; all fixture tests required.' if native else
                           'public source checks; private tests explicitly skipped.'))
        print('Host checks only: device timing, behavior and recovery are not covered.')
        return 0
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    if sys.argv[1:2] == ['_suite']:
        raise SystemExit(run_suite('--require-all' in sys.argv[2:]))
    raise SystemExit(main())
