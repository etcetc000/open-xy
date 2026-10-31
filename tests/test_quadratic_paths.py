"""Variable-count M/L/Q streams with fixed native storage and exact Q10."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tooling import definitions, native, preview
from tooling.machines import machine_at
from machine_fixtures import fixture
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}


def write_fixture(folder):
    shutil.copytree(fixture('probesynth').root, folder, dirs_exist_ok=True)
    path = folder/'definition.json'; value=json.loads(path.read_text())
    value['resources']['display'].update(stateBytes=4,stateAlignment=4,paths=8,points=0,commands=16,
        nodes=8,texts=0,palette=0,affine=1,fullCanvas=1,quadratic=1,rgba=1)
    path.write_text(json.dumps(value))
    (folder/'display.h').write_text('''#pragma once
#include <opxy/draw.hpp>
namespace probesynth {
struct DisplayState { opxy::u32 serial; };
using Frame=opxy::draw::RgbaFrame<opxy::draw::AffineQuadraticFrame<8,16,8,0,0>>;
}
''')
    (folder/'display.cpp').write_text('''#include "display.h"
namespace probesynth {
inline void display_reset(DisplayState& state){state.serial=0;}
inline void display(DisplayState& state,const opxy::draw::Input&,Frame& frame){
    if(++state.serial==0)++state.serial;
    const unsigned counts[3]={16,15,10};
    for(unsigned p=0;p<8;++p){
        auto& path=frame.paths[p];path.key=state.serial;path.count=counts[state.serial%3];
        for(unsigned i=0;i<path.count;++i)
            path.commands[i]={i==0?opxy::draw::move_to:i==path.count-1?opxy::draw::line_to:opxy::draw::quadratic_to,
                              {opxy::i32(i*1024+1),204802},{opxy::i32(i*2048+3),307204}};
        opxy::draw::affine(frame,p,opxy::draw::Matrix{1024,0,0,1024,0,0});
        opxy::draw::rgba(frame,p,opxy::draw::color(12,34,56,255));
    }
    for(unsigned i=0;i<4;++i)opxy::draw::header(frame,i,"");
}
}
''')
    outline='M0 0'+' Q10 20 30 40'*15
    (folder/'panel.svg.fragment').write_text('<g id="probesynth_panel">'+''.join(
        f'<path id="probesynth_node_{i}" d="{outline}" fill="none" stroke="#112233" stroke-width="2"/>'
        for i in range(8))+'</g>')
    metadata = folder / 'machine.json'
    record = json.loads(metadata.read_text()); record['sdk'] = '1.8'; record.pop('identity', None)
    metadata.write_text(json.dumps(record) + '\n')
    return machine_at(folder)


class QuadraticPaths(unittest.TestCase):
    def run_command(self, command):
        result=subprocess.run(list(map(str,command)),capture_output=True,text=True,timeout=120,**HIDDEN)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_schema_budget_and_initial_storage_shape(self):
        with tempfile.TemporaryDirectory() as temp:
            machine=write_fixture(Path(temp));native.validate_panel(machine)
            for changes in (dict(commands=17),dict(points=16),dict(quadratic=0),dict(cubic=1),
                            dict(affine=0),dict(openPaths=1),dict(commands=True),dict(commands=1)):
                value=copy.deepcopy(machine.definition);value['resources']['display'].update(changes)
                with self.assertRaises(ValueError):definitions.validate_definition(value,'synth')
            art=(machine.root/'panel.svg.fragment').read_text()
            (machine.root/'panel.svg.fragment').write_text(art.replace(' Q10 20 30 40',' L30 40',1))
            with self.assertRaises(ValueError):native.validate_panel(machine)

    @unittest.skipUnless(HOST_CXX,'Host C++ compiler required')
    def test_native_records_change_length_without_overwriting_capacity_or_canaries(self):
        source=r'''
#include "path_geometry.hpp"
#include <cstring>
using namespace opxy;
using namespace opxy::screen;
int main(){
    struct Guarded {u32 before;Command c[17];u32 after;} storage;
    std::memset(&storage,0xa5,sizeof(storage));storage.before=123;storage.after=456;
    for(u32 i=0;i<16;++i)storage.c[i].type=i?3:1;
    if(!quadratic_types(storage.c,16,16,true))return 1;
    draw::AffineQuadraticFrame<8,16,8,0,0> frame={};
    const unsigned counts[]={15,10,14,11,16,2};unsigned current=16;
    for(unsigned pass=0;pass<6;++pass){
        for(unsigned p=0;p<8;++p){auto& path=frame.paths[p];path.key=pass+1;path.count=counts[pass];
            for(unsigned i=0;i<path.count;++i)path.commands[i]={i==0?draw::move_to:i+1==path.count?draw::line_to:draw::quadratic_to,
                {i32(i*1024+1),204802},{i32(i*2048+3),307204}};
            draw::affine(frame,p,draw::Matrix{1024,0,0,1024,0,0});}
        if(!draw::valid(frame)||!quadratic_types(storage.c,current,16,false))return 2;
        current=path_coordinates(storage.c,frame.paths[0],open_polyline);
        if(current!=counts[pass]||storage.c[0].x!=3||storage.c[0].control_x||storage.c[current-1].type!=2)return 3;
        if(current>2&&(storage.c[1].x!=1025||storage.c[1].control_x!=2051||storage.c[1].end_x))return 4;
        if(storage.c[16].type!=0xa5||storage.c[1].stock25[0]!=0xa5||storage.before!=123||storage.after!=456)return 5;
    }
    frame.paths[0].count=17;if(draw::valid(frame))return 6;
    frame.paths[0].count=2;frame.paths[0].commands[0].kind=draw::line_to;if(draw::valid(frame))return 7;
    frame.paths[0].commands[0].kind=draw::move_to;frame.paths[0].commands[1].kind=draw::quadratic_to;
    frame.paths[0].commands[1].control.x=8192*1024+1;if(draw::valid(frame))return 8;
    storage.c[1].type=4;if(quadratic_types(storage.c,2,16,false))return 9;
    if(quadratic_types(storage.c,17,16,false)||quadratic_types(storage.c,0,16,false))return 10;
    static_assert(sizeof(draw::QuadraticCommand)==20,"Command ABI");
    static_assert(sizeof(frame.paths[0])==328,"Path ABI");
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);source_path=folder/'test.cpp';source_path.write_text(source);binary=folder/'test.exe'
            self.run_command([HOST_CXX,'-std=c++11','-O2','-Wall','-Werror','-I'+str(ROOT/'sdk/include'),
                              '-I'+str(ROOT/'sdk/platform/opxy-1.2.1'),source_path,'-o',binary])
            self.run_command([binary])

    @unittest.skipUnless(shutil.which('clang++') and shutil.which('node'),'WASM and Node required')
    def test_compiled_variable_paths_reach_browser_without_stale_segments(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);machine=write_fixture(folder/'machine');out=folder/'preview';out.mkdir()
            preview.compile_preview('clang++',machine.definition,out,machine.root)
            self.run_command(['node',ROOT/'tools/check_quadratic_paths.mjs',out/'engine.wasm'])

if __name__=='__main__':unittest.main()
