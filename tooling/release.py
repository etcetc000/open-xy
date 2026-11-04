"""Release the builder site: the catalog's machines in a verified kit, then the site.

1. Fetch every published catalog machine at its pinned commit, confirming
   its tag (sdk.py catalog fetch --tags).
2. Export the release kit from the bundled machines plus those (sdk.py kit, with
   release verification on the release host).
3. Build the site around the kit and deploy it to Cloudflare: --deploy beta
   from staging (beta.open-xy.com), --deploy production from main
   (open-xy.com). --dry-run builds and checks everything without publishing.

Stock files, the kit and the site stay outside every checkout.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

from .formats import require
from .native import STOCK_CONTENT
from .pipeline import NO_WINDOW, ROOT, add_overwrite_argument, fresh_output

# The site each deployment publishes, and the branch it comes from (docs/builder.md).
TARGETS = {'beta': ('staging', ['--env', 'beta']), 'production': ('main', [])}


def run(command, **options):
    print('$ ' + ' '.join(str(part) for part in command), flush=True)
    subprocess.run([str(part) for part in command], cwd=ROOT, check=True, creationflags=NO_WINDOW, **options)


def git(*arguments):
    result = subprocess.run(['git', *arguments], cwd=ROOT, capture_output=True, text=True, check=True,
                            creationflags=NO_WINDOW)
    return result.stdout.strip()


def branch():
    return git('rev-parse', '--abbrev-ref', 'HEAD')


def published(source):
    """The checkout is exactly the reviewed branch: no local edits, nothing unpushed or behind."""
    git('fetch', '--quiet', 'origin', source)
    return not git('status', '--porcelain') and git('rev-parse', 'HEAD') == git('rev-parse', 'origin/' + source)


def outside_checkouts(path):
    """No folder above the path is a git checkout: firmware and stock content stay out of every one."""
    path = Path(os.path.abspath(path))
    return not any((folder / '.git').exists() for folder in (path, *path.parents))


def main(argv=None):
    parser = argparse.ArgumentParser(prog='sdk.py release', description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--stock-os', type=Path, required=True)
    parser.add_argument('--stock-content', type=Path, default=STOCK_CONTENT,
                        help="the folder holding op1_factory.json (default: this repository's copy)")
    parser.add_argument('--out', type=Path, required=True, help='new folder outside every checkout: kit and site')
    add_overwrite_argument(parser)
    parser.add_argument('--deploy', choices=sorted(TARGETS), help='publish the site to this Cloudflare environment')
    parser.add_argument('--dry-run', action='store_true', help='build and check the deployment without publishing')
    parser.add_argument('--preview-cxx', default='clang++', help='Clang++ with wasm32/wasm-ld')
    parser.add_argument('--host-cxx', default='g++', help='host C++11 compiler for the release fixtures')
    parser.add_argument('--toolchain', help='pinned binutils directory (default: local/toolchain/bin)')
    parser.add_argument('--compiler', help='pinned Blackfin GCC bundle root (default: local/toolchain)')
    args = parser.parse_args(argv)
    try:
        require(args.deploy or not args.dry_run, '--dry-run checks a deployment: name it with --deploy')
        require(outside_checkouts(args.out), 'Keep --out outside every checkout: the kit holds firmware and stock content')
        if args.deploy and not args.dry_run:
            source, _options = TARGETS[args.deploy]
            require(branch() == source, f'Deploy {args.deploy} from {source}; this checkout is on {branch()}')
            require(published(source), f'Deploy exactly origin/{source}: commit, push or pull first')
        out = fresh_output(args.out, args.overwrite,
                           [args.stock_os, args.stock_content, *filter(None, (args.toolchain, args.compiler))])
        python = sys.executable
        run([python, 'sdk.py', 'catalog', 'fetch', '--tags'])
        kit = out / 'kit'
        run([python, 'sdk.py', 'kit', '--collection', ROOT / 'local/catalog/collection.json',
             '--stock-os', args.stock_os.resolve(), '--stock-content', args.stock_content.resolve(),
             '--preview-cxx', args.preview_cxx, '--host-cxx', args.host_cxx, '--out', kit,
             *(['--toolchain', args.toolchain] if args.toolchain else []),
             *(['--compiler', args.compiler] if args.compiler else [])])
        print('Release kit: ' + str(kit / 'kit.json'))
        if args.deploy:
            _source, options = TARGETS[args.deploy]
            # The builder's own tests, then the release site built around this kit.
            run(['npm', 'run', 'test:web'], shell=os.name == 'nt')
            environment = dict(os.environ, OPEN_XY_KIT=str(kit / 'kit.json'), OPEN_XY_OUT_DIR=str(out / 'site'))
            run(['node', 'tools/deploy_cloudflare.mjs', *options, *(['--dry-run'] if args.dry_run else [])],
                env=environment)
            print(('Checked, not published: ' if args.dry_run else 'Published: ') +
                  ('beta.open-xy.com' if args.deploy == 'beta' else 'open-xy.com'))
        return 0
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        return 1
