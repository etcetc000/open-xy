"""Machine starters (sdk.py new) and browser-only previews of local synths. No firmware build."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys
import xml.etree.ElementTree as ET

from .contributors import validate_source_boundary
from .definitions import ROOT, label, stock_name, validate_definition
from .formats import require, sha
from .pipeline import fresh_output

KIND = 'cpp-browser-custom-preview'
REQUIRED = {'definition.json', 'core.h', 'engine.cpp', 'display.h', 'display.cpp', 'panel.svg.fragment'}
SUFFIXES = {'.json', '.cpp', '.h', '.hpp', '.inc', '.fragment'}


def source_files(directory):
    directory = Path(os.path.abspath(directory))
    require(directory.resolve() == directory and directory.is_dir(), 'Missing or redirected machine directory')
    files = {}
    for path in directory.rglob('*'):
        require(path.resolve() == path, 'Redirected machine source: ' + str(path))
        if path.is_file() and path.suffix in SUFFIXES:
            require(path.stat().st_size <= 1024 * 1024 and len(files) < 128, 'Machine source limit exceeded')
            files[path.relative_to(directory).as_posix()] = path.read_bytes()
    require(REQUIRED <= files.keys() and sum(map(len, files.values())) <= 8 * 1024 * 1024,
            'Machine needs definition.json, core.h, engine.cpp, display.h, display.cpp and panel.svg.fragment')
    for name, data in files.items():
        if Path(name).suffix not in {'.cpp', '.h', '.hpp', '.inc'}:
            continue
        text = data.decode('utf-8')
        validate_source_boundary(text, name)
        if name.startswith('tests/'):
            continue  # host-only checks (sdk.py test): never part of a preview or firmware
        code = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
        for directive in re.findall(r'^\s*#\s*(include[^\n]*)', code, re.M):
            match = re.fullmatch(r'include\s*[<"]([a-zA-Z0-9_./-]+)[>"]\s*', directive)
            require(match is not None, 'Use literal, local includes in ' + name)
            include = match[1]
            require('..' not in include.split('/') and not include.startswith('/'), 'Include escapes machine: ' + include)
            local = (Path(name).parent / include).as_posix()
            sdk = re.fullmatch(r'opxy/[a-z0-9_]+\.hpp', include) and (ROOT / 'sdk/include' / include).is_file()
            require(local in files or include in files or sdk, 'Include is not a machine or SDK header: ' + include)
    return files


def custom_identity(identity):
    stock_name(identity)
    require(not (ROOT / 'machines' / identity).exists(), 'Choose a distinct local machine ID')


def metadata(files):
    from .native import check_bindings, display_ids, panel_root
    from .preview import drawing_bindings
    definition = validate_definition(json.loads(files['definition.json']), 'synth')
    display = definition['resources']['display']
    custom_identity(definition['id'])
    # Artwork IDs carry the machine's ID: <id>_node_N, <id>_text_N and <id>_palette_N.
    prefix = definition['id']
    graphics = drawing_bindings(display_ids(prefix, display), display)
    artwork = files['panel.svg.fragment']
    require(len(artwork) <= 65536 and not re.search(br'<!DOCTYPE|<!ENTITY', artwork, re.I), 'Invalid drawing asset')
    # The native build's rules, so a machine that previews also builds for the device.
    root = panel_root(artwork, 'panel.svg.fragment')
    elements = {}
    allowed = {'id', 'd', 'cx', 'cy', 'r', 'transform', 'fill', 'stroke', 'stroke-width', 'stroke-linejoin',
               'font-family', 'font-size'}
    for node in root.iter():
        require(node.tag in {'g', 'path', 'circle', 'text'} and set(node.attrib) <= allowed,
                'Unsupported drawing element or attribute')
        for key in ('fill', 'stroke'):
            require(key not in node.attrib or re.fullmatch(r'none|#[0-9a-fA-F]{6}', node.attrib[key]),
                    'Drawing colors must be none or six-digit hex colors')
        identity = node.get('id')
        if identity:
            require(identity not in elements, 'Duplicate drawing asset ID')
            elements[identity] = node
    check_bindings(elements, prefix, display)
    for identity in graphics['palette']:
        node = elements.get(identity)
        require(node is not None and node.tag in {'path', 'circle'} and
                all(re.fullmatch(r'#[0-9a-fA-F]{6}', node.get(key, '')) for key in ('fill', 'stroke')) and
                0 < float(node.get('stroke-width', '0')) <= 8, 'Invalid drawing palette: ' + identity)
    return definition, dict(graphics, asset_sha256=sha(artwork))


def source_hashes(files):
    from .preview import SDK_FOLDERS, SDK_SOURCES
    paths = set(SDK_SOURCES) | {'tooling/custom_preview.py', 'tooling/native.py'}
    for folder in SDK_FOLDERS:
        paths |= {p.relative_to(ROOT).as_posix() for p in (ROOT / folder).rglob('*')
                  if p.is_file() and '__pycache__' not in p.parts and p.suffix != '.md'}
    return {'machine': {name: sha(data) for name, data in sorted(files.items())},
            'sdk': {name: sha((ROOT / name).read_bytes()) for name in sorted(paths)}}


def build(compiler, source, out):
    from .preview import compile_preview, preview_bundle
    source = Path(os.path.abspath(source))
    require(not Path(out).resolve().is_relative_to(source.resolve()), 'Keep preview output outside the machine source directory')
    files = source_files(source)
    definition, graphics = metadata(files)
    before = source_hashes(files)
    compiler = shutil.which(str(compiler))
    require(compiler, 'Clang++ with wasm32/wasm-ld required; run python sdk.py doctor --preview')
    compiler = Path(compiler).resolve()
    out = fresh_output(out)
    compile_preview(compiler, definition, out, source)
    require(source_hashes(source_files(source)) == before, 'Sources changed during preview build')
    (out / 'panel.svg').write_bytes(files['panel.svg.fragment'])
    manifest = dict(schema=1, kind=KIND, engine=definition['id'], sample_rate=44100, definition=definition,
                    source_root=str(source), sources=before, graphics=graphics,
                    wasm_sha256=sha((out / 'engine.wasm').read_bytes()), compiler={'sha256': sha(compiler.read_bytes())})
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    preview_bundle(out)
    print('Local custom-machine preview: ' + str(out))
    print('Run: python tools/serve_harness.py --preview "' + str(out) + '"')


def authenticate(manifest, artifacts):
    require(set(manifest) == {'schema', 'kind', 'engine', 'sample_rate', 'definition', 'source_root',
                              'sources', 'graphics', 'wasm_sha256', 'compiler'} and
            type(manifest['schema']) is int and manifest['schema'] == 1 and manifest['kind'] == KIND and
            type(manifest['sample_rate']) is int and manifest['sample_rate'] == 44100, 'Invalid custom preview manifest')
    require(isinstance(manifest['source_root'], str) and Path(manifest['source_root']).is_absolute(), 'Invalid machine source path')
    files = source_files(Path(manifest['source_root']))
    definition, graphics = metadata(files)
    require(manifest['sources'] == source_hashes(files), 'Preview is stale; rebuild from current source')
    require(manifest['definition'] == definition and manifest['engine'] == definition['id'] and
            manifest['graphics'] == graphics, 'Custom preview metadata differs from source')
    require(artifacts['panel.svg'] == files['panel.svg.fragment'] and
            artifacts['engine.wasm'][:8] == b'\0asm\x01\0\0\0' and
            sha(artifacts['engine.wasm']) == manifest['wasm_sha256'], 'Custom preview artifact identity mismatch')


# Machine kinds sdk.py new starts, each from sdk/templates/<kind>/.
STARTERS = ('synth', 'effect')


def scaffold(out, identity, name, kind='synth'):
    require(kind in STARTERS, 'sdk.py new starts a ' + ' or '.join(STARTERS))
    custom_identity(identity); label(name)
    out = fresh_output(out)
    starter = ROOT / 'sdk/templates' / kind
    for template in sorted(starter.rglob('*.in')):
        text = template.read_text(encoding='utf-8').replace('@ID@', identity).replace('@NAME@', name)
        destination = out / template.relative_to(starter).as_posix().removesuffix('.in')
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(text, encoding='utf-8')
    from .machines import SDK_VERSION
    python = 'python3' if Path(sys.executable).name.startswith('python3') else 'python'
    if kind == 'effect':
        from .machines import free_effect_identity, machine_at
        # The send's dry level follows the Dry control (docs/api.md, effects).
        record = {'schema': 1, 'sdk': '%d.%d' % SDK_VERSION, 'kind': 'effect', 'description': name,
                  'identity': free_effect_identity(), 'dryParameter': 'dry'}
        (out / 'machine.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        machine_at(out)
        print('Created local effect: ' + str(out))
        print(f'machine.json suggests effect {record["identity"]["effect"]}, free in this repository. '
              f'Effects have no browser preview; build with: {python} sdk.py compile --machine "{out}" --out <folder>')
        return
    from .native import display_ids
    display = json.loads((out / 'definition.json').read_text(encoding='utf-8'))['resources']['display']
    ids = display_ids(identity, display)
    nodes, palette = ids[:display['nodes']], ids[display['nodes'] + display['texts']:]
    outline = square_outline(display['points'])
    artwork = [f'<g id="{identity}_panel">']
    artwork += [f'  <path id="{node}" d="{outline}" fill="none" stroke="#ffffff" stroke-width="1"/>'
                for node in nodes[:display['paths']]]
    artwork += [f'  <circle id="{node}" cx="0" cy="0" r="2" fill="#ffffff"/>' for node in nodes[display['paths']:]]
    for i, entry in enumerate(palette):
        color = '#' + f'{i * 17:02x}' * 3
        artwork.append(f'  <circle id="{entry}" cx="640" cy="0" r="1" fill="{color}" stroke="{color}" stroke-width="1"/>')
    artwork.append('</g>')
    (out / 'panel.svg.fragment').write_text('\n'.join(artwork) + '\n', encoding='utf-8')
    from .machines import free_synth_identity
    # For a device build of this folder (sdk.py build --machine): an identity
    # no machine in this repository uses.
    record = {'schema': 1, 'sdk': '%d.%d' % SDK_VERSION, 'kind': 'synth', 'description': name,
              'identity': free_synth_identity()}
    (out / 'machine.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    metadata(source_files(out))
    print('Created local machine: ' + str(out))
    print(f'Preview with: {python} sdk.py preview --source "{out}" --out local/my-preview-001'
          ' (add --cxx <clang++> if doctor --preview needed one)')
    print(f'machine.json suggests engine {record["identity"]["engine"]}, page {record["identity"]["page"]} '
          f'for a device build, free in this repository: {python} sdk.py build --machine "{out}" (docs/build.md, step 3).')


def square_outline(points, half=32):
    """A square traced through exactly `points` vertices, as drawing paths require."""
    corners = [(-half, -half), (half, -half), (half, half), (-half, half)]
    vertices = []
    for i in range(points):
        side, fraction = divmod(4 * i / points, 1)
        (x0, y0), (x1, y1) = corners[int(side)], corners[(int(side) + 1) % 4]
        vertices.append((round(x0 + (x1 - x0) * fraction, 2), round(y0 + (y1 - y0) * fraction, 2)))
    return 'M' + ' L'.join(f'{x:g} {y:g}' for x, y in vertices) + ' Z'


def main(argv=None):
    parser = argparse.ArgumentParser(description='Start a machine folder: portable C++ that previews (synths) and builds anywhere')
    parser.add_argument('--id', required=True, help='lower-case machine ID, also its stock name and artwork prefix')
    parser.add_argument('--name', required=True, help='name shown on the OP-XY')
    parser.add_argument('--kind', choices=STARTERS, default='synth', help='machine kind to start (default: synth)')
    parser.add_argument('--out', required=True, type=Path, help='new folder for the machine source, such as local/mymachine')
    args = parser.parse_args(argv)
    try:
        scaffold(args.out, args.id, args.name, args.kind)
        return 0
    except (ValueError, OSError, ET.ParseError) as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        return 1
