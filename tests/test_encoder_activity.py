"""Opt-in physical GUI events preserve legacy input contracts and audio layout."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tooling import adapters, definitions, preview
import test_display_extension as fixture

ROOT = Path(__file__).resolve().parents[1]
CXX = os.environ.get('OPXY_WASM_CXX', 'C:/Program Files/LLVM/bin/clang++.exe' if os.name == 'nt' else 'clang++')


class EncoderActivity(unittest.TestCase):
    run_command = fixture.DisplayExtension.run_command

    def test_explicit_opt_in_and_legacy_types(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp); machine = fixture.write_fixture(folder)
            for value in (-1, 2, True, '1'):
                trial = copy.deepcopy(machine.definition)
                trial['resources']['display']['encoderActivity'] = value
                with self.assertRaises(ValueError): definitions.validate_definition(trial, 'synth')
            adapters.generate_display(folder, definition=machine.definition)
            header = (folder/'display_binding.hpp').read_text()
            self.assertIn('using DisplayInput = draw::AudioInput;', header)
            self.assertIn('display_abi = 4;', header)
            self.assertNotIn('platform::encoder_activity()', header)

    @unittest.skipUnless(shutil.which(CXX) and shutil.which('node'), 'WASM compiler and Node required')
    def test_all_three_input_layouts_and_atomic_validation(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp); machine = fixture.write_fixture(folder)
            for words, name, abi, size in ((0,'EncoderInput',6,92),(1,'EncoderAudioInput',7,160),(256,'EncoderTelemetryInput',8,1140)):
                d = copy.deepcopy(machine.definition); display = d['resources']['display']
                display.update(encoderActivity=1)
                if words: display['audioWords'] = words
                else: del display['audioWords']
                if words == 256: display.update(audioTransport=1,audioMode=0,audioReset=1)
                (folder/'display.h').write_text(fixture.DRAW_HEADER.replace('<opxy/draw.hpp>','<opxy/encoder_activity.hpp>'))
                (folder/'display.cpp').write_text('''#include "display.h"
namespace probesynth {
void display_reset(DisplayState& s) { s.frames=0; }
void display(DisplayState& s, const opxy::draw::TYPE& input, Frame& f) {
 s.frames = input.encoders.available ? 256+input.encoders.mask : 0;
 opxy::draw::affine(f,0,{1024,0,0,1024,0,0});
 opxy::draw::affine(f,1,{1024,0,0,1024,0,0});
}
}
'''.replace('TYPE',name))
                adapters.generate_display(folder, definition=d)
                wasm = folder/(name+'.wasm')
                self.run_command([CXX,*preview.FLAGS,'-I'+str(ROOT/'sdk/include'),'-I'+str(folder),
                    folder/'display_binding.cpp',ROOT/'sdk/host/display_preview.cpp',ROOT/'sdk/platform/blackfin/memory.cpp',
                    '-o',wasm,'-Wl,--no-entry','-Wl,--initial-memory=524288','-Wl,--max-memory=524288',
                    *('-Wl,--export='+n for n in preview.DISPLAY_EXPORTS)])
                self.run_command(['node',ROOT/'tools/check_encoder_activity.mjs',wasm,abi,size,words])

    def test_native_getter_read_only_and_frame_order(self):
        source = (ROOT/'sdk/platform/opxy-1.2.1/encoder_activity.hpp').read_text()
        self.assertIn('0x8d51f1beu', source)
        self.assertIn('getter(i, 0)', source)
        self.assertNotIn('0x0803be8c', source)
        frame = (ROOT/'sdk/platform/opxy-1.2.1/screen_drawing.hpp').read_text()
        self.assertLess(frame.index('snapshot_encoders(display_input)'),frame.index('display_render(&display_state'))

if __name__ == '__main__': unittest.main()
