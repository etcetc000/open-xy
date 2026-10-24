#!/usr/bin/env python3
"""OP-XY SDK commands. Local builds and checks only; no device operations."""
import sys

COMMANDS = '''Commands:
  check     Contributor checks: python sdk.py check [--help]
  preview   Browser audition build: python sdk.py preview --engine fm2op --out local/preview-001
  new       Start a machine: python sdk.py new [--kind synth|effect] --id mymachine --name "My Machine" --out ../mymachine
  test      Check machine folders anywhere: python sdk.py test --machine ../mymachine [--firmware]
  compile   Native module only: python sdk.py compile --help
  build     Native firmware package: python sdk.py build [--machine <folder>] --help
  verify    Rebuild and compare a native package: python sdk.py verify --help
  kit       Browser builder export: python sdk.py kit --help
  toolchain Install the pinned Blackfin tools: python sdk.py toolchain fetch
  doctor    Check tools: python sdk.py doctor --preview | --toolchain [<dir>]
Preview server: python tools/serve_harness.py --preview local/preview-001'''

if __name__ == '__main__':
    command, argv = (sys.argv[1], sys.argv[2:]) if len(sys.argv) > 1 else (None, [])
    if command in ('compile', 'build', 'verify'):
        from tooling.native import main
        raise SystemExit(main(argv, command))
    if command == 'new':
        from tooling.custom_preview import main
        raise SystemExit(main(argv))
    if command == 'test':
        from tooling.machine_test import main
        raise SystemExit(main(argv))
    if command == 'kit':
        from tooling.browser_kit import main
        raise SystemExit(main(argv))
    if command == 'preview':
        from tooling.preview import main
        raise SystemExit(main(argv))
    if command == 'toolchain':
        from tooling.toolchain import main
        raise SystemExit(main(argv))
    if command == 'check':
        from tooling.checks import main
        raise SystemExit(main(argv))
    if command == 'doctor':
        import argparse
        from tooling.pipeline import host_platform, resolve_toolchain, tool_paths
        parser = argparse.ArgumentParser(prog='sdk.py doctor')
        kind = parser.add_mutually_exclusive_group(required=True)
        kind.add_argument('--toolchain', nargs='?', const='', help='binutils directory (default: local/toolchain/bin)')
        kind.add_argument('--preview', action='store_true', help='Check Python, NumPy, Node and actual WASM compilation')
        parser.add_argument('--cxx', default='clang++', help='Clang++ for --preview')
        args = parser.parse_args(argv)
        if args.preview:
            from tooling.preview_doctor import check
            try:
                raise SystemExit(check(args.cxx))
            except (ValueError, OSError) as error:
                parser.exit(1, 'FAIL: ' + str(error) + '\n')
        try:
            tool_paths(args.toolchain or resolve_toolchain()[0])
        except (ValueError, KeyError) as error:
            parser.exit(1, 'FAIL: ' + str(error) + '\n')
        print(f'PASS: pinned Blackfin tools for {host_platform()}; no device operations')
        raise SystemExit(0)
    print(COMMANDS)
    raise SystemExit(0 if command in (None, '--help', '-h') else 2)
