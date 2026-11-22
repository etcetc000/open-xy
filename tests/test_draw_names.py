"""A machine's own display helpers may share a name with the SDK's extended-drawing setters
(draw.hpp extended_helpers) and be called unqualified on draw types, as published machines do:
argument-dependent lookup must not make those calls ambiguous, on the plain renderer or the
extended one."""
import json
import re
from pathlib import Path
import shutil
import tempfile
import unittest
from tooling import definitions, preview
from tooling.machines import machine_at
from machine_fixtures import fixture

HELPERS = '''
// The machine's own helpers, named as the SDK's extended setters.
template <class F> inline void conceal(F& frame, opxy::u32 node) { frame.nodes[node].shade = 0; }
template <class F> inline void rgba(F&, opxy::u32, opxy::u32, opxy::draw::Paint) {}
template <class F> inline void affine(F&, opxy::u32, opxy::draw::Matrix, opxy::u32, opxy::draw::Paint) {}
inline opxy::u32 color(opxy::u8 r, opxy::u8 g, opxy::u8 b, opxy::u8 a) { return r ^ g ^ b ^ a; }
inline opxy::i32 matrix(const opxy::draw::Node& node) { return node.x; }
inline opxy::i32 matrix(const opxy::draw::AffineNode& node) { return node.x; }
'''
PLAIN = ('''#pragma once
#include <opxy/draw.hpp>
namespace probesynth {
using Frame = opxy::draw::Frame<2, 64, 2, 0, 16>;
struct DisplayState { opxy::u32 frames; };
}
''', '''#include "display.h"
namespace probesynth {
''' + HELPERS + '''
inline void display_reset(DisplayState& state) { state.frames = 0; }
inline void display(DisplayState& state, const opxy::draw::Input&, Frame& frame) {
    using namespace opxy::draw;
    ++state.frames;
    for (opxy::u32 p = 0; p < Frame::path_count; ++p) {
        frame.paths[p].key = 1;
        for (opxy::u32 i = 0; i < Frame::point_count; ++i) frame.paths[p].points[i] = {0, 0};
    }
    place(frame, 0, (100 + matrix(frame.nodes[1]) % 8) * unit, 100 * unit, unit, 1 + color(1, 2, 3, 4) % 8, stroke);
    place(frame, 1, 200 * unit, 100 * unit, unit, 2, stroke);
    conceal(frame, 1);
}
}
''')
EXTENDED = ('''#pragma once
#include <opxy/draw.hpp>
namespace probesynth {
using Frame = opxy::draw::AffineFrame<0, 0, 2, 0, 0>;
struct DisplayState { opxy::u32 frames; };
}
''', '''#include "display.h"
namespace probesynth {
''' + HELPERS + '''
inline void display_reset(DisplayState& state) { state.frames = 0; }
inline void display(DisplayState& state, const opxy::draw::Input&, Frame& frame) {
    ++state.frames;
    const opxy::draw::Matrix m = {1024, 0, 0, 1024, 0, 0};
    opxy::draw::affine(frame, 0, m);
    opxy::draw::affine(frame, 1, m);
    affine(frame, 0, m, 0, opxy::draw::keep);
    rgba(frame, 0, color(1, 2, 3, 4), opxy::draw::keep);
    if (matrix(frame.nodes[0]) != 0) conceal(frame, 1);
    for (unsigned i = 0; i < 4; ++i) opxy::draw::header(frame, i, "");
}
}
''')
EXTENDED_ART = '''<g id="probesynth_panel">
<path id="probesynth_node_0" d="M0 0L10 0" fill="none" stroke="#ffffff" stroke-width="2"/>
<path id="probesynth_node_1" d="M0 0L0 10" fill="none" stroke="#ffffff" stroke-width="2"/>
</g>'''


def write_probe(folder, header, source, extended):
    shutil.copytree(fixture('probesynth').root, folder, dirs_exist_ok=True)
    (folder / 'display.h').write_text(header)
    (folder / 'display.cpp').write_text(source)
    if extended:
        path = folder / 'definition.json'
        value = json.loads(path.read_text())
        value['resources']['display'].update(paths=0, points=0, nodes=2, palette=0, affine=1)
        path.write_text(json.dumps(value))
        (folder / 'panel.svg.fragment').write_text(EXTENDED_ART)
        record = json.loads((folder / 'machine.json').read_text())
        record['sdk'] = '%d.%d' % definitions.EXTENDED_SDK
        record.pop('identity', None)
        (folder / 'machine.json').write_text(json.dumps(record))
    return machine_at(folder)


class DrawNames(unittest.TestCase):
    @unittest.skipUnless(shutil.which('clang++'), 'WASM compiler required')
    def test_a_machines_own_helpers_are_not_ambiguous(self):
        for name, (header, source), extended in (('plain', PLAIN, False), ('extended', EXTENDED, True)):
            with self.subTest(renderer=name), tempfile.TemporaryDirectory() as temp:
                m = write_probe(Path(temp) / 'machine', header, source, extended)
                self.assertEqual(definitions.extended_display(m.definition['resources']['display']), extended)
                out = Path(temp) / 'out'
                out.mkdir()
                preview.compile_preview('clang++', m.definition, out, m.root, m.sources)
                self.assertTrue((out / 'engine.wasm').is_file())

    def test_the_setters_are_nominated_not_declared_in_draw(self):
        # The using-directive is what keeps them out of argument-dependent lookup.
        text = (Path(__file__).resolve().parents[1] / 'sdk/include/opxy/draw.hpp').read_text(encoding='utf-8')
        body = text[text.index('namespace extended_helpers {'):text.index('using namespace extended_helpers;')]
        before = text[:text.index('namespace extended_helpers {')]
        for name in ('conceal', 'color', 'rgba', 'matrix', 'affine'):
            declaration = re.compile(r'static inline (?:void|u32|Matrix) ' + name + r'\(')
            self.assertTrue(declaration.search(body), name)
            self.assertFalse(declaration.search(before), name)


if __name__ == '__main__':
    unittest.main()
