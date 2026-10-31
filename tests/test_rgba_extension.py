"""Opt-in RGBA paint: bounded native records and real WASM/browser flow."""
import copy
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from tooling import definitions, native, preview
import test_display_extension as display_fixture
from test_sample_services import HOST_CXX
ROOT = Path(__file__).resolve().parents[1]


def write_fixture(folder):
    machine = display_fixture.write_fixture(folder)
    import json
    path = folder / 'definition.json'
    value = json.loads(path.read_text()); value['resources']['display']['rgba'] = 1
    path.write_text(json.dumps(value, indent=2) + '\n')
    path = folder / 'display.h'
    path.write_text(path.read_text().replace('opxy::draw::AffineFrame<0, 0, 2, 0, 0>',
                                            'opxy::draw::RgbaFrame<opxy::draw::AffineFrame<0, 0, 2, 0, 0>>'))
    path = folder / 'display.cpp'
    source = path.read_text().replace('    for (unsigned i',
        '    opxy::draw::rgba(frame, 0, opxy::draw::color(18, 171, 239, 128), opxy::draw::rgba_stroke);\n'
        '    opxy::draw::rgba(frame, 1, opxy::draw::color(237, 67, 33, 0), opxy::draw::rgba_fill_and_stroke);\n'
        '    for (unsigned i')
    path.write_text(source)
    path = folder / 'panel.svg.fragment'
    path.write_bytes(path.read_bytes().replace(b'id="probesynth_node_1" d="M0 0L0 10" fill="none"', b'id="probesynth_node_1" d="M0 0L0 10" fill="#334455"'))
    from tooling.machines import machine_at
    metadata = folder / 'machine.json'
    record = json.loads(metadata.read_text()); record['sdk'] = '1.8'; record.pop('identity', None)
    metadata.write_text(json.dumps(record) + '\n')
    return machine_at(folder)


class RgbaExtension(unittest.TestCase):
    run_command = display_fixture.DisplayExtension.run_command

    def test_opt_in_definition_and_asset(self):
        with tempfile.TemporaryDirectory() as temp:
            machine = write_fixture(Path(temp))
            native.validate_panel(machine)
            value = copy.deepcopy(machine.definition)
            for key, invalid in [('rgba', 2), ('rgba', True), ('palette', 1), ('affine', 0)]:
                trial = copy.deepcopy(value); trial['resources']['display'][key] = invalid
                with self.assertRaises(ValueError): definitions.validate_definition(trial, 'synth')
            path = machine.root / 'panel.svg.fragment'
            path.write_bytes(path.read_bytes().replace(b'stroke="#12AbEf"', b'opacity="0.5" stroke="#12AbEf"'))
            with self.assertRaises(ValueError): native.validate_panel(machine)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_native_palette_bounds_canaries_and_frame_capability(self):
        source = r'''#include <opxy/draw.hpp>
#include "rgba_palette.hpp"
int main() {
    using namespace opxy; using namespace opxy::screen::rgba_detail;
    u32 records[] = {0xabcdef01u, 1, 1024, 2, 3, 4, 5, 2048, 6, 7, 8, 0x12345678u};
    Address begin = reinterpret_cast<Address>(records + 1), end = reinterpret_cast<Address>(records + 11);
    if (!set_color(begin, end, end, 20, 2, 0x80efab12u)) return 1;
    if (records[6] != 0x80efab12u || records[7] != 2048 || records[5] != 4 ||
        records[0] != 0xabcdef01u || records[11] != 0x12345678u) return 2;
    if (set_color(begin, end, end, 20, 0, 42) || set_color(begin, end, end, 20, 3, 42) ||
        set_color(begin+1, end, end, 20, 1, 42) || set_color(begin,end,end-4,20,1,42) ||
        set_color(begin,end-1,end,20,1,42) || set_color(begin,end,end,4,11,42)) return 3;
    struct Node { u16 fill, stroke; } a={1,1}, b={2,2}; Node* nodes[]={&a,&b};
    if (!unique(nodes,2,0,false)) return 4;
    b.stroke=1; if (unique(nodes,2,0,false) || !unique(nodes,2,0,true)) return 5;
    draw::RgbaFrame<draw::AffineFrame<0,0,2,0,0>> frame = {};
    draw::rgba(frame,0,draw::color(18,171,239,128),draw::rgba_stroke);
    draw::rgba(frame,1,draw::color(237,67,33,0),draw::rgba_fill);
    if (!draw::valid(frame) || frame.nodes[0].shade != 0x80efab12u || sizeof(frame)!=136) return 6;
    draw::AffineFrame<0,0,2,0,0> ordinary = {};
    ordinary.nodes[0].paint=draw::rgba_stroke;
    if (draw::valid(ordinary)) return 7;
    frame.nodes[0].paint=static_cast<draw::Paint>(6);
    if (draw::valid(frame)) return 8;
    return 0;
}'''
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp); path = folder / 'test.cpp'; path.write_text(source)
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            self.run_command([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                              '-I'+str(ROOT/'sdk/include'), '-I'+str(ROOT/'sdk/platform/opxy-1.2.1'), path, '-o', binary])
            self.run_command([binary])

    @unittest.skipUnless(shutil.which('clang++') and shutil.which('node'), 'WASM compiler and Node required')
    def test_actual_wasm_to_browser_color_and_alpha(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp); machine=write_fixture(folder/'machine'); out=folder/'preview';out.mkdir()
            preview.compile_preview('clang++', machine.definition, out, machine.root)
            self.run_command(['node', ROOT/'tools/check_rgba_extension.mjs', out/'engine.wasm'])

if __name__ == '__main__': unittest.main()
