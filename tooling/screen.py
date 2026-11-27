"""SDK-owned OP-XY 1.2.1 screen shell; no stock artwork input."""
from pathlib import Path
import xml.etree.ElementTree as ET

from .formats import require, sha
from .pipeline import config

TEMPLATE = Path(__file__).resolve().parents[1] / 'sdk/platform/opxy-1.2.1/screen.svg'
HEADER = Path(__file__).resolve().parents[1] / 'sdk/templates/shared/page_header.svg.in'
NS = '{http://www.w3.org/2000/svg}'


def validate_template(data):
    root = ET.fromstring(data)
    require(root.tag == NS + 'svg' and root.get('viewBox') == '0 0 480 220', 'Invalid platform screen dimensions')
    require(root.get('width') == '480' and root.get('height') == '220', 'Invalid platform screen size')
    require(len(root) == 4 and root[0].tag == NS + 'rect', 'Unexpected platform screen content')
    # The stepped header bar: eight explicitly filled blocks across the top.
    bars = [n for n in root if n.get('id') == 'bar']
    require(len(bars) == 1 and bars[0].tag == NS + 'g' and len(bars[0]) == 8 and
            all(r.tag == NS + 'rect' and r.get('fill') for r in bars[0]), 'Screen requires the header bar')
    # Stock helper 0x8d521704 looks up these groups, indexes four children,
    # then resolves the text leaf in each child with 0x8d520228.
    for name in ('titles', 'values'):
        groups = [n for n in root if n.get('id') == name]
        require(len(groups) == 1 and groups[0].tag == NS + 'g' and len(groups[0]) == 4,
                'Screen requires four ' + name)
        for child in groups[0]:
            require(child.tag == NS + 'g' and len(child) == 1 and child[0].tag == NS + 'text',
                    'Screen control must contain one text leaf')
            require(child[0].get('fill') and child[0].get('font-size') and child[0].text,
                    'Screen control requires explicit paint and text')
    require(data.count(b'</svg>') == 1, 'Unexpected screen root boundary')
    return data


def template():
    data = TEMPLATE.read_bytes()
    require(sha(data) == config()['screen_template_sha256'], 'Platform screen template differs from the profile')
    return validate_template(data)


def page_header(identity, first_node):
    """Authored half of opxy/page_header.hpp: eight bar nodes and sixteen texts."""
    text = HEADER.read_text(encoding='utf-8')
    for i in range(8):
        text = text.replace('@NODE_%d@' % i, str(first_node + i))
    return text.replace('@ID@', identity)


# A whole-screen canvas (extended drawing's fullCanvas): the machine's artwork alone,
# without the stock header the template carries.
CANVAS = b'<svg xmlns="http://www.w3.org/2000/svg" width="480" height="220" viewBox="0 0 480 220"></svg>'


def kit_header(data):
    """The template's header as a whole-screen drum kit's screen carries it, over the kit's
    drawing (extended_drawing.hpp CanvasKitHeader shows it only while Shift is held): the bar's
    eight blocks as paths bar_0..bar_7, which the renderer can hide, in the same place and
    paint, then the template's values and titles groups as they are."""
    root = ET.fromstring(data)
    bar = next(n for n in root if n.get('id') == 'bar')
    blocks = []
    for i, block in enumerate(bar):
        x, width, height = (int(block.get(key, '0')) for key in ('x', 'width', 'height'))
        blocks.append(f'<path id="bar_{i}" d="M{x} 0L{x + width} 0L{x + width} {height}L{x} {height}Z" fill="{block.get("fill")}"/>')
    start, end = data.index(b'<g id="values">'), data.rindex(b'</svg>')
    require(len(blocks) == 8 and all(block.tag == NS + 'rect' for block in bar), 'Unexpected screen template header')
    return ('<g id="bar">' + ''.join(blocks) + '</g>\n  ').encode('ascii') + data[start:end]


def compose(fragment, full_canvas=False, kit=False):
    """A machine's display file: the template's header, then the machine's artwork; with
    full_canvas the artwork alone on a bare canvas, and for a drum kit (kit) the header's
    bar, values and titles after it (kit_header)."""
    data = template()  # authenticated even when the canvas leaves it out
    header = kit_header(data) if full_canvas and kit else b''
    data = (CANVAS if full_canvas else data).replace(b'</svg>', fragment + header + b'</svg>')
    root = ET.fromstring(data)
    ids = [n.attrib['id'] for n in root.iter() if 'id' in n.attrib]
    require(len(ids) == len(set(ids)), 'Duplicate SVG binding IDs')
    return data
