"""Per-key sends (resources.padMix): the mixer hooks, the adapter's codes and the build wiring."""
import copy
import json
import os
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

from tooling.adapters import generate_drum_callbacks
from tooling.definitions import validate_definition
from tooling.firmware import active_services
from tooling import native
from machine_fixtures import fixture
import test_drum_voices   # its run_adapter, without collecting its tests twice
import test_drum_preview
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
PROFILE = json.loads((ROOT / 'profiles/opxy-1.2.1.json').read_text(encoding='utf-8'))
# The five reviewed sites, as drum_send_hooks.s describes them.
SITES = {'0x8d510bea': ('sdk_drum_send_track', '00e38f5d'),
         '0x8d51c7b2': ('sdk_drum_send_voice_hook', '0adab01148150000'),
         '0x8d511088': ('sdk_drum_send_source_1', '09da901100360000'),
         '0x8d5110ca': ('sdk_drum_send_source_2', '09da901100360000'),
         '0x8d511116': ('sdk_drum_send_own', '30e51200')}
# Stock's loads of the two send buses (8d511096 for FX I, 8d5110da for FX II),
# which the own-amount hook writes into.
BUS_LOADS = {'0x8d511096': '08daca8440a10000', '0x8d5110da': '0bdaca8440a30000'}

SEND_ENGINE = '''#include "core.h"
namespace probe {
inline void initialize(Voice& v, Shared&, opxy::u32 id) { v.owner = id; ++initialized[id]; }
inline void start(Voice&, Shared&, opxy::u32 id) { ++started[id]; }
inline void destroy(Voice&, Shared&, opxy::u32 id) { ++destroyed[id]; }
inline void drum_defaults(opxy::u32 key, opxy::u8* model, opxy::u8* raw) {
    *model = key % 12;
    for (unsigned i = 0; i < 8; ++i) raw[i] = 64;
}
inline void drum_select(Voice&, Shared&, opxy::u32, opxy::u32 model) { captured_model = model; }
inline const char* parameter_name(opxy::u32, opxy::u32) { return "parameter"; }
inline const char* model_name(opxy::u32) { return "model"; }
inline opxy::u32 parameter_count(opxy::u32) { return 8; }
inline void render(Voice&, Shared&, opxy::u32, opxy::Output output, opxy::Parameters, opxy::Pitch) {
    for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = 1 << 20;
}
}
'''
SEND_DRIVER = r'''#include "voice.cpp"
#include <cstdio>
const opxy::i32* fixture_region;
opxy::i32 captured[8];
opxy::u32 captured_model = 99;
opxy::u32 fixture_pitch[opxy::block_frames], captured_pitch[opxy::block_frames];
opxy::i32 fixture_amplitude[opxy::block_frames];
bool negative_output;
unsigned destroyed[257], initialized[257], started[257];
unsigned bench_rendered[12][128], clock_value;
volatile opxy::i32 sdk_track_octaves[8];
extern "C" const char probe_engine_id[] = "probe";
const char* sdk_drum_title(opxy::u32, opxy::u32) { return "parameter"; }
extern "C" opxy::u32 sdk_benchmark_cycles() { return 0; }
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
namespace {
void* objects[256];
opxy::i32 offsets[8];
typedef int (*Start)(void*, opxy::u32, opxy::u32, const opxy::i32*, const opxy::i32*);
typedef int (*End)(void*);
typedef int (*Render)(void*, opxy::i32*, const opxy::i32*, const opxy::i32*);
const void* const* table(unsigned id) { return *static_cast<const void* const* const*>(objects[id]); }
void note(unsigned id, unsigned key) {
    reinterpret_cast<Start>(const_cast<void*>(table(id)[5]))(objects[id], key, 100, fixture_region, offsets);
}
void end(unsigned id) { reinterpret_cast<End>(const_cast<void*>(table(id)[0]))(objects[id]); }
void render(unsigned id, const opxy::i32* payload) {
    opxy::i32 output[opxy::block_frames];
    reinterpret_cast<Render>(const_cast<void*>(table(id)[3]))(objects[id], output, payload, offsets);
}
const opxy::i32 kMarked = opxy::i32(42u * 32537631u + 16268815u);
}
int main() {
    for (unsigned i = 0; i < opxy::block_frames; ++i) fixture_amplitude[i] = 0x7fffffff;
    for (unsigned i = 0; i < 8; ++i) offsets[i] = 0x40000000;
    for (unsigned i = 0; i < 256; ++i) CHECK(objects[i] = probe_factory(i, i % 8));
    // In word 6, FILTER and SRR bits, FX I's code 5 and FX II's 3, on a key the kit set up.
    const opxy::i32 word = opxy::platform::drum_send_store(
        opxy::platform::drum_send_store(0x00120000 | opxy::platform::kDrumPadReady, 0, 5), 1, 3);
    opxy::i32 marked[8] = {kMarked, 0, 0, 0, 0, 0, word, 0};
    opxy::i32 unmarked[8] = {0, 0, 0, 0, 0, 0, word, 0};
    // A key an SDK 1.9 kit marked keeps whatever stock left in the word: here stock's
    // default kit's last key (0x26660000, FX I 3 and FX II 2 if it were read) and an
    // editor value with its low half.
    opxy::i32 old_default[8] = {kMarked, 0, 0, 0, 0, 0, 0x26660000, 0};
    opxy::i32 old_editor[8] = {kMarked, 0, 0, 0, 0, 0, opxy::i32(199 * 10737418 + 5368709), 0};
    note(1, 53);
    note(2, 54);
    note(3, 55);
    note(4, 56);
    CHECK(probe_drum_send(objects[1]) == 0);       // nothing rendered yet
    render(1, marked);
    render(2, unmarked);
    render(3, old_default);
    render(4, old_editor);
    CHECK(probe_drum_send(objects[1]) == (PAD_MIX ? 5u + (3u << 3) : 0u));
    CHECK(probe_drum_send(objects[2]) == 0);       // a pad the kit has not marked follows
    CHECK(probe_drum_send(objects[3]) == 0);       // nor one it has not set up
    CHECK(probe_drum_send(objects[4]) == 0);
    end(3);
    end(4);
    render(1, nullptr);
    CHECK(probe_drum_send(objects[1]) == 0);
    render(1, marked);
    // A destroyed handle has no voice: whatever it renders is silence.
    end(1);
    CHECK(probe_drum_send(objects[1]) == 0);
    CHECK(probe_drum_send(objects[5]) == 0);       // never played
    std::puts("adapter sends ok");
    return 0;
}
'''


def kit(pad_mix=None, sdk='1.10'):
    definition = copy.deepcopy(fixture('probesynth').definition)
    definition['resources'].update(modelCount=12)
    if pad_mix is not None:
        definition['resources']['padMix'] = pad_mix
    return SimpleNamespace(kind='drum', id='probe', prefix='probe', native_name='probe', definition=definition,
                           sdk=sdk)


def run(command, **kwargs):
    hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
    return subprocess.run(command, capture_output=True, text=True, **kwargs, **hidden)


class DrumSend(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_the_hooks_keep_stock_mixing_and_route_each_keys_share(self):
        source = (PLATFORM / 'drum_send.cpp').read_text(encoding='utf-8')
        for stock, host in (('reinterpret_cast<i32*>(0x11903600)', 'host_sum'),
                            ('reinterpret_cast<const i32*>(0x11b01548)', 'host_voice'),
                            ('reinterpret_cast<i32*>(0x84caa140)', 'host_fx1'),
                            ('reinterpret_cast<i32*>(0x84caa340)', 'host_fx2')):
            self.assertEqual(source.count(stock), 1, stock)
            source = source.replace(stock, host)
        with tempfile.TemporaryDirectory(prefix='drum-send-') as folder:
            folder = Path(folder)
            (folder / 'drum_send.cpp').write_text(source, encoding='utf-8')
            binary = folder / ('drum_send.exe' if os.name == 'nt' else 'drum_send')
            built = run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(folder),
                         '-I' + str(ROOT / 'sdk/include'), '-I' + str(PLATFORM),
                         str(ROOT / 'tests/fixtures/drum_send_mixer.cpp'), '-o', str(binary)], timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = run([str(binary)], timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_the_adapter_keeps_each_voices_send_code(self):
        for pad_mix in (1, 0):
            with self.subTest(padMix=pad_mix):
                driver = SEND_DRIVER.replace('PAD_MIX', 'true' if pad_mix else 'false')
                test_drum_voices.DrumVoices.run_adapter(self, SEND_ENGINE, driver, 4, padMix=pad_mix)

    def test_definitions_and_versions(self):
        definition = test_drum_preview.DrumPreviewTests.definition(None, "drum")
        for value in (0, 1):
            validate_definition(dict(definition, resources=dict(definition['resources'], padMix=value)), 'drum')
        for value in (2, -1):
            with self.assertRaises(ValueError):
                validate_definition(dict(definition, resources=dict(definition['resources'], padMix=value)), 'drum')
        # The sends come with padMix alone: the editor that sets them is its Shift row.
        for value in (0, 1):
            with self.assertRaises(ValueError):
                validate_definition(dict(definition, resources=dict(definition['resources'], padSend=value)), 'drum')
        synth = copy.deepcopy(fixture('probesynth').definition)
        synth['resources']['padMix'] = 1
        with self.assertRaises(ValueError):
            validate_definition(synth, 'synth')

    def test_only_a_pad_mix_kit_links_the_mixer_hooks(self):
        services = PROFILE['services']
        self.assertEqual(services['drum_sends'], ['padMix'])
        with_sends, without = kit(1), kit()
        self.assertIn('padMix', native.service_users([with_sends]))
        self.assertNotIn('padMix', native.service_users([without]))
        self.assertIn('drum_sends', active_services(PROFILE, native.service_users([with_sends])))
        self.assertNotIn('drum_sends', active_services(PROFILE, native.service_users([without])))
        self.assertEqual(native.UNIT_SERVICES['drum_send'], 'drum_sends')
        self.assertEqual(native.UNIT_SERVICES['drum_send_hooks'], 'drum_sends')

    def test_the_profile_reviews_five_call_sites_outside_hardware_loops(self):
        hooks = {p['address']: p for p in PROFILE['patches'] if p.get('service') == 'drum_sends'}
        self.assertEqual(set(hooks), set(SITES))
        for address, (symbol, before) in SITES.items():
            patch = hooks[address]
            self.assertEqual((patch['kind'], patch['symbol'], patch['before'], patch['call']),
                             ('hook', symbol, before, True))
            self.assertEqual(patch['length'], len(before) // 2)
        stubs = (PLATFORM / 'drum_send_hooks.s').read_text(encoding='utf-8')
        for symbol, _before in SITES.values():
            self.assertIn('.global %s' % symbol, stubs)
        # Each stub replays the instruction it replaces: the loop's call, P2's and P1's loads.
        self.assertIn('P0.H = 0x8d51; P0.L = 0xc708;\n    JUMP (P0);', stubs)
        self.assertIn('P2.H = 0x11b0; P2.L = 0x1548;', stubs)
        self.assertIn('P1.H = 0x1190; P1.L = 0x3600;', stubs)

    def test_each_stub_replays_the_immediate_its_site_held(self):
        # An early hardware test replayed P1 = 0x11900000 where stock loads 0x11903600 (a decimal
        # misread), so every send read another buffer. Decode the reviewed bytes
        # instead of trusting a disassembler's numbers: Blackfin+ `reg = imm32` is
        # 0xdaNN (NN the register: 0..7 R0..R7, 8..13 P0..P5), then the high and low halves.
        names = ['R%d' % i for i in range(8)] + ['P%d' % i for i in range(6)]
        stubs = (PLATFORM / 'drum_send_hooks.s').read_text(encoding='utf-8')
        source = (PLATFORM / 'drum_send.cpp').read_text(encoding='utf-8')
        loads = {}
        for address, (symbol, before) in SITES.items():
            words = [int.from_bytes(bytes.fromhex(before)[i:i + 2], 'little') for i in range(0, len(before) // 2, 2)]
            if words[0] & 0xff00 != 0xda00:
                continue
            register, value = names[words[0] & 0xff], words[1] << 16 | words[2]
            loads[address] = value
            body = stubs[stubs.index(symbol + ':\n'):]
            body = body[:body.index('.size ' + symbol)]
            self.assertIn('%s.H = 0x%04x; %s.L = 0x%04x;' % (register, value >> 16, register, value & 0xffff), body,
                          '%s must replay %s = 0x%08x' % (symbol, register, value))
        self.assertEqual(loads, {'0x8d51c7b2': 0x11b01548, '0x8d511088': 0x11903600, '0x8d5110ca': 0x11903600})
        buses = []
        for before in BUS_LOADS.values():
            words = [int.from_bytes(bytes.fromhex(before)[i:i + 2], 'little') for i in range(0, 8, 2)]
            buses.append(words[1] << 16 | words[2])
        self.assertEqual(buses, [0x84caa140, 0x84caa340])
        for bus in buses:
            self.assertIn('reinterpret_cast<i32*>(0x%08x)' % bus, source)
        # The own-amount hook replays the load it replaces (8d511116: 30e5 1200).
        body = stubs[stubs.index('sdk_drum_send_own:\n'):]
        self.assertIn('    HOOK_RESTORE\n    P0 = [SP + 0x48];\n    RTS;', body[:body.index('.size')])
        # The track hook replaces a relative CALL and must jump to its target.
        from tooling.firmware import decode_branch
        target, call = decode_branch(0x8d510bea, bytes.fromhex(SITES['0x8d510bea'][1]))
        self.assertTrue(call)
        self.assertIn('P0.H = 0x%04x; P0.L = 0x%04x;\n    JUMP (P0);' % (target >> 16, target & 0xffff), stubs)
        # The C++ side reads the same two stock buffers.
        self.assertIn('reinterpret_cast<i32*>(0x%08x)' % loads['0x8d511088'], source)
        self.assertIn('reinterpret_cast<const i32*>(0x%08x)' % loads['0x8d51c7b2'], source)

    def test_the_send_table_names_kits_that_declare_pad_mix(self):
        plain = kit()
        sending = SimpleNamespace(**dict(vars(kit(1)), prefix='sender', id='sender'))
        with tempfile.TemporaryDirectory() as folder:
            source = generate_drum_callbacks(folder, [plain, sending]).read_text(encoding='utf-8')
        self.assertIn('{sender_vtable, sender_drum_send},', source)
        self.assertNotIn('probe_vtable', source)
        self.assertIn('sdk_drum_send_kit_count = 1;', source)
        with tempfile.TemporaryDirectory() as folder:
            source = generate_drum_callbacks(folder, [plain]).read_text(encoding='utf-8')
        self.assertIn('sdk_drum_send_kit_count = 0;', source)
        machine = SimpleNamespace(**dict(vars(kit(1)), display='drawing'))
        absent = native.absent_symbols(machine)
        self.assertEqual(absent['probe_vtable'], 'kit_absent')
        self.assertEqual(absent['probe_drum_send'], 'kit_absent')


if __name__ == '__main__':
    unittest.main()
