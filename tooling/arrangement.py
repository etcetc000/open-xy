"""Bounded Arrange artwork and authenticated input-entry continuations."""
from pathlib import Path
import struct
import json
from .formats import require
from .stock_tables import SCREEN_COUNT

# Whole entry instructions, reviewed for position independence: LINK and
# register/stack moves. No relative call/branch is copied into the module.
ENTRIES = {
    'sdk_arrangement_stock_raw': (0x8c0b5d58, '00e80000fd052832'),
    'sdk_arrangement_stock_encoder': (0x8c0b5c0c, '00e800006805a66f'),
}


def continuation(name):
    address, preimage = ENTRIES[name]
    before = bytes.fromhex(preimage)
    target = address + len(before)
    return before + struct.pack('<4H', 0xdc00, target >> 16, target & 65535, 0)


def generate(folder):
    lines = ['.section .text', '.align 2']
    for name in ENTRIES:
        lines += [f'.global {name}', f'.type {name}, STT_FUNC', name + ':',
                  '.byte ' + ','.join(hex(b) for b in continuation(name)), f'.size {name}, .-{name}']
    target = Path(folder) / 'arrangement_stock.s'
    target.write_text('\n'.join(lines) + '\n')
    return target


def checked_ranges(profile, module, symbols, text):
    ranges = []
    for name, (address, before) in ENTRIES.items():
        if name not in symbols:
            continue
        matches = [p for p in profile['patches'] if int(p['address'], 16) == address]
        require(len(matches) == 1 and matches[0]['before'] == before and
                matches[0].get('encoding') == 'absolute_jump' and matches[0]['length'] == 8,
                'Changed arrangement entry preimage')
        s = symbols[name]
        at, code = s['value'] - profile['module_base'], continuation(name)
        require(s['type'] == 2 and s['size'] == len(code) and
                text['address'] <= s['value'] and s['value'] + len(code) <= text['address'] + text['size'] and
                at >= 0 and module[at:at + len(code)] == code, 'Changed arrangement stock continuation')
        ranges.append((s['value'], s['value'] + len(code)))
    return ranges


def ids():
    return ([f'arrangement_node_{i}' for i in range(128)] +
            [f'arrangement_text_{i}' for i in range(80)] + [f'arrangement_palette_{i}' for i in range(16)])


def header(folder):
    target = Path(folder) / 'arrangement_artwork.hpp'
    from .screen import compose
    svg = compose(artwork())
    require(len(svg) <= 65536, 'Arrangement artwork exceeds SVG budget')
    target.write_text('namespace { const char arrangement_svg[] = ' + json.dumps(svg.decode('utf-8')) + '; }\n' +
                      '#define OPXY_ARRANGEMENT_PAGE_LIMIT %d\n' % SCREEN_COUNT +
                      '#define OPXY_ARRANGEMENT_IDS ' + ','.join('"' + x + '"' for x in ids()) + '\n')
    return target


def artwork():
    nodes = [f'<path id="arrangement_node_{i}" d="M-64 0 L64 0 L64 8 L-64 8 Z" '
             'fill="none" stroke="#ffffff" stroke-width="1" transform="matrix(1 0 0 1 640 320)"/>' for i in range(128)]
    nodes += [f'<text id="arrangement_text_{i}" fill="#ffffff" font-family="UniversNextPro-Light" font-size="8px" transform="matrix(1 0 0 1 640 320)"> </text>' for i in range(80)]
    for i in range(16):
        shade = '%02x' % (i * 17)
        nodes += [f'<path id="arrangement_palette_{i}" d="M0 0 L1 0 L1 1 L0 1 Z" '
                  f'fill="#{shade * 3}" stroke="#{shade * 3}" stroke-width="1" transform="matrix(1 0 0 1 640 320)"/>']
    fragment = ('<g id="arrangement">' + ''.join(nodes) + '</g>').encode()
    from types import SimpleNamespace
    from .native import validate_panel
    shape = dict(paths=128, points=4, nodes=128, texts=80, palette=16)
    source = SimpleNamespace(prefix='arrangement', source=lambda _: 'SDK arrangement artwork',
                             definition={'resources': {'display': shape}})
    return validate_panel(source, fragment)
