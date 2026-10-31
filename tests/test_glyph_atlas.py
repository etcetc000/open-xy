# Owned immutable glyph atlas qualification.
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tooling import definitions,native,preview,adapters
from tooling.machines import machine_at
from machine_fixtures import fixture
ROOT=Path(__file__).resolve().parents[1]
HIDDEN=dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name=='nt' else {}

def write_fixture(folder):
    shutil.copytree(fixture('probesynth').root,folder,dirs_exist_ok=True)
    path=folder/'definition.json';value=json.loads(path.read_text())
    value['resources']['display'].update(stateBytes=4,stateAlignment=4,paths=0,points=0,nodes=4,texts=0,palette=0,
        affine=1,fullCanvas=1,glyphs=2,glyphDefinitions=2,glyphStart=0)
    path.write_text(json.dumps(value))
    (folder/'display.h').write_text('''#pragma once
#include <opxy/draw.hpp>
namespace probesynth { struct DisplayState{opxy::u32 serial;};
using Frame=opxy::draw::GlyphFrame<opxy::draw::AffineFrame<0,0,4,0,0>,2,2,0>; }
''')
    (folder/'display.cpp').write_text('''#include "display.h"
namespace probesynth {
inline void display_reset(DisplayState& s){s.serial=0;}
inline void display(DisplayState& s,const opxy::draw::Input&,Frame& f){
    ++s.serial;for(unsigned i=0;i<4;++i)opxy::draw::affine(f,i,{1024,0,0,1024,0,0});
    f.glyphs[0]=s.serial&1;f.glyphs[1]=1-f.glyphs[0];
    for(unsigned i=0;i<4;++i)opxy::draw::header(f,i,"");
}}
''')
    scaffold='M0,0 '+ ' '.join(f'L{i},0' for i in range(1,16))
    sources=['M0 0 L1 2 Z','M2 3 L4 5','M3.0009 4 C5 6 7 8 9 10 Z','M0 0']
    art='<g id="probesynth_panel"><g transform="translate(1024,1024)">'+''.join(
        f'<path id="probesynth_glyph_{i//2}_{i%2}" d="{d}" fill="none" stroke="#ffffff" stroke-width="1.3"/>' for i,d in enumerate(sources))+'</g>'
    art+=''.join(f'<path id="probesynth_node_{i}" d="{scaffold}" fill="none" stroke="#ffffff" stroke-width="1.3"/>' for i in range(4))+'</g>'
    (folder/'panel.svg.fragment').write_text(art)
    metadata = folder / 'machine.json'
    record = json.loads(metadata.read_text()); record['sdk'] = '1.8'; record.pop('identity', None)
    metadata.write_text(json.dumps(record) + '\n')
    return machine_at(folder)

class GlyphAtlas(unittest.TestCase):
    def command(self,values):
        r=subprocess.run(list(map(str,values)),capture_output=True,text=True,timeout=120,**HIDDEN)
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
    def test_schema_and_owned_scaffolds(self):
        with tempfile.TemporaryDirectory() as t:
            m=write_fixture(Path(t));native.validate_panel(m)
            d=m.definition['resources']['display']
            self.assertEqual(adapters.drawing_layout(d)['FRAME_BYTES'],208)
            b=preview.drawing_bindings(native.display_ids(m.prefix,d),d)
            self.assertEqual(b['glyphSources'],['probesynth_glyph_0_0','probesynth_glyph_0_1','probesynth_glyph_1_0','probesynth_glyph_1_1'])
            for update in [dict(glyphs=65),dict(glyphs=True),dict(glyphDefinitions=17),dict(glyphDefinitions=0),dict(glyphStart=1),dict(affine=0),dict(glyphs=0)]:
                trial=copy.deepcopy(m.definition);trial['resources']['display'].update(update)
                with self.assertRaises(ValueError):definitions.validate_definition(trial,'synth')
            art=(m.root/'panel.svg.fragment').read_text()
            for bad in [art.replace('M3.0009 4 C','M3.0009 4 Q'),art.replace('L15,0','L0,0'),art.replace('M3.0009 4 C','M9000 4 C')]:
                with self.assertRaises(ValueError):native.validate_panel(m,bad)
    @unittest.skipUnless(shutil.which('clang++') and shutil.which('node'),'WASM and Node required')
    def test_native_binder_ownership_and_failure_atomicity(self):
        with tempfile.TemporaryDirectory() as t:
            out=Path(t)/'native.wasm'
            self.command(['clang++',*preview.FLAGS,'-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform/opxy-1.2.1'),
                ROOT/'tests/glyph_atlas_native.cpp',ROOT/'sdk/platform/blackfin/memory.cpp','-Wl,--no-entry','-Wl,--export=run','-o',out])
            self.command(['node','-e',"const fs=require('fs');const m=new WebAssembly.Module(fs.readFileSync(process.argv[1]));if(WebAssembly.Module.imports(m).length)throw Error('imports');const code=new WebAssembly.Instance(m).exports.run();if(code)throw Error('case '+code);",out])
    @unittest.skipUnless(shutil.which('clang++') and shutil.which('node'),'WASM and Node required')
    def test_compiled_selectors_browser_copy_and_empty_part(self):
        with tempfile.TemporaryDirectory() as t:
            folder=Path(t);m=write_fixture(folder/'machine');out=folder/'preview';out.mkdir()
            preview.compile_preview('clang++',m.definition,out,m.root)
            self.command(['node',ROOT/'tools/check_glyph_atlas.mjs',out/'engine.wasm'])
if __name__=='__main__':unittest.main()
