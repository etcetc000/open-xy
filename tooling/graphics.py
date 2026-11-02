"""Developer grayscale artwork: paint, animation bindings and authored origins."""
import re
import xml.etree.ElementTree as ET

from .formats import require


def validate_grayscale(m, original, variant):
    from .native import display_ids, validate_panel
    from .screen import compose
    require(0 < len(variant) <= 65536 and not re.search(br'<!DOCTYPE|<!ENTITY', variant, re.I),
            'Invalid grayscale artwork size or declaration')
    validate_panel(m, variant)
    compose(variant)  # Also reject bindings that shadow the platform header.
    def indexed(data):
        nodes, ancestors = {}, {}
        def visit(node, parents):
            if node.get('id'):
                nodes[node.get('id')] = node
                ancestors[node.get('id')] = parents
            for child in node:
                visit(child, parents + [node.get('transform', '')])
        visit(ET.fromstring(data), [])
        return nodes, ancestors
    base, base_ancestors = indexed(original)
    gray, gray_ancestors = indexed(variant)
    for node in ET.fromstring(variant).iter():
        for key in ('fill', 'stroke'):
            if key not in node.attrib:
                continue
            paint = node.get(key).strip().lower()
            if paint in ('none', 'black', 'white', 'gray', 'grey', 'silver'):
                continue
            require(re.fullmatch(r'#(?:[0-9a-f]{3}|[0-9a-f]{6})', paint), 'Unsupported grayscale paint: ' + paint)
            digits = paint[1:]
            channels = list(digits) if len(digits) == 3 else [digits[i:i + 2] for i in (0, 2, 4)]
            require(len(set(channels)) == 1, 'Developer grayscale artwork contains color: ' + paint)
    display = m.definition['resources']['display']
    for name in display_ids(m.prefix, display):
        a, b = base[name], gray[name]
        require(a.tag == b.tag, 'Grayscale binding changed type: ' + name)
        if '_palette_' in name:
            require(0 < float(b.get('stroke-width', '0')) <= 16, 'Invalid grayscale palette width: ' + name)
            continue
        require((a.get('fill') != 'none') == (b.get('fill') != 'none') and
                a.get('transform', '') == b.get('transform', '') and base_ancestors[name] == gray_ancestors[name],
                'Grayscale binding origin or fill behavior changed: ' + name)
    return variant
