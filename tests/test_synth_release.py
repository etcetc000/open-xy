"""Opt-in release delivery: real native adapter, host ABI and browser WASM."""
import copy
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

from tooling.adapters import generate_voice, generate_voice_preview
from tooling.definitions import validate_definition
from tooling.preview import VOICE_EXPORTS, FLAGS, MEMORY
from machine_fixtures import fixture
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
WASM_CXX = os.environ.get('OPXY_TEST_PREVIEW_CXX', 'clang++')
CORE = '''#pragma once
#include <opxy/audio.hpp>
namespace probe {
struct Voice { unsigned owner, starts, releases, destroys; };
struct Shared { unsigned releases, destroys, last_owner; };
}
'''
ENGINE = '''#include "core.h"
namespace probe {
void initialize(Voice& v, Shared&, unsigned owner) { v.owner=owner; v.starts=v.releases=v.destroys=0; }
void start(Voice& v, Shared&, unsigned) { ++v.starts; }
void release(Voice& v, Shared& s, unsigned owner) { ++v.releases; ++s.releases; s.last_owner=owner; }
void destroy(Voice& v, Shared& s, unsigned owner) { ++v.destroys; ++s.destroys; s.last_owner=owner; }
void render(Voice& v, Shared&, unsigned, opxy::Output out, opxy::Parameters, opxy::Pitch) {
    for (unsigned i=0;i<64;++i) out[i]=v.releases ? 100 : 1000;
}
}
'''
NATIVE = r'''#include "voice.cpp"
opxy::u32 sdk_voice_ticks() { return 0; }
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 i) { return i; }
int main() {
    void* h=probe_factory(0); void* other=probe_factory(1);
    const void* const* table=*static_cast<const void* const* const*>(h);
    auto off=reinterpret_cast<int(*)(void*)>(const_cast<void*>(table[7]));
    off(h); if (shared.releases) return 1;
    probe_start(h); off(h);
    if (shared.releases!=1 || shared.last_owner!=1 || held(static_cast<Handle*>(h))->voice.releases!=1) return 2;
    reinterpret_cast<void(*)()>(const_cast<void*>(table[5]))();
    if (shared.releases!=1) return 3;
    probe_start(h); if (held(static_cast<Handle*>(h))->voice.starts!=2 || shared.destroys) return 4;
    off(h); if (shared.releases!=2) return 5;
    probe_start(other);
    opxy::i32 output[64], parameters[8]={};
    probe_render(h,output,parameters); probe_render(other,output,parameters);
    void* waiting=probe_factory(2); probe_start(waiting);
    Slot* taken=pending(static_cast<Handle*>(waiting)); if (!taken) return 6;
    void* previous=&handles[taken->owner-1];
    off(waiting); if (shared.releases!=2) return 7;
    probe_render(previous,output,parameters);
    Slot* started=held(static_cast<Handle*>(waiting));
    if (!started || started->voice.starts!=1 || started->voice.releases!=1 || shared.last_owner!=3) return 8;
    off(previous); if (shared.releases!=3) return 9;
    probe_destroy(waiting); off(waiting);
    if (shared.releases!=3 || shared.destroys!=2) return 10;
    return 0;
}
'''
HOST = '''#include "voice_preview.cpp"
int main() {
    auto* v=opxy_voice_state(0); opxy_initialize(v); opxy_start(v); opxy_release(v);
    if (v->starts!=1 || v->releases!=1 || shared.last_owner!=1) return 1;
    opxy_start(v); opxy_release(v); opxy_destroy(v);
    return v->starts!=2 || v->releases!=2 || v->destroys!=1;
}
'''

def definition():
    value=copy.deepcopy(fixture('probesynth').definition)
    value['id']='probe'
    value['resources'].update(voiceBytes=16, voiceAlignment=4, voiceCapacity=1,
                              sharedBytes=12, releaseCallback=1)
    return value

class SynthRelease(unittest.TestCase):
    def run_command(self, command):
        r=subprocess.run(list(map(str,command)), capture_output=True, text=True, timeout=120, **HIDDEN)
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_resource_and_default_compatibility(self):
        value=definition()
        for flag in (0,1):
            value['resources']['releaseCallback']=flag
            validate_definition(value,'synth')
        for flag in (-1,2,True,'1',None):
            value['resources']['releaseCallback']=flag
            with self.assertRaises(ValueError): validate_definition(value,'synth')
        with tempfile.TemporaryDirectory() as folder:
            del value['resources']['releaseCallback']
            m=SimpleNamespace(definition=value,prefix='probe',native_name='probe')
            absent=generate_voice(folder,m).read_bytes()
            value['resources']['releaseCallback']=0
            self.assertEqual(absent,generate_voice(folder,m).read_bytes())
            self.assertNotIn(b'probe_release',absent)
            preview=generate_voice_preview(folder,value).read_text()
            self.assertIn('void opxy_release(Voice* voice) {  }',preview)
            self.assertNotIn('probe::release',preview)

    @unittest.skipUnless(HOST_CXX,'Host C++ compiler required')
    def test_native_slot7_and_host_lifecycle(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)
            (p/'core.h').write_text(CORE)
            (p/'engine.cpp').write_text(ENGINE)
            generate_voice(p,SimpleNamespace(definition=definition(),prefix='probe',native_name='probe'))
            generate_voice_preview(p,definition())
            for name,source in [('native',NATIVE),('host',HOST)]:
                (p/(name+'.cpp')).write_text(source)
                binary=p/(name+('.exe' if os.name=='nt' else ''))
                self.run_command([HOST_CXX,'-std=c++11','-O2','-Wall','-Werror','-Wno-unused',
                    '-I'+str(ROOT/'sdk/include'),'-I'+str(ROOT/'sdk/platform/opxy-1.2.1'),p/(name+'.cpp'),'-o',binary])
                self.run_command([binary])

    @unittest.skipUnless(shutil.which(WASM_CXX) and shutil.which('node'),'WASM compiler and Node required')
    def test_wasm_noteoff_retrigger_steal_and_old_modules(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)
            (p/'core.h').write_text(CORE)
            (p/'engine.cpp').write_text(ENGINE)
            source=generate_voice_preview(p,definition())
            binary=p/'engine.wasm'
            for exports in (VOICE_EXPORTS,tuple(x for x in VOICE_EXPORTS if x!='opxy_release')):
                self.run_command([WASM_CXX,*FLAGS,'-I'+str(ROOT/'sdk/include'),source,
                    ROOT/'sdk/platform/blackfin/memory.cpp','-o',binary,'-Wl,--no-entry',
                    '-Wl,-z,stack-size=16384',f'-Wl,--initial-memory={MEMORY}',f'-Wl,--max-memory={MEMORY}',
                    *('-Wl,--export='+x for x in exports)])
                self.run_command(['node',ROOT/'tools/check_synth_release.mjs',binary,
                                  '1' if 'opxy_release' in exports else '0'])

if __name__=='__main__': unittest.main()
