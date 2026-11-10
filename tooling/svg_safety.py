"""Common authored SVG safety policy, independent of its consumer."""
import re
from .formats import require
# Stock SVG parse (0x8d5f38ba) accepts at most four fractional digits and no exponents.
SVG_ELEMENTS = {'g', 'rect', 'circle', 'ellipse', 'line', 'polyline', 'polygon', 'path', 'text'}
SVG_NUMERIC = {'viewBox', 'd', 'points', 'transform', 'x', 'y', 'x1', 'y1', 'x2', 'y2', 'cx', 'cy', 'r', 'rx', 'ry',
               'width', 'height', 'stroke-width', 'stroke-miterlimit', 'stroke-dasharray',
               'stroke-dashoffset', 'font-size', 'opacity', 'fill-opacity', 'stroke-opacity'}
NUMBER = r'[-+]?(?:\d+(?:\.\d*)?|\.\d+)'
SVG_INHERITED = {'fill', 'stroke', 'stroke-width', 'font-family', 'font-size', 'style'}

def validate_xml(data):
    """Stock's loader recognizes literal SVG names, not XML namespace prefixes."""
    if isinstance(data, str):
        data = data.encode('utf-8')
    require(not re.search(rb'<\s*/?\s*[A-Za-z_][A-Za-z0-9_.-]*:', data),
            'Stock SVG requires unprefixed element names; use the default SVG namespace')


def validate_nodes(nodes):
    ids = [n.get("id") for n in nodes if n.get("id")]
    require(len(ids) == len(set(ids)), "Duplicate authored SVG ID")
    for node in nodes:
        for attribute in SVG_NUMERIC.intersection(node.attrib):
            value = node.attrib[attribute]
            require(not re.search(r"\d[eE][-+]?\d", value), "Exponent notation is outside the authored SVG numeric subset")
            require(not re.search(r"\.\d{5,}", value), "Authored SVG numbers must have at most four decimal places")
        require(node.tag in SVG_ELEMENTS, "Unsupported authored SVG element")
        require("style" not in node.attrib and not any(k.lower().startswith("on") or "href" in k.lower() or "url(" in v.lower() for k,v in node.attrib.items()), "Unsupported SVG style, event or external reference")
        if node.tag == "g":
            require(not SVG_INHERITED.intersection(node.attrib), "Put SVG paint/font attributes on leaves")
        else:
            require("fill" in node.attrib, "Missing explicit SVG fill")
            if node.tag == "text":
                require({"font-family", "font-size"} <= node.attrib.keys(), "Missing explicit text font")
            if node.get("fill") == "none":
                require(node.get("stroke") not in (None,"none") and float(node.get("stroke-width","0")) > 0, "Invisible authored SVG outline")
