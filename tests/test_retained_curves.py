"""Bounded cubic/open/closed/segment paths: host tests are not hardware rendering."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tooling import definitions, native, preview, adapters
from tooling.machines import machine_at
from machine_fixtures import fixture
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}


def write_fixture(folder):
    shutil.copytree(fixture('probesynth').root, folder, dirs_exist_ok=True)
    path = folder / 'definition.json'
    value = json.loads(path.read_text())
    value['resources']['display'].update(stateBytes=4, stateAlignment=4, paths=17, points=4,
                                         nodes=17, texts=0, palette=0, affine=1, fullCanvas=1, cubic=1)
    path.write_text(json.dumps(value))
    (folder / 'display.h').write_text('''#pragma once
#include <opxy/draw.hpp>
namespace probesynth {
struct DisplayState { opxy::u32 serial; };
using Frame = opxy::draw::AffineCubicFrame<17,4,17,0,0>;
}
''')
    (folder / 'display.cpp').write_text('''#include "display.h"
namespace probesynth {
inline void display_reset(DisplayState& state) { state.serial=0; }
inline void display(DisplayState& state,const opxy::draw::Input&,Frame& frame) {
    if (++state.serial == 0) ++state.serial;
    for (unsigned i=0;i<17;++i) {
        frame.paths[i].key=state.serial;
        frame.paths[i].points[0]={102401,204802};
        frame.paths[i].points[1]={102401,204802};
        frame.paths[i].points[2]={307203,409604};
        frame.paths[i].points[3]={512005,614406};
        opxy::draw::affine(frame,i,opxy::draw::Matrix{1024,0,0,1024,0,0});
    }
    for(unsigned i=0;i<4;++i) opxy::draw::header(frame,i,"");
}
}
''')
    artwork = '<g id="probesynth_panel">' + ''.join(
        f'<path id="probesynth_node_{i}" d="M0 0 C0 0 10 20 30 40" fill="none" stroke="#12ABEF" stroke-width="2"/>'
        for i in range(17)) + '</g>'
    (folder / 'panel.svg.fragment').write_text(artwork)
    metadata = folder / 'machine.json'
    record = json.loads(metadata.read_text()); record['sdk'] = '1.8'; record.pop('identity', None)
    metadata.write_text(json.dumps(record) + '\n')
    return machine_at(folder)


class RetainedCurves(unittest.TestCase):
    def run_command(self, command):
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=120, **HIDDEN)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_limits_masks_and_immutable_topology(self):
        with tempfile.TemporaryDirectory() as temp:
            machine = write_fixture(Path(temp))
            native.validate_panel(machine)
            value = copy.deepcopy(machine.definition)
            for update in [dict(paths=33), dict(points=5), dict(openPaths=1), dict(cubic=True),
                           dict(cubic=0, paths=3, points=128), dict(cubic=0, openPaths=1<<17),
                           dict(cubic=0, openPaths=1, segmentPaths=1)]:
                trial = copy.deepcopy(value); trial['resources']['display'].update(update)
                with self.assertRaises(ValueError): definitions.validate_definition(trial, 'synth')
            display = value['resources']['display']; display.update(cubic=0, paths=2, points=120, nodes=17, segmentPaths=1)
            definitions.validate_definition(value, 'synth')
            with tempfile.TemporaryDirectory() as generated:
                adapters.generate_display(generated, definition=value)
                self.assertIn('AffineFrame<2, 120, 17, 0, 0, 0u, 1u>', (Path(generated)/'display_binding.hpp').read_text())
            display.update(paths=1, points=256, segmentPaths=0, openPaths=1)
            definitions.validate_definition(value, 'synth')
            display['points'] = 257
            with self.assertRaises(ValueError): definitions.validate_definition(value, 'synth')
            art = (machine.root/'panel.svg.fragment').read_text()
            (machine.root/'panel.svg.fragment').write_text(art.replace(' C0 0 10 20 30 40', ' Q10 20 30 40'))
            with self.assertRaises(ValueError): native.validate_panel(machine)

    @unittest.skipUnless(HOST_CXX, 'Host C++ compiler required')
    def test_native_command_writes_preserve_topology_and_canaries(self):
        source = r'''
#include "path_geometry.hpp"
#include <cstring>
using namespace opxy;
using namespace opxy::screen;
int main() {
    struct Guarded {u32 before; Command commands[511]; u32 after;} storage;
    std::memset(&storage,0xa5,sizeof(storage));
    storage.before=123;storage.after=456;
    draw::Point points[256];
    for(u32 i=0;i<256;++i) points[i]={i32(i*1024+1),i32(220*1024-i*71)};
    Command* c=storage.commands;
    c[0].type=1;c[1].type=4;
    if(!command_types(c,4,cubic_curve))return 1;
    command_coordinates(c,points,4,cubic_curve);
    if(c[0].x!=1 || c[1].x!=1025 || c[1].control_x!=2049 || c[1].end_x!=3073)return 2;
    if(c[0].control_x!=i32(0xa5a5a5a5) || c[1].stock25[0]!=0xa5)return 3;
    for(u32 i=0;i<238;++i)c[i].type=(i%2)?2:1;
    if(!command_types(c,120,separate_segments)||command_count(120,separate_segments)!=238)return 4;
    command_coordinates(c,points,120,separate_segments);
    for(u32 i=0;i<119;++i) if(c[2*i].x!=points[i].x||c[2*i+1].y!=points[i+1].y)return 5;
    c[237].type=5;if(command_types(c,120,separate_segments))return 6;
    for(u32 i=0;i<120;++i) { c[i].type=i?2:1; }
    c[120].type=5;
    if(!command_types(c,120,closed_polyline)||!command_types(c,120,open_polyline))return 7;
    command_coordinates(c,points,120,closed_polyline);
    if(c[120].type!=5||c[119].x!=points[119].x||storage.before!=123||storage.after!=456)return 8;
    for(u32 i=0;i<510;++i) { c[i].type=(i%2)?2:1; }
    if(!command_types(c,256,separate_segments)||command_count(256,separate_segments)!=510)return 11;
    command_coordinates(c,points,256,separate_segments);
    if(c[509].x!=points[255].x||c[510].type!=0xa5||storage.after!=456)return 12;
    draw::AffineCubicFrame<17,4,17,0,0> frame={};
    for(u32 p=0;p<17;++p){frame.paths[p].key=1;for(u32 j=0;j<4;++j)frame.paths[p].points[j]=points[j];
      draw::affine(frame,p,draw::Matrix{1024,0,0,1024,0,0});}
    if(!draw::valid(frame))return 9;
    frame.paths[0].points[0].x=8192*1024+1;if(draw::valid(frame))return 10;
    static_assert(sizeof(Command)==32 && __builtin_offsetof(Command,type)==24,"XY command");
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp); path=folder/'test.cpp'; path.write_text(source); binary=folder/'test.exe'
            self.run_command([HOST_CXX,'-std=c++11','-O2','-Wall','-Werror','-I'+str(ROOT/'sdk/include'),
                              '-I'+str(ROOT/'sdk/platform/opxy-1.2.1'),path,'-o',binary])
            self.run_command([binary])

    @unittest.skipUnless(shutil.which('clang++') and shutil.which('node'), 'WASM and Node required')
    def test_actual_wasm_frame_and_browser_cubic_application(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp); machine=write_fixture(folder/'machine'); out=folder/'preview';out.mkdir()
            preview.compile_preview('clang++',machine.definition,out,machine.root)
            self.run_command(['node',ROOT/'tools/check_retained_curves.mjs',out/'engine.wasm'])

if __name__ == '__main__': unittest.main()
