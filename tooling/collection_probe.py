"""Bounded collection-file inspection; external execution is deliberately absent."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct

from .formats import elf32, require, sha
from .pipeline import ROOT, check_patch_boundaries, child, document, fresh_output, read, save_json

OVERLAY = 'profiles/collection-probe-1.2.1.json'
OVERLAY_SHA256 = '16fc6dec18521741f55109c90677a2378563d722b4d51c0e75f8438eb0f1bc97'
MAGIC = b'OXYPROB1'
HEADER_BYTES = 128
DIGEST_AT = 72


def overlay():
    raw = read(ROOT / OVERLAY)
    require(sha(raw) == OVERLAY_SHA256, 'Unreviewed collection-probe MTP profile')
    return json.loads(raw)


def profile(base):
    """Append one authenticated MTP matcher call; every original rule stays intact."""
    from .firmware import decode_branch
    reviewed = overlay()
    require(reviewed['schema'] == 2 and reviewed['base'] == base['id'], 'Wrong collection-probe profile')
    result = copy.deepcopy(base)
    hook = reviewed['hook']
    require(decode_branch(int(hook['address'], 16), bytes.fromhex(hook['before'])) == (0x8d5c1bb0, True),
            'Collection MTP hook must replace the original matcher call')
    result['patches'].append(hook)
    check_patch_boundaries(result, len(base['patches']) + 1)
    return result


def platform_id(sources, services, profiling, compiler):
    """Machine selection is excluded; SDK, service capabilities and tools are bound."""
    shared = {k: v for k, v in sources.items() if k.startswith(('sdk/', 'tooling/', 'profiles/', 'linker/'))}
    value = dict(format='openxy-collection-probe/1', shared=shared, services=sorted(services),
                 profiling=profiling, compiler=compiler)
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).digest()


def generate(directory, sources, services, profiling, compiler, base, capacity):
    platform = platform_id(sources, services, profiling, compiler)
    source = read(ROOT / 'sdk/platform/opxy-1.2.1/collection_probe.cpp.in').decode()
    for name, value in dict(BASE=hex(base), BYTES=hex(capacity),
                            PLATFORM=', '.join(str(x) for x in platform),
                            MTP_PATTERN=json.dumps(overlay()['pattern'])).items():
        source = source.replace('@' + name + '@', value)
    require('@' not in source, 'Unexpanded collection probe')
    destination = Path(directory) / 'collection_probe.cpp'
    destination.write_text(source, encoding='utf-8', newline='\n')
    return destination


def encode(payload, *, base, capacity, bss_offset, bss_bytes, text_bytes, platform):
    require(len(platform) == 32, 'Invalid platform identifier')
    require(0 <= base <= 0xffffffff and 0 < capacity <= 0xffffffff - base, 'Invalid pool')
    require(0 < text_bytes <= len(payload) <= bss_offset <= capacity and
            0 <= bss_bytes <= capacity - bss_offset and not ((bss_offset | bss_bytes) & 3),
            'Collection outside bounded pool')
    header = bytearray(HEADER_BYTES)
    header[:8] = MAGIC
    struct.pack_into('<8I', header, 8, 1, HEADER_BYTES, base, len(payload), bss_offset, bss_bytes, text_bytes, 1)
    header[40:72] = platform
    header[DIGEST_AT:DIGEST_AT+32] = hashlib.sha256(header + payload).digest()
    return bytes(header) + payload


def inspect(data, *, base, capacity, platform):
    require(len(data) >= HEADER_BYTES, 'Truncated collection header')
    header = bytearray(data[:HEADER_BYTES])
    version, length, address, size, bss_at, bss_bytes, text_bytes, flags = struct.unpack_from('<8I', header, 8)
    require(header[:8] == MAGIC and version == 1 and length == HEADER_BYTES and flags == 1 and
            not any(header[104:]), 'Malformed inspection-only collection')
    require(header[40:72] == platform, 'Incompatible collection platform')
    require(address == base and 0 <= base <= 0xffffffff and 0 < capacity <= 0xffffffff - base and
            0 < text_bytes <= size <= bss_at <= capacity and
            bss_bytes <= capacity - bss_at and not ((bss_at | bss_bytes) & 3), 'Collection outside bounded pool')
    require(len(data) == HEADER_BYTES + size, 'Truncated collection or trailing bytes')
    digest = bytes(header[72:104]); header[72:104] = bytes(32)
    require(hashlib.sha256(header + data[HEADER_BYTES:]).digest() == digest, 'Collection checksum mismatch')
    return dict(format='openxy-collection-probe/1', execution=False, platform=platform.hex(),
                sha256=digest.hex(), initialized_bytes=size, bss_bytes=bss_bytes)


def pack(package, out):
    """Wrap a verified local static package; this never creates an executable loader."""
    from . import native
    from .release import outside_checkouts
    package = Path(package).resolve()
    report = document(package / 'analysis/compile-report.json')
    manifest = document(package / 'manifest.json')
    require(manifest.get('collection_probe') is True and report.get('collection_probe') is True,
            'Build the static package with --collection-probe first')
    require(document(package / 'verification.json').get('fresh_source_rebuild_matches') is True,
            'Package has no independent build verification')
    for name, digest in manifest['files'].items():
        require(sha(read(child(package, name))) == digest, 'Package hash mismatch: ' + name)
    p, machines, _ = native.configuration(native.package_collection(package), native.package_layout(package), True)
    require(report['sources'] == native.provenance(machines), 'Package sources changed; rebuild before packing')
    sections, symbols = elf32(read(package / 'analysis/module.elf'))
    payload = read(package / 'analysis/module.bin')
    require(sha(payload) == report['module_sha256'], 'Module hash mismatch')
    identity = platform_id(report['sources'], report['services'], report['profiling'], report['compiler'])
    options = dict(base=p['module_base'], capacity=native.module_bytes(p),
                   bss_offset=sections['.bss']['address']-p['module_base'], bss_bytes=sections['.bss']['size'],
                   text_bytes=sections['.text']['size'], platform=identity)
    require(symbols['__extensions_initialized_end']['value'] == p['module_base'] + len(payload), 'Module extent differs')
    data = encode(payload, **options)
    result = inspect(data, base=options['base'], capacity=options['capacity'], platform=identity)
    require(outside_checkouts(out), 'Keep collection code and packages outside every checkout')
    output = fresh_output(out)
    path = output / 'candidate/projects/openxy/collection.oxc'
    path.parent.mkdir(parents=True)
    path.write_bytes(data)
    save_json(output / 'inspection.json', result)
    return output, result


def main(argv=None):
    parser = argparse.ArgumentParser(prog='sdk.py collection', description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('pack', help='Create an inspection-only file from a --collection-probe build')
    command.add_argument('--package', type=Path, required=True)
    command.add_argument('--out', type=Path, required=True)
    executable=commands.add_parser('executable',help='Build a RAM collection for an already verified resident kernel')
    executable.add_argument('--kernel',type=Path,required=True)
    executable.add_argument('--out',type=Path,required=True)
    executable.add_argument('--toolchain',type=Path)
    executable.add_argument('--compiler',type=Path)
    executable.add_argument('--layout',type=Path)
    executable.add_argument('--previous',type=Path,help='Previous executable collection; preserves its selection and ID ledger')
    executable.add_argument('--remove',action='append',default=[],help='Machine ID to omit from the replacement collection')
    from . import native
    native.add_collection_arguments(executable)
    args = parser.parse_args(argv)
    try:
        if args.command=='executable':
            from . import collection_bundle
            from .pipeline import resolve_toolchain,tool_paths
            from .machines import read_layout
            tools=resolve_toolchain(args.toolchain,args.compiler);tool_paths(tools[0]);native.compiler_paths(tools[1])
            previous=collection_bundle.read_bundle(args.previous)[0] if args.previous else None
            entries=native.selected_collection(args) if args.collection else (
                (previous['collection'] if previous else native.package_collection(args.kernel))+native.selected_collection(args))
            layout=read_layout(args.layout) if args.layout else read_layout(
                (args.previous if args.previous else args.kernel)/'layout.json')
            if args.remove:
                manifest = document(args.kernel / 'manifest.json')
                _, selected, _ = native.configuration(entries, layout, loader=True,
                                                      expanded_memory=manifest.get('expanded_memory', False))
                require(set(args.remove)<=set(m.id for m in selected),'Cannot remove a machine absent from this collection')
                removed={m.source() for m in selected if m.id in args.remove}
                entries=[e for e in entries if e.get('path') not in removed]
            output,result=collection_bundle.pack(args.kernel,args.out,*tools,entries,
                layout)
            print(json.dumps(result,indent=2));print('Executable collection: '+str(output))
            return 0
        output, result = pack(args.package, args.out)
        print(json.dumps(result, indent=2))
        print('Inspection only; no external code can be executed. Files: ' + str(output / 'candidate'))
        return 0
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, str(error) + '\n')
