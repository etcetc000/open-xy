"""Resident dispatch guards and exact displaced-stock paths for OS 1.2.1.

These generators are offline only. A zero resident flag selects stock behavior;
the flag may be published only by a separately qualified startup coordinator.
"""
import struct
import hashlib

from .collection_activation import MAX_ENTRIES
from .firmware import absolute_jump, absolute_call, absolute_target, decode_branch
from .formats import elf32, require

# Every non-branch span is reviewed at its original site. In particular, copying
# an unknown PC-relative instruction to resident RAM must never be accepted.
REPLAY = {
    0x8d506a02: ('c66cbb05', False),
    0x8d523dac: ('01e80000', False),
    0x8d523e08: ('01e80000', False),
    0x8d505356: ('09618808', True),
    0x8d5214c2: ('0f6420e151008708', True),
    0x8d5b79c0: ('980d00e80000', False),
    0x8d5ad670: ('f5602f08', True),
    0x8d4daf7e: ('00d80384d0930000', True),
    0x8d4da57c: ('abe46400', True),
    0x8d589210: ('00e80000', False),
    0x8d5355ac: ('48a1404d', True),
    0x8c0a1bd6: ('0dda010808f40000', True),
    0x8c0a1bbc: ('0cda010824f40000', True),
    0x8d53f888: ('504f79c20820b14f', True),
    0x8d5b79f0: ('980d00e80000', False),
    0x8d4e36a8: ('08e44a001808', True),
    0x8d4e39c0: ('21600808', True),
    0x8d4e74b0: ('21600808', True),
    0x8d4e6aa8: ('216088a10808', True),
    0x8d4e6bd0: ('88a10808', True),
    0x8d4e6e08: ('88a11008', True),
    0x8d589296: ('8ba1299b', True),
    0x8d595a4c: ('22601008', True),
    0x8d588ec8: ('88a10808', True),
    0x8d4e6a7e: ('88a1180c', True),
    0x8d5b1260: ('00e80700', False),
    0x8d57ee6c: ('00e80000', False),
    0x8d588d94: ('48a3a80d', False),
    0x8d5bddd8: ('00e80500', False),
    0x8d5bde60: ('00e80000', False),
    # Aux controller and LFO register loads, stack saves and key dispatch.
    0x8c08f540: ('09da0a8434050000', True),
    0x8c08f610: ('6005213039307160', True),
    0x8c08fa14: ('0dda0a8434050000', True),
    0x8c08fd30: ('926982ce00c4419100003830', True),
    0x8d53d2a0: ('0ada0584506d0000', True),
    # Master processor entry prologues.
    0x8c093888: ('4360990900e80300', False),
    0x8c09392c: ('093200e803006160', False),
    0x8c093a70: ('00e80000eb052132', False),
    0x8d537d04: ('00e82400e305666f', False),
    0x8d588114: ('00e80c15e305666f', False),
    # Per-key send sites lie outside the stock hardware loops.
    0x8d511088: ('09da901100360000', True),
    0x8d5110ca: ('09da901100360000', True),
    0x8d511116: ('30e51200', True),
    0x8d51c7b2: ('0adab01148150000', True),
    # Profiling-off hooks replace loads and cycle-counter instructions.
    0x11a09322: ('08da0308c04f0000', True),
    0x11a0963a: ('09da0308c04f0000', True),
    0x11a0966c: ('08da0308c04f0000', True),
    0x11a097be: ('5db9c56f30009631', True),
    0x11a0985c: ('59b9300096318f31', True),
    0x11a09ac4: ('59b9300096318f31', True),
    0x8d51b288: ('08da0308c04f0000', True),
    0x8d51b52a: ('09da0308c04f0000', True),
    0x8d51b55c: ('08da0308c04f0000', True),
    0x8d51b78e: ('300096318f314000', True),
}
CONDITIONAL = (0x8c0b1456, '2160baa00a081810', True)
CONDITIONAL_JUMP = (0x8d5aca48, '000c1b18', False)
AUX_SHIFT_RETURN = (0x8c08f6ec, '01e8000008601000', True)
CALL_SUFFIX = (0x8c0b6184, 'ffe3eafd00da0208783c0000', True)
CALL_PREFIX = (0x8c084d18,'05301930ffe3f0fe',True)

RESIDENT_OBJECTS = ('sdk/divide.o', 'sdk/memory.o', 'sdk/hook.o', 'sdk/voice_clock.o', 'sdk/startup_label.o',
                    'sdk/collection_inspector.o', 'sdk/collection_probe.o', 'sdk/collection_rpc.o',
                    'sdk/collection_rpc_stock.o', 'sdk/collection_gate.o',
                    'sdk/collection_rpc_entry.o', 'sdk/collection_resident.o',
                    'sdk/collection_activation.o', 'sdk/collection_resources.o')
KERNEL_OBJECTS=tuple(p for p in RESIDENT_OBJECTS if p not in ('sdk/collection_probe.o','sdk/collection_rpc.o'))+(
    'sdk/collection_kernel.o','sdk/collection_mtp.o','sdk/collection_io.o','sdk/collection_cache.o',
    'sdk/collection_cache_core.o','sdk/collection_handoff.o','sdk/collection_resource_stock.o')


def linker(original, profile, resident_bytes=0x8000, *, kernel=False):
    """Keep the pool/tables unchanged and give resident state separate sections.

    This layout alone is not bootable: the coordinator still needs a qualified
    publication backend before hooks may leave their stock paths.
    """
    from .collection_activation import geometry
    from .pipeline import config
    require(hashlib.sha256(original.encode()).hexdigest()==profile['linker_sha256'],
            'Unreviewed input linker for resident placement')
    layout=geometry(profile,resident_bytes)
    old='.text : { *(.text .text.*) } > MODULE'
    require(original.count(old)==1, 'Changed resident linker boundary')
    def select(patterns):
        return ' '.join(name+'('+patterns+')' for name in (KERNEL_OBJECTS if kernel else RESIDENT_OBJECTS))
    replacement=(
        '.resident_text : { '+select('.text .text.*')+' } > MODULE\n'
        '  .resident_rodata : ALIGN(4) { '+select('.rodata .rodata.* .data.rel.ro .data.rel.ro.*')+' } > MODULE\n'
        '  .resident_data : ALIGN(4) { '+select('.data .data.* .sdata .sdata.*')+' } > MODULE\n'
        '  .resident_bss (NOLOAD) : ALIGN(8) { '+select('.bss .bss.* .sbss .sbss.* COMMON')+' } > MODULE\n'
        '  __collection_resident_end = .;\n'
        f'  ASSERT(__collection_resident_end <= {layout["payload_base"]:#x}, "Resident kernel exceeds its partition")\n'
        f'  .text {layout["payload_base"]:#x} : {{ '
        'KEEP(sdk/collection_entries.o(.text.collection_entries)) *(.text .text.*) } > MODULE\n'
        '  __collection_payload_text_end = .;')
    standard = config()
    def memory_region(p):
        return f'ORIGIN = {p["module_base"]:#x}, LENGTH = {p["reserved_bytes"] - p["table_bytes"]:#x}'
    original_region = memory_region(standard)
    require(original.count(original_region) == 1, 'Changed module memory region')
    result = original.replace(original_region, memory_region(profile)).replace(old, replacement)
    if kernel:
        marker='  .rodata : ALIGN(4)'
        require(result.count(marker)==1, 'Changed payload resource boundary')
        result=result.replace(marker,'  .collection_resources : ALIGN(4) { KEEP(sdk/collection_descriptors.o(.rodata .rodata.*)) } > MODULE\n'+marker)
    return result


def stock_path(entry):
    """Classify an exact profile preimage, refusing unknown displaced code."""
    site, length = int(entry['site'], 16), entry['length']
    raw, call = bytes.fromhex(entry['before']), entry['call']
    require(type(call) is bool and len(raw) == length and site % 2 == 0,
            'Invalid stock dispatch preimage')
    first = struct.unpack_from('<H', raw)[0] if len(raw) >= 2 else 0
    if length == 4 and first & 0xfe00 == 0xe200:
        target, original_call = decode_branch(site, raw)
        require(original_call == call, 'Stock branch and hook have different return semantics')
        return dict(kind='branch', target=target)
    if length == 8 and first in (0xdc00, 0xde00):
        original_call = first == 0xde00
        require(original_call == call, 'Stock absolute branch and hook differ')
        return dict(kind='branch', target=absolute_target(raw, original_call))
    if REPLAY.get(site) == (entry['before'], call):
        return dict(kind='replay', raw=raw)
    exact = (site, entry['before'], call)
    if exact==CALL_PREFIX:
        target,original_call=decode_branch(site+4,raw[4:])
        require(original_call and target==0x8c084afc, 'Changed player-turn stock call')
        return dict(kind='call_prefix',target=target,raw=raw[:4])
    if exact == AUX_SHIFT_RETURN:
        # UNLINK restores the containing method's RETS before its stock return.
        return dict(kind='return', raw=raw)
    if exact in (CONDITIONAL, CONDITIONAL_JUMP):
        # The final short conditional branch is relative to its own address.
        word = struct.unpack_from('<H', raw, length - 2)[0]
        offset = word & 0x3ff
        if offset & 0x200:
            offset -= 0x400
        return dict(kind='conditional', raw=raw[:-2], opcode=word & 0xfc00,
                    target=site+length-2+offset*2)
    if exact == CALL_SUFFIX:
        target, original_call = decode_branch(site, raw[:4])
        require(original_call, 'Displaced queue instruction is not a call')
        return dict(kind='call_suffix', target=target, raw=raw[4:])
    raise ValueError(f'Unreviewed displaced stock code at {site:#x}')


def _halfwords(raw):
    require(len(raw) % 2 == 0, 'Unaligned resident instruction span')
    return '    .short '+', '.join(f'{n:#06x}' for n in struct.unpack('<'+'H'*(len(raw)//2), raw))


def _absolute(target, call=False):
    return _halfwords(absolute_call(target) if call else absolute_jump(target))


def guarded_dispatch(interface):
    """Preserve SP, RETS and ASTAT before choosing either absolute tail path."""
    require(interface['target'] == 'opxy-1.2.1', 'Unreviewed resident target')
    entries = interface['entries']
    require(0 < len(entries) <= MAX_ENTRIES, 'Invalid guarded dispatch count')
    base = interface['geometry']['payload_base']
    require(base % 32 == 0, 'Unaligned resident payload')
    lines = ['.section .bss.collection_resident,"aw",@nobits', '.balign 4',
             '.global sdk_collection_dispatch_enabled', '.type sdk_collection_dispatch_enabled, STT_OBJECT',
             'sdk_collection_dispatch_enabled:', '    .space 4',
             '.size sdk_collection_dispatch_enabled, 4',
             '.section .text.collection_resident,"ax"', '.balign 4']
    for i, entry in enumerate(entries):
        path = stock_path(entry)
        site = int(entry['site'], 16)
        name, stock = f'sdk_collection_resident_{i}', f'sdk_collection_stock_{i}'
        lines += [f'.global {name}', f'.type {name}, STT_FUNC', f'{name}:',
                  '    [--SP] = P0;', '    [--SP] = R0;', '    [--SP] = ASTAT;',
                  '    P0.H = sdk_collection_dispatch_enabled;',
                  '    P0.L = sdk_collection_dispatch_enabled;',
                  '    R0 = [P0];', '    CC = R0 == 0;', f'    IF CC JUMP .Lstock_{i};',
                  '    ASTAT = [SP++];', '    R0 = [SP++];', '    P0 = [SP++];',
                  _absolute(base+i*4), f'.Lstock_{i}:',
                  '    ASTAT = [SP++];', '    R0 = [SP++];', '    P0 = [SP++];',
                  # The stock path follows its guard in the same text section.
                  f'.size {name}, .-{name}',
                  f'.global {stock}', f'.type {stock}, STT_FUNC', f'{stock}:']
        end = '    RTS;' if entry['call'] else _absolute(site+entry['length'])
        if path['kind'] == 'branch':
            lines += [_absolute(path['target'])]
        elif path['kind'] == 'replay':
            lines += [_halfwords(path['raw']), end]
        elif path['kind'] == 'return':
            lines += [_halfwords(path['raw'])]
        elif path['kind'] == 'conditional':
            # The short branch skips the fallback RTS or absolute jump.
            skip = 2 if entry['call'] else 5
            lines += [_halfwords(path['raw']),
                      _halfwords(struct.pack('<H', path['opcode'] | skip)),
                      end, _absolute(path['target'])]
        elif path['kind']=='call_prefix':
            # The wide hook CALL already supplies the exact original RETS.
            lines += [_halfwords(path['raw']),_absolute(path['target'])]
        else:
            # The callee sees the original SP, including all stack arguments.
            # Restore the stock CALL's RETS before its trailing address load.
            lines += [_absolute(path['target'], True), '    [--SP] = P0;',
                      f'    P0.H = {(site+4) >> 16:#x};',
                      f'    P0.L = {(site+4) & 65535:#x};',
                      '    RETS = P0;', '    P0 = [SP++];', _halfwords(path['raw']),
                      _absolute(site+entry['length'])]
        lines += [f'.size {stock}, .-{stock}']
    return '\n'.join(lines)+'\n'


def check_guarded_dispatch(elf, interface):
    """Check actual linked instructions, not assembly spelling or disassembly."""
    sections, symbols = elf32(elf)
    layout = interface['geometry']
    low, high = layout['resident_base'], layout['payload_base']
    flag = symbols['sdk_collection_dispatch_enabled']
    require(flag['type'] == 1 and flag['size'] == 4 and flag['value'] % 4 == 0 and
            low <= flag['value'] <= high-4, 'Dispatch flag outside resident RAM')
    require(any(s['type'] == 8 and s['flags'] & 7 == 3 and s['address'] <= flag['value'] and
                flag['value']+4 <= s['address']+s['size'] for s in sections.values()),
            'Dispatch must start disabled in zero-filled BSS')

    def contents(name):
        symbol = symbols[name]
        start, size = symbol['value'], symbol['size']
        require(symbol['type'] == 2 and size > 0 and low <= start < start+size <= high,
                'Dispatch function outside resident text')
        found = [s for s in sections.values() if s['type'] == 1 and s['flags'] & 6 == 6 and
                 s['address'] <= start and start+size <= s['address']+s['size']]
        require(len(found) == 1, 'Ambiguous resident instruction section')
        offset = start-found[0]['address']
        return start, found[0]['data'][offset:offset+size]

    ranges = [(flag['value'], flag['value']+4)]
    for i, entry in enumerate(interface['entries']):
        start, data = contents(f'sdk_collection_resident_{i}')
        target, original = contents(f'sdk_collection_stock_{i}')
        expected = (bytes.fromhex('480140016601') +
                    struct.pack('<4H', 0xe148, flag['value'] >> 16, 0xe108, flag['value'] & 65535) +
                    bytes.fromhex('0091000c0818260130907090') +
                    absolute_jump(layout['payload_base']+i*4) +
                    bytes.fromhex('260130907090'))
        require(data == expected and target == start + len(expected),
                'Dispatch does not preserve the exact register/flag paths')
        path, site = stock_path(entry), int(entry['site'], 16)
        end = bytes.fromhex('1000') if entry['call'] else absolute_jump(site+entry['length'])
        if path['kind'] == 'branch':
            expected = absolute_jump(path['target'])
        elif path['kind'] == 'replay':
            expected = path['raw']+end
        elif path['kind'] == 'return':
            expected = path['raw']
        elif path['kind'] == 'conditional':
            skip = 1 + len(end) // 2
            expected = (path['raw'] + struct.pack('<H', path['opcode'] | skip) +
                        end + absolute_jump(path['target']))
        elif path['kind']=='call_prefix':
            expected=path['raw']+absolute_jump(path['target'])
        else:
            expected = (absolute_call(path['target'])+bytes.fromhex('4801')+
                        struct.pack('<4H', 0xe148, (site+4) >> 16, 0xe108, (site+4) & 65535)+
                        bytes.fromhex('78387090')+path['raw']+absolute_jump(site+entry['length']))
        require(original == expected, 'Displaced stock path differs from its reviewed instructions')
        ranges.extend(((start, start+len(data)), (target, target+len(original))))
    ranges.sort()
    require(all(a[1] <= b[0] for a,b in zip(ranges, ranges[1:])), 'Overlapping resident dispatch functions')
    return dict(entries=len(interface['entries']), instruction_bytes=sum(b-a for a,b in ranges)-4,
                flag=flag['value'])


def check_cache_operations(elf, interface):
    sections,symbols=elf32(elf)
    low,high=(interface['geometry'][name] for name in ('resident_base','payload_base'))
    expected=bytes.fromhex('8856fb609a54020c1910010c1718c850830914180032083211302a4e2400'
                           '7002fa67020cfd132400294e7902f967010cfd13240023000860100000601000')
    scratch=bytes.fromhex('20e157131000')+bytes(26)
    ranges=[]
    try_lock=bytes.fromhex('003232000091000c061008600093420024001000420000601000')
    unlock=bytes.fromhex('003224000160019324001000')
    for name,data in (('sdk_collection_sync_code',expected),('sdk_collection_cache_scratch',scratch),
                      ('sdk_collection_try_lock',try_lock),('sdk_collection_unlock',unlock),
                      ('sdk_collection_fence',bytes.fromhex('24001000'))):
        symbol=symbols[name];address=symbol['value']
        require(symbol['type']==2 and symbol['size']==len(data) and low<=address and address+len(data)<=high,
                'Cache routine outside resident partition')
        found=[s for s in sections.values() if s['type']==1 and s['flags'] & 6 == 6 and
               s['address']<=address and address+len(data)<=s['address']+s['size']]
        require(len(found)==1, 'Cache routine is not allocated executable code')
        section=found[0];offset=address-section['address']
        require(section['data'][offset:offset+len(data)]==data, 'Changed native cache routine')
        ranges.append((address,address+len(data)))
    address=symbols['sdk_collection_cache_scratch']['value']
    require(address%32==0, 'Unaligned cache scratch line')
    for name,symbol in symbols.items():
        if name=='sdk_collection_cache_scratch' or symbol['type'] not in (1,2) or not symbol['size']:
            continue
        require(symbol['value']+symbol['size']<=address or address+32<=symbol['value'],
                'Cache scratch shares a line with another function or object')
    require(ranges[0][1]<=ranges[1][0] or ranges[1][1]<=ranges[0][0], 'Cache code overlaps its scratch')
    return dict(scratch=address,bytes=32,sync_bytes=len(expected))


def check_kernel_layout(elf,interface):
    from . import collection_activation as activation
    sections,symbols=elf32(elf);layout=interface['geometry']
    low,base,end=(layout[n] for n in ('resident_base','payload_base','reserved_end'))
    for name,flags,kind in (('.resident_text',6,1),('.resident_rodata',2,1),
                             ('.resident_data',3,1),('.resident_bss',3,8)):
        s=sections[name]
        require(s['size']>0 and s['flags']==flags and s['type']==kind and
                low<=s['address']<s['address']+s['size']<=base, 'Invalid resident partition section')
    require(sections['.resident_text']['address']==low and sections['.text']['address']==base and
            symbols['__collection_resident_end']['value']<=base, 'Moved resident/payload boundary')
    for name in ('.text','.rodata','.data','.bss','.collection_resources'):
        if name in sections and sections[name]['size']:
            s=sections[name]
            require(base<=s['address']<s['address']+s['size']<=end, 'Payload section outside fixed partition')
    text=sections['.text'];metadata=sections['.collection_resources']
    require(text['type']==1 and text['flags']==6 and metadata['type']==1 and metadata['flags']==2,
            'Payload code or resource descriptor permissions changed')
    entry_symbols=[symbols[e['symbol']] for e in interface['entries']]
    activation.check_entries(text['data'],base,text['size'],len(entry_symbols),entry_symbols)
    symbol=symbols['sdk_collection_resource_descriptors']
    require(symbol['type']==1 and symbol['size']>0 and symbol['size']%activation.RESOURCE_BYTES==0 and
            metadata['address']<=symbol['value'] and symbol['value']+symbol['size']<=metadata['address']+metadata['size'],
            'Unbounded collection resource descriptors')
    initialized=symbols['__extensions_initialized_end']['value']
    payload=bytearray(initialized-base)
    for name in ('.text','.rodata','.data','.collection_resources'):
        if name in sections:
            s=sections[name];offset=s['address']-base;payload[offset:offset+s['size']]=s['data']
    offset=symbol['value']-base;count=symbol['size']//activation.RESOURCE_BYTES
    resources=activation.check_resources(payload,text['size'],offset,count,
        readonly=(metadata['address']-base,metadata['size']))
    check_guarded_dispatch(elf,interface);check_cache_operations(elf,interface)
    for name in activation.RESIDENT_HOOKS|{'sdk_collection_startup_ready','sdk_collection_selected_resources','sdk_collection_resource_fallback'}:
        s=symbols[name];resident=sections['.resident_text']
        require(s['type']==2 and s['size']>0 and resident['address']<=s['value'] and
                s['value']+s['size']<=resident['address']+resident['size'], 'Resident entry escaped its code partition')
    return dict(resources=resources,resources_offset=offset,resources_count=count,payload_bytes=len(payload))
