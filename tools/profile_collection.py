"""Worker: import only the explicitly selected frozen SDK for native collection work."""
import argparse
import json
from pathlib import Path
import sys

FORMAT = 'openxy-collection-profile/1'
PINNED = ('collection.json', 'layout.json', 'kernel.oxc', 'analysis/compile-report.json',
          'analysis/module.bin', 'analysis/module.elf')


def build(args):
    from tooling import collection_bundle as bundle, collection_activation as activation, native, collection_kernel as kernel
    from tooling.collection_resident import check_kernel_layout
    from tooling.formats import elf32, require, sha
    from tooling.machines import read_collection, read_layout
    from tooling.pipeline import document, fresh_output, read, save_json
    from tooling.release import outside_checkouts
    _, baseline, info = bundle.check_package(args.kernel)
    previous, _, _, _ = bundle.read_bundle(args.previous)
    require(previous['kernel'] == sha(read(args.kernel / 'candidate/projects/openxy/kernel.oxc')),
            'Previous collection belongs to a different kernel')
    _, embedded, _ = native.configuration(native.package_collection(args.kernel), native.package_layout(args.kernel), loader=True)
    require(native.provenance(embedded) == baseline['sources'], 'Kernel SDK sources changed; use its exact snapshot')
    require(baseline['profiling'] is True, 'The resident kernel must have profiling enabled')
    entries = read_collection(args.collection) if args.collection else previous['collection']
    layout = read_layout(args.previous / 'layout.json')
    require(all(layout.get(k) == v for k, v in read_layout(args.kernel / 'layout.json').items()),
            'An embedded machine ID changed')
    profile, machines, definitions = native.configuration(entries, layout, loader=True)
    machines.collection_external = True
    require(outside_checkouts(args.out), 'Keep executable collections outside every checkout')
    out = fresh_output(args.out); analysis = out / 'analysis'; analysis.mkdir()
    module, report, _ = native.compile_collection(analysis, args.toolchain, args.compiler, profile, machines,
                                                definitions, native.package_version(args.kernel), True, True)
    require(report['collection_interface'] == info['interface'] and report['collection_platform'] == info['platform'],
            'Collection requires a different resident kernel')
    geometry = info['interface']['geometry']; resident = geometry['resident_bytes']
    original = read(args.kernel / 'analysis/module.elf')
    require(module[:resident] == read(args.kernel / 'analysis/module.bin')[:resident] and
            bundle.resident_exports(read(analysis / 'module.elf'), geometry) == bundle.resident_exports(original, geometry),
            'Collection changed resident bytes or exported addresses')
    checked = check_kernel_layout(read(analysis / 'module.elf'), info['interface'])
    sections, _ = elf32(read(analysis / 'module.elf')); base = geometry['payload_base']
    image = activation.encode(module[resident:], base=base, capacity=geometry['payload_bytes'],
        bss_offset=sections['.bss']['address']-base, bss_bytes=sections['.bss']['size'], text_bytes=sections['.text']['size'],
        entries=len(info['interface']['entries']), platform=bytes.fromhex(info['platform']), abi=bytes.fromhex(info['abi']),
        resources_offset=checked['resources_offset'], resources_count=checked['resources_count'])
    (out / 'bundle.oxc').write_bytes(image)
    assets = native.prepare_assets(read(native.STOCK_CONTENT / 'op1_factory.json'), profile, machines)
    resources = {}
    for kind, digest, content in kernel.resource_files(assets):
        name = ('asset-' if kind == 1 else 'catalog-') + digest.hex() + '.oxc'
        path = out / 'resources' / name; path.parent.mkdir(exist_ok=True); path.write_bytes(content)
        resources[name] = sha(content)
    description = dict(format='openxy-executable-collection/1', kernel=previous['kernel'], platform=info['platform'],
        abi=info['abi'], image_sha256=sha(image), digest=image[112:144].hex(), resources=resources, presets={},
        linked=report['linked'], interface=info['interface'], collection=entries)
    save_json(out / 'collection.json', description)
    save_json(out / 'layout.json', native.layout_document(native.numbers_after(layout, machines)))
    (out / 'kernel.oxc').write_bytes(read(args.kernel / 'candidate/projects/openxy/kernel.oxc'))
    # Bind the actual loaded bytes and the report; there is no pretend candidate OS.
    save_json(out / 'profiling.json', dict(format=FORMAT, kernel_package=str(args.kernel.resolve()),
        files={name: sha(read(out / name)) for name in PINNED}))
    result = inspect(out)
    return dict(package=str(out), collection_sha256=result['identity']['collection_sha256'],
                benchmark_cases=len(report['benchmark_cases']), sizes=report['sizes'], device_operations=False)


def inspect(package):
    from tooling import collection_bundle as bundle, collection_activation as activation, native, benchmark
    from tooling.collection_resident import check_kernel_layout
    from tooling.formats import require, sha
    from tooling.machines import read_layout
    from tooling.pipeline import document, read
    raw = read(package / 'profiling.json'); manifest = json.loads(raw)
    require(manifest.get('format') == FORMAT and set(manifest.get('files', {})) == set(PINNED), 'Invalid collection profiling manifest')
    require(all(sha(read(package / name)) == manifest['files'][name] for name in PINNED), 'Collection profiling file hash mismatch')
    _, baseline, kernel_info = bundle.check_package(Path(manifest['kernel_package']))
    description, info, image, _ = bundle.read_bundle(package)
    require(info == kernel_info and description['kernel'] == sha(read(Path(manifest['kernel_package']) / 'candidate/projects/openxy/kernel.oxc')),
            'Profiling collection/kernel mismatch')
    _, embedded, _ = native.configuration(native.package_collection(Path(manifest['kernel_package'])),
                                         native.package_layout(Path(manifest['kernel_package'])), loader=True)
    require(native.provenance(embedded) == baseline['sources'], 'Kernel SDK snapshot changed')
    report = document(package / 'analysis/compile-report.json')
    _, machines, _ = native.configuration(description['collection'], read_layout(package / 'layout.json'), loader=True)
    require(report.get('profiling') is True and report.get('benchmark_sweep') is True and
            report.get('benchmark_cases') == benchmark.cases(machines, True) and
            report['sources'] == native.provenance(machines) and report['linked'] == description['linked'],
            'Profiling workload, source or selection mismatch')
    require(report['collection_interface'] == info['interface'] and report['collection_platform'] == info['platform'],
            'Profiling interface mismatch')
    module = read(package / 'analysis/module.bin'); elf = read(package / 'analysis/module.elf')
    resident = info['interface']['geometry']['resident_bytes']; kernel = Path(manifest['kernel_package'])
    require(sha(module) == report['module_sha256'] and sha(elf) == report['elf_sha256'] and
            image[activation.HEADER_BYTES:] == module[resident:] and
            module[:resident] == read(kernel / 'analysis/module.bin')[:resident] and
            bundle.resident_exports(elf, info['interface']['geometry']) == bundle.resident_exports(read(kernel / 'analysis/module.elf'), info['interface']['geometry']),
            'Profiling module differs from the loaded payload or resident kernel')
    check_kernel_layout(elf, info['interface'])
    return dict(manifest=manifest, report=report, identity=dict(manifest_sha256=sha(raw),
        compile_report_sha256=manifest['files']['analysis/compile-report.json'], firmware_sha256=info['os_sha256'],
        collection_sha256=description['image_sha256'], kernel_record_sha256=description['kernel']))


def source(package, name):
    from tooling import native, catalog
    from tooling.formats import require, sha
    from tooling.machines import read_layout
    from tooling.pipeline import document
    result = inspect(package)
    description = document(package / 'collection.json')
    _, machines, _ = native.configuration(description['collection'], read_layout(package / 'layout.json'), loader=True)
    machine = next((m for m in machines if m.id == name), None)
    require(machine is not None, 'Unknown collection machine')
    require(not catalog.git('status', '--porcelain', '--untracked-files=all', '--', '.', cwd=machine.root).strip(),
            'Commit the machine before building and profiling it')
    files = machine.files(); prefix = 'external/' + name + '/'
    require({k: sha(v.read_bytes()) for k, v in files.items()} ==
            {k: v for k, v in result['report']['sources'].items() if k.startswith(prefix)}, 'Machine source differs from measured build')
    for path in files.values():
        catalog.git('ls-files', '--error-unmatch', '--', str(path.relative_to(machine.root)), cwd=machine.root)
    return dict(commit=catalog.git('rev-parse', 'HEAD', cwd=machine.root).strip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['build', 'inspect', 'source'])
    for name in ('sdk', 'package', 'kernel', 'previous', 'out', 'toolchain', 'compiler', 'collection'):
        parser.add_argument('--' + name, type=Path, required=name == 'sdk')
    parser.add_argument('--machine')
    args = parser.parse_args()
    # The caller selects trusted SDK code explicitly; package metadata never selects executable Python.
    sys.path.insert(0, str(args.sdk.resolve()))
    try:
        result = build(args) if args.action == 'build' else inspect(args.package) if args.action == 'inspect' else source(args.package, args.machine)
        print(json.dumps(result))
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
