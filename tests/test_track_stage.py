"""A synth's track stage (SDK 1.24, tooling/track_stage.py): one state per instrument track,
processed in place on the track's voice sum after stock's voice loop.

Stock's mixer calls each track's voice loop from one place (8d510bea). Per-key sends and
track stages both need that call, so it has one record, the track call: a build with
per-key sends alone enters their own handler (sdk_drum_send_track), unchanged, and a build
with a track stage enters the shared one (sdk_track_call, track_call.s), which resets the
per-key sends, runs stock's loop and then the stage. A build without either leaves the site
stock. The host fixtures drive the runtime's block (track_stage.hpp) and the probe's stage."""
import copy
import json
import os
from pathlib import Path
import random
import re
import shutil
import struct
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

from tooling import native, records, track_stage
from tooling.adapters import generate_voice
from tooling.definitions import validate_definition
from tooling.firmware import active_services, decode_branch, hook_symbol, patch_active
from tooling.machines import machine_at
from tooling.pipeline import ROOT, check_patch_boundaries, config
from machine_fixtures import FOLDER, fixture
from test_divide import blackfin_compiler
from test_sample_services import HOST_CXX
import test_service_links

PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
SITE = '0x8d510bea'
SIM = ROOT / 'tests/fixtures/track_call_sim'
CCES = Path(os.environ.get('OPXY_TEST_CCES') or r'C:/Analog Devices/CrossCore Embedded Studio 2.12.1')
LOOP = 0x8d51c708


def track_call(profile):
    found = [p for p in profile['patches'] if p['address'] == SITE]
    assert len(found) == 1
    return found[0]


def synth(resources, shift=None, prefix='probe'):
    """probestage's definition with these resources (and Shift row), as the adapters read a machine."""
    definition = copy.deepcopy(fixture('probestage').definition)
    definition['id'] = prefix
    definition['resources'].update(resources)
    if shift is not None:
        definition['shift'] = shift
    validate_definition(definition, 'synth')
    return SimpleNamespace(kind='synth', definition=definition, prefix=prefix, native_name=prefix, id=prefix,
                           identity=dict(engine=5, page=85), display='drawing', sdk=track_stage.TRACK_STAGE_SDK)


class Declaration(unittest.TestCase):
    def test_the_probe_declares_a_stage_held_for_its_tail_and_margin(self):
        m = fixture('probestage')
        self.assertEqual(m.sdk, track_stage.TRACK_STAGE_SDK)
        self.assertEqual(track_stage.track_stage(m), {'stateBytes': 2052, 'stateAlignment': 4, 'tailFrames': 512})
        self.assertEqual(track_stage.hold(m), 512 // 64 + 64)
        self.assertEqual(track_stage.hold(synth({'trackStage': dict(stateBytes=4, stateAlignment=4, tailFrames=1)})), 65)
        self.assertEqual(track_stage.hold(synth({'trackStage': dict(stateBytes=4, stateAlignment=4, tailFrames=0)})), 64)
        self.assertEqual(track_stage.hold(synth({'trackStage': dict(stateBytes=4, stateAlignment=4, tailFrames='unbounded')})),
                         track_stage.UNBOUNDED)
        self.assertIsNone(track_stage.track_stage(fixture('probesynth')))

    def test_the_declaration_is_checked(self):
        good = dict(stateBytes=8, stateAlignment=8, tailFrames=1 << 24)
        synth({'trackStage': good})
        for bad in (dict(good, stateBytes=6), dict(good, stateBytes=0), dict(good, stateAlignment=16),
                    dict(good, stateBytes=12, stateAlignment=8), dict(good, stateBytes=True),
                    dict(good, tailFrames=(1 << 24) + 1), dict(good, tailFrames=-1), dict(good, tailFrames='forever'),
                    dict(good, tailFrames=1.5), {'stateBytes': 8, 'stateAlignment': 8},
                    dict(good, latency=0), 8192):
            with self.subTest(stage=bad), self.assertRaises(ValueError):
                synth({'trackStage': bad})

    def test_only_a_synth_declares_one(self):
        stage = dict(stateBytes=4, stateAlignment=4, tailFrames=0)
        for name, kind in (('probekit21', 'drum'), ('probefx', 'effect'), ('probesample', 'sample')):
            definition = copy.deepcopy(fixture(name).definition)
            definition['resources']['trackStage'] = stage
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                validate_definition(definition, kind)

    def test_a_machine_declaring_an_older_sdk_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory) / 'probestage'
            shutil.copytree(FOLDER / 'probestage', folder)
            record = json.loads((folder / 'machine.json').read_text(encoding='utf-8'))
            record['sdk'] = '%d.%d' % (track_stage.TRACK_STAGE_SDK[0], track_stage.TRACK_STAGE_SDK[1] - 1)
            (folder / 'machine.json').write_text(json.dumps(record), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'track stage'):
                machine_at(folder)


class TrackCall(unittest.TestCase):
    def test_one_record_holds_the_voice_loops_call(self):
        profile = config()
        patch = track_call(profile)
        self.assertEqual((patch['kind'], patch['length'], patch['before'], patch['call'], patch['service'], patch['symbol']),
                         ('hook', 4, '00e38f5d', True, 'track_call', 'sdk_drum_send_track'))
        self.assertEqual(list(patch['symbols'].items()), [('track_stage', 'sdk_track_call'), ('drum_sends', 'sdk_drum_send_track')])
        # The instruction it replaces calls the voice loop, which the shared handler calls in its place.
        self.assertEqual(decode_branch(int(SITE, 16), bytes.fromhex(patch['before'])), (LOOP, True))
        handler = (PLATFORM / 'track_call.s').read_text(encoding='utf-8')
        self.assertIn('P0.H = 0x%04x; P0.L = 0x%04x;\n    CALL (P0);' % (LOOP >> 16, LOOP & 0xffff), handler)
        self.assertIn('P0.H = 0x8d51; P0.L = 0xc708;\n    JUMP (P0);', (PLATFORM / 'drum_send_hooks.s').read_text(encoding='utf-8'))

    def test_the_shared_handler_runs_the_sends_reset_the_loop_and_the_stage_in_order(self):
        handler = (PLATFORM / 'track_call.s').read_text(encoding='utf-8')
        body = handler[handler.index('sdk_track_call:\n'):handler.index('.size sdk_track_call')]
        calls = re.findall(r'CALL (\S+);', body)
        self.assertEqual(calls, ['sdk_drum_send_begin', '(P0)', 'sdk_track_stage'])
        # The engine's R0..R2 are kept in callee-saved registers across the reset and passed
        # to the loop; the loop's count (R0) goes to the stage as its third argument.
        self.assertIn('    LINK 0;\n    [--SP] = (R7:5);\n    SP += -12;\n    R5 = R0;\n    R6 = R1;\n    R7 = R2;\n', body)
        self.assertIn('    R0 = R5;\n    R1 = R6;\n    R2 = R7;\n    P0.H', body)
        self.assertIn('    R2 = R0;\n    R0 = R6;\n    R1 = R7;\n    CALL sdk_track_stage;\n    SP += 12;\n'
                      '    (R7:5) = [SP++];\n    UNLINK;\n    RTS;\n', body)

    def test_the_track_call_is_used_by_every_user_of_its_services(self):
        services = config()['services']
        self.assertEqual(services['track_stage'], ['trackStage'])
        self.assertEqual(set(services['track_call']), set(services['drum_sends']) | set(services['track_stage']))

    def test_the_handler_follows_which_services_the_build_has(self):
        profile = config()
        patch = track_call(profile)
        for users, symbol in (([], None), (['synth'], None), (['padMix'], 'sdk_drum_send_track'),
                              (['drumline_page'], 'sdk_drum_send_track'), (['trackStage'], 'sdk_track_call'),
                              (['padMix', 'trackStage'], 'sdk_track_call'),
                              (['drumline_page', 'trackStage'], 'sdk_track_call')):
            services = active_services(profile, users)
            with self.subTest(users=users):
                self.assertEqual(patch_active(patch, services, False), symbol is not None)
                if symbol:
                    self.assertEqual(hook_symbol(patch, services), symbol)
        # Every other hook enters its own symbol, whatever is linked, but the compositor's loop
        # advance, which the track instrument's entry replaces (tests/test_players.py).
        everything = set(profile['services']) - {'track_instrument'}
        for p in profile['patches']:
            if p['kind'] == 'hook' and p['address'] != SITE:
                self.assertEqual('symbols' in p, p['symbol'] == 'sdk_screen_advance')
                self.assertEqual(hook_symbol(p, everything), p['symbol'])

    def test_a_shared_hook_names_declared_services(self):
        profile = config()
        both = {'track_stage': 'sdk_track_call', 'drum_sends': 'sdk_drum_send_track'}
        bad = []
        for symbols, service in (({'nothing': 'sdk_track_call', 'drum_sends': 'sdk_drum_send_track'}, 'track_call'),
                                 ({}, 'track_call'), (dict(both, track_stage=''), 'track_call'), (both, None),
                                 (['track_stage'], 'track_call'),
                                 # One for every user of the record's service, its own symbol among them.
                                 ({'track_stage': 'sdk_track_call'}, 'track_call'),
                                 ({'track_stage': 'sdk_track_call', 'drum_sends': 'sdk_drum_send_own'}, 'track_call'),
                                 (both, 'drum_sends')):
            changed = copy.deepcopy(profile)
            patch = track_call(changed)
            patch['symbols'] = symbols
            if service is None:
                del patch['service']
            else:
                patch['service'] = service
            with self.subTest(symbols=symbols, service=service), self.assertRaises(ValueError):
                check_patch_boundaries(changed, len(changed['patches']))
            bad.append(patch)
        check_patch_boundaries(profile, len(profile['patches']))
        # The browser's check (builder/core.mjs validSharedHook) refuses the same records.
        script = ('import {validSharedHook} from %s;\nconst [services, edits] = JSON.parse(process.argv[1]);\n'
                  'process.stdout.write(JSON.stringify(edits.map(e => validSharedHook(e, services))));'
                  % json.dumps((ROOT / 'builder/core.mjs').as_uri()))
        result = subprocess.run(['node', '--input-type=module', '-e', script,
                                 json.dumps([profile['services'], [track_call(profile), *bad]])],
                                capture_output=True, text=True, cwd=ROOT, timeout=60, check=True)
        self.assertEqual(json.loads(result.stdout), [True] + [False] * len(bad))

    def test_a_hook_every_build_applies_names_handlers_that_replace_its_own(self):
        # The compositor's loop advance (no service): its `symbols` replace its own symbol in a
        # build with one of their services, so its own symbol is none of them; the same in the
        # browser (builder/core.mjs validSharedHook) and for the browser's whole profile.
        profile = config()
        find = lambda p: next(e for e in p['patches'] if e.get('symbol') == 'sdk_screen_advance')
        bad = []
        for change in (lambda e: e['symbols'].update(players='sdk_screen_advance'),
                       lambda e: e['symbols'].update(nothing='sdk_instrument_advance'),
                       lambda e: e['symbols'].update(track_instrument=''),
                       lambda e: e.update(profiling=False), lambda e: e.update(symbols={})):
            changed = copy.deepcopy(profile)
            change(find(changed))
            with self.assertRaises(ValueError):
                check_patch_boundaries(changed, len(changed['patches']))
            bad.append(find(changed))
        check_patch_boundaries(profile, len(profile['patches']))
        script = ('import {validSharedHook} from %s;\nconst [services, edits] = JSON.parse(process.argv[1]);\n'
                  'process.stdout.write(JSON.stringify(edits.map(e => validSharedHook(e, services))));'
                  % json.dumps((ROOT / 'builder/core.mjs').as_uri()))
        edits = [p for p in profile['patches'] if 'symbols' in p]
        result = subprocess.run(['node', '--input-type=module', '-e', script,
                                 json.dumps([profile['services'], [*edits, *bad]])],
                                capture_output=True, text=True, cwd=ROOT, timeout=60, check=True)
        self.assertEqual(json.loads(result.stdout), [True] * len(edits) + [False] * len(bad))
        self.assertEqual(len(edits), 2)

    def test_the_browser_enters_the_same_handler(self):
        profile = config()
        patch = track_call(profile)
        cases = [sorted(active_services(profile, users)) for users in
                 (['padMix'], ['trackStage'], ['padMix', 'trackStage'], ['drumline_page'])]
        script = ('import {hookSymbol} from %s;\nconst [edit, cases] = JSON.parse(process.argv[1]);\n'
                  'process.stdout.write(JSON.stringify(cases.map(c => hookSymbol(edit, new Set(c)))));'
                  % json.dumps((ROOT / 'builder/core.mjs').as_uri()))
        result = subprocess.run(['node', '--input-type=module', '-e', script, json.dumps([patch, cases])],
                                capture_output=True, text=True, cwd=ROOT, timeout=60, check=True)
        self.assertEqual(json.loads(result.stdout), [hook_symbol(patch, set(c)) for c in cases])
        self.assertEqual(json.loads(result.stdout), ['sdk_drum_send_track', 'sdk_track_call', 'sdk_track_call',
                                                     'sdk_drum_send_track'])


class Plan(unittest.TestCase):
    def test_only_a_build_with_a_stage_links_its_units(self):
        self.assertEqual({u: native.UNIT_SERVICES[u] for u in ('track_call', 'track_stage')},
                         dict.fromkeys(('track_call', 'track_stage'), 'track_stage'))
        units = lambda machines: [step for _name, m, step in native.plan(machines) if m is None]
        plain = units([fixture('probesynth')])
        staged = units([fixture('probesynth'), fixture('probestage')])
        self.assertFalse({'track_call', 'track_stage'} & set(plain))
        self.assertEqual([u for u in staged if u not in plain], ['track_call', 'track_stage'])
        self.assertIn('trackStage', native.service_users([fixture('probestage')]))
        self.assertNotIn('trackStage', native.service_users([fixture('probesynth')]))

    def test_without_per_key_sends_the_handler_resets_nothing(self):
        profile = config()
        self.assertEqual(native.service_fallbacks(active_services(profile, ['synth', 'trackStage'])),
                         {'drum_sends': {'sdk_drum_send_begin': 'kit_absent'}})
        self.assertNotIn('drum_sends', native.service_fallbacks(active_services(profile, ['drum', 'padMix'])))

    def test_only_the_stage_synths_record_names_its_stage(self):
        record = records.render(fixture('probestage'))
        for symbol in ('probestage_track_reset', 'probestage_track_process', 'probestage_record_ready',
                       'probestage_stage_benchmark'):
            self.assertIn(symbol, record)
        self.assertIn('&stage_row', record)
        self.assertNotIn('track_', records.render(fixture('probesynth')))
        self.assertNotIn('stage_benchmark', records.render(fixture('probesynth')))


class Adapter(unittest.TestCase):
    def test_a_synth_without_a_stage_keeps_its_adapter(self):
        m = SimpleNamespace(kind='synth', definition=fixture('probesynth').definition, prefix='probe',
                            native_name='probe', identity=dict(engine=5, page=85), sdk=(1, 1))
        template = (PLATFORM / 'voice.cpp.in').read_text(encoding='utf-8')
        self.assertTrue(template.endswith('};\n}@TRACK_STAGE@\n'))
        with tempfile.TemporaryDirectory() as directory:
            text = generate_voice(Path(directory), m).read_text(encoding='utf-8')
        self.assertTrue(text.endswith('};\n}\n'))
        self.assertNotIn('track_stage', text)
        self.assertNotIn('TrackState', text)

    def test_a_stage_adapter_checks_the_state_and_passes_the_tracks_words(self):
        with tempfile.TemporaryDirectory() as directory:
            text = generate_voice(Path(directory), fixture('probestage')).read_text(encoding='utf-8')
        self.assertIn('static_assert(sizeof(TrackState) == 2052,', text)
        self.assertIn('static_assert(alignof(TrackState) == 4,', text)
        self.assertIn('static TrackState track_states[opxy::platform::track_stage::kTracks];', text)
        self.assertIn('    probestage::track_process(track_states[track], shared, track, sum, parameters, voices);', text)
        self.assertNotIn('shift_words(parameters, controls + opxy::parameter_slots, shift_kept[track])', text)

    def test_a_shift_row_synths_stage_gets_its_sixteen_words(self):
        shift = [{'id': 'mode', 'label': 'Mode', 'default': 0,
                  'mapping': {'kind': 'enum', 'labels': ['A', 'B']}, 'display': 'enum'}]
        m = synth({}, shift=shift)
        with tempfile.TemporaryDirectory() as directory:
            text = generate_voice(Path(directory), m).read_text(encoding='utf-8')
        stage = text[text.index('void probe_track_process('):]
        self.assertIn('    shift_words(parameters, controls + opxy::parameter_slots, shift_kept[track]);\n'
                      '    probe::track_process(track_states[track], shared, track, sum, controls, voices);', stage)
        # The diagnostic's stage run plays the row at its defaults, as a new project does.
        from tooling.cpu import control_defaults
        from tooling.definitions import shift_words
        words = control_defaults(m.definition['parameters'])[:8] + shift_words(m.definition)
        self.assertNotEqual(shift_words(m.definition), [0] * 8)
        self.assertIn('parameters[2 * parameter_slots] = {%s};' % ', '.join(map(str, words)), text)

    def test_the_table_lists_each_declaring_synth_with_its_hold(self):
        unbounded = synth({'trackStage': dict(stateBytes=4, stateAlignment=4, tailFrames='unbounded')}, prefix='endless')
        with tempfile.TemporaryDirectory() as directory:
            text = track_stage.generate_table(directory, [fixture('probesynth'), fixture('probestage'), unbounded]).read_text(
                encoding='utf-8')
        rows = re.findall(r'^    \{(.*)\},$', text, re.M)
        self.assertEqual(rows, ['probestage_engine_id, probestage_record_ready, probestage_track_reset, '
                                'probestage_track_process, 72u',
                                'endless_engine_id, endless_record_ready, endless_track_reset, endless_track_process, '
                                '4294967295u'])
        self.assertIn('opxy::u32 sdk_track_stage_count = 2;', text)
        self.assertNotIn('probesynth', text)


def host_run(test, folder, sources, includes=()):
    binary = Path(folder) / ('test.exe' if os.name == 'nt' else 'test')
    command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-Wno-unused-function',
               *('-I' + str(path) for path in (ROOT / 'sdk/include', PLATFORM, *includes)), *map(str, sources),
               '-o', str(binary)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=120, **HIDDEN)
    test.assertEqual(built.returncode, 0, built.stdout + built.stderr)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60, **HIDDEN)
    test.assertEqual(result.returncode, 0, result.stdout + result.stderr)
    return result.stdout


# The probe's generated adapter, driven as track_stage.cpp drives it: reset, then blocks of
# a known sum with the track's words. Prints every word of each processed block.
# The diagnostic's own services, as tests/fixtures/benchmark_callbacks.cpp gives them: a
# clock that advances 100 cycles a reading, and counted heap blocks.
BENCHMARK_STUBS = '''#include <cstdlib>
static unsigned allocations, releases, clock_value;
extern "C" opxy::u32 sdk_benchmark_cycles() { clock_value += 100; return clock_value; }
namespace opxy { namespace benchmark {
void* allocate(u32 bytes) { ++allocations; return std::calloc(1, bytes); }
void release(void* data) { ++releases; std::free(data); }
}}
'''
PROBE_DRIVER = '''#include <cstdio>
#include "voice.cpp"
''' + BENCHMARK_STUBS + '''opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index + 4; }
opxy::u32 sdk_voice_ticks() { return 0; }
int main() {
    static opxy::i32 words[58];
    words[1] = @WIDTH@; words[2] = @CROSS@; words[3] = @GAIN@;
    opxy::u32 seed = 1;
    for (opxy::u32 track = 0; track < 2; ++track) {
        track_states[track].write = 99;  // the reset zeroes it
        probestage_track_reset(track);
        for (int block = 0; block < @BLOCKS@; ++block) {
            opxy::i32 sum[128];
            for (int i = 0; i < 128; ++i) { seed = seed * 1103515245u + 12345u; sum[i] = static_cast<opxy::i32>(seed); }
            if (block >= @SILENT_FROM@) for (int i = 0; i < 128; ++i) sum[i] = 0;
            probestage_track_process(track, sum, words, 1);
            for (int i = 0; i < 128; ++i) std::printf("%d\\n", sum[i]);
        }
    }
    return 0;
}
'''


def q31_multiply(a, b):
    return min((a * b) >> 31, 0x7fffffff)


def q31_add(a, b):
    return max(-0x80000000, min(0x7fffffff, a + b))


def probe_reference(width, cross, gain, blocks, silent_from):
    """The probe's stage (probestage/engine.cpp) in Python, over the driver's input."""
    out, seed = [], 1
    for _track in range(2):
        line, write = [0] * 512, 0
        delay, cross_q, keep = 1 + (width >> 22), cross >> 1, 0x7fffffff - (cross >> 1)
        for block in range(blocks):
            words = []
            for _ in range(128):
                seed = (seed * 1103515245 + 12345) & 0xffffffff
                words.append(seed - (1 << 32) if seed & 0x80000000 else seed)
            if block >= silent_from:
                words = [0] * 128
            for i in range(64):
                left, right = words[2 * i], words[2 * i + 1]
                delayed = line[(write - delay) & 511]
                line[write & 511] = right
                write += 1
                out.append(q31_multiply(left, gain))
                out.append(q31_multiply(q31_add(q31_multiply(delayed, keep), q31_multiply(left, cross_q)), gain))
    return out


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class Host(unittest.TestCase):
    def test_the_runtime_resets_processes_sleeps_and_wakes(self):
        with tempfile.TemporaryDirectory() as folder:
            self.assertIn('track stage ok', host_run(self, folder, [ROOT / 'tests/fixtures/track_stage.cpp']))

    def test_the_probes_stage_matches_its_reference_through_the_adapter(self):
        m = fixture('probestage')
        for width, cross, gain in ((0x4ccccccd, 0x40000000, 0x7fffffff), (0x7fffffff, 0x7fffffff, 0x40000000), (0, 0, 0x7fffffff)):
            with self.subTest(width=width, cross=cross, gain=gain), tempfile.TemporaryDirectory() as directory:
                folder = Path(directory)
                generate_voice(folder, m)
                for name in ('core.h', 'engine.cpp'):
                    shutil.copy(FOLDER / 'probestage' / name, folder / name)
                driver = PROBE_DRIVER
                for key, value in (('WIDTH', width), ('CROSS', cross), ('GAIN', gain), ('BLOCKS', 24), ('SILENT_FROM', 12)):
                    driver = driver.replace('@%s@' % key, str(value))
                (folder / 'test.cpp').write_text(driver, encoding='utf-8')
                words = [int(v) for v in host_run(self, folder, [folder / 'test.cpp'], [folder]).split()]
                expected = probe_reference(width, cross, gain, 24, 12)
                self.assertEqual(words, expected)
                # Its tail: the delayed right channel still sounds once the input stops, and ends
                # within the 512 frames (8 blocks) it declares.
                track = expected[:24 * 128]
                self.assertTrue(any(track[12 * 128 + 1:13 * 128:2]))
                self.assertFalse(any(track[(12 + 8) * 128:]))

    def test_the_diagnostics_stage_run_times_every_block_on_a_state_of_its_own(self):
        # docs/profiling.md, opxy-synth-stage-v1: 1,500 timed blocks, sounding, its own state
        # released, no track's state touched.
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            generate_voice(folder, fixture('probestage'))
            for name in ('core.h', 'engine.cpp'):
                shutil.copy(FOLDER / 'probestage' / name, folder / name)
            driver = ('#include <cstdio>\n#include "voice.cpp"\n' + BENCHMARK_STUBS + '''
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index + 4; }
opxy::u32 sdk_voice_ticks() { return 0; }
int main() {
    for (auto& state : track_states) state.write = 7;
    const opxy::benchmark::Stats s = probestage_stage_benchmark();
    for (auto& state : track_states) if (state.write != 7) return 2;
    std::printf("%u %u %u %u %u %u\\n", s.count, s.maximum, s.minimum, s.total_low, s.audible ? 1u : 0u,
                allocations == 1 && releases == 1 ? 1u : 0u);
    return 0;
}''')
            (folder / 'test.cpp').write_text(driver, encoding='utf-8')
            self.assertEqual(host_run(self, folder, [folder / 'test.cpp'], [folder]).split(),
                             ['1500', '100', '100', '150000', '1', '1'])


@unittest.skipUnless(blackfin_compiler() and (CCES / 'System/chipfactory.exe').is_file(),
                     'Needs the Blackfin compiler and CCES')
class Simulator(unittest.TestCase):
    def test_the_shared_handler_keeps_a_stageless_tracks_call_on_the_bf707_simulator(self):
        result = run_simulator()
        # 256 blocks of tracks whose engines have no stage: the count, the sum and every
        # callee-saved register as from the engine's own call, the per-key reset once each.
        self.assertEqual((result['blocks'], result['mismatches']), (256, 0))
        self.assertEqual((result['resets'], result['processes']), (0, 0))
        # A stage's track: reset once, processed after the loop, its tail keeping the track on.
        self.assertEqual((result['stage_mismatches'], result['stage_resets'], result['stage_processes']), (0, 1, 4))
        # What the handler adds to a track without a stage, in sim cycles (no DDR model).
        self.assertLess(result['handler'] - result['direct'], 120)


def run_simulator(out=None):
    """Build and run the probe (tests/fixtures/track_call_sim); returns its comparison."""
    compiler = blackfin_compiler()

    def tool(name):
        return compiler.with_name('bfin-elf-' + name + compiler.suffix)
    flags = ['-mcpu=bf707', '-O2', '-std=gnu++11', '-ffreestanding', '-fno-exceptions', '-fno-rtti',
             '-fno-threadsafe-statics', '-fno-use-cxa-atexit', '-fno-asynchronous-unwind-tables', '-fno-unwind-tables',
             '-fno-common', '-fno-tree-loop-distribute-patterns', '-fomit-frame-pointer', '-nostdinc++',
             '-ffunction-sections', '-fdata-sections', f'-I{ROOT / "sdk/include"}', f'-I{PLATFORM}']
    with tempfile.TemporaryDirectory(prefix='track-call-sim-') as scratch:
        folder = Path(out or scratch)
        folder.mkdir(parents=True, exist_ok=True)

        def run(command, timeout=600):
            r = subprocess.run([str(c) for c in command], cwd=folder, capture_output=True, timeout=timeout, **HIDDEN)
            if r.returncode:
                raise AssertionError(f'{command[0]} failed:\n' + (r.stdout + r.stderr).decode(errors='replace')[-3000:])
            return r.stdout + r.stderr

        # The handler as built, its call of stock's loop sent to the stub.
        handler = (PLATFORM / 'track_call.s').read_text(encoding='utf-8')
        loop = '    P0.H = 0x8d51; P0.L = 0xc708;\n'
        if handler.count(loop) != 1:
            raise AssertionError('track_call.s no longer loads the loop address this probe replaces')
        (folder / 'track_call.s').write_text(handler.replace(loop, '    P0.H = stub_loop; P0.L = stub_loop;\n'))
        run([compiler, *flags, '-S', SIM / 'harness.cpp', '-o', 'harness_cpp.s'])
        run([compiler, *flags, '-S', PLATFORM / 'track_stage.cpp', '-o', 'track_stage.s'])
        for source, obj in (('harness_cpp.s', 'harness.o'), ('track_stage.s', 'stage.o'), ('track_call.s', 'call.o'),
                            (SIM / 'harness.s', 'glue.o')):
            run([tool('as'), '-mcpu=bf707', '-I', ROOT, source, '-o', obj])
        (folder / 'native.ld').write_text(
            'OUTPUT_ARCH(bfin)\nENTRY(_sim_probe)\nSECTIONS { .text 0x08050000 : { *(.text*) } .rodata : { *(.rodata*) } '
            '.data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } /DISCARD/ : { *(.comment .note* .eh_frame*) } }\n')
        libgcc = next(compiler.parent.parent.glob('lib/gcc/bfin-elf/*/libgcc.a'))
        run([tool('ld'), '--gc-sections', '-T', 'native.ld', '-o', 'native.elf', 'harness.o', 'stage.o', 'call.o',
             'glue.o', libgcc])
        run([tool('objcopy'), '-O', 'binary', 'native.elf', 'native.bin'])
        symbols = run([tool('objdump'), '-t', 'native.elf']).decode().replace('\r', '')
        entry = int(re.search(r'^([0-9a-f]{8}) .*\s_sim_probe$', symbols, re.M)[1], 16)
        blob = (folder / 'native.bin').read_bytes()
        blob += b'\0' * (len(blob) % 2)
        (folder / 'code.s').write_text('.section candidate;\n.global _candidate;\n_candidate:\n.byte2 candidate_bytes[]=' +
                                       ','.join(hex(v) for v in struct.unpack('<%dH' % (len(blob) // 2), blob)) + ';\n')
        (folder / 'probe.c').write_text('#include <stdio.h>\nstatic unsigned a[4096];\nint main(){unsigned n=((unsigned(*)(unsigned*))' +
                                        hex(entry) + ')(a),i;puts("COST_BEGIN");for(i=0;i<n;++i)printf("%08x\\n",a[i]);'
                                        'puts("COST_END");return 0;}\n')
        ldf = (CCES / 'Blackfin/ldf/ADSP-BF707.ldf').read_text()
        # The probe's own code and data stay clear of the stock addresses the code under test
        # uses: L1 data B from 0x11903400 (the track sum), L1 data C from 0x11b01400 (the
        # engine words).
        for old, new in (('START(0x08002000)', 'START(0x080a0000)'),
                         ('MEMORY {', 'MEMORY {\nMEM_CANDIDATE {START(0x08050000) END(0x0809ffff) TYPE(RAM) WIDTH(8)}\n'),
                         ('  SECTIONS {', '  SECTIONS {\ncandidate {INPUT_SECTIONS($OBJECTS(candidate))}>MEM_CANDIDATE\n'),
                         ('START(0x11B00000) END(0x11B01FFF)', 'START(0x11B00000) END(0x11B013FF)'),
                         ('START(0x11900000) END(0x11903FFF)', 'START(0x11900000) END(0x119033FF)'),
                         ('START(0x11900000) END(0x11907FFF)', 'START(0x11900000) END(0x119033FF)')):
            if old not in ldf:
                raise AssertionError('Unexpected CCES BF707 ldf: ' + old)
            ldf = ldf.replace(old, new, 1)
        (folder / 'probe.ldf').write_text(ldf)
        run([CCES / 'ccblkfn.exe', '-proc', 'ADSP-BF707', '-si-revision', '1.0', 'probe.c', 'code.s', '-T', 'probe.ldf',
             '-o', 'probe.dxe'])
        log = run([CCES / 'System/chipfactory.exe', '-proc', 'ADSP-BF707', '-sim-type', 'Functional-Sim', '-bound-exe-time',
                   '40960000000', '-exit-value', 'probe.dxe'], timeout=3000)
        found = re.search(rb'COST_BEGIN\s+(.*?)COST_END', log, re.S)
        if not found:
            raise AssertionError('No probe output:\n' + log.decode(errors='replace')[-2000:])
        words = [int(v, 16) for v in found[1].split()]
    i, k = words.index(0xaaaa0001), words.index(0xaaaa0002)
    return dict(blocks=words[i + 1], mismatches=words[i + 2], direct=words[i + 3], handler=words[i + 4],
                resets=words[i + 5], processes=words[i + 6], stage_cycles=words[k - 4:k],
                stage_mismatches=words[k + 1], stage_resets=words[k + 2], stage_processes=words[k + 3])


@unittest.skipUnless(test_service_links.TOOLS.joinpath('bfin-elf-g++' + ('.exe' if os.name == 'nt' else '')).is_file(),
                     'No Blackfin toolchain in local/toolchain (python sdk.py toolchain fetch, or set OPXY_TEST_COMPILER)')
class NativeLink(unittest.TestCase):
    """The pinned tools build the stage: the hook enters the handler the build's services choose."""
    compile = test_service_links.ServiceLinks.compile

    def site(self, report):
        return next(p for p in report['patches'] if p['address'] == SITE)

    def test_a_stage_alone_enters_the_shared_handler_without_the_sends(self):
        with tempfile.TemporaryDirectory(prefix='stage-alone-') as folder:
            out, report = self.compile(folder, FOLDER / 'probestage')
            self.assertTrue({'track_stage', 'track_call'} <= set(report['services']))
            self.assertNotIn('drum_sends', report['services'])
            self.assertIn('sdk_track_call', report['hooks'])
            self.assertNotIn('sdk_drum_send_track', report['hooks'])
            patch = self.site(report)
            self.assertEqual(decode_branch(int(SITE, 16), bytes.fromhex(patch['after'])),
                             (report['hooks']['sdk_track_call'], True))
            self.assertIn('probestage', report['audio_stack_bytes'])
            self.assertIn('probestage/record.o', report['objects'])
            self.assertIn('sdk/machine_tables.o', report['objects'])
            self.assertNotIn('sdk/track_stage_table.o', report['objects'])

    def test_a_stage_beside_a_pad_mix_kit_runs_both_from_the_shared_handler(self):
        with tempfile.TemporaryDirectory(prefix='stage-mix-') as folder:
            kit = test_service_links.kit_folder(Path(folder) / 'probekit', pad_mix=True)
            _out, report = self.compile(folder, kit, FOLDER / 'probestage')
            self.assertTrue({'track_stage', 'track_call', 'drum_sends'} <= set(report['services']))
            self.assertIn('sdk_track_call', report['hooks'])
            self.assertIn('sdk_drum_send_voice_hook', report['hooks'])
            self.assertNotIn('sdk_drum_send_track', report['hooks'])

    def test_a_pad_mix_kit_alone_keeps_its_own_handler(self):
        with tempfile.TemporaryDirectory(prefix='stage-none-') as folder:
            kit = test_service_links.kit_folder(Path(folder) / 'probekit', pad_mix=True)
            _out, report = self.compile(folder, kit, FOLDER / 'probesynth')
            self.assertNotIn('track_stage', report['services'])
            self.assertIn('sdk_drum_send_track', report['hooks'])
            self.assertNotIn('sdk_track_call', report['hooks'])
            self.assertFalse([name for name in report['objects'] if 'track_stage' in name or 'track_call' in name])


if __name__ == '__main__':
    unittest.main()
