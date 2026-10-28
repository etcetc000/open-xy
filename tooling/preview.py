"""Build synth auditions or sample drawing previews and authenticate served artifacts."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
from xml.etree.ElementTree import ParseError

from .formats import require, sha
from .definitions import ROOT
from .machines import machine_at
from .pipeline import NO_WINDOW, fresh_output


VOICE_EXPORTS = ('opxy_preview_abi', 'opxy_state_bytes', 'opxy_state_alignment', 'opxy_parameter_count',
                 'opxy_block_frames', 'opxy_sample_rate', 'opxy_output_peak_q31', 'opxy_voice_capacity',
                 'opxy_voice_state', 'opxy_initialize', 'opxy_start', 'opxy_destroy', 'opxy_render', '__heap_base')
DISPLAY_EXPORTS = tuple('opxy_display_' + name for name in ('abi', 'state_bytes', 'input_bytes', 'frame_bytes', 'state',
                                                            'input', 'frame', 'reset', 'render', 'valid', 'activity'))
MANIFEST_LIMIT = 65536
# Shared inputs of every curated preview; the machine's own folder is added per build.
SDK_SOURCES = ('tooling/preview.py', 'tooling/machines.py', 'tooling/adapters.py', 'tooling/definitions.py',
               'tooling/contributors.py', 'tools/check_preview_contract.mjs', 'sdk/host/voice_preview.cpp.in',
               'sdk/host/display_preview.cpp', 'sdk/platform/blackfin/memory.cpp')
SDK_FOLDERS = ('sdk/include', 'sdk/browser', 'sdk/platform/blackfin/freestanding')
FLAGS = ['--target=wasm32', '-std=c++11', '-O2', '-ffreestanding', '-fno-exceptions', '-fno-rtti',
         '-fno-threadsafe-statics', '-fno-use-cxa-atexit', '-fno-c++-static-destructors', '-nostdlib',
         '-Wall', '-Werror', '-Wno-unused']
# Fixed browser allowance for code/data/state; no memory growth during audio.
# Native collections have a separate 2040 KiB linker limit.
MEMORY = 2 * 1024 * 1024


def compile_preview(compiler, definition, out, root, sources=(), includes=(), machine=None):
    """One WebAssembly module: the SDK voice preview, the machine's sources and its display transport."""
    from .adapters import generate_display, generate_voice_preview
    audio = machine is None or machine.kind == 'synth'
    units = ([generate_voice_preview(out, definition), *(root / s for s in sources)] if audio else [])
    units.append(ROOT / 'sdk/platform/blackfin/memory.cpp')
    if machine is None:
        generate_display(out, definition=definition)
    else:
        generate_display(out, machine=machine)
    units += [out / 'display_binding.cpp', ROOT / 'sdk/host/display_preview.cpp']
    paths = [ROOT, ROOT / 'sdk/include', ROOT / 'sdk/platform/blackfin/freestanding', root,
             *(root / extra for extra in includes), out]
    command = [compiler, *FLAGS, *('-I' + str(path) for path in paths), *units, '-o', out / 'engine.wasm',
               '-Wl,--no-entry', '-Wl,-z,stack-size=16384', f'-Wl,--initial-memory={MEMORY}',
               f'-Wl,--max-memory={MEMORY}',
               *('-Wl,--export=' + name for name in (VOICE_EXPORTS if audio else ()) + DISPLAY_EXPORTS)]
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=120,
                            creationflags=NO_WINDOW)
    require(result.returncode == 0, 'Preview compilation failed:\n' + result.stderr +
            f'\nCheck the compiler with: python sdk.py doctor --preview --cxx "{compiler}"')
    display = definition.get('resources', {}).get('display', {})
    check_runtime(out / 'engine.wasm', display.get('paths', 0), display.get('points', 0))


def check_runtime(module, paths=0, points=0):
    """Smoke-test host contracts without requiring a particular sound."""
    result = subprocess.run(['node', str(ROOT / 'tools/check_preview_contract.mjs'), str(module), str(paths), str(points)],
                            capture_output=True, text=True, timeout=120,
                            creationflags=NO_WINDOW)
    require(result.returncode == 0, 'Preview contract check failed:\n' + result.stdout + result.stderr)
    print(result.stdout.strip())


def source_hashes(m):
    paths = set(SDK_SOURCES)
    for folder in SDK_FOLDERS:
        paths |= {p.relative_to(ROOT).as_posix() for p in (ROOT / folder).rglob('*')
                  if p.is_file() and '__pycache__' not in p.parts and p.suffix != '.md'}
    hashes = {name: sha((ROOT / name).read_bytes()) for name in paths}
    hashes.update({name: sha(path.read_bytes()) for name, path in m.files().items()})
    return dict(sorted(hashes.items()))


def graphics_metadata(m):
    from .native import display_ids
    asset = sha((m.root / 'panel.svg.fragment').read_bytes())
    display = m.definition['resources']['display']
    return dict(drawing_bindings(display_ids(m.prefix, display), display), asset_sha256=asset)


def drawing_bindings(ids, display):
    """Browser renderer bindings: node, text and palette IDs, how many nodes are paths, and their point count."""
    count = display['nodes'] + display['texts']
    return dict(nodes=ids[:display['nodes']], texts=ids[display['nodes']:count], palette=ids[count:],
                paths=display['paths'], points=display['points'])


def build(compiler, m, out):
    """The browser audition of a collection synth (a Machine)."""
    from .native import check_sources, validate_panel
    require(m.kind == 'synth' or (m.kind == 'sample' and m.display != 'stock'),
            'Browser previews need an authored drawing display; sample previews are silent')
    require(not m.contains(out), 'Keep preview output outside the machine folder')
    executable = shutil.which(str(compiler))
    require(executable is not None, 'Clang++ with wasm32/wasm-ld is required; run python sdk.py doctor --preview')
    compiler = Path(executable).resolve()
    check_sources([m])
    validate_panel(m)
    before = source_hashes(m)
    out = fresh_output(out)
    compile_preview(compiler, m.definition, out, m.root, m.sources, m.includes, machine=m)
    (out / 'panel.svg').write_bytes((m.root / 'panel.svg.fragment').read_bytes())
    require(source_hashes(m) == before, 'Source changed during preview build')
    version = subprocess.check_output([str(compiler), '--version'], text=True, timeout=30,
                                      creationflags=NO_WINDOW).splitlines()[0]
    manifest = dict(schema=1, kind='cpp-browser-preview', engine=m.id, root=str(m.root),
                    description=m.description,
                    definition=m.definition, sample_rate=44100,
                    wasm_sha256=sha((out / 'engine.wasm').read_bytes()), sources=before,
                    graphics=graphics_metadata(m), compiler=dict(version=version, sha256=sha(compiler.read_bytes())))
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
    preview_bundle(out)
    print(m.name + ' preview built: ' + str(out))
    print('Run: python tools/serve_harness.py --preview "' + str(out) + '"')


def preview_bundle(directory, *, require_reference=False):
    """Authenticate exactly three artifacts; return an immutable serving snapshot."""
    directory = Path(directory).absolute()
    require(directory.resolve() == directory, 'Redirected preview directory')
    files = {}
    for name, limit in [('manifest.json', MANIFEST_LIMIT), ('engine.wasm', 1048576), ('panel.svg', 65536)]:
        path = directory / name
        require(path.resolve() == path and path.is_file() and 0 < path.stat().st_size <= limit,
                'Missing, oversized or redirected preview artifact')
        files[name] = path.read_bytes()
    manifest = json.loads(files['manifest.json'])
    require(isinstance(manifest, dict), 'Invalid preview manifest')
    if manifest.get('kind') == 'cpp-browser-custom-preview':
        require(not require_reference, 'Custom previews are local auditions, not qualified collection inputs')
        from .custom_preview import authenticate
        authenticate(manifest, files)
        return artifact_bundle(files)
    require(set(manifest) == {'schema', 'kind', 'engine', 'root', 'description', 'definition', 'sample_rate', 'wasm_sha256',
                              'sources', 'compiler', 'graphics'}, 'Unexpected preview manifest fields')
    require(type(manifest['schema']) is int and manifest['schema'] == 1 and
            manifest['kind'] == 'cpp-browser-preview' and type(manifest['sample_rate']) is int and
            manifest['sample_rate'] == 44100, 'Unsupported preview contract')
    root = manifest['root']
    require(isinstance(root, str) and Path(root).is_absolute(), 'Invalid machine folder in preview')
    m = machine_at(root)
    require(m.id == manifest['engine'] and m.kind in ('synth', 'sample') and m.display != 'stock',
            'Browser previews need an authored drawing display')
    require(manifest['sources'] == source_hashes(m), 'Preview is stale; rebuild from current source')
    require(manifest['definition'] == m.definition and manifest['description'] == m.description,
            'Preview controls or description differ from the machine')
    require(manifest['graphics'] == graphics_metadata(m), 'Preview graphics identity mismatch')
    require(sha(files['panel.svg']) == manifest['graphics']['asset_sha256'], 'Preview artwork identity mismatch')
    require(files['engine.wasm'][:8] == b'\0asm\x01\0\0\0' and
            sha(files['engine.wasm']) == manifest['wasm_sha256'], 'Preview module identity mismatch')
    return artifact_bundle(files)


def artifact_bundle(files):
    return {'/preview/' + name: (mime, files[name]) for name, mime in (
        ('manifest.json', 'application/json'), ('engine.wasm', 'application/wasm'), ('panel.svg', 'image/svg+xml'))}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='clang++')
    parser.add_argument('--out', required=True, type=Path)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--machine', type=Path, help='Instrument folder with an authored display')
    source.add_argument('--source', type=Path, help='Local custom-machine directory')
    args = parser.parse_args(argv)
    try:
        if args.source:
            from .custom_preview import build as build_custom
            build_custom(args.cxx, args.source, args.out)
        else:
            build(args.cxx, machine_at(args.machine), args.out)
        return 0
    except (ValueError, OSError, ParseError, subprocess.SubprocessError) as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        return 1
