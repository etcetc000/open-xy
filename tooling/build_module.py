"""Build a module file (.opx, format 0, see sdk/platform/module_loader.hpp) from a module folder.

  python sdk.py module build <folder> --out <file>.opx

The folder holds src/*.cpp; one unit exports extern "C" opxy_module_init.
Steps: compile with the pinned compiler, link at base 0 with relocations kept
(ld -q), turn the kept relocations into the .opx list (ABS32 / HI16 / LO16 with
target offsets; pc-relative ones need nothing), write image + relocs + trailer.
Self-check: link again at another base and require that applying the list to
the base-0 image gives exactly that second image.
"""
import argparse
import json
from pathlib import Path
import struct
import tempfile
import zlib

from .formats import elf_sections, require
from .pipeline import ROOT, execute, resolve_toolchain, tool_paths

LINKER = ROOT / 'linker/module.ld'
CHECK_BASE = 0x01234560  # aligned like the loader's bases (32)
MAGIC = b'OPXYMOD\0'
FORMAT = 0
TRAILER = struct.Struct('<9I2H8s')
OS_1_2_1 = 0x00010201
ABS32, HI16, LO16 = 1, 2, 3
# binutils include/elf/bfin.h
R_BFIN_LUIMM16, R_BFIN_HUIMM16, R_BFIN_BYTE4_DATA = 0x06, 0x07, 0x12
PC_RELATIVE = {0x01, 0x03, 0x04, 0x08, 0x09, 0x0a, 0x0d, 0x0e, 0x13}
KINDS = {R_BFIN_BYTE4_DATA: ABS32, R_BFIN_HUIMM16: HI16, R_BFIN_LUIMM16: LO16}
LOADED = ('.text', '.rodata', '.data')
ENTRY = '_opxy_module_init'


def parse(data):
    """Sections, symbols and kept relocations of a linked module ELF."""
    headers = [h for _at, h in elf_sections(data)]
    names_index = struct.unpack_from('<H', data, 50)[0]

    def contents(h):
        return b'' if h[1] == 8 else data[h[4]:h[4] + h[5]]

    def string(table, index):
        return table[index:table.index(b'\0', index)].decode('ascii')

    names = contents(headers[names_index])
    sections = {string(names, h[0]): dict(type=h[1], flags=h[2], address=h[3], size=h[5], index=i)
                for i, h in enumerate(headers)}
    by_index = {s['index']: n for n, s in sections.items()}
    symbols, values = {}, []
    for h in headers:
        if h[1] != 2:
            continue
        strings = contents(headers[h[6]])
        table = contents(h)
        for pos in range(0, h[5], 16):
            index, value, _size, _info, _other, _section = struct.unpack_from('<IIIBBH', table, pos)
            values.append(value)
            if index:
                symbols[string(strings, index)] = value
    relocs = []
    for h in headers:
        if h[1] == 4:  # SHT_RELA
            target = by_index[h[7]]
            table = contents(h)
            for pos in range(0, h[5], 12):
                offset, info, addend = struct.unpack_from('<IIi', table, pos)
                relocs.append((target, offset, info & 0xff, values[info >> 8] + addend))
        else:
            require(h[1] != 9, 'REL relocations are not supported')
    return sections, symbols, relocs


def link(tools, work, objects, base):
    elf = work / f'module-{base:08x}.elf'
    execute([tools['ld'], '-T', LINKER, f'--defsym=MODULE_BASE={base:#x}', '-q', '--gc-sections',
             '--no-undefined', *objects, '-o', elf])
    sections, symbols, relocs = parse(elf.read_bytes())
    allocated = {n for n, s in sections.items() if s['flags'] & 2 and s['size']}
    extra = allocated - set(LOADED) - {'.bss'}
    require(not extra, f'Unsupported sections: {sorted(extra)}')
    binary = work / f'image-{base:08x}.bin'
    execute([tools['objcopy'], '-O', 'binary', *[a for n in LOADED for a in ('-j', n)], elf, binary])
    image = binary.read_bytes()
    present = [n for n in LOADED if n in sections and sections[n]['size']]
    require(present and min(sections[n]['address'] for n in present) == base, 'Image does not start at the base')
    image += bytes(-len(image) % 4)
    bss = sections.get('.bss', dict(address=base + len(image), size=0))
    require(bss['size'] == 0 or bss['address'] == base + len(image), 'bss does not follow the image')
    require(ENTRY in symbols, 'opxy_module_init not found (extern "C")')
    return image, bss['size'], symbols[ENTRY] - base, relocs


def relocations(relocs, image, base):
    """(where, target) pairs for a link at base; pc-relative ones need nothing."""
    out = []
    for section, offset, kind, value in relocs:
        if section not in LOADED or kind in PC_RELATIVE:
            continue
        require(kind in KINDS, f'Unsupported relocation type {kind:#x} at {offset:#x}')
        where = offset - base  # for HI16 / LO16 the offset is already the immediate half
        require(0 <= where < len(image), f'Relocation outside the image at {offset:#x}')
        out.append((where | KINDS[kind] << 28, (value - base) & 0xffffffff))
    return sorted(out)


def apply(image, relocs, base):
    image = bytearray(image)
    for where, target in relocs:
        offset, kind, value = where & 0x0fffffff, where >> 28, (base + target) & 0xffffffff
        if kind == ABS32:
            image[offset:offset + 4] = struct.pack('<I', value)
        else:
            image[offset:offset + 2] = struct.pack('<H', value >> 16 if kind == HI16 else value & 0xffff)
    return bytes(image)


def pack(image, bss, init, relocs):
    reloc_bytes = b''.join(struct.pack('<2I', w, t) for w, t in relocs)
    crc = zlib.crc32(reloc_bytes, zlib.crc32(image))
    trailer = TRAILER.pack(0, 0, len(image), bss, len(image), len(relocs), init, crc,
                           OS_1_2_1, FORMAT, TRAILER.size, MAGIC)
    require(TRAILER.size == 48, 'Trailer size')
    return image + reloc_bytes + trailer


def inspect(data):
    """The loader's own checks, host side: trailer, layout, CRC and relocations."""
    require(len(data) >= TRAILER.size, 'Truncated module file')
    (_manifest, image_offset, image_bytes, bss, reloc_offset, reloc_count, init, crc, os_version,
     format_, size, magic) = TRAILER.unpack(data[-TRAILER.size:])
    require(magic == MAGIC and format_ == FORMAT and size == TRAILER.size, 'Not a module file (format 0)')
    require(os_version == OS_1_2_1, 'Module built for another OS')
    end = len(data) - TRAILER.size
    require(not (image_offset | image_bytes) & 3 and image_bytes and image_offset + image_bytes <= end and
            reloc_offset + 8 * reloc_count <= end and init < image_bytes and not init & 1 and
            bss <= 0x01000000 and image_bytes <= 0x01000000, 'Module layout outside the file')
    image = data[image_offset:image_offset + image_bytes]
    relocs = data[reloc_offset:reloc_offset + 8 * reloc_count]
    require(zlib.crc32(relocs, zlib.crc32(image)) == crc, 'Module checksum mismatch')
    for where, target in struct.iter_unpack('<2I', relocs):
        kind, offset = where >> 28, where & 0x0fffffff
        require(kind in (ABS32, HI16, LO16) and offset + (4 if kind == ABS32 else 2) <= image_bytes and
                target <= image_bytes + bss, 'Invalid module relocation')
    return dict(image=image_bytes, bss=bss, relocs=reloc_count, init=init)


def build(folder, out, toolchain=None, compiler=None):
    from . import native
    tool_dir, compiler_dir = resolve_toolchain(toolchain, compiler)
    tools = tool_paths(tool_dir)
    cxx = native.compiler_paths(compiler_dir)['cxx']
    sources = sorted((Path(folder) / 'src').glob('*.cpp'))
    require(sources, f'No src/*.cpp in {folder}')
    with tempfile.TemporaryDirectory(prefix='opxy-module-') as temp:
        work = Path(temp)
        objects = [native.compile_cxx(cxx, tools['as'], src.resolve(), work / (src.stem + '.o'),
                                      ['sdk/include', 'sdk/platform/blackfin/freestanding'])
                   for src in sources]
        image, bss, init, kept = link(tools, work, objects, 0)
        relocs = relocations(kept, image, 0)
        image1, bss1, init1, _ = link(tools, work, objects, CHECK_BASE)
        require((bss1, init1, len(image1)) == (bss, init, len(image)), 'Second link differs in layout')
        moved = apply(image, relocs, CHECK_BASE)
        if moved != image1:
            diff = [f'{i:#06x}: base0 {image[i]:02x} ours {moved[i]:02x} link {image1[i]:02x}'
                    for i in range(len(image)) if moved[i] != image1[i]]
            raise ValueError('Relocation list does not reproduce the second link:\n  ' + '\n  '.join(diff[:20]))
        require(apply(image, relocs, 0) == image, 'Relocation list changes the base-0 image')
    data = pack(image, bss, init, relocs)
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    return dict(file=str(out), bytes=len(data), image=len(image), bss=bss, relocs=len(relocs),
                init=hex(init), crc=hex(zlib.crc32(data[len(image):len(image) + 8 * len(relocs)], zlib.crc32(image))))


def main(argv=None):
    parser = argparse.ArgumentParser(prog='sdk.py module', description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('build', help='Build a .opx module file from a module folder')
    command.add_argument('folder', type=Path)
    command.add_argument('--out', type=Path, required=True)
    command.add_argument('--toolchain', type=Path)
    command.add_argument('--compiler', type=Path)
    args = parser.parse_args(argv)
    try:
        print(json.dumps(build(args.folder, args.out, args.toolchain, args.compiler), indent=2))
        return 0
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, str(error) + '\n')
