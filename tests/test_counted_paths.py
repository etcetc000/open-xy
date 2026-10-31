"""Active polyline counts retain exact topology within a fixed native allocation."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tooling import definitions,native,preview
from tooling.machines import machine_at
from machine_fixtures import fixture
from test_sample_services import HOST_CXX
ROOT=Path(__file__).resolve().parents[1]
HIDDEN=dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name=='nt' else {}

def write_fixture(folder):
    shutil.copytree(fixture('probesynth').root,folder,dirs_exist_ok=True)
    path=folder/'definition.json';value=json.loads(path.read_text())
    value['resources']['display'].update(stateBytes=4,stateAlignment=4,paths=4,points=82,nodes=4,texts=0,palette=0,
                                        affine=1,fullCanvas=1,rgba=1,countedPaths=1,segmentPaths=15)
    path.write_text(json.dumps(value))
    (folder/'display.h').write_text('''#pragma once
#include <opxy/draw.hpp>
namespace probesynth {
struct DisplayState { opxy::u32 serial; };
using Frame=opxy::draw::RgbaFrame<opxy::draw::AffineCountedFrame<4,82,4,0,0,0,15>>;
}
''')
    (folder/'display.cpp').write_text('''#include "display.h"
namespace probesynth {
inline void display_reset(DisplayState& state){state.serial=0;}
inline void display(DisplayState& state,const opxy::draw::Input&,Frame& frame){
 if(++state.serial==0)++state.serial;
 const unsigned counts[3][4]={{64,64,64,64},{82,62,50,42},{2,2,2,2}};
 for(unsigned p=0;p<4;++p){auto& path=frame.paths[p];path.key=state.serial;path.count=counts[state.serial%3][p];
  for(unsigned i=0;i<path.count;++i)path.points[i]={opxy::i32(i*1024+1),opxy::i32(p*20480+102402)};
  opxy::draw::affine(frame,p,opxy::draw::Matrix{1024,0,0,1024,0,0});
  opxy::draw::rgba(frame,p,opxy::draw::color(12,34,56,255));}
 for(unsigned i=0;i<4;++i)opxy::draw::header(frame,i,"");
}
}
''')
    outline=' '.join(f'M{i} 100 L{i+1} 100' for i in range(81))
    (folder/'panel.svg.fragment').write_text('<g id="probesynth_panel">'+''.join(
        f'<path id="probesynth_node_{i}" d="{outline}" fill="none" stroke="#112233" stroke-width="1.3"/>'
        for i in range(4))+'</g>')
    metadata = folder / 'machine.json'
    record = json.loads(metadata.read_text()); record['sdk'] = '1.8'; record.pop('identity', None)
    metadata.write_text(json.dumps(record) + '\n')
    return machine_at(folder)

def write_glyph_fixture(folder):
    machine=write_fixture(folder);definition=machine.definition
    definition['resources']['display'].update(nodes=6,glyphs=1,glyphDefinitions=2,glyphStart=4)
    (machine.root/'definition.json').write_text(json.dumps(definition))
    header=(machine.root/'display.h').read_text().replace(
        'using Frame=opxy::draw::RgbaFrame<opxy::draw::AffineCountedFrame<4,82,4,0,0,0,15>>;',
        'using Frame=opxy::draw::GlyphFrame<opxy::draw::RgbaFrame<opxy::draw::AffineCountedFrame<4,82,6,0,0,0,15>>,1,2,4>;')
    (machine.root/'display.h').write_text(header)
    source=(machine.root/'display.cpp').read_text().replace(' for(unsigned i=0;i<4;++i)opxy::draw::header',
        ' for(unsigned i=4;i<6;++i)opxy::draw::affine(frame,i,opxy::draw::Matrix{1024,0,0,1024,0,0});\n frame.glyphs[0]=state.serial%2;\n for(unsigned i=0;i<4;++i)opxy::draw::header')
    (machine.root/'display.cpp').write_text(source)
    scaffold='M0 0 '+' '.join(f'L{i} 0' for i in range(1,16))
    extra='<g transform="translate(1024,1024)">'+''.join(
        f'<path id="probesynth_glyph_{i//2}_{i%2}" d="{d}" fill="none" stroke="#ffffff" stroke-width="1.3"/>'
        for i,d in enumerate(['M0 0 L1 2','M0 0','M3 4 L5 6','M7 8 L9 10']))+'</g>'
    extra+=''.join(f'<path id="probesynth_node_{i}" d="{scaffold}" fill="none" stroke="#ffffff" stroke-width="1.3"/>' for i in range(4,6))
    art=(machine.root/'panel.svg.fragment').read_text();(machine.root/'panel.svg.fragment').write_text(art[:-4]+extra+'</g>')
    return machine_at(machine.root)

class CountedPaths(unittest.TestCase):
    def run_command(self,command):
        r=subprocess.run(list(map(str,command)),capture_output=True,text=True,timeout=120,**HIDDEN)
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_schema_storage_bound_and_default_abi(self):
        with tempfile.TemporaryDirectory() as temp:
            machine=write_fixture(Path(temp));native.validate_panel(machine)
            for changes in (dict(points=88),dict(countedPaths=0),dict(cubic=1),dict(quadratic=1),dict(affine=0),dict(countedPaths=True)):
                value=copy.deepcopy(machine.definition);value['resources']['display'].update(changes)
                with self.assertRaises(ValueError):definitions.validate_definition(value,'synth')

    @unittest.skipUnless(HOST_CXX,'Host C++ compiler required')
    def test_exact_counts_and_native_canaries(self):
        source=r'''
#include "path_geometry.hpp"
#include <cstring>
using namespace opxy;using namespace opxy::screen;
int main(){
 draw::AffineCountedFrame<4,82,4,0,0,0,15> frame={};
 const unsigned counts[]={82,62,50,42};
 for(unsigned p=0;p<4;++p){auto& path=frame.paths[p];path.key=1;path.count=counts[p];
  for(unsigned i=0;i<path.count;++i)path.points[i]={i32(i*1024+1),102402};
  draw::affine(frame,p,draw::Matrix{1024,0,0,1024,0,0});}
 if(!draw::valid(frame))return 1;
 frame.paths[1].count=82;frame.paths[2].count=82;if(draw::valid(frame))return 2;
 frame.paths[1].count=62;frame.paths[2].count=50;
 struct Guarded {u32 before;Command c[163];u32 after;} storage;
 const PathKind kinds[]={closed_polyline,open_polyline,separate_segments};
 for(auto kind:kinds){std::memset(&storage,0xa5,sizeof(storage));storage.before=123;storage.after=456;
  for(unsigned count:{82u,2u,64u,82u}){auto& path=frame.paths[0];path.count=count;
   unsigned records=path_coordinates(storage.c,path,kind);
   if(!counted_types(storage.c,records,82,kind,false)||records!=command_count(count,kind))return 3;
   if(storage.c[records-1].type!=(kind==closed_polyline?5:2))return 4;
   if(storage.c[162].type!=0xa5||storage.c[1].stock25[0]!=0xa5||storage.before!=123||storage.after!=456)return 5;
   if(counted_types(storage.c,records,82,kind,true)!=(count==82))return 6;
  }
 }
 frame.paths[0].count=83;if(draw::valid(frame))return 7;
 frame.paths[0].count=82;frame.paths[0].points[0].x=8192*1024+1;if(draw::valid(frame))return 8;
 static_assert(sizeof(frame.paths)==2656,"Physical storage bound");
 static_assert(sizeof(frame)==2852,"Frame ABI");
 static_assert(sizeof(draw::AffineFrame<1,2,1,0,0>)==120,"Existing ABI unchanged");
 return 0;
}
'''.replace('#include <cstring>','#include <cstring>\n#include <initializer_list>')
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);cpp=folder/'test.cpp';cpp.write_text(source);binary=folder/'test.exe'
            self.run_command([HOST_CXX,'-std=c++11','-O2','-Wall','-Werror','-I'+str(ROOT/'sdk/include'),
                              '-I'+str(ROOT/'sdk/platform/opxy-1.2.1'),cpp,'-o',binary]);self.run_command([binary])

    @unittest.skipUnless(shutil.which('clang++') and shutil.which('node'),'WASM and Node required')
    def test_compiled_counts_reach_browser_without_padded_segments(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);machine=write_fixture(folder/'machine');out=folder/'preview';out.mkdir()
            preview.compile_preview('clang++',machine.definition,out,machine.root)
            self.run_command(['node',ROOT/'tools/check_counted_paths.mjs',out/'engine.wasm'])

    @unittest.skipUnless(shutil.which('clang++') and shutil.which('node'),'WASM and Node required')
    def test_glyph_tail_composes_with_counted_path_prefix(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp);machine=write_glyph_fixture(folder/'machine');native.validate_panel(machine)
            out=folder/'preview';out.mkdir();preview.compile_preview('clang++',machine.definition,out,machine.root)
            self.run_command(['node',ROOT/'tools/check_counted_glyph_paths.mjs',out/'engine.wasm'])

if __name__=='__main__':unittest.main()
