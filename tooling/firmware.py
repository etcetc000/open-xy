"""Version-bound OS container assembly shared by curated engine builds."""
import struct
from .formats import header, ldr, mapped_offset, require, sha
from .pipeline import check_patch_boundaries
from . import startup_label, steps


def active_services(profile, kinds):
    """Profile services a collection or selection needs; each names the machine kinds that use it."""
    kinds = set(kinds)
    return {name for name, users in profile.get('services', {}).items() if kinds & set(users)}


def patch_active(patch, services, profiling=True):
    """Select optional service hooks, the reviewed profiling bypasses and assigned step slots.
    `services` holds a step slot's pseudo-service (steps.service) when a machine replaces it."""
    require(type(profiling) is bool, 'Profiling must be a boolean')
    require('profiling' not in patch or (patch['profiling'] is False and patch['kind'] == 'hook'),
            'Invalid profiling hook')
    return (('service' not in patch or patch['service'] in services) and
            ('profiling' not in patch or patch['profiling'] is profiling) and
            (patch['kind'] != 'step' or steps.service(patch['slot']) in services))


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


def absolute_target(data):
    require(len(data) == 8, 'Absolute call must occupy eight bytes')
    opcode, high, low, zero = struct.unpack('<4H', data)
    require(opcode == 0xde00 and zero == 0 and low % 2 == 0, 'Invalid absolute call encoding')
    return high << 16 | low


def hook_instruction(patch, target):
    """Replacement bytes of a hook: an absolute call over eight bytes, else a relative branch plus padding."""
    site, length, call = int(patch['address'], 16), patch['length'], patch['call']
    if length == 8:
        require(call, 'Eight-byte hook must be an absolute call')
        return absolute_call(target)
    require(length in (4, 6), 'Unsupported relative-hook footprint')
    return branch_instruction(site, target, call) + bytes(length - 4)


def hook_target(patch, instruction):
    """(destination, call) of hook bytes produced by hook_instruction."""
    if patch['length'] == 8:
        return absolute_target(instruction), True
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
        require(all(patch.get(k) == original.get(k) for k in ('service', 'profiling', 'symbol', 'call', 'slot', 'words')),
                'Changed patch policy')
        if not patch_active(original, services, profiling):
            require(patch['after'] == original['before'], 'An inactive hook changed stock')
        elif original['kind'] == 'startup':
            target = startup_label.destination(after)
            require(low <= target < low + len(module), 'Startup hook does not target module')
        elif original['kind'] == 'hook':
            target, call = hook_target(original, after)
            require(call == original['call'] and low <= target < low + len(module), 'Hook does not target module')
        elif original['kind'] == 'step':
            steps.check_after(original, after, low, low + len(module))
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
