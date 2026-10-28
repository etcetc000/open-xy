"""Export a self-contained browser builder from a freshly verified native build.

Published kits come from the release host after release verification;
--personal exports one for your own collection on any pinned host. The
delivered HTML needs neither Python nor a server.
Object ownership comes from the native build; presentation from machine.json.
"""
import argparse
import base64
import json
from pathlib import Path
import subprocess
import sys

from .adapters import catalog_entry, sample_callbacks
from .catalog import official
from .definitions import control_positions, paged
from .formats import require, sha
from .machines import FIXED_NUMBERS
from .pipeline import ROOT, child, fresh_output, read, require_release_host, resolve_toolchain, tool_paths
from . import native, preview, screen


def notices(machines):
    texts = ['OP-XY SDK modules\n\n' + read(ROOT / 'LICENSE').decode('utf-8')]
    for m in machines:
        license_file = child(m.root, 'vendor/LICENSE')
        if license_file.is_file():
            texts.append(m.name + '\n\n' + read(license_file).decode('utf-8'))
    return '\n\n'.join(texts)


def catalog_sources(machines, personal):
    """The release of each catalog machine at its pinned commit, by ID. A published kit
    holds reviewed machines only: those. A personal kit may hold any folder."""
    from .catalog import load, pinned
    entries = load()
    sources = {m.id: next(e for e in entries if e['id'] == m.id) for m in machines
               if pinned(m, entries)}
    require(personal or all(m.id in sources for m in machines),
            'A published kit holds catalog machines at their pinned commits '
            '(sdk.py catalog fetch); build other folders into a --personal kit')
    return sources


def export(package, output, tools, preview_cxx='clang++', *, stock_catalog, personal=False):
    profile, machines, definitions = native.configuration(native.package_collection(package))
    sources = catalog_sources(machines, personal)
    require(sha(stock_catalog) == profile['stock_catalog_sha256'], 'Stock catalog hash mismatch')
    owners, services = native.object_owners(machines, profile), native.object_services(machines, profile)
    kit_machines = []
    for m in machines:
        # The numbers it asks for; the builder numbers each selection (core.mjs assignNumbers).
        entry = dict(id=m.id, name=m.name, kind=m.kind, prefix=m.prefix, description=m.description,
                     identity=m.requested or m.identity, controls=[p['label'] for p in m.definition['parameters']],
                     positions=control_positions(m.definition), pages=paged(m.definition))
        entry['catalog'] = catalog_entry(m)
        # Where the machine comes from, for the builder to show (core.mjs validOrigin): a
        # catalog machine is Open-XY's own only when maintainers list it (catalog.OFFICIAL).
        entry['origin'] = ('personal' if m.id not in sources
                           else 'official' if official(sources[m.id]) else 'catalog')
        if m.id in sources:
            entry['source'] = {key: sources[m.id][key] for key in ('repository', 'tag', 'commit', 'maintainer')}
            if 'cpu' in sources[m.id]:
                entry['cpu'] = sources[m.id]['cpu']
        entry['presetDefaults'] = m.preset_defaults
        if m.presets:
            entry['presets'] = m.presets
        if m.icon:
            entry['icon'] = m.icon
        # An unselected machine's startup and catalog callbacks decline.
        if m.kind == 'effect':
            entry['absent'] = {m.prefix + suffix: 'kit_absent' for suffix in ('_fx_register', '_ready')}
        else:
            entry['absent'] = {m.prefix + suffix: 'kit_absent' for suffix in ('_register', '_name_matches', '_ready', '_record_ready')}
            if m.kind == 'sample':
                # The shared sample table names these; unselected, its ready check declines first.
                entry['absent'].update({symbol: 'kit_absent' for symbol in sample_callbacks(m)})
            if m.display != 'stock':
                entry['absent'][m.prefix + '_screen_register'] = 'kit_absent'
        if m.kind in ('effect', 'sample'):
            entry['absent'][m.prefix + '_benchmark'] = 'kit_absent'
        if m.display != 'stock':
            entry['artwork'] = read(m.root / 'panel.svg.fragment').decode('utf-8')
            entry['asset'] = m.native_name + '.svg'
        if m.kind == 'synth':
            preview_dir = output / ('preview-' + m.id)
            preview.build(preview_cxx, m, preview_dir)
            preview.preview_bundle(preview_dir, require_reference=True)
            wasm = read(preview_dir / 'engine.wasm')
            entry['preview'] = dict(wasm=base64.b64encode(wasm).decode('ascii'), sha256=sha(wasm),
                                    graphics=preview.graphics_metadata(m),
                                    defaults=[round(p['default'] * 0x7fffffff) for p in m.definition['parameters']])
        kit_machines.append(entry)
    # The native build's own objects, in its link order.
    object_dir = output / 'objects'
    object_dir.mkdir()
    objects = []
    for name, owner in owners.items():
        data = read(child(package / 'analysis', name))
        destination = child(object_dir, name)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        entry = dict(name=name, owner=owner, bytes=base64.b64encode(data).decode('ascii'), sha256=sha(data))
        if name in services:
            entry['service'] = services[name]
        objects.append(entry)
    # Stock addresses only: each selection's machine numbers are the builder's.
    stock = {key: value for key, value in definitions.items() if key in native.DEFINITIONS}
    # The release version, as the firmware's boot label shows it.
    kit = dict(format='opxy-browser-kit/1', version=native.package_version(package), profile=profile,
               profileSource=base64.b64encode(read(ROOT / ('profiles/' + profile['id'] + '.json'))).decode('ascii'),
               definitions=stock, fixed=FIXED_NUMBERS,
               machines=kit_machines, objects=objects, notices=notices(machines),
               stockCatalog=base64.b64encode(stock_catalog).decode('ascii'),
               screenTemplate=screen.template().decode('utf-8'))
    kit_path = output / 'kit.json'
    kit_path.write_text(json.dumps(kit, separators=(',', ':')), encoding='utf-8')
    # Reference validation exercises every subset against GNU ld. Failure leaves
    # diagnostics only; no deliverable HTML is emitted.
    subprocess.run(['node', str(ROOT / 'tools/check_browser_kit.mjs'), str(kit_path),
                    str(object_dir), str(tools['ld']), str(tools['objcopy'])], cwd=ROOT, check=True)
    write_builder(kit, output)
    return kit


def write_builder(kit, output):
    """Bundle reviewed data and the shared UI without recompiling machine code."""
    require(kit.get('screenTemplate') == screen.template().decode('utf-8'), 'Unsupported platform screen template')
    if 'stockCatalog' in kit:
        catalog = base64.b64decode(kit['stockCatalog'], validate=True)
        require(sha(catalog) == kit['profile']['stock_catalog_sha256'], 'Bundled stock catalog hash mismatch')
    template = read(ROOT / 'builder/index.html').decode('utf-8')
    script = read(ROOT / 'builder/core.mjs').decode('utf-8').replace('export ', '')
    app = read(ROOT / 'builder/app.mjs').decode('utf-8')
    imports = ("import * as Core from './core.mjs';\nimport * as Wizard from './wizard.mjs';\n"
               "import * as Retained from '../sdk/browser/display.mjs';\n"
               "import * as Visuals from './visuals.mjs';\n"
               "import {Audition} from './audition.mjs';\n"
               "import {editedShown, shownControls} from '../sdk/browser/controls.mjs';\n"
               "import {supportsDeviceApis} from './browser.mjs';\n")
    require(app.startswith(imports), 'Unexpected browser imports')
    # Keep the public source as ES modules; inline a closed namespace for file://.
    exports = ('requireThat decode64 sha256 equal readObject readLayout assignNumbers chosenByDefault moduleBytes openKit linkKit readLdr '
               'patchFirmware recoverFirmware presetFiles buildAssets zipFiles sourceUrl sourceName').split()
    bundled = 'const Core = (() => {\n' + script + '\nreturn {' + ','.join(exports) + '};\n})();\n'
    bundled += 'const {editedShown, shownControls, inputWords} = (() => {\n' + read(ROOT / 'sdk/browser/controls.mjs').decode(
        'utf-8').replace('export ', '') + '\nreturn {editedShown, shownControls, inputWords};\n})();\n'
    controls_import = "import { inputWords } from './controls.mjs';\n"
    for namespace, file, classes in (('Retained', 'display.mjs', 'PreviewDisplay,SvgDisplay,headerSvg,NoteActivity'),):
        source = read(ROOT / 'sdk/browser' / file).decode('utf-8')
        require(source.count(controls_import) == 1, 'Unexpected display imports')
        bundled += 'const ' + namespace + '= (() => {\n' + source.replace(controls_import, '').replace(
            'export ', '') + '\nreturn {' + classes + '};\n})();\n'
    bundled += 'const Ferret = (() => {\n'+ read(ROOT / 'builder/ferret.mjs').decode('utf-8').replace(
        'export ', '') + '\nreturn {Session};\n})();\n'
    bundled += 'const Visuals = (() => {\n' + read(ROOT / 'builder/visuals.mjs').decode('utf-8').replace(
        'export ', '') + '\nreturn {icon,device,fileMap,bundle,guide,reveal};\n})();\n'
    wizard = read(ROOT / 'builder/wizard.mjs').decode('utf-8')
    bundled += 'const Mtp = (() => {\n' + read(ROOT / 'builder/mtp.mjs').decode('utf-8').replace(
        'export ', '') + '\nreturn {Device,supported};\n})();\n'
    wizard_import = ("import * as Ferret from './ferret.mjs';\n"
                     "import * as Mtp from './mtp.mjs';\n"
                     "import * as Visuals from './visuals.mjs';\n"
                     "import {presetFiles, sha256} from './core.mjs';\n")
    require(wizard.startswith(wizard_import), 'Unexpected wizard imports')
    bundled += 'const Wizard = (() => {\nconst {presetFiles, sha256} = Core;\n' + wizard[len(wizard_import):].replace(
        'export ', '') + '\nreturn {createWizard};\n})();\n'
    audition = read(ROOT / 'builder/audition.mjs').decode('utf-8')
    audition_imports = ("import engineSource from '../sdk/browser/engine.mjs?raw';\n"
                        "import processorSource from '../sdk/browser/processor.mjs?raw';\n")
    require(audition.startswith(audition_imports), 'Unexpected audition imports')
    bundled += 'const Audition = (() => {\n'
    for variable, file in [('engineSource', 'engine.mjs'), ('processorSource', 'processor.mjs')]:
        bundled += 'const ' + variable + '=' + json.dumps(read(ROOT / 'sdk/browser' / file).decode('utf-8')) + ';\n'
    bundled += audition[len(audition_imports):].replace('export ', '') + '\nreturn Audition;\n})();\n'
    bundled += read(ROOT / 'builder/browser.mjs').decode('utf-8').replace('export ', '') + '\n'
    bundled += app[len(imports):]
    # Include attribution even when refreshing a kit exported before device setup existed.
    kit = dict(kit)
    kit['notices'] += '\n\nFerret device setup — Bag of Waffles\n\n' + read(
        ROOT / 'builder/ferret.LICENSE').decode('utf-8')
    html = template.replace('<!-- styles -->', '<style>' + read(ROOT / 'builder/style.css').decode('utf-8') + '</style>')
    html = html.replace('<!-- kit -->', '<script id="kit-data" type="application/json">' +
                        json.dumps(kit, separators=(',', ':')).replace('<', '\\u003c') + '</script>')
    require('</script' not in bundled.lower(), 'Unsafe inline script boundary')
    html = html.replace('<!-- app -->', '<script type="module">' + bundled + '</script>')
    (output / 'builder.pending.html').write_text(html.replace('\r\n', '\n'), encoding='utf-8', newline='\n')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stock-os', 'out'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--stock-content', default=str(native.STOCK_CONTENT),
                        help="the folder holding op1_factory.json (default: this repository's copy)")
    parser.add_argument('--toolchain', help='pinned binutils directory (default: local/toolchain/bin)')
    parser.add_argument('--compiler', help='pinned Blackfin GCC bundle root (default: local/toolchain)')
    parser.add_argument('--preview-cxx', default='clang++', help='Clang++ with wasm32/wasm-ld')
    parser.add_argument('--host-cxx', default='g++', help='host C++11 compiler for the release fixtures')
    parser.add_argument('--personal', action='store_true',
                        help='a kit for your own collection on any pinned host, without release verification')
    native.add_collection_arguments(parser)
    args = parser.parse_args(argv)
    try:
        if not args.personal:
            require_release_host()
        args.toolchain, args.compiler = resolve_toolchain(args.toolchain, args.compiler)
    except ValueError as error:
        parser.exit(1, 'ERROR: ' + str(error) + '\n')
    try:
        # Refuse an unreviewed machine before the long build, not after it.
        catalog_sources(native.configuration(native.selected_collection(args))[1], args.personal)
    except ValueError as error:
        parser.exit(1, 'ERROR: ' + str(error) + '\n')
    out = fresh_output(args.out)
    try:
        package = out / 'source'
        native.build(package, Path(args.toolchain), Path(args.compiler),
                     Path(args.stock_os), Path(args.stock_content), native.selected_collection(args))
        options = []
        for name in ('toolchain', 'compiler', 'stock_os', 'stock_content'):
            options += ['--' + name.replace('_', '-'), str(Path(getattr(args, name)).resolve())]
        if not args.personal:  # independent rebuild against the reference bytes
            subprocess.run([sys.executable, str(ROOT / 'sdk.py'), 'check', '--native-package', str(package), *options,
                            '--host-cxx', args.host_cxx], cwd=ROOT, check=True)
        export(package, out, tool_paths(args.toolchain), args.preview_cxx,
               stock_catalog=read(Path(args.stock_content) / 'op1_factory.json'), personal=args.personal)
        # Validate firmware and content output against the independent Python writer.
        subprocess.run([sys.executable, str(ROOT / 'tools/check_browser_firmware.py'), str(out),
                        args.stock_os, args.stock_content], cwd=ROOT, check=True)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        # The unnamed builder and its inputs stay in --out for inspection.
        parser.exit(1, f'ERROR: {error}\nIncomplete export left in {out}; use a new --out to retry.\n')
    (out / 'builder.pending.html').rename(out / 'opxy-builder.html')
    print('Browser builder: ' + str(out / 'opxy-builder.html'))
    if args.personal:
        print('Personal kit: for your own collection and device; published kits come from release verification.')
    return 0
