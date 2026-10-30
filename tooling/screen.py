"""SDK-owned OP-XY 1.2.1 screen shell; no stock artwork input."""
from pathlib import Path
import xml.etree.ElementTree as ET

from .formats import require, sha
from .pipeline import config

TEMPLATE = Path(__file__).resolve().parents[1] / 'sdk/platform/opxy-1.2.1/screen.svg'
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


def compose(fragment):
    data = template().replace(b'</svg>', fragment + b'</svg>')
    root = ET.fromstring(data)
    ids = [n.attrib['id'] for n in root.iter() if 'id' in n.attrib]
    require(len(ids) == len(set(ids)), 'Duplicate SVG binding IDs')
    return data
