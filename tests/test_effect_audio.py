"""An effect's audio snapshots (docs/display.md#audio-snapshots): the same keys a synth
declares, published per send by its process and read by its own page on the device."""
import copy
from dataclasses import replace
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from tooling import adapters, audio_telemetry, definitions, native, records
from tooling.firmware import active_services
from tooling.pipeline import ROOT, config
from machine_fixtures import fixture
from test_sample_services import HOST_CXX

HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
DISPLAY = dict(stateBytes=4, stateAlignment=4, paths=0, points=0, nodes=1, texts=0, palette=0, affine=1,
               audioTransport=1, audioWords=4, audioMode=0)
FX = '''#include <opxy/effect.hpp>
#include <opxy/telemetry.hpp>
namespace probefx {
struct State { unsigned initialized; };
void initialize(State& state) { state.initialized = 1; }
void process(State&, opxy::Channel left, opxy::Channel right, opxy::Parameters, const opxy::AudioContext& audio) {
    audio.publish_word(0, static_cast<opxy::u32>(left[0]));
    audio.publish_word(1, static_cast<opxy::u32>(right[0]));
    audio.publish_word(2, audio.track);
}
}
'''


def effect(display=DISPLAY):
    m = fixture('probefx')
    resources = dict(m.definition['resources'], display=dict(display))
    return replace(m, definition=dict(m.definition, resources=resources))


def scope_synth():
    """A synth with two audio words (one accumulator) and the reset handshake."""
    m = fixture('probesynth')
    display = dict(m.definition['resources'].get('display', {}), audioTransport=1, audioWords=2, audioMode=2,
                   audioAccumulators=1, audioReset=1)
    return replace(m, definition=dict(m.definition, resources=dict(m.definition['resources'], display=display)))


class EffectAudio(unittest.TestCase):
    def test_an_effect_declares_the_synth_keys(self):
        m = effect()
        definitions.validate_definition(m.definition, 'effect')
        for changes in ({'audioTransport': 0}, {'audioWords': 257}, {'audioMode': 1}):
            trial = copy.deepcopy(m.definition)
            trial['resources']['display'].update(changes)
            with self.assertRaises(ValueError):
                definitions.validate_definition(trial, 'effect')
        mixed = copy.deepcopy(m.definition)
        mixed['resources']['display'].update(audioWords=40, audioMode=2, audioAccumulators=8, audioReset=1)
        definitions.validate_definition(mixed, 'effect')
        # Its key is its effect number with the effect bit: never a synth engine's.
        self.assertEqual(audio_telemetry.wire_key(m), 0x8000 | m.identity['effect'])
        self.assertEqual(audio_telemetry.users([m, fixture('probesynth')]), [m])
        self.assertIn('telemetry', active_services(config(), native.service_users([m])))

    def test_the_adapter_and_page_change_only_for_a_declaring_effect(self):
        with tempfile.TemporaryDirectory() as temp:
            plain = adapters.generate_fx_object(Path(temp), fixture('probefx')).read_text()
            for text in ('sdk_audio', 'audio_context', 'telemetry', '@'):
                self.assertNotIn(text, plain)
            text = adapters.generate_fx_object(Path(temp), effect()).read_text()
            self.assertIn('static opxy::platform::telemetry::NativeBank<4, 0, false, 1, '
                          'opxy::platform::telemetry::effect_sends> sdk_audio;', text)
            self.assertIn('sdk_audio.clear(slot - opxy::platform::telemetry::effect_first_send);', text)
            self.assertIn('process(object->state, left, right, parameters, audio_context);', text)
            self.assertIn('effect_owns(send, probefx_fx_vtable)', text)
            # Timed alone, the effect publishes into scratch words as on a send, which no screen reads.
            timed = adapters.generate_fx_object(Path(temp), effect(), benchmark=True).read_text()
            self.assertIn('    volatile u32 audio_words[4] = {}, audio_handshake[3] = {};\n', timed)
            self.assertIn('left, right, parameters, AudioContext(14, audio_words, 4, nullptr, nullptr, nullptr, block, true));',
                          timed)
            m = effect()
            adapters.generate_display(Path(temp), machine=replace(m, display='drawing'))
            header = (Path(temp) / 'display_binding.hpp').read_text()
            self.assertIn('using DisplayInput = draw::AudioInput;', header)
            self.assertIn('return platform::telemetry::effect_key | reinterpret_cast<u32>(probefx_fx_id);', header)
            # Each declaring machine's record names its row by link-time symbol, so its row is the
            # same whatever else a build holds; a machine without audio snapshots has none.
            synth = scope_synth()
            row = audio_telemetry.owner_row(m)
            self.assertIn('extern const char probefx_fx_id[] __asm__("probefx_fx_id");', row.declarations)
            self.assertEqual(row.value, '{probefx_fx_id, probefx_audio_bank, 4, 0, false, 1, true}')
            self.assertEqual(audio_telemetry.owner_row(synth).value,
                             '{probesynth_engine_id, probesynth_audio_bank, 2, 2, true, 1, false}')
            self.assertIsNone(audio_telemetry.owner_row(fixture('probefx')))
            for number in (m.identity['effect'], 0x8000 | m.identity['effect'], synth.identity['engine']):
                self.assertNotIn(str(number), row.value)
            record = records.render(m)
            self.assertIn('const opxy::platform::telemetry::Owner audio_row = ' + row.value + ';', record)
            self.assertNotIn('audio_row', records.render(fixture('probefx')))

    def test_a_synth_with_a_track_stage_keeps_both_in_its_adapter(self):
        # Audio snapshots around each voice's render, the stage after the voice loop.
        m = fixture('probestage')
        display = dict(m.definition['resources'].get('display', {}), audioTransport=1, audioWords=2, audioMode=0)
        m = replace(m, definition=dict(m.definition, resources=dict(m.definition['resources'], display=display)))
        with tempfile.TemporaryDirectory() as temp:
            text = adapters.generate_voice(Path(temp), m).read_text(encoding='utf-8')
        self.assertNotIn('@', text.replace('__asm__', ''))
        self.assertIn('sdk_audio.begin(audio_bank);', text)
        self.assertIn('probestage::track_process(track_states[track], shared, track, sum, parameters, voices);', text)
        self.assertLess(text.index('sdk_audio.end(audio_bank);'), text.index('track_process('))

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_each_send_publishes_its_own_words_while_it_holds_the_effect(self):
        m = effect()
        m = replace(m, identity=dict(m.identity, effect=5))
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            generated = adapters.generate_fx_object(folder, m)
            generated.write_text(generated.read_text().replace('__builtin_offsetof(Object, state) == 4',
                                                               '__builtin_offsetof(Object, state) == sizeof(void*)'))
            audio_telemetry.generate_registry(folder, [m, scope_synth()])
            platform = ROOT / 'sdk/platform/opxy-1.2.1'
            stock = (platform / 'stock.hpp').read_text()
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*'):
                stock = stock.replace(f'reinterpret_cast<{pointer}>(address)',
                                      f'reinterpret_cast<{pointer}>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock)
            # The transport's headers beside it, so their own "stock.hpp" is the substituted one.
            for name in ('audio_transport_native.hpp', 'audio_telemetry.hpp', 'telemetry/protocol.hpp'):
                (folder / name).parent.mkdir(exist_ok=True)
                (folder / name).write_bytes((platform / name).read_bytes())
            # A link-time number is a symbol's address on the device; the host maps the symbol to it.
            transport = (folder / 'audio_transport_native.hpp').read_text()
            for text in ('inline u32 link_number(const char* symbol) {',):
                self.assertEqual(transport.count(text), 1, text)
            transport = transport.replace('inline u32 link_number(const char* symbol) {',
                                          'u32 host_link_number(const char*);\n'
                                          'inline u32 link_number(const char* symbol) {\n    return host_link_number(symbol);')
            (folder / 'audio_transport_native.hpp').write_text(transport)
            (folder / 'fx.cpp').write_text(FX)
            (folder / 'test.cpp').write_text((ROOT / 'tests/fixtures/effect_audio.cpp').read_text())
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            for level in ('-O0', '-O2'):
                command = [HOST_CXX, '-std=c++11', level, '-Wall', '-Werror', '-I' + str(folder),
                           '-I' + str(ROOT / 'sdk/include'), '-I' + str(platform), str(folder / 'test.cpp'),
                           str(platform / 'telemetry/protocol.cpp'), '-o', str(binary)]
                result = subprocess.run(command, capture_output=True, text=True, timeout=120, **HIDDEN)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **HIDDEN)
                self.assertEqual(result.returncode, 0, 'fixture step %d failed' % result.returncode)


if __name__ == '__main__':
    unittest.main()
