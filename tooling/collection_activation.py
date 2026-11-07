"""Whole-collection staging ABI and register-preserving dispatch generation.

These helpers produce no installable firmware and never contact a device.
Native handoff requires a separately established quiescent two-role gate.
"""
import hashlib
import json
import struct

from .firmware import branch_instruction, decode_branch, patch_active
from .formats import require

HEADER_BYTES = 160
MAX_ENTRIES = 192
MAX_RESOURCES = 64
RESOURCE_BYTES = 40
MAX_RESOURCE_TOTAL = 2*1024*1024
MAX_SVG_BYTES = 256*1024
MAX_CATALOG_BYTES = 64*1024
RESIDENT_BYTES = 0x4000
RESIDENT_HOOKS = {'sdk_collection_mtp_match', 'sdk_collection_rpc_dispatch', 'sdk_collection_rpc_entry_hook',
                  'sdk_collection_asset_load','sdk_collection_catalog_load'}


def geometry(profile, resident_bytes=RESIDENT_BYTES):
    base = profile['module_base']
    capacity = profile['reserved_bytes'] - profile['table_bytes']
    require(base % 32 == 0 and resident_bytes % 32 == 0 and 0 < resident_bytes < capacity,
            'Resident layout must fit the unchanged aligned reservation')
    return dict(resident_base=base, resident_bytes=resident_bytes,
                payload_base=base+resident_bytes, payload_bytes=capacity-resident_bytes,
                reserved_end=base+capacity)


def interface(profile, services, profiling, resident_bytes=RESIDENT_BYTES):
    require(set(services) <= set(profile.get('services', {})), 'Unknown resident capability')
    entries = [dict(site=p['address'], symbol=p['symbol'], call=p['call'], length=p['length'], before=p['before'])
               for p in profile['patches'] if p['kind'] == 'hook' and p['symbol'] not in RESIDENT_HOOKS
               and patch_active(p, set(services), profiling)]
    entries.sort(key=lambda p: int(p['site'], 16))
    require(0 < len(entries) <= MAX_ENTRIES, 'Unsupported collection dispatch size')
    return dict(format='openxy-collection-abi/1', target=profile['id'], stock_sha256=profile['stock_sha256'],
                geometry=geometry(profile, resident_bytes), services=sorted(services),
                profiling=profiling, entries=entries)


def fingerprint(interface):
    return hashlib.sha256(json.dumps(interface, sort_keys=True, separators=(',', ':')).encode()).digest()


def payload_entries(interface):
    lines = ['.section .text.collection_entries,"ax"', '.balign 4']
    for i, entry in enumerate(interface['entries']):
        name = f'sdk_collection_entry_{i}'
        lines += [f'.global {name}', f'.type {name}, STT_FUNC', f'{name}:',
                  f'    JUMP.L {entry["symbol"]};', f'.size {name}, .-{name}']
    return '\n'.join(lines)+'\n'


def resident_veneers(interface):
    base = interface['geometry']['payload_base']
    lines = ['.section .text.collection_resident,"ax"', '.balign 4']
    for i, _entry in enumerate(interface['entries']):
        target = base+i*4
        name = f'sdk_collection_resident_{i}'
        lines += [f'.global {name}', f'.type {name}, STT_FUNC', f'{name}:',
                  f'    .short 0xdc00, {target >> 16:#x}, {target & 65535:#x}, 0',
                  f'.size {name}, .-{name}']
    return '\n'.join(lines)+'\n'


def payload_linker(original, profile, resident_bytes=RESIDENT_BYTES):
    """Derive only the payload placement and entry prefix from the pinned linker."""
    require(hashlib.sha256(original.encode()).hexdigest() == profile['linker_sha256'],
            'Unreviewed input linker for collection placement')
    layout = geometry(profile, resident_bytes)
    old = f"ORIGIN = {profile['module_base']:#x}, LENGTH = {profile['reserved_bytes']-profile['table_bytes']:#x}"
    new = f"ORIGIN = {layout['payload_base']:#x}, LENGTH = {layout['payload_bytes']:#x}"
    text = '.text : { *(.text .text.*) } > MODULE'
    require(original.count(old) == original.count(text) == 1, 'Changed linker layout boundary')
    return original.replace(old, new).replace(text,
        '.text : { KEEP(collection_entries.o(.text.collection_entries)) *(.text .text.*) } > MODULE')


def check_entries(payload, base, text_bytes, entries, symbols=None):
    prefix = entries*4
    require(0 < entries <= MAX_ENTRIES and prefix < text_bytes <= len(payload) and text_bytes % 2 == 0,
            'Invalid entry prefix')
    targets = []
    for i in range(entries):
        raw = payload[i*4:i*4+4]
        require(struct.unpack_from('<H', raw)[0] & 0xff00 == 0xe200,
                'Collection entry is not an unconditional long jump')
        target, call = decode_branch(base+i*4, raw)
        require(not call and base+prefix <= target <= base+text_bytes-2, 'Collection entry outside initialized text')
        if symbols is not None:
            symbol = symbols[i]
            require(symbol['type'] == 2 and symbol['size'] > 0 and symbol['value'] == target and
                    target+symbol['size'] <= base+text_bytes, 'Entry differs from the expected payload function')
        targets.append(target)
    return targets


def check_veneers(data, interface):
    entries = interface['entries']
    require(len(data) == len(entries)*8, 'Unexpected resident veneer extent')
    for i in range(len(entries)):
        target = interface['geometry']['payload_base']+i*4
        require(data[i*8:i*8+8] == struct.pack('<4H', 0xdc00, target >> 16, target & 65535, 0),
                'Resident veneer changes registers or targets the wrong entry')


def check_resources(payload, text_bytes, offset, count, *, readonly=None):
    require(type(offset) is int and type(count) is int and offset % 4 == 0 and
            text_bytes <= offset <= len(payload) and 1 <= count <= MAX_RESOURCES and
            count <= (len(payload)-offset)//RESOURCE_BYTES, 'Resource table outside initialized read-only data')
    if readonly is not None:
        low,size=readonly
        require(text_bytes <= low <= offset and offset+count*RESOURCE_BYTES <= low+size <= len(payload),
                'Resource table outside ELF read-only section')
    paths=set();resources=[]
    for i in range(count):
        at=offset+i*RESOURCE_BYTES
        digest=payload[at:at+32]
        length,kind=struct.unpack_from('<2I',payload,at+32)
        require(kind in (1,2) and 0 < length <= (MAX_SVG_BYTES if kind==1 else MAX_CATALOG_BYTES) and digest!=bytes(32),
                'Invalid collection resource')
        path=('asset-' if kind==1 else 'catalog-')+digest.hex()+'.oxc'
        require(path not in paths, 'Duplicate collection resource path')
        paths.add(path);resources.append(dict(path=path,sha256=digest.hex(),bytes=length,kind=kind))
    require(sum(r['kind']==2 for r in resources)==1, 'Collection requires exactly one catalog')
    require(sum(r['bytes'] for r in resources)<=MAX_RESOURCE_TOTAL, 'Collection resource cache exceeds its budget')
    return resources


def encode(payload, *, base, capacity, bss_offset, bss_bytes, text_bytes, entries, platform, abi,
           resources_offset=None, resources_count=None):
    require(len(platform) == len(abi) == 32, 'Invalid platform or ABI fingerprint')
    require(base % 32 == 0 and 0 < capacity <= 0xffffffff-base and
            0 < len(payload) <= bss_offset <= capacity and bss_bytes <= capacity-bss_offset and
            bss_bytes >= 0 and not ((bss_offset | bss_bytes) & 3), 'Collection outside payload reservation')
    check_entries(payload, base, text_bytes, entries)
    resources=resources_offset is not None or resources_count is not None
    if resources:
        check_resources(payload,text_bytes,resources_offset,resources_count)
    header = bytearray(HEADER_BYTES)
    header[:8] = b'OXYLOAD1'
    struct.pack_into('<10I', header, 8, 2 if resources else 1, HEADER_BYTES, base, len(payload), bss_offset,
                     bss_bytes, text_bytes, entries, 1, 0)
    header[48:80] = platform
    header[80:112] = abi
    if resources:
        struct.pack_into('<4I',header,144,resources_offset,resources_count,RESOURCE_BYTES,0)
    header[112:144] = hashlib.sha256(header+payload).digest()
    return bytes(header)+payload


def entry_instruction(site, destination):
    return branch_instruction(site, destination, False)


def control(*, phase, generation, slot, digest, platform, abi):
    require(type(phase) is int and 0 <= phase <= 4 and type(generation) is int and
            0 < generation <= 0xffffffff and type(slot) is int and slot in (0,1), 'Invalid activation control')
    require(len(digest) == len(platform) == len(abi) == 32, 'Invalid control fingerprint')
    require((phase == 0 and slot == 0 and digest == bytes(32)) or
            (phase != 0 and digest != bytes(32)), 'Invalid control selection')
    result = bytearray(160)
    result[:8] = b'OXYCTL01'
    struct.pack_into('<4I', result, 8, 1, phase, generation, slot)
    result[32:64] = platform
    result[64:96] = abi
    result[96:128] = digest
    result[128:160] = hashlib.sha256(result).digest()
    return bytes(result)


def read_control(data, *, platform, abi):
    require(len(data)==160 and data[:8]==b'OXYCTL01', 'Invalid collection control length or magic')
    version,phase,generation,slot=struct.unpack_from('<4I',data,8)
    require(version==1 and data[32:64]==platform and data[64:96]==abi, 'Foreign collection control')
    result=dict(phase=phase,generation=generation,slot=slot,digest=data[96:128])
    require(control(**result,platform=platform,abi=abi)==data, 'Corrupt collection control')
    return result


def inspect(data, *, geometry, entries, platform, abi):
    require(len(data)>=HEADER_BYTES and data[:8]==b'OXYLOAD1', 'Invalid executable collection header')
    fields=struct.unpack_from('<10I',data,8)
    version,length,base,initialized,bss_offset,bss_bytes,text_bytes,count,flags,reserved=fields
    offset,resources,stride,last=struct.unpack_from('<4I',data,144)
    require(version==2 and length==HEADER_BYTES and count==entries and flags==1 and not reserved and
            stride==RESOURCE_BYTES and not last and base==geometry['payload_base'] and
            data[48:80]==platform and data[80:112]==abi and len(data)==HEADER_BYTES+initialized,
            'Incompatible executable collection')
    payload=data[HEADER_BYTES:]
    rebuilt=encode(payload,base=base,capacity=geometry['payload_bytes'],bss_offset=bss_offset,
                   bss_bytes=bss_bytes,text_bytes=text_bytes,entries=count,platform=platform,abi=abi,
                   resources_offset=offset,resources_count=resources)
    require(rebuilt==data, 'Executable collection checksum or reserved fields differ')
    return dict(digest=data[112:144].hex(),initialized_bytes=initialized,bss_offset=bss_offset,bss_bytes=bss_bytes,
                text_bytes=text_bytes,resources=check_resources(payload,text_bytes,offset,resources))
