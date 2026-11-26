"""Affine nodes and the whole-screen canvas, from declaration to the browser renderer."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tooling import adapters, definitions, native, preview, screen
from tooling.machines import machine_at
from machine_fixtures import fixture
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
DRAW_HEADER = '''#pragma once
#include <opxy/draw.hpp>
namespace probesynth {
struct DisplayState { opxy::u32 frames; };
using Frame = opxy::draw::AffineFrame<0, 0, 2, 0, 0>;
}
'''
DRAW_SOURCE = '''#include "display.h"
namespace probesynth {
inline void display_reset(DisplayState& state) { state.frames = 0; }
inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    ++state.frames;
    const opxy::i32 x = static_cast<opxy::i32>(input.track) + 1;
    opxy::draw::affine(frame, 0, opxy::draw::Matrix{1025, 3, -5, 512, x, 0});
    opxy::draw::affine(frame, 1, opxy::draw::Matrix{-1024, 0, 0, 0, 0, 1024});
    for (unsigned i = 0; i < 4; ++i) opxy::draw::header(frame, i, "");
}
}
'''
ART = b'''<g id="probesynth_panel"><g id="original_group">
<path id="probesynth_node_0" d="M0 0L10 0" fill="none" stroke="#12AbEf" stroke-width="2"/>
<path id="probesynth_node_1" d="M0 0L0 10" fill="none" stroke="#eD4321" stroke-width="3"/>
</g></g>'''


def write_fixture(folder):
    shutil.copytree(fixture('probesynth').root, folder, dirs_exist_ok=True)
    path = folder / 'definition.json'
    value = json.loads(path.read_text())
    value['resources']['display'].update(paths=0, points=0, nodes=2, palette=0,
                                         affine=1, fullCanvas=1)
    path.write_text(json.dumps(value, indent=2) + '\n')
    (folder / 'display.h').write_text(DRAW_HEADER)
    (folder / 'display.cpp').write_text(DRAW_SOURCE)
    (folder / 'panel.svg.fragment').write_bytes(ART)
    metadata = folder / 'machine.json'
    record = json.loads(metadata.read_text()); record['sdk'] = '%d.%d' % definitions.EXTENDED_SDK; record.pop('identity', None)
    metadata.write_text(json.dumps(record) + '\n')
    return machine_at(folder)


class DisplayExtension(unittest.TestCase):
    def run_command(self, command):
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=120, **HIDDEN)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_opt_in_resources_and_exact_canvas(self):
        with tempfile.TemporaryDirectory() as temp:
            machine = write_fixture(Path(temp))
            self.assertEqual(native.validate_panel(machine), ART)
            full = screen.compose(ART, full_canvas=True)
            self.assertIn(ART, full)
            self.assertNotIn(b'id="bar"', full)
            self.assertNotIn(b'id="values"', full)
            self.assertIn(b'viewBox="0 0 480 220"', full)
            value = copy.deepcopy(machine.definition)
            for key, invalid in [('affine', 2), ('fullCanvas', True), ('audioWords', 1), ('glyphs', 1)]:
                trial = copy.deepcopy(value); trial['resources']['display'][key] = invalid
                with self.assertRaises(ValueError): definitions.validate_definition(trial, 'synth')
            value['resources']['display']['affine'] = 0
            with self.assertRaises(ValueError): definitions.validate_definition(value, 'synth')

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_affine_validation(self):
        source = r'''
#include <opxy/draw.hpp>
int main() {
    opxy::draw::AffineFrame<0, 0, 1, 0, 0> f = {};
    opxy::draw::affine(f, 0, opxy::draw::Matrix{-1024, 4, -8, 0, 6000000, -6000000});
    if (!opxy::draw::valid(f)) return 1;
    f.nodes[0].paint = opxy::draw::stroke;
    if (opxy::draw::valid(f)) return 2;
    f.nodes[0].paint = opxy::draw::keep; f.nodes[0].a = 16385;
    if (opxy::draw::valid(f)) return 3;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp); path = folder / 'test.cpp'; path.write_text(source)
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            self.run_command([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                              '-I' + str(ROOT / 'sdk/include'), '-I' + str(ROOT / 'sdk/platform/opxy-1.2.1'),
                              path, '-o', binary])
            self.run_command([binary])

    @unittest.skipUnless(shutil.which('clang++') and shutil.which('node'), 'WASM compiler and Node required')
    def test_wasm_affine_backend(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp); machine = write_fixture(folder / 'machine')
            out = folder / 'preview'; out.mkdir()
            preview.compile_preview('clang++', machine.definition, out, machine.root)
            self.run_command(['node', ROOT / 'tools/check_display_extension.mjs', out / 'engine.wasm'])


if __name__ == '__main__': unittest.main()
