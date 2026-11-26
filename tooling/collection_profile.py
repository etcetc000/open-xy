"""Profile collections with their frozen compilation SDK, without changing that SDK."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from .formats import require
from .pipeline import ROOT


def run(action, sdk, package=None, **options):
    require(sdk is not None, 'RAM profiling requires --sdk with the resident kernel SDK snapshot')
    sdk = Path(sdk).resolve()
    require((sdk / 'tooling/collection_bundle.py').is_file(), 'The selected SDK must support resident collections')
    command = [sys.executable, str(ROOT / 'tools/profile_collection.py'), action, '--sdk', str(sdk)]
    if package is not None:
        command += ['--package', str(Path(package).resolve())]
    for key, value in options.items():
        if value is not None:
            command += ['--' + key.replace('_', '-'), str(Path(value).resolve()) if key != 'machine' else value]
    result = subprocess.run(command, capture_output=True, text=True, timeout=1800,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    require(result.returncode == 0, 'Collection profiling: ' + result.stderr[-3000:])
    return json.loads(result.stdout)


def main(argv=None):
    parser = argparse.ArgumentParser(prog='sdk.py profile collection', description='Build a swept RAM collection using its exact resident SDK. No device operations.')
    for name in ('sdk', 'kernel', 'previous', 'out', 'toolchain', 'compiler'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--collection', type=Path, help='Explicit complete selection; default: keep the previous collection')
    args = parser.parse_args(argv)
    result = run('build', args.sdk, kernel=args.kernel, previous=args.previous, out=args.out,
                 toolchain=args.toolchain, compiler=args.compiler, collection=args.collection)
    print(json.dumps(result, indent=2))
