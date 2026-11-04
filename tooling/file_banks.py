"""File banks (opxy/file_bank.hpp): immutable compiler tables exported as
content-addressed files instead of occupying the module, and the native table
the SDK loads them from at startup."""
import hashlib
import re
import struct
from pathlib import Path

from .formats import elf_sections, elf32, require

PREFIX = '.opxy.filebank.'
MAGIC = b'OPXYHB01'
HEADER_BYTES = 128
ALLOCATION_LIMIT = 1024 * 1024
ALLOWANCE = 64  # per allocation, a conservative allowance for the stock allocator's own bytes
MAX_PAYLOAD = ALLOCATION_LIMIT - ALLOWANCE
# The most a build's unique banks may take from one role's 18 MiB application
# heap, payload and allowance. Measured headroom on the audio role is about
# 18 MiB in the projects tested; this keeps most of it for stock.
SESSION_CAP = 4 * 1024 * 1024
# Stock stdio resolves absolute names only under its /yaffs2 and /fat32 mounts,
# and MTP shows /yaffs2 as the device root: content/... is /yaffs2/content/...
DEVICE_ROOT = '/yaffs2/'
OWNER, NAME = b'shared'.ljust(32, b'\0'), b'bank'.ljust(32, b'\0')


def package_path(digest):
    require(re.fullmatch(r'[0-9a-f]{64}', digest), 'Invalid file bank digest')
    return f'content/banks/shared/{digest}-bank.hb'


def device_path(path):
    require(re.fullmatch(r'content/banks/shared/[0-9a-f]{64}-bank\.hb', path), 'Unexpected file bank path')
    return DEVICE_ROOT + path


def container(payload):
    """The file for one bank: a 128-byte header, then the payload."""
    require(isinstance(payload, bytes) and 0 < len(payload) <= MAX_PAYLOAD and len(payload) % 4 == 0,
            'A file bank holds 1 to %d bytes of whole u32 words' % MAX_PAYLOAD)
    return (MAGIC + struct.pack('<II', 1, HEADER_BYTES) + OWNER + NAME +
            struct.pack('<IIII', len(payload), len(payload) // 4, 1, 0) + hashlib.sha256(payload).digest() + payload)


def validate(data, digest):
    """The payload of a file bank whose content hashes to digest."""
    require(isinstance(data, bytes) and HEADER_BYTES < len(data) <= HEADER_BYTES + MAX_PAYLOAD,
            'File bank length is invalid')
    size = len(data) - HEADER_BYTES
    require(data[:8] == MAGIC and struct.unpack_from('<II', data, 8) == (1, HEADER_BYTES) and
            data[16:48] == OWNER and data[48:80] == NAME and
            struct.unpack_from('<IIII', data, 80) == (size, size // 4, 1, 0) and size % 4 == 0,
            'Unsupported file bank header')
    payload = data[HEADER_BYTES:]
    require(data[96:128].hex() == digest == hashlib.sha256(payload).hexdigest(), 'File bank content hash mismatch')
    return payload


def inspect(data, *, allocated):
    """Each file bank array in a relocatable object: name -> payload bytes. Each
    is the one global array at the start of its own section, with no runtime
    reference to its address."""
    sections = elf_sections(data)
    headers = [h for _at, h in sections]
    strings_header = headers[struct.unpack_from('<H', data, 50)[0]]
    strings = data[strings_header[4]:strings_header[4] + strings_header[5]]
    found = {}
    for index, (at, h) in enumerate(sections):
        label = strings[h[0]:].split(b'\0', 1)[0].decode('ascii')
        if not label.startswith(PREFIX):
            continue
        name = label[len(PREFIX):]
        require(re.fullmatch(r'[a-z][a-z0-9_]{0,30}', name), 'Invalid file bank name: ' + name)
        require(h[1] == 1 and h[2] == (2 if allocated else 0) and h[8] == 32 and
                h[5] > 0 and h[5] % 4 == 0 and h[4] + h[5] <= len(data), 'Invalid file bank section: ' + label)
        require(not any(r[1] in (4, 9) and r[7] == index and r[5] for r in headers),
                'A file bank may not contain relocations')
        objects = []
        for table_index, table in enumerate(headers):
            if table[1] != 2:
                continue
            require(table[9] == 16 and table[5] % 16 == 0 and table[4] + table[5] <= len(data),
                    'Invalid symbol table')
            members = set()
            for symbol_index, offset in enumerate(range(table[4], table[4] + table[5], 16)):
                _name, value, size, info, _other, section = struct.unpack_from('<IIIBBH', data, offset)
                if section == index:
                    members.add(symbol_index)
                    if info & 15 == 1:
                        objects.append((value, size, info >> 4))
            # Nothing at run time may hold the array's address: readers go through the view.
            for relocation in headers:
                if relocation[1] not in (4, 9) or relocation[6] != table_index:
                    continue
                entry = 12 if relocation[1] == 4 else 8
                require(relocation[9] == entry and relocation[5] % entry == 0 and
                        relocation[4] + relocation[5] <= len(data), 'Invalid relocation table')
                for offset in range(relocation[4], relocation[4] + relocation[5], entry):
                    info = struct.unpack_from('<I', data, offset + 4)[0]
                    require(info >> 8 not in members, 'Code refers to file bank ' + name + ' directly; read it through its view')
        require(len(objects) == 1 and objects[0][0] == 0 and objects[0][2] == 1,
                'A file bank section holds one global array at its start: ' + name)
        size = objects[0][1]
        require(0 < size <= MAX_PAYLOAD and size % 4 == 0 and size <= h[5] and h[5] - size < 32 and
                not any(data[h[4] + size:h[4] + h[5]]), 'Unexpected file bank size or padding: ' + name)
        require(name not in found, 'Duplicate file bank array: ' + name)
        found[name] = dict(payload=data[h[4]:h[4] + size], header_offset=at)
    return found


def detach(data):
    """Clear SHF_ALLOC on file bank sections so they take no module memory,
    after proving no relocation can reach them."""
    banks = inspect(data, allocated=True)
    result = bytearray(data)
    for bank in banks.values():
        struct.pack_into('<I', result, bank['header_offset'] + 8, 0)
    output = bytes(result)
    require(inspect(output, allocated=False) == banks, 'Detaching a file bank changed its payload')
    return output


def compiled(directory, machines):
    """The build's unique bank files, from each machine's compiled objects:
    [{path, digest, bytes, data, uses: [(namespace, view, array, machine id)]}]."""
    files = {}
    for m in machines:
        if not m.file_banks:
            continue
        found = {}
        for path in sorted((Path(directory) / m.id).glob('*.o')):
            for name, asset in inspect(path.read_bytes(), allocated=False).items():
                require(name not in found, 'File bank defined in more than one unit: ' + name)
                found[name] = asset['payload']
        arrays = {array for _view, array in m.file_banks}
        require(set(found) == arrays, f'{m.id}: declared and compiled file banks differ: '
                f'{sorted(arrays)} vs {sorted(found)}')
        for view, array in m.file_banks:
            payload = found[array]
            digest = hashlib.sha256(payload).hexdigest()
            entry = files.setdefault(digest, dict(path=package_path(digest), digest=digest, bytes=len(payload),
                                                  data=container(payload), uses=[]))
            entry['uses'].append((m.definition['id'], view, array, m.id))
    result = sorted(files.values(), key=lambda f: f['digest'])
    require(budget(result) <= SESSION_CAP, 'File banks exceed the %d KiB session cap' % (SESSION_CAP // 1024))
    return result


def budget(files):
    return sum(f['bytes'] + ALLOWANCE for f in files)


def generate(directory, files, *, gui):
    """The native table (file_banks.hpp) for a build's banks."""
    require(type(gui) is bool, 'The GUI copy flag is a boolean')
    declarations, specs, uses = [], [], []
    for index, f in enumerate(files):
        digest = ', '.join('0x' + f['digest'][n:n + 2] for n in range(0, 64, 2))
        specs.append(f'    {{"{device_path(f["path"])}", {f["bytes"]}, {{{digest}}}}},')
        for namespace, view, _array, _machine in f['uses']:
            declarations.append(f'namespace {namespace} {{ extern opxy::FileBank {view}; }}')
            uses.append(f'    {{&{namespace}::{view}, {index}}},')
    lines = ['// Generated: the build\'s file banks (tooling/file_banks.py).', '#include "file_banks.hpp"',
             *sorted(set(declarations)), 'namespace opxy { namespace platform {',
             'const file_banks::Spec native_file_banks[] = {', *specs, '};',
             f'const u32 native_file_bank_count = {len(files)};',
             'const FileBankUse native_file_bank_uses[] = {', *uses, '};',
             f'const u32 native_file_bank_use_count = {len(uses)};',
             f'const u32 native_file_bank_budget = {budget(files)};',
             f'const bool native_file_banks_gui = {"true" if gui else "false"};', '}}', '']
    path = Path(directory) / 'file_bank_table.cpp'
    path.write_text('\n'.join(lines), encoding='utf-8', newline='\n')
    return path


def placed(elf, files):
    """Each view is an 8-byte object in the linked module, and no bank array is."""
    sections, symbols = elf32(elf)
    allocated = [s for s in sections.values() if s['flags'] & 2 and s['size']]

    def in_module(symbol):
        return any(s['address'] <= symbol['value'] < s['address'] + s['size'] for s in allocated)

    for f in files:
        for namespace, view, array, _machine in f['uses']:
            symbol = f'_ZN{len(namespace)}{namespace}{len(view)}{view}E'
            found = symbols.get('_' + symbol) or symbols.get(symbol)
            require(found and found['size'] == 8 and in_module(found), f'Missing file bank view: {namespace}::{view}')
            mangled = f'_ZN{len(namespace)}{namespace}{len(array)}{array}E'
            for name in ('_' + mangled, mangled):
                require(name not in symbols or not in_module(symbols[name]),
                        f'File bank array {namespace}::{array} was linked into the module')
    return [dict(path=f['path'], sha256=f['digest'], bytes=f['bytes'],
                 views=[f'{n}::{v}' for n, v, _a, _m in f['uses']]) for f in files]
