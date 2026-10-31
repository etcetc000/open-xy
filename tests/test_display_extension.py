"""Affine/full-canvas and per-track audio telemetry integration."""
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
inline void display(DisplayState& state, const opxy::draw::AudioInput& input, Frame& frame) {
    ++state.frames;
    const opxy::i32 x = input.audio_count ? (input.audio_words[0] & 2047) : -1;
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
                                         affine=1, fullCanvas=1, audioWords=1)
    path.write_text(json.dumps(value, indent=2) + '\n')
    (folder / 'display.h').write_text(DRAW_HEADER)
    (folder / 'display.cpp').write_text(DRAW_SOURCE)
    (folder / 'panel.svg.fragment').write_bytes(ART)
    path = folder / 'engine.cpp'
    source = '#include <opxy/telemetry.hpp>\n' + path.read_text()
    source = source.replace('opxy::Parameters parameters, opxy::Pitch pitch) {',
                            'opxy::Parameters parameters, opxy::Pitch pitch, const opxy::AudioContext& context) {\n'
                            '    context.add_packed16(0, 0x01000001u);')
    path.write_text(source)
    metadata = folder / 'machine.json'
    record = json.loads(metadata.read_text()); record['sdk'] = '1.8'; record.pop('identity', None)
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
            for key, invalid in [('affine', 2), ('fullCanvas', True), ('audioWords', 17)]:
                trial = copy.deepcopy(value); trial['resources']['display'][key] = invalid
                with self.assertRaises(ValueError): definitions.validate_definition(trial, 'synth')
            value['resources']['display']['affine'] = 0
            with self.assertRaises(ValueError): definitions.validate_definition(value, 'synth')

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler')
    def test_affine_validation_track_mapping_and_telemetry_canaries(self):
        source = r'''
#include <opxy/draw.hpp>
#include "audio_telemetry.hpp"
int main() {
    opxy::draw::AffineFrame<0, 0, 1, 0, 0> f = {};
    opxy::draw::affine(f, 0, opxy::draw::Matrix{-1024, 4, -8, 0, 6000000, -6000000});
    if (!opxy::draw::valid(f)) return 1;
    f.nodes[0].paint = opxy::draw::stroke;
    if (opxy::draw::valid(f)) return 2;
    f.nodes[0].paint = opxy::draw::keep; f.nodes[0].a = 16385;
    if (opxy::draw::valid(f)) return 3;
    opxy::TelemetryBank<2> bank = {};
    bank.context(0).publish_word(0, 123);
    bank.context(1).publish_word(0, 456);
    bank.context(1).publish_word(1, 789);
    opxy::u32 guarded[18] = {}; guarded[0] = 777; guarded[17] = 888;
    if (bank.take(0, guarded + 1) != 2 || guarded[1] != 123 || guarded[2]) return 4;
    if (bank.take(0, guarded + 1) != 2 || guarded[1]) return 5;
    if (bank.take(1, guarded + 1) != 2 || guarded[1] != 456 || guarded[2] != 789) return 6;
    if (guarded[0] != 777 || guarded[17] != 888) return 7;
    bank.context(99).publish_word(0, 999);
    if (bank.take(99, guarded + 1) || guarded[1]) return 8;
    for (unsigned t = 0; t < 8; ++t) {
        const auto p = reinterpret_cast<const opxy::i32*>(opxy::platform::Address(0x84eab39c + t * 0xe8));
        if (opxy::platform::synth_audio_track(p) != t) return 9;
        if (opxy::platform::synth_audio_track(p + 1) != 8) return 10;
    }
    if (opxy::platform::synth_audio_track(nullptr) != 8) return 11;
    if (opxy::platform::synth_audio_track(reinterpret_cast<const opxy::i32*>(0x84eabaDC)) != 8) return 12;
    bank.context(0).publish_word(0, 0x7fff8000u);
    bank.context(0).add_packed16(0, 0x0001ffffu);
    if (bank.take(0, guarded + 1) != 2 || guarded[1] != 0x7fff8000u) return 13;
    bank.context(0).publish_word(0, 555);
    // Native gate reports unavailable and never touches producer pending data.
    if (opxy::platform::take_audio(bank, 0, guarded + 1) || guarded[1]) return 14;
    if (bank.take(0, guarded + 1) != 2 || guarded[1] != 555) return 15;
    opxy::TelemetryBank<16> wide = {};
    for (unsigned i = 0; i < 16; ++i) wide.context(7).publish_word(i, 100 + i);
    if (wide.take(7, guarded + 1) != 16) return 16;
    for (unsigned i = 0; i < 16; ++i) if (guarded[1+i] != 100+i) return 17;
    if (guarded[0] != 777 || guarded[17] != 888 || sizeof(bank) != 8*2*4) return 18;
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
    def test_wasm_audio_to_display_and_affine_backend(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp); machine = write_fixture(folder / 'machine')
            out = folder / 'preview'; out.mkdir()
            preview.compile_preview('clang++', machine.definition, out, machine.root)
            self.run_command(['node', ROOT / 'tools/check_display_extension.mjs', out / 'engine.wasm'])


if __name__ == '__main__': unittest.main()
