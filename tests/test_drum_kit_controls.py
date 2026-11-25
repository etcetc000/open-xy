"""The drum-kit controls (SDK 1.23): padVolume, padRandom and ampEnvelope.

Their validation and version gate, what a kit's generated code gets from them (and that a
kit without them gets what it had), the adapter's audio on the host, the services and
fallbacks a build selects, and the fixture kit that declares them. The GUI side runs in
tests/fixtures/drum_gui.cpp (test_drum_services, with OPXY_KIT_CONTROLS)."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

from machine_fixtures import FOLDER, fixture
from test_drum_voices import CORE, DRIVER, ENGINE, FILTER_STOCK
from test_sample_services import HOST_CXX
from tooling.adapters import generate_drum_callbacks, generate_screen, generate_voice, kit_amp_defaults, kit_controls
from tooling.definitions import KIT_CONTROLS_SDK, validate_definition
from tooling.formats import require

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'

PREFIX = DRIVER[:DRIVER.index('int main() {')] + r'''
volatile unsigned char sdk_drum_released;
// Stock's drum sampler step tables (drum_kit_voice.hpp), as host doubles: each attack
// step 1/16 of full scale a frame, each fade step 1/32.
opxy::i32 host_attack_steps[64], host_fade_steps[64];
void block(unsigned id, const opxy::i32* region, opxy::i32* output) {
    reinterpret_cast<Render>(const_cast<void*>(table(id)[3]))(objects[id], output, region, offsets);
}
opxy::i32 flat(unsigned id, const opxy::i32* region) {
    opxy::i32 output[opxy::block_frames];
    block(id, region, output);
    for (auto sample : output) if (sample != output[0]) return -2;
    return output[0];
}
opxy::i32 marked_key[8];
void setup() {
    for (unsigned i = 0; i < opxy::block_frames; ++i) fixture_amplitude[i] = 0x7fffffff;
    for (unsigned i = 0; i < 8; ++i) offsets[i] = 0x40000000;
    for (unsigned i = 0; i < 64; ++i) { host_attack_steps[i] = 0x08000000; host_fade_steps[i] = 0x04000000; }
    for (unsigned i = 0; i < 256; ++i) objects[i] = probe_factory(i, i % 8);
    const opxy::u8 controls[8] = {64, 64, 64, 64, 64, 64, 64, 64};
    marked_key[0] = kMarked;
    opxy::platform::drum_pack(5, controls, marked_key + 1);
}
const opxy::i32 one = opxy::q31::multiply(2 << 16, 0x7fffffff);   // voice 1's sample at unity
bool near(opxy::i32 a, opxy::i32 b) { return a - b <= 1 && b - a <= 1; }
// A gain (Q27) as the adapter applies it: the Q31 product, scaled back by 16.
opxy::i32 scaled(opxy::i32 sample, opxy::i32 gain) { return opxy::q31::multiply(sample, gain) << 4; }
'''

VOLUME_DRIVER = PREFIX + r'''
int main() {
    setup();
    opxy::i32 key[8];
    for (unsigned i = 0; i < 8; ++i) key[i] = marked_key[i];
    // 0 dB, a cut and a boost (Q27, word 7), each from the note's first block.
    key[7] = 1 << 27;
    note(1, 53);
    CHECK(flat(1, key) == one);
    key[7] = 2 << 27;
    note(1, 53);
    CHECK(flat(1, key) == scaled(one, 2 << 27) && near(flat(1, key) / 16, 2 * one / 16));
    CHECK(flat(1, key) == scaled(one, 2 << 27));   // held: the same gain, no ramp
    key[7] = 1 << 26;
    note(1, 53);
    CHECK(flat(1, key) == scaled(one, 1 << 26) && near(flat(1, key) / 16, one / 32));
    // A turn while the note sounds ramps over one block to the new gain.
    key[7] = 4 << 27;
    opxy::i32 output[opxy::block_frames];
    block(1, key, output);
    CHECK(output[0] > one / 2 && output[0] < one && output[63] == scaled(one, 4 << 27));
    for (unsigned i = 1; i < 64; ++i) CHECK(output[i] >= output[i - 1]);
    CHECK(flat(1, key) == scaled(one, 4 << 27));
    // A key the kit has not marked plays at 0 dB, whatever its level byte says.
    opxy::i32 unmarked[8] = {0, 0, 0, 0, 0, 0, 0, 4 << 27};
    note(1, 53);
    CHECK(flat(1, unmarked) == one);
    // Silence stays silent; a boost saturates at full scale.
    key[7] = 0;
    note(1, 53);
    CHECK(flat(1, key) == 0);
    negative_output = true;
    key[7] = 10 << 27;
    note(255, 53);
    CHECK(flat(255, key) == scaled(-opxy::q31::multiply(256 << 16, 0x7fffffff), 10 << 27));
    negative_output = false;
    // A full-scale sample boosted saturates.
    opxy::i32 loud[opxy::block_frames];
    for (unsigned i = 0; i < 64; ++i) fixture_amplitude[i] = 0x7fffffff;
    typedef opxy::platform::DrumKitVoice<1, false> Volume;
    CHECK(Volume::pad_scale(0x40000000, 10 << 27) == 0x7fffffff);
    CHECK(Volume::pad_scale(-0x40000000, 10 << 27) == opxy::i32(0x80000000u));
    (void)loud;
    // No random: the controls play as stored.
    note(1, 53);
    flat(1, key);
    for (unsigned i = 0; i < 8; ++i) CHECK(raw(i) == 64);
    return 0;
}
'''

RANDOM_DRIVER = PREFIX + r'''
int main() {
    setup();
    opxy::i32 key[8];
    for (unsigned i = 0; i < 8; ++i) key[i] = marked_key[i];
    key[6] = 0x7fffffff;   // a full random (word 6)
    unsigned moved = 0, notes = 0;
    opxy::u32 first[8], previous[8] = {};
    for (unsigned n = 0; n < 16; ++n) {
        note(1, 53);
        flat(1, key);
        unsigned same = 0;
        for (unsigned i = 0; i < 8; ++i) {
            first[i] = raw(i);
            CHECK(first[i] >= 64 - 32 && first[i] <= 64 + 32);
            moved += first[i] != 64;
            same += first[i] == previous[i];
            previous[i] = first[i];
        }
        notes += same < 8;
        // Each block of the note plays the same offsets.
        flat(1, key);
        for (unsigned i = 0; i < 8; ++i) CHECK(raw(i) == first[i]);
    }
    CHECK(moved > 64 && notes >= 15);
    // A tenth of full: within 3 steps either way.
    key[6] = 0x7fffffff / 10;
    for (unsigned n = 0; n < 16; ++n) {
        note(1, 53);
        flat(1, key);
        for (unsigned i = 0; i < 8; ++i) CHECK(raw(i) >= 64 - 4 && raw(i) <= 64 + 4);
    }
    // None, and a key the kit has not marked: the controls as stored.
    key[6] = 0;
    note(1, 53);
    flat(1, key);
    for (unsigned i = 0; i < 8; ++i) CHECK(raw(i) == 64);
    opxy::i32 unmarked[8] = {0, 0, 0, 0, 0, 0, 0x7fffffff, 1 << 27};
    note(1, 53);
    flat(1, unmarked);
    for (unsigned i = 0; i < 8; ++i) CHECK(raw(i) == 53);   // the key's defaults (ENGINE)
    // Offsets are clamped at both ends.
    const opxy::u8 low[8] = {0, 0, 0, 0, 127, 127, 127, 127};
    opxy::platform::drum_pack(5, low, key + 1);
    key[6] = 0x7fffffff;
    for (unsigned n = 0; n < 8; ++n) {
        note(1, 53);
        flat(1, key);
        for (unsigned i = 0; i < 4; ++i) CHECK(raw(i) <= 32 && raw(4 + i) >= 95);
    }
    // The voice level is untouched (no padVolume).
    key[7] = 4 << 27;
    note(1, 53);
    CHECK(flat(1, key) == one);
    return 0;
}
'''

AMP_DRIVER = PREFIX + r'''
int main() {
    setup();
    opxy::i32 key[8];
    for (unsigned i = 0; i < 8; ++i) key[i] = marked_key[i];
    // The key's own values in its words' low halves: start, attack 0 (from full), hold
    // full, decay. Its model and controls read past them.
    const opxy::i32 amp[4] = {0, 0, 0x7fff0000, 0};
    for (unsigned i = 0; i < 4; ++i) key[1 + i] = opxy::platform::kit_amp_store(key[1 + i], amp[i]);
    note(1, 53);
    CHECK(flat(1, key) == one && model() == 5);
    for (unsigned i = 0; i < 8; ++i) CHECK(raw(i) == 64);
    for (unsigned b = 0; b < 100; ++b) CHECK(flat(1, key) == one);   // held to the end
    // The note-off: it fades at the decay's step, 1/32 a frame, and is freed when silent.
    sdk_drum_released = 1;
    opxy::i32 output[opxy::block_frames];
    block(1, key, output);
    CHECK(!sdk_drum_released);   // taken by this render alone
    // Each frame is the product times the level, so a full level is one LSB under.
    CHECK(near(output[0], one) && output[1] < output[0] && output[31] > 0 && output[33] == 0 && output[63] == 0);
    // Silent within the block, it is freed once the block is mixed.
    CHECK(!is_active(1));
    sdk_drum_released = 0;
    // An attack: from silence, rising 1/16 of full scale a frame.
    key[2] = opxy::platform::kit_amp_store(key[2], 0x40000000);
    note(1, 53);
    block(1, key, output);
    CHECK(output[0] == 0 && output[1] > 0 && output[15] < one - 1 && near(output[17], one) && near(output[63], one));
    CHECK(flat(1, key) == one);
    // Hold: the fade begins after hold x 2 s, here 1/1378 of full, one block.
    key[2] = opxy::platform::kit_amp_store(key[2], 0);
    key[3] = opxy::platform::kit_amp_store(key[3], 0x7fff0000 / 1378 + 0x10000);
    note(1, 53);
    CHECK(flat(1, key) == one);
    block(1, key, output);
    CHECK(near(output[0], one) && output[33] == 0);
    // A hold of 0 lets the attack finish before the fade begins.
    key[2] = opxy::platform::kit_amp_store(key[2], 0x40000000);
    key[3] = opxy::platform::kit_amp_store(key[3], 0);
    note(1, 53);
    block(1, key, output);
    CHECK(output[0] == 0 && near(output[17], one) && near(output[63], one));
    block(1, key, output);
    CHECK(near(output[0], one) && output[33] == 0 && !is_active(1));
    key[2] = opxy::platform::kit_amp_store(key[2], 0);
    // A key the kit has not marked plays the kit's defaults (this fixture's: the
    // template's, attack 8192: a rise from silence).
    opxy::i32 unmarked[8] = {0, 0, 0, 0, 0, 0, 0, 1 << 27};
    note(1, 53);
    block(1, unmarked, output);
    CHECK(output[0] == 0 && output[63] > 0);
    // chokeSameKey: a retrigger renders silence for the block the cut voice fades in,
    // which is one block (64 frames), then starts.
    key[3] = opxy::platform::kit_amp_store(key[3], 0x7fff0000);
    note(1, 53);
    CHECK(flat(1, key) == one);
    note(9, 53);   // another handle, the same pad: cuts voice 1
    block(9, key, output);
    for (unsigned i = 0; i < 64; ++i) CHECK(output[i] == 0);
    block(1, key, output);
    CHECK(output[0] > 0 && output[62] > 0 && output[63] == 0 && !is_active(1));
    CHECK(flat(9, key) == opxy::q31::multiply(10 << 16, 0x7fffffff));
    // A retrigger cut again while it waits never sounds.
    note(1, 53);                // waits out voice 9's fade
    note(17, 53);               // track 1 too: cuts voice 1 before its first render
    block(1, key, output);
    for (unsigned i = 0; i < 64; ++i) CHECK(output[i] == 0);
    CHECK(!is_active(1));
    // A note on a pad with nothing sounding starts at once.
    note(5, 60);
    CHECK(flat(5, key) == opxy::q31::multiply(6 << 16, 0x7fffffff));
    return 0;
}
'''


def kit(**resources):
    definition = copy.deepcopy(fixture('probekit21').definition)
    definition['resources'].update(resources)
    return definition


class Validation(unittest.TestCase):
    def test_each_control_is_0_or_1_and_not_with_pad_mix(self):
        for key in ('padVolume', 'padRandom', 'ampEnvelope'):
            for value in (0, 1):
                validate_definition(kit(**{key: value}), 'drum')
            for value in (2, -1, True):
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    validate_definition(kit(**{key: value}), 'drum')
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'without padMix'):
                validate_definition(kit(padMix=1, **{key: 1}), 'drum')
            validate_definition(kit(padMix=1, **{key: 0}), 'drum')
        for kind in ('synth', 'sample', 'effect'):
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                definition = copy.deepcopy(fixture({'synth': 'probesynth', 'sample': 'probesample',
                                                    'effect': 'probefx'}[kind]).definition)
                definition['resources']['padVolume'] = 1
                validate_definition(definition, kind)

    def test_the_controls_need_sdk_1_23_and_the_fixture_kit_declares_it(self):
        from tooling.machines import machine_at
        self.assertEqual(KIT_CONTROLS_SDK, (1, 23))
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory) / 'probekit23'
            shutil.copytree(FOLDER / 'probekit23', folder)
            self.assertEqual(kit_controls(machine_at(folder)), 7)
            record = json.loads((folder / 'machine.json').read_text(encoding='utf-8'))
            for version, ok in (('1.22', False), ('1.21', False), ('1.23', True)):
                record['sdk'] = version
                (folder / 'machine.json').write_text(json.dumps(record), encoding='utf-8')
                with self.subTest(version=version):
                    if ok:
                        machine_at(folder)
                    else:
                        with self.assertRaisesRegex(ValueError, 'drum-kit controls'):
                            machine_at(folder)


class Generation(unittest.TestCase):
    def machine(self, sdk=(1, 23), **resources):
        definition = kit(**resources)
        definition['id'] = 'probe'
        return SimpleNamespace(kind='drum', definition=definition, prefix='probe', native_name='probe', sdk=sdk,
                               preset_defaults={}, id='probe')

    def test_a_kit_without_them_generates_the_adapter_it_had(self):
        # Its constants are 0, so drum_kit_voice.hpp's DrumKitVoice<0, ...> does nothing
        # and DrumKitState<0> is empty; the release build's package check compares objects.
        with tempfile.TemporaryDirectory() as directory:
            text = generate_voice(directory, self.machine(sdk=(1, 21))).read_text(encoding='utf-8')
        self.assertIn('const opxy::u32 kKitControls = 0;', text)
        self.assertIn('struct Slot : opxy::platform::DrumKitState<kKitControls> {', text)
        source = (PLATFORM / 'drum_kit_voice.hpp').read_text(encoding='utf-8')
        self.assertIn('template <> struct DrumKitState<0> {};', source)
        for bits, resources in ((1, dict(padVolume=1)), (2, dict(padRandom=1)), (4, dict(ampEnvelope=1)),
                                (7, dict(padVolume=1, padRandom=1, ampEnvelope=1))):
            with self.subTest(resources=resources), tempfile.TemporaryDirectory() as directory:
                text = generate_voice(directory, self.machine(**resources)).read_text(encoding='utf-8')
                self.assertIn(f'const opxy::u32 kKitControls = {bits};', text)

    def test_the_amp_defaults_are_the_kits_init_preset(self):
        m = self.machine(ampEnvelope=1)
        self.assertEqual(kit_amp_defaults(m), [128 << 16, 8192 << 16, 32767 << 16, 32767 << 16])
        m.preset_defaults = {'amp': {'attack': 0, 'sustain': 1000}}
        self.assertEqual(kit_amp_defaults(m), [0, 8192 << 16, 1000 << 16, 32767 << 16])

    def test_the_kit_table_lists_only_kits_with_them(self):
        plain, volume, amp = self.machine(), self.machine(padVolume=1), self.machine(ampEnvelope=1, padRandom=1)
        volume.prefix, amp.prefix = 'vol', 'amp'
        with tempfile.TemporaryDirectory() as directory:
            text = Path(generate_drum_callbacks(directory, [plain])).read_text(encoding='utf-8')
            self.assertIn('opxy::u32 sdk_drum_kit_count = 0;', text)
            text = Path(generate_drum_callbacks(directory, [plain, volume, amp])).read_text(encoding='utf-8')
        self.assertIn('    {vol_engine_id, 1, nullptr},\n    {amp_engine_id, 6, amp_amp},\n', text)
        self.assertIn('opxy::u32 sdk_drum_kit_count = 2;', text)
        self.assertIn('static const opxy::i32 amp_amp[] = {0x800000, 0x20000000, 0x7fff0000, 0x7fff0000};', text)

    def test_a_kit_screen_reads_its_cells_and_selected_key_only_when_it_has_them(self):
        def screen(**kwargs):
            m = self.machine(**kwargs)
            m.definition['id'] = 'probekit'
            with tempfile.TemporaryDirectory() as directory:
                return generate_screen(directory, m).read_text(encoding='utf-8')
        old = screen(sdk=(1, 21))
        self.assertIn('static const bool kit_controls = false;', old)
        self.assertIn('static const bool selected_key = false;', old)
        new = screen(padRandom=1)
        self.assertIn('static const bool kit_controls = true;', new)
        self.assertIn('static const bool selected_key = true;', new)
        # ampEnvelope has no Shift cell.
        self.assertIn('static const bool kit_controls = false;', screen(ampEnvelope=1))


class Adapter(unittest.TestCase):
    def run_kit(self, driver, **resources):
        definition = copy.deepcopy(fixture('probesynth').definition)
        definition['id'] = 'probe'
        definition['resources'].update(voiceBytes=4, voiceAlignment=4, voiceCapacity=4, sharedBytes=0, modelCount=12,
                                       **resources)
        machine = SimpleNamespace(kind='drum', definition=definition, prefix='probe', native_name='probe',
                                  sdk=(1, 23), preset_defaults={})
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = generate_voice(folder, machine)
            generated = source.read_text(encoding='utf-8')
            generated = (generated.replace('reinterpret_cast<const opxy::u32*>(opxy::platform::kPitchIncrements)', 'fixture_pitch')
                         .replace('reinterpret_cast<const opxy::i32*>(0x11903400)', 'fixture_amplitude')
                         .replace('kTransport ? opxy::platform::audio_transport() : opxy::Transport()',
                                  'kTransport ? fixture_transport() : opxy::Transport()'))
            for native, double in FILTER_STOCK.items():
                generated = generated.replace(native, double)
            source.write_text(generated, encoding='utf-8')
            # The stock step tables, as the driver's doubles: the copy beside voice.cpp wins.
            header = (PLATFORM / 'drum_kit_voice.hpp').read_text(encoding='utf-8')
            for native, double in (('reinterpret_cast<const i32*>(0x8404650c)', 'host_attack_steps'),
                                   ('reinterpret_cast<const i32*>(0x840512e4)', 'host_fade_steps')):
                self.assertEqual(header.count(native), 1)
                header = header.replace(native, double)
            header = header.replace('namespace opxy { namespace platform {\n// A note',
                                    'extern opxy::i32 host_attack_steps[64], host_fade_steps[64];\n'
                                    'namespace opxy { namespace platform {\n// A note', 1)
            (folder / 'drum_kit_voice.hpp').write_text(header, encoding='utf-8')
            driver += 'opxy::Transport fixture_transport() { return opxy::Transport(); }\n'
            for name, content in (('core.h', CORE), ('engine.cpp', ENGINE), ('test.cpp', driver)):
                (folder / name).write_text(content, encoding='utf-8')
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-Wno-unused-function',
                       *('-I' + str(path) for path in (ROOT / 'sdk/include', PLATFORM, folder)),
                       str(folder / 'test.cpp'), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_pad_volume_plays_each_marked_keys_level_ramped(self):
        self.run_kit(VOLUME_DRIVER, padVolume=1)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_pad_random_moves_each_hits_controls_by_its_keys_random(self):
        self.run_kit(RANDOM_DRIVER, padRandom=1)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_amp_envelope_shapes_each_note_and_a_retrigger_waits_out_the_cut(self):
        self.run_kit(AMP_DRIVER, ampEnvelope=1, chokeSameKey=1)


class Services(unittest.TestCase):
    def test_a_build_links_and_hooks_them_only_with_a_kit_declaring_them(self):
        from tooling.firmware import active_services, patch_active
        from tooling.native import KIT_CONTROLS_CALLS, UNIT_SERVICES, service_fallbacks, service_users
        from tooling.pipeline import config
        profile = config()
        self.assertEqual(UNIT_SERVICES['drum_kit_controls'], 'kit_controls')
        self.assertEqual(UNIT_SERVICES['drum_amp_envelope'], 'amp_envelope')
        hooks = [p for p in profile['patches'] if p.get('symbol') == 'sdk_drum_envelope_setup']
        self.assertEqual(len(hooks), 1)
        self.assertEqual(hooks[0]['service'], 'amp_envelope')
        old, new = fixture('probekit21'), fixture('probekit23')
        self.assertEqual(active_services(profile, service_users([old])) & {'kit_controls', 'amp_envelope'}, set())
        self.assertEqual(active_services(profile, service_users([new])) & {'kit_controls', 'amp_envelope'},
                         {'kit_controls', 'amp_envelope'})
        self.assertFalse(patch_active(hooks[0], active_services(profile, service_users([old]))))
        self.assertEqual(service_fallbacks({'kit_controls'})['kit_controls'], dict.fromkeys(KIT_CONTROLS_CALLS, 'kit_absent'))
        # Every entry point the shared units call has its fallback.
        shared = ''.join((PLATFORM / name).read_text(encoding='utf-8')
                         for name in ('drum_values.hpp', 'drum_synthesis_stock.cpp', 'sample_cache.cpp'))
        header = (PLATFORM / 'drum_kit_controls.hpp').read_text(encoding='utf-8')
        called = {name for name in ('sdk_drum_kit_unpack', 'sdk_drum_kit_keeps', 'sdk_drum_kit_setup', 'sdk_drum_kit_edit',
                                    'sdk_drum_amp_tick', 'sdk_drum_kit_readout', 'sdk_drum_kit')
                  if name + '(' in shared or (name == 'sdk_drum_kit_keeps' and 'drum_kit_keep(' in shared)}
        self.assertEqual(called, set(KIT_CONTROLS_CALLS))
        for name in KIT_CONTROLS_CALLS:
            self.assertIn(f'__asm__("{name}")', header)

    def test_only_a_build_with_such_a_kit_compiles_the_shared_units_with_the_define(self):
        source = (ROOT / 'tooling/native.py').read_text(encoding='utf-8')
        self.assertIn("defines = (KIT_CONTROLS_DEFINE,) if 'kit_controls' in active_services(", source)
        self.assertIn("defines if m is None else ()", source)
        for name in ('drum_values.hpp', 'drum_synthesis_stock.cpp', 'sample_cache.cpp'):
            text = (PLATFORM / name).read_text(encoding='utf-8')
            self.assertIn('#if OPXY_KIT_CONTROLS', text)


if __name__ == '__main__':
    unittest.main()
