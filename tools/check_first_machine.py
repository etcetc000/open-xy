"""Exercise new-machine creation, preview, stale-source rejection and octave edit."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tooling.custom_preview import scaffold, build
from tooling.formats import require
from tooling.preview import preview_bundle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='clang++')
    args = parser.parse_args()
    compiler = shutil.which(args.cxx)
    require(compiler, 'Clang++ with wasm32/wasm-ld required')
    with tempfile.TemporaryDirectory(prefix='opxy-first-machine-') as temporary:
        directory = Path(temporary).resolve()
        source = directory / 'machine'
        first, second = directory / 'first', directory / 'second'
        scaffold(source, 'firstmachine', 'First Machine', repository=True)
        build(compiler, source, first)
        preview_bundle(first)
        engine = source / 'engine.cpp'
        original = engine.read_text()
        require(original.count('voice.phase += pitch[i];') == 1, 'Tutorial edit no longer matches the starter')
        engine.write_text(original.replace('voice.phase += pitch[i];', 'voice.phase += pitch[i] / 2;'))
        try:
            preview_bundle(first)
        except ValueError as error:
            require('stale' in str(error), 'Unexpected stale-source failure')
        else:
            raise ValueError('Edited source must invalidate an existing preview')
        build(compiler, source, second)
        subprocess.run(['node', str(Path(__file__).with_suffix('.mjs')), str(first / 'engine.wasm'), str(second / 'engine.wasm')],
                       check=True, timeout=30, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    print('PASS: complete first-machine workflow; source freshness and audible edit verified.')


if __name__ == '__main__':
    main()
