# Owned retained glyph atlas

An opt-in glyph frame selects bounded immutable SVG outlines into independently
owned path leaves. It uses no system font, per-frame allocation, or shared
mutable command buffer. The source artwork remains the machine's input.

```cpp
using Base = opxy::draw::AffineFrame<13, 2, 134, 0, 0, 8191>;
using Frame = opxy::draw::GlyphFrame<Base, 39, 16, 56>;
// Definition index, not an ASCII character:
opxy::draw::glyph(frame, instance, definition);
```

Declare matching numeric display resources: `glyphs:39`,
`glyphDefinitions:16`, `glyphStart:56`. At most 64 instances and 16 definitions
are supported, with two owned path nodes per instance. The destination interval
must follow mutable paths and fit the existing 256-node budget. Ordinary frames
and renderer objects keep their prior layouts. GlyphFrame repeats the ordinary
standard-layout prefix and appends one selector word per instance.

Author source paths as `<prefix>_glyph_<definition>_<part>` (part 0 or 1), under
an offscreen static group such as `transform="translate(1024,1024)"`. Source
paths have no individual transform and contain explicit absolute M/L/C/Z
commands, at most 16 per path, within +/-8192 pixels. The first part must have
at least two records; use `M0,0` for an absent second part. Sources are not frame
nodes. Geometry is quantized to the original local Q10 coordinates once.

Destination nodes `glyphStart+2*instance+part` are stroke-only (`fill="none"`)
paths reserved with sixteen distinct endpoints: `M0,0 L1,0 ... L15,0`. Repeated
identical points are unsuitable because the stock parser merges them. Initial
scaffolds may be parked with `matrix(1 0 0 1 1024 1024)`. Ordinary frame nodes
continue to own their matrices and paint; selection changes only geometry.

Native initialization validates all vector capacities, vtables and allocation
ranges, then copies sources to object-owned immutable storage. Every update
preflights all selectors and saved begin/end/capacity values before writing.
Changed definitions copy into the same allocations, adjust end within capacity,
clear renderer scratch and invalidate stock caches. Empty second parts have
zero records; unchanged definitions write nothing. Added native storage is
`1032*definitions + 28*instances + 4` bytes. Sources and destination allocations
must never alias. Initialization or preflight failure rejects the update.

The browser snapshots the same source paths into exact Q10 outlines and applies
selectors to the same pair of leaves. Its affine non-scaling stroke treatment
preserves logical line width. This is geometry and layout equivalence, not a
claim of pixel-identical raster coverage.

`tests/test_glyph_atlas.py` executes the actual native binder as self-contained
WASM with explicit stock lookup/invalidation doubles, verifies compiled frame
transport and browser application, and rejects malformed schemas/assets.
Hardware lifecycle and rendering qualification remain separate.
