"""Version-bound OS container assembly shared by curated engine builds."""
import copy
import re
import struct
from .formats import header, ldr, mapped_offset, require, sha
from .pipeline import check_patch_boundaries
from . import startup_label, stock_tables


def expanded_profile(profile):
    """Derive the reviewed expanded layout using the stock instruction decoder."""
    x = profile.get('experimental_memory')
    require(isinstance(x, dict) and set(x) == {'module_base', 'reserved_bytes', 'heap_after'} and
            isinstance(x['heap_after'], str) and re.fullmatch('[0-9a-f]{16}', x['heap_after']) is not None,
            'Experimental memory layout is inconsistent')
    # Checked here too, with the stock instruction decoder, rather than trusted.
    opcode, heap = stock_tables.decode('imm32', bytes.fromhex(x['heap_after']))
    heaps = [p for p in profile['patches'] if p['kind'] == 'heap']
    end = profile['module_base'] + profile['reserved_bytes']
    mib = 1024 * 1024
    require(len(heaps) == 2 and
            all(type(x[k]) is int for k in ('module_base', 'reserved_bytes')) and
            (profile['module_base'] - profile['heap_fill_base']) % mib == 0 and
            x['reserved_bytes'] > profile['reserved_bytes'] and x['reserved_bytes'] % mib == 0 and
            x['module_base'] + x['reserved_bytes'] == end == profile['heap_fill_base'] + profile['heap_fill_bytes'] and
            profile['heap_fill_base'] < x['module_base'] and heap == x['module_base'] - profile['heap_fill_base'] and
            heap % mib == 0 and profile['reserved_bytes'] % mib == 0 and
            all(stock_tables.decode('imm32', bytes.fromhex(p['before'])) == (opcode, profile['heap_fill_bytes']) and
                stock_tables.decode('imm32', bytes.fromhex(p['after'])) == (opcode, profile['module_base'] - profile['heap_fill_base'])
                for p in heaps), 'Experimental memory layout is inconsistent')
    expanded = copy.deepcopy(profile)
    del expanded['experimental_memory']  # the derived profile is the layout itself
    expanded.update(module_base=x['module_base'], reserved_bytes=x['reserved_bytes'])
    for patch in expanded['patches']:
        if patch['kind'] == 'heap':
            patch['after'] = x['heap_after']
    return expanded


def active_services(profile, uses):
    """Profile services a collection or selection needs; each names the machine kinds, the
    features machines declare (tooling.native.service_users), or 'extension' for selected
    extensions, that use it."""
    uses = set(uses)
    return {name for name, users in profile.get('services', {}).items() if uses & set(users)}


def patch_active(patch, services, profiling=True):
    """Select optional service hooks and the reviewed profiling bypasses."""
    require(type(profiling) is bool, 'Profiling must be a boolean')
    require('profiling' not in patch or (patch['profiling'] is False and patch['kind'] == 'hook'),
            'Invalid profiling hook')
    return (('service' not in patch or patch['service'] in services) and
            ('profiling' not in patch or patch['profiling'] is profiling))


def hooks(profile):
    """Hook site -> (module symbol, call) for every redirect the profile reviews."""
    return {int(p['address'], 16): (p['symbol'], p['call']) for p in profile['patches'] if p['kind'] == 'hook'}


def branch_instruction(site, destination, call):
    delta = destination - site
    require(delta % 2 == 0 and -(1 << 24) <= delta < (1 << 24), 'Branch outside signed PC-relative range')
    encoded = (delta // 2) & 0xffffff
    return struct.pack('<HH', (0xe300 if call else 0xe200) | (encoded >> 16), encoded & 0xffff)


def decode_branch(site, instruction):
    require(len(instruction) == 4, 'Branch must occupy four bytes')
    first, second = struct.unpack('<HH', instruction)
    require(first & 0xfe00 == 0xe200, 'Unexpected branch opcode')
    offset = ((first & 0xff) << 16) | second
    if offset & 0x800000:
        offset -= 1 << 24
    return site + offset * 2, bool(first & 0x100)


def absolute_call(target):
    require(0 <= target <= 0xffffffff and target % 2 == 0, 'Invalid absolute call target')
    return struct.pack('<4H', 0xde00, target >> 16, target & 65535, 0)


def absolute_jump(target):
    require(0 <= target <= 0xffffffff and target % 2 == 0, 'Invalid absolute jump target')
    return struct.pack('<4H', 0xdc00, target >> 16, target & 65535, 0)


def absolute_target(data, call=True):
    require(len(data) == 8, 'Absolute call must occupy eight bytes')
    opcode, high, low, zero = struct.unpack('<4H', data)
    require(opcode == (0xde00 if call else 0xdc00) and zero == 0 and low % 2 == 0,
            'Invalid absolute branch encoding')
    return high << 16 | low


def hook_instruction(patch, target):
    """Reviewed absolute/relative call or branch, with zero NOP padding."""
    site, length, call = int(patch['address'], 16), patch['length'], patch['call']
    if length in (8, 12):
        require(type(call) is bool and (call or length == 8), 'Invalid absolute hook policy')
        return (absolute_call(target) if call else startup_label.jump(target)[:8]) + bytes(length - 8)
    require(length in (4, 6), 'Unsupported relative-hook footprint')
    return branch_instruction(site, target, call) + bytes(length - 4)


def hook_target(patch, instruction):
    """(destination, call) of hook bytes produced by hook_instruction."""
    if patch['length'] in (8, 12):
        require(type(patch['call']) is bool and (patch['call'] or patch['length'] == 8),
                'Invalid absolute hook policy')
        require(len(instruction) == patch['length'] and instruction[8:] == bytes(patch['length'] - 8),
                'Changed absolute-hook padding')
        if patch['call']: return absolute_target(instruction[:8]), True
        require(patch['length'] == 8 and instruction == startup_label.jump(startup_label.destination(instruction + bytes(2)))[:8],
                'Invalid absolute entry branch')
        return startup_label.destination(instruction + bytes(2)), False
    require(instruction[4:] == bytes(patch['length'] - 4), 'Changed hook padding')
    return decode_branch(int(patch['address'], 16), instruction[:4])


def assemble(stock, module, profile, original_profile, edits, services, profiling=True):
    require(type(profiling) is bool, 'Profiling must be a boolean')
    require(sha(stock) == original_profile['stock_sha256'] and len(stock) == original_profile['stock_bytes'],
            'Expected exact decrypted stock OP-XY 1.2.1')
    require(len(module) > 0 and len(module) <= original_profile['reserved_bytes'], 'Invalid module size')
    require(len(profile['patches']) == len(original_profile['patches']) == edits, f'Expected {edits} edit intervals')
    low = original_profile['module_base']
    for patch, original in zip(profile['patches'], original_profile['patches']):
        require(all(patch[k] == original[k] for k in ('kind', 'address', 'before', 'file_offset', 'length')),
                'Changed patch boundary')
        after = bytes.fromhex(patch['after'])
        require(all(patch.get(k) == original.get(k) for k in ('service', 'profiling', 'symbol', 'call', 'absolute_jump')),
                'Changed patch policy')
        if not patch_active(original, services, profiling):
            require(patch['after'] == original['before'], 'An inactive hook changed stock')
        elif original['kind'] == 'startup':
            target = startup_label.destination(after)
            require(low <= target < low + len(module), 'Startup hook does not target module')
        elif original['kind'] == 'hook':
            target, call = hook_target(original, after)
            require(call == original['call'] and low <= target < low + len(module), 'Hook does not target module')
        else:
            require(patch['after'] == original['after'], 'Changed heap reservation or table edit')
    check_patch_boundaries(profile, edits)
    blocks = ldr(stock)
    require(len(blocks) == original_profile['stock_records'], 'Unexpected stock records')
    first, final = blocks[0], blocks[-1]
    require(first['code'] & 0xffff == 0x5004 and first['count'] == 0 and
            final['code'] & 0xffff == 0x8004 and final['count'] == 0, 'Unsupported FIRST/FINAL')
    low, high = original_profile['module_base'], original_profile['module_base'] + original_profile['reserved_bytes']
    overlapping = [b for b in blocks if b['target'] < high and low < b['target'] + b['count']]
    require(len(overlapping) == 1, 'Unexpected writes to shared reservation')
    fill = overlapping[0]
    require(fill['fill'] and fill['argument'] == 0 and fill['target'] == original_profile['heap_fill_base'] and
            fill['count'] == original_profile['heap_fill_bytes'] and fill['target'] <= low and high <= fill['target'] + fill['count'],
            'Shared state not zero-filled by unchanged stock loader record')
    patched = bytearray(stock)
    for patch in profile['patches']:
        address = int(patch['address'], 16)
        before, after = bytes.fromhex(patch['before']), bytes.fromhex(patch['after'])
        offset = mapped_offset(blocks, address, len(before))
        require(offset == int(patch['file_offset'], 16) and stock[offset:offset + len(before)] == before,
                'Patch preimage mismatch')
        patched[offset:offset + len(after)] = after
    at = final['offset']
    candidate = bytes(patched[:at]) + header(4, low, len(module), 0) + module + bytes(patched[at:])
    candidate = header(0x5004, first['target'], 0, len(candidate) - 16) + candidate[16:]
    result = ldr(candidate)
    require(len(result) == len(blocks) + 1 and result[0]['target'] == first['target'], 'Entry/record count changed')
    require(candidate[-16:] == stock[-16:], 'FINAL entry changed')
    # Recover the original image from candidate using only the known edits,
    # the new module record and FIRST length/checksum, then compare every byte.
    restored = bytearray(candidate[:at] + candidate[at + 16 + len(module):])
    restored[:16] = stock[:16]
    for patch in profile['patches']:
        offset = int(patch['file_offset'], 16)
        restored[offset:offset + patch['length']] = bytes.fromhex(patch['before'])
    require(bytes(restored) == stock, 'Unauthorized difference outside exact OS edits')
    return candidate
