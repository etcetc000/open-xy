"""A track stage's audio snapshots (trackStage audio, SDK 1.27; docs/api.md#track-stage):
track_process takes the synth's AudioContext for its track, so the stage publishes to the
synth's own screen as its voices do. A stage without it keeps its adapter's text."""
import copy
import json
from dataclasses import replace
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tooling import track_stage
from tooling.adapters import generate_voice
from tooling.definitions import validate_definition
from tooling.machines import machine_at
from tooling.pipeline import ROOT
from machine_fixtures import FOLDER, fixture
from test_sample_services import HOST_CXX

PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}


def stage_text(text):
    """The adapter's stage entries: from track_process's definition to the end."""
    return text[text.index('// parameters: the track\'s words, as stock passes them'):]


class TrackStageAudio(unittest.TestCase):
    def test_the_key_is_0_or_1_and_needs_the_screens_audio(self):
        definition = fixture('probestagemeter').definition
        validate_definition(definition, 'synth')
        for audio in (2, True, '1', -1):
            trial = copy.deepcopy(definition)
            trial['resources']['trackStage']['audio'] = audio
            with self.subTest(audio=audio), self.assertRaisesRegex(ValueError, 'trackStage audio is 0 or 1'):
                validate_definition(trial, 'synth')
        trial = copy.deepcopy(definition)
        del trial['resources']['display']['audioTransport']
        for key in ('audioWords', 'audioMode', 'audioReset'):
            del trial['resources']['display'][key]
        with self.assertRaisesRegex(ValueError, 'trackStage audio needs the display to declare audioTransport'):
            validate_definition(trial, 'synth')
        # 0 is the default: a stage without the screen's audio may say so.
        trial['resources']['trackStage']['audio'] = 0
        validate_definition(trial, 'synth')
        trial['resources']['trackStage']['other'] = 1
        with self.assertRaisesRegex(ValueError, 'Unexpected trackStage fields'):
            validate_definition(trial, 'synth')

    def test_it_needs_sdk_1_27(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'probestagemeter'
            shutil.copytree(FOLDER / 'probestagemeter', root)
            machine_at(root)
            record = (root / 'machine.json').read_text(encoding='utf-8')
            (root / 'machine.json').write_text(record.replace('"1.27"', '"1.26"'), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, r'trackStage audio\) needs "sdk": "1\.27"'):
                machine_at(root)
            # The key itself is 1.27's, even as 0 beside a display without audio.
            definition = json.loads((root / 'definition.json').read_text(encoding='utf-8'))
            for key in ('audioTransport', 'audioWords', 'audioMode', 'audioReset'):
                del definition['resources']['display'][key]
            definition['resources']['trackStage']['audio'] = 0
            (root / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, r"trackStage audio\) needs \"sdk\": \"1\.27\""):
                machine_at(root)

    def test_a_stage_without_it_keeps_its_adapter_text(self):
        # The placeholders expand to nothing: the stage entries read as before the key existed.
        with tempfile.TemporaryDirectory() as directory:
            text = generate_voice(Path(directory), fixture('probestage')).read_text(encoding='utf-8')
        stage = stage_text(text)
        self.assertNotIn('audio_', stage)
        self.assertIn('void probestage_track_process(opxy::u32 track, opxy::i32* sum, const opxy::i32* parameters, '
                      'opxy::u32 voices) {\n    probestage::track_process(track_states[track], shared, track, sum, '
                      'parameters, voices);\n}\n', stage)
        self.assertIn('    static const i32 parameters[2 * parameter_slots] = {', stage)
        self.assertIn('};\n    i32 sum[128];\n', stage)
        self.assertIn('            probestage::track_process(state, shared, 0, sum, parameters, voices);\n', stage)
        # audio 0 generates the same adapter as no key at all.
        m = fixture('probestagemeter')
        texts = []
        for audio in (None, 0):
            definition = copy.deepcopy(m.definition)
            for key in ('audioTransport', 'audioWords', 'audioMode', 'audioReset'):
                del definition['resources']['display'][key]
            if audio is None:
                del definition['resources']['trackStage']['audio']
            else:
                definition['resources']['trackStage']['audio'] = audio
            with tempfile.TemporaryDirectory() as directory:
                texts.append(generate_voice(Path(directory), replace(m, definition=definition)).read_text(encoding='utf-8'))
        self.assertEqual(texts[0], texts[1])
        self.assertNotIn('audio_', stage_text(texts[0]))
        self.assertEqual(track_stage.audio_values(fixture('probestage')),
                         dict(STAGE_AUDIO_BEGIN='', STAGE_AUDIO_ARGUMENT='', STAGE_AUDIO_END='', STAGE_BENCHMARK_AUDIO=''))

    def test_the_stage_publishes_on_its_tracks_bank_after_the_voices(self):
        with tempfile.TemporaryDirectory() as directory:
            text = generate_voice(Path(directory), fixture('probestagemeter')).read_text(encoding='utf-8')
        self.assertNotIn('@', text.replace('__asm__', ''))
        stage = text[text.index('void probestagemeter_track_process(opxy::u32 track, opxy::i32* sum, const opxy::i32* '
                                'parameters, opxy::u32 voices) {'):]
        self.assertIn('    auto* audio_bank = sdk_audio.resolve(track);\n'
                      '    const opxy::AudioContext audio_context = sdk_audio.context(track, audio_bank);\n'
                      '    sdk_audio.begin(audio_bank);\n'
                      '    probestagemeter::track_process(track_states[track], shared, track, sum, parameters, voices, '
                      'audio_context);\n'
                      '    sdk_audio.end(audio_bank);\n}\n', stage)
        # The voices' bank: one per track, the same object the transport's registry reads.
        self.assertEqual(text.count('NativeBank<3, 0, true, 1> sdk_audio;'), 1)
        # The diagnostic's run times the stage's publishing into scratch words.
        self.assertIn('    static volatile u32 audio_words[3];\n    volatile u32 audio_handshake[3] = {1, 0, 1};\n'
                      '    const AudioContext audio_context(0, audio_words, 3, audio_handshake, audio_handshake + 1, '
                      'audio_handshake + 2, 0, true);\n    i32 sum[128];\n', stage)
        self.assertIn('probestagemeter::track_process(state, shared, 0, sum, parameters, voices, audio_context);', stage)
        # With a Shift row, the row's words come first, then the context, then the call.
        shift = track_stage.adapter_values(fixture('probestagemeter'), shift_row=True)['TRACK_STAGE']
        self.assertIn('    shift_words(parameters, controls + opxy::parameter_slots, shift_kept[track]);\n'
                      '    auto* audio_bank = sdk_audio.resolve(track);\n', shift)
        self.assertIn('track_process(track_states[track], shared, track, sum, controls, voices, audio_context);\n'
                      '    sdk_audio.end(audio_bank);\n}', shift)
        # Without the reset handshake the scratch context has none.
        m = fixture('probestagemeter')
        plain = copy.deepcopy(m.definition)
        del plain['resources']['display']['audioReset']
        values = track_stage.audio_values(replace(m, definition=plain))
        self.assertEqual(values['STAGE_BENCHMARK_AUDIO'],
                         '    static volatile u32 audio_words[3];\n'
                         '    const AudioContext audio_context(0, audio_words, 3, nullptr, nullptr, nullptr, 0, true);\n')

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_the_generated_stage_publishes_its_tracks_words_on_the_host(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            # The platform beside the adapter, with only stock's addresses substituted, so every
            # header's own "stock.hpp" is the substituted one.
            platform = folder / 'platform'
            shutil.copytree(PLATFORM, platform)
            stock = (platform / 'stock.hpp').read_text(encoding='utf-8')
            self.assertEqual(stock.count('typedef __UINTPTR_TYPE__ Address;'), 1)
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*'):
                stock = stock.replace(f'reinterpret_cast<{pointer}>(address)',
                                      f'reinterpret_cast<{pointer}>(host_stock_address(address))')
            (platform / 'stock.hpp').write_text(stock, encoding='utf-8')
            generate_voice(folder, fixture('probestagemeter'))
            for name in ('core.h', 'engine.cpp'):
                shutil.copy(FOLDER / 'probestagemeter' / name, folder / name)
            shutil.copy(ROOT / 'tests/fixtures/track_stage_audio.cpp', folder / 'test.cpp')
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            for level in ('-O0', '-O2'):
                command = [HOST_CXX, '-std=c++11', level, '-Wall', '-Werror', '-Wno-unused-function',
                           '-I' + str(folder), '-I' + str(ROOT / 'sdk/include'), '-I' + str(platform),
                           str(folder / 'test.cpp'), str(platform / 'telemetry/protocol.cpp'), '-o', str(binary)]
                result = subprocess.run(command, capture_output=True, text=True, timeout=120, **HIDDEN)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60, **HIDDEN)
                self.assertEqual(result.returncode, 0, 'fixture step %d failed' % result.returncode)
                self.assertEqual(result.stdout.strip(), 'ok')


if __name__ == '__main__':
    unittest.main()
