"""Heap voices: a synth whose voice pool exceeds the static limit keeps its voices in an
arena reserved from the stock heap at boot (tooling/voice_heap.py, voice_heap.hpp).

Placement follows the declared voice size and capacity alone. The host traces drive the
generated adapter both ways and require the same notes, steals, fades and releases."""
import copy
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from tooling import machines, voice_heap
from tooling.adapters import generate_registration, generate_voice
from tooling.definitions import validate_definition
from tooling.pipeline import ROOT, TOOLCHAIN_HOME
from machine_fixtures import FOLDER, fixture
from test_sample_services import HOST_CXX
import test_synth_release
import test_voices

HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
TOOLS = Path(os.environ.get('OPXY_TEST_COMPILER') or ROOT / TOOLCHAIN_HOME) / 'bin'
LIMIT = voice_heap.STATIC_VOICE_LIMIT


def synth(resources, sdk=voice_heap.VOICE_HEAP_SDK, prefix='probe', kind='synth'):
    """A probe synth with these resources, as the adapters read a machine."""
    definition = copy.deepcopy(fixture('probesynth').definition)
    definition['id'] = prefix
    definition['resources'].update(resources)
    validate_definition(definition, 'synth')
    return SimpleNamespace(kind=kind, definition=definition, prefix=prefix, native_name=prefix, id=prefix,
                           identity=dict(engine=5, page=85), display='drawing', sdk=sdk)


# A host heap for the arena: its role answer, a counted lock, and an allocator whose
# block is garbage-filled and one byte past alignment, so the arena must align and zero
# what it gets. HOST_HEAP alone defines it; HEAP_PRELUDE also reserves before main, as
# the audio role's registration does at boot.
HOST_HEAP = '''#include <cstring>
alignas(16) static unsigned char heap_pool[@BYTES@ + 32];
static opxy::u32 heap_requests, heap_requested, heap_locks, heap_unlocks, host_gui;
opxy::u32 stock_gui_role() { return host_gui; }
static opxy::u32 host_exists() { return 1; }
static void host_lock() { ++heap_locks; }
static void host_unlock() { ++heap_unlocks; }
static void* host_allocate(opxy::u32 bytes) {
    ++heap_requests;
    heap_requested = bytes;
    std::memset(heap_pool, 0xa5, sizeof(heap_pool));
    return heap_pool + 1;
}
static const opxy::platform::voice_heap::Heap host_heap = {stock_gui_role, host_exists, host_lock, host_allocate, host_unlock};
'''
HEAP_PRELUDE = HOST_HEAP + '''opxy::u32 heap_reserved = voice_arena.reserve(host_heap);
static Voice& voice_in(const Slot& slot) { return voice_of(slot); }
'''
STATIC_PRELUDE = '''opxy::u32 heap_reserved = 1;
static Voice& voice_in(const Slot& slot) { return const_cast<Slot&>(slot).voice; }
'''


def with_prelude(source, prelude):
    """A trace that includes voice.cpp, given its storage's prelude right after the include."""
    head = '#include "voice.cpp"\n'
    if source.count(head) != 1:
        raise AssertionError('The trace includes voice.cpp once')
    return source.replace(head, head + prelude)


class Placement(unittest.TestCase):
    def test_the_pool_of_min_capacity_27_plus_a_spare_decides(self):
        # 8 voices of 16384 bytes are exactly the limit: static. Four more bytes a voice: heap.
        self.assertEqual(voice_heap.pool_bytes(synth(dict(voiceBytes=16384, voiceCapacity=7)).definition['resources']), LIMIT)
        self.assertFalse(voice_heap.on_heap(synth(dict(voiceBytes=16384, voiceCapacity=7))))
        self.assertTrue(voice_heap.on_heap(synth(dict(voiceBytes=16388, voiceCapacity=7))))
        # Capacities above 27 keep 28 voices.
        for capacity in (27, 100, 256):
            m = synth(dict(voiceBytes=4096, voiceCapacity=capacity))
            self.assertEqual(voice_heap.pool_bytes(m.definition['resources']), 4096 * 28)
            self.assertFalse(voice_heap.on_heap(m))
        self.assertTrue(voice_heap.on_heap(synth(dict(voiceBytes=4684, voiceCapacity=256))))
        self.assertEqual(voice_heap.arena_bytes(synth(dict(voiceBytes=16400, voiceAlignment=16, voiceCapacity=7))),
                         16400 * 8 + 15)

    def test_only_synths_declaring_the_heap_version_move(self):
        resources = dict(voiceBytes=65536, voiceCapacity=8)
        self.assertTrue(voice_heap.on_heap(synth(resources)))
        # An older machine builds as it always has, in module memory.
        self.assertFalse(voice_heap.on_heap(synth(resources, sdk=(1, 17))))
        self.assertFalse(voice_heap.on_heap(synth(resources, kind='drum')))
        self.assertGreaterEqual(machines.SDK_VERSION, voice_heap.VOICE_HEAP_SDK)

    def test_the_fixture_probes(self):
        self.assertTrue(voice_heap.on_heap(fixture('voiceheap')))
        self.assertGreater(voice_heap.pool_bytes(fixture('voiceheap').definition['resources']), LIMIT)
        for name in ('probesynth', 'probesources'):
            self.assertFalse(voice_heap.on_heap(fixture(name)))

    def test_a_static_synth_keeps_its_adapter_text(self):
        with tempfile.TemporaryDirectory() as folder:
            for resources in (dict(voiceBytes=16384, voiceCapacity=7), dict(voiceBytes=65536, voiceCapacity=8, releaseCallback=1)):
                for sdk in ((1, 17), voice_heap.VOICE_HEAP_SDK):
                    m = synth(resources, sdk=sdk)
                    if voice_heap.on_heap(m):
                        continue
                    voice = generate_voice(folder, m).read_text(encoding='utf-8')
                    registration = generate_registration(folder, m).read_text(encoding='utf-8')
                    self.assertIn('struct Slot { Voice voice; opxy::u32 owner', voice)
                    self.assertIn('probe::render(slot->voice,', voice)
                    self.assertIn('opxy::platform::zero_voice(slot.voice);', voice)
                    for marker in ('voice_heap', 'voice_arena', 'voice_of'):
                        self.assertNotIn(marker, voice + registration)
                    self.assertIn('int probe_register() { return sdk_register_engine(&record); }', registration)

    def test_a_heap_synth_reserves_as_it_registers(self):
        with tempfile.TemporaryDirectory() as folder:
            m = synth(dict(voiceBytes=65536, voiceCapacity=8, releaseCallback=1))
            voice = generate_voice(folder, m).read_text(encoding='utf-8')
            registration = generate_registration(folder, m).read_text(encoding='utf-8')
        self.assertIn('struct Slot { opxy::u32 owner', voice)
        self.assertNotIn('slot.voice', voice)
        self.assertNotIn('slot->voice', voice)
        self.assertIn('== %d, "Heap voice arena' % (65536 * 9 + 3), voice)
        self.assertIn('if (!voice_arena.voices) return 0;', voice)
        self.assertIn('opxy::platform::zero_voice(voice_of(slot));', voice)
        self.assertIn('return voice_arena.reserve(opxy::platform::voice_heap::kStockHeap); }', voice)
        self.assertNotIn('@', voice)
        self.assertIn('probe::release(voice_of(*slot), shared, slot->owner);', voice)
        self.assertIn('int probe_register() { return (probe_voice_heap_reserve(), sdk_register_engine(&record)); }',
                      registration)


class Budget(unittest.TestCase):
    def machine(self, name, **resources):
        m = fixture(name)
        m.definition['resources'].update(resources)
        return m

    def test_heap_voices_have_their_own_megabyte(self):
        self.assertEqual(voice_heap.VOICE_HEAP_LIMIT, 1024 * 1024)
        heap = self.machine('voiceheap')
        # Apart from the analysis allowance: both may be full at once.
        voice_heap.check_budget([heap, fixture('probesynth'), self.machine('probesample', sampleAnalysis=2 * 1024 * 1024)])
        # 1 MiB of voices exactly (+ 3 bytes of alignment slack) is refused; 4 bytes a voice less fits.
        fits = self.machine('voiceheap', voiceCapacity=27, voiceBytes=37448)
        self.assertLessEqual(voice_heap.arena_bytes(fits), voice_heap.VOICE_HEAP_LIMIT)
        voice_heap.check_budget([fits])
        over = self.machine('voiceheap', voiceCapacity=27, voiceBytes=37452)
        with self.assertRaisesRegex(ValueError, r'Heap voices \(voiceheap: 1048659 bytes\) exceed the 1048576 bytes'):
            voice_heap.check_budget([over])

    def test_the_megabyte_counts_every_heap_synth(self):
        many = [self.machine('voiceheap') for _ in range(7)]
        voice_heap.check_budget(many)  # 7 x 147,567 bytes
        with self.assertRaisesRegex(ValueError, r'exceed the 1048576 bytes'):
            voice_heap.check_budget(many + [self.machine('voiceheap')])

    def test_a_collection_with_heap_voices_checks_its_budget(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / 'voiceheap'
            shutil.copytree(FOLDER / 'voiceheap', root)
            definition = json.loads((root / 'definition.json').read_text(encoding='utf-8'))
            machines.collection([{'path': str(root)}])
            definition['resources'].update(voiceCapacity=200, voiceBytes=40012)
            (root / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
            core = (root / 'core.h').read_text(encoding='utf-8').replace('kEchoFrames = 4096', 'kEchoFrames = 10000')
            (root / 'core.h').write_text(core, encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'exceed the 1048576 bytes of stock heap'):
                machines.collection([{'path': str(root)}])


@unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
class HostTraces(unittest.TestCase):
    def build(self, m, core, engine, source, heap, prelude=None, idle=False):
        """Generates m's adapter with its voices on the heap or in module memory (and the
        idle capability's pieces if idle), compiles source beside it and returns the
        program's output."""
        limit = 0 if heap else 1 << 30
        with mock.patch.object(voice_heap, 'STATIC_VOICE_LIMIT', limit), tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            self.assertEqual(voice_heap.on_heap(m), heap)
            generate_voice(folder, m, idle=idle)
            if prelude is None:
                prelude = HEAP_PRELUDE.replace('@BYTES@', str(voice_heap.arena_bytes(m))) if heap else STATIC_PRELUDE
            (folder / 'core.h').write_text(core, encoding='utf-8')
            (folder / 'engine.cpp').write_text(engine, encoding='utf-8')
            (folder / 'test.cpp').write_text(with_prelude(source, prelude), encoding='utf-8')
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-Wno-unused-function',
                       *('-I' + str(path) for path in (ROOT / 'sdk/include', ROOT / 'sdk/platform/opxy-1.2.1', folder)),
                       str(folder / 'test.cpp'), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **HIDDEN)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=120, **HIDDEN)
            self.assertEqual(result.returncode, 0, 'trace step %d failed' % result.returncode)
            return result.stdout

    def test_steals_fades_and_releases_trace_as_with_static_voices(self):
        # The adapter's own trace (synth_voice.cpp: steals, fades, waits, long random runs).
        trace = (ROOT / 'tests/fixtures/synth_voice.cpp').read_text(encoding='utf-8')
        old = 'total += slot.voice.initialized;'
        self.assertEqual(trace.count(old), 1)
        trace = trace.replace(old, 'total += voice_in(slot).initialized;')
        definition = copy.deepcopy(fixture('probesynth').definition)
        definition['id'] = 'probe'
        for i in range(4):
            parameter = copy.deepcopy(definition['parameters'][i])
            parameter.update(id='extra' + str(i), label='Extra ' + str(i), control=i)
            definition['parameters'].append(parameter)
            for preset in definition['presets'].values():
                preset[parameter['id']] = parameter['default']
        definition['resources'].update(voiceBytes=12, voiceAlignment=4, voiceCapacity=2, sharedBytes=0)
        validate_definition(definition, 'synth')
        m = SimpleNamespace(kind='synth', definition=definition, prefix='probe', native_name='probe',
                            identity=dict(engine=5, page=85), sdk=voice_heap.VOICE_HEAP_SDK)
        for heap in (False, True):
            with self.subTest(heap=heap):
                self.build(m, test_voices.CORE, test_voices.ENGINE, trace, heap)
        # The release callback's lifecycle (test_synth_release), voices read through voice_in.
        native, count = re.subn(r'(held\(static_cast<Handle\*>\(h\)\)|started)->voice\.', r'voice_in(*\1).',
                                test_synth_release.NATIVE)
        self.assertEqual(count, 4)
        release = synth(dict(voiceBytes=16, voiceAlignment=4, voiceCapacity=1, sharedBytes=12, releaseCallback=1))
        for heap in (False, True):
            with self.subTest(release=True, heap=heap):
                self.build(release, test_synth_release.CORE, test_synth_release.ENGINE, native, heap)

    def test_the_idle_cut_follows_a_heap_voice_as_a_static_one(self):
        # The idle capability's follower asks the engine's settled() of the held voice,
        # wherever it lives (test_voices' settle trace, both ways).
        m = synth(dict(voiceBytes=12, voiceAlignment=4, voiceCapacity=4, sharedBytes=0), sdk=machines.IDLE_SDK)
        with mock.patch.object(voice_heap, 'STATIC_VOICE_LIMIT', 0), tempfile.TemporaryDirectory() as folder:
            voice = generate_voice(folder, m, idle=True).read_text(encoding='utf-8')
        self.assertIn('sdk_settles::query(voice_of(*slot), shared)', voice)
        self.assertNotIn('slot->voice', voice)
        for heap in (False, True):
            with self.subTest(heap=heap):
                self.build(m, test_voices.CORE, test_voices.SETTLE_ENGINE, test_voices.SETTLE_DRIVER, heap, idle=True)

    ARENA = r'''#include "voice.cpp"
int destroyed[257];
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index; }
opxy::u32 sdk_voice_ticks() { return 0; }
int main() {
    // Boot reserved once: the voices plus room to align, aligned and zeroed over garbage.
    if (heap_reserved != 1 || probe_voice_heap_status() != 1 || heap_requests != 1) return 1;
    if (heap_locks != 1 || heap_unlocks != 1) return 8;
    if (heap_requested != sizeof(Voice) * kVoices + alignof(Voice) - 1) return 2;
    if (reinterpret_cast<unsigned char*>(voice_arena.voices) != heap_pool + 16) return 3;
    for (unsigned i = 0; i < sizeof(Voice) * kVoices; ++i)
        if (heap_pool[16 + i]) return 4;
    if (heap_pool[16 + sizeof(Voice) * kVoices] != 0xa5) return 5;
    // A second reservation keeps the first.
    if (voice_arena.reserve(host_heap) != 1 || heap_requests != 1) return 6;
    // Each slot's voice is its own, in order.
    for (unsigned i = 0; i < kVoices; ++i)
        if (&voice_of(voices[i]) != voice_arena.voices + i) return 7;
    return 0;
}
'''

    def test_the_arena_is_reserved_once_aligned_and_zeroed(self):
        core = test_voices.CORE.replace('struct Voice {', 'struct alignas(16) Voice {').replace(
            'opxy::u32 owner, notes, initialized;', 'opxy::u32 owner, notes, initialized, pad;')
        m = synth(dict(voiceBytes=16, voiceAlignment=16, voiceCapacity=2, sharedBytes=0))
        self.build(m, core, test_voices.ENGINE, self.ARENA, True)

    ROLES = r'''#include "voice.cpp"
int destroyed[257];
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index; }
opxy::u32 sdk_voice_ticks() { return 0; }
int main() {
    // Registering on the GUI role reserves nothing and takes no lock, however often.
    host_gui = 1;
    for (int i = 0; i < 3; ++i)
        if (voice_arena.reserve(host_heap) != 0 || heap_locks || heap_requests || voice_arena.voices) return 1;
    // The first registration on the audio role reserves, once.
    host_gui = 0;
    if (voice_arena.reserve(host_heap) != 1 || heap_requests != 1 || heap_locks != 1 || heap_unlocks != 1) return 2;
    // Later registrations on either role keep it, without the lock.
    for (opxy::u32 i = 0; i < 4; ++i) {
        host_gui = i % 2;
        if (voice_arena.reserve(host_heap) != 1 || heap_requests != 1 || heap_locks != 1) return 3;
    }
    return 0;
}
'''

    RACE = r'''#include "voice.cpp"
int destroyed[257];
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index; }
opxy::u32 sdk_voice_ticks() { return 0; }
static unsigned interleaved;
// Taking the lock, this caller finds the other caller has reserved in between.
static void racing_lock() {
    if (interleaved++ == 0) {
        const opxy::platform::voice_heap::Heap other = {stock_gui_role, host_exists, host_lock, host_allocate, host_unlock};
        voice_arena.reserve(other);
    }
    ++heap_locks;
}
int main() {
    const opxy::platform::voice_heap::Heap racing = {stock_gui_role, host_exists, racing_lock, host_allocate, host_unlock};
    // The status is checked again under the lock: one allocation for two callers.
    if (voice_arena.reserve(racing) != 1 || heap_requests != 1 || heap_locks != 2 || heap_unlocks != 2) return 1;
    return 0;
}
'''

    UNCREATED = r'''#include "voice.cpp"
int destroyed[257];
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index; }
opxy::u32 sdk_voice_ticks() { return 0; }
static opxy::u32 absent() { return 0; }
int main() {
    // Before stock creates its heap (a zero mutex word), the arena is refused without
    // taking the lock or allocating, and never asked again.
    const opxy::platform::voice_heap::Heap uncreated = {stock_gui_role, absent, host_lock, host_allocate, host_unlock};
    if (voice_arena.reserve(uncreated) != 2 || heap_locks || heap_requests || voice_arena.voices) return 1;
    if (voice_arena.reserve(host_heap) != 2 || heap_locks || heap_requests) return 2;
    return 0;
}
'''

    def test_only_the_first_audio_role_registration_reserves(self):
        m = synth(dict(voiceBytes=12, voiceAlignment=4, voiceCapacity=2, sharedBytes=0))
        prelude = HOST_HEAP.replace('@BYTES@', str(voice_heap.arena_bytes(m)))
        for source in (self.ROLES, self.RACE, self.UNCREATED):
            with self.subTest(source=source[source.index('int main'):][:40]):
                self.build(m, test_voices.CORE, test_voices.ENGINE, source, True, prelude=prelude)

    REFUSED = r'''#include "voice.cpp"
int destroyed[257];
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index; }
opxy::u32 ticks = 1000;
opxy::u32 sdk_voice_ticks() { return ticks; }
static unsigned refusals;
opxy::u32 stock_gui_role() { return 0; }
static opxy::u32 exists() { return 1; }
static void nothing() {}
static void* refuse(opxy::u32) { ++refusals; return nullptr; }
static const opxy::platform::voice_heap::Heap refusing = {stock_gui_role, exists, nothing, refuse, nothing};
typedef int (*Method)(void*);
typedef int (*Render)(void*, opxy::i32*, const opxy::i32*);
static int method(void* handle, unsigned entry) {
    const void* const* table = *static_cast<const void* const* const*>(handle);
    return reinterpret_cast<Method>(const_cast<void*>(table[entry]))(handle);
}
// Notes on many indices, rendered for blocks: every output word silent, no voice owned.
static int silent_run(unsigned seed) {
    opxy::i32 output[64], parameters[8] = {};
    void* handle[256];
    for (unsigned n = 0; n < 400; ++n) {
        seed = seed * 1103515245u + 12345u;
        const unsigned index = (seed >> 8) % 256;
        handle[index] = probe_factory(index);
        if (method(handle[index], 4)) return 1;
        for (unsigned b = 0; b < 3; ++b) {
            for (opxy::i32& word : output) word = -7;
            const void* const* table = *static_cast<const void* const* const*>(handle[index]);
            reinterpret_cast<Render>(const_cast<void*>(table[2]))(handle[index], output, parameters);
            for (opxy::i32 word : output) if (word) return 2;
            ticks += 5;
        }
        if (n % 7 == 0) method(handle[index], 0);
    }
    for (const Slot& slot : voices) if (slot.owner) return 3;
    for (int count : destroyed) if (count) return 4;
    return 0;
}
int main() {
    // Before boot reserves, nothing sounds and no note takes a voice.
    if (probe_voice_heap_status() != 0) return 10;
    if (int failed = silent_run(1)) return 10 + failed;
    // The heap refuses: the status says so, and it is never asked again.
    if (voice_arena.reserve(refusing) != 2 || probe_voice_heap_status() != 2 || voice_arena.voices) return 20;
    if (voice_arena.reserve(refusing) != 2 || refusals != 1) return 21;
    if (int failed = silent_run(2)) return 30 + failed;
    return 0;
}
'''

    def test_a_refused_arena_leaves_the_synth_silent_with_a_status(self):
        m = synth(dict(voiceBytes=12, voiceAlignment=4, voiceCapacity=2, sharedBytes=0))
        # No reservation before main: the trace plays as before boot reserves, then as the heap refused.
        self.build(m, test_voices.CORE, test_voices.ENGINE, self.REFUSED, True, prelude='')

    AUDIO = r'''#include "voice.cpp"
#include <cstdio>
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index; }
unsigned long long cycles = 1ull << 40;
opxy::u32 sdk_voice_ticks() { return static_cast<opxy::u32>(cycles >> 17); }
typedef int (*Method)(void*);
typedef int (*Render)(void*, opxy::i32*, const opxy::i32*);
int main() {
    if (heap_reserved != 1) return 1;
    // Twelve notes sound at a time over nine voices: notes end, take free voices and steal.
    const opxy::i32 parameters[8] = {0x30000000, 0x50000000, 0x7fffffff, 0x20000000};
    void* handle[256];
    unsigned sounding[12], count = 0, next = 0;
    unsigned long long sum = 0;
    for (unsigned block = 0; block < 600; ++block) {
        if (block % 5 == 0) {
            const unsigned index = (next++ * 37 + 11) % 256;
            if (count == 12) { for (unsigned i = 1; i < count; ++i) sounding[i - 1] = sounding[i]; --count; }
            sounding[count++] = index;
            handle[index] = voiceheap_factory(index);
            const void* const* table = *static_cast<const void* const* const*>(handle[index]);
            reinterpret_cast<Method>(const_cast<void*>(table[4]))(handle[index]);
        }
        for (unsigned i = 0; i < count; ++i) {
            opxy::i32 output[64];
            void* h = handle[sounding[i]];
            const void* const* table = *static_cast<const void* const* const*>(h);
            reinterpret_cast<Render>(const_cast<void*>(table[2]))(h, output, parameters);
            for (opxy::i32 word : output) { std::printf("%ld\n", static_cast<long>(word)); sum += word < 0 ? -word : word; }
        }
        cycles += 580352;
    }
    return sum ? 0 : 2;
}
'''

    def test_the_probe_sounds_the_same_from_the_heap(self):
        # The voiceheap probe, static and heap, renders the same words for the same notes.
        m = fixture('voiceheap')
        engine = (FOLDER / 'voiceheap/engine.cpp').read_text(encoding='utf-8')
        # The host has no stock pitch buffer: a fixed increment stands in for it.
        self.assertEqual(engine.count('pitch[i]'), 1)
        engine = engine.replace('pitch[i]', '0x0147ae14u')
        core = (FOLDER / 'voiceheap/core.h').read_text(encoding='utf-8')
        outputs = [self.build(m, core, engine, self.AUDIO, heap) for heap in (False, True)]
        self.assertGreater(len(outputs[0]), 100000)
        self.assertEqual(outputs[0], outputs[1])


@unittest.skipUnless((TOOLS / ('bfin-elf-g++' + ('.exe' if os.name == 'nt' else ''))).is_file(),
                     'No Blackfin toolchain in local/toolchain (python sdk.py toolchain fetch, or set OPXY_TEST_COMPILER)')
class NativeLink(unittest.TestCase):
    def test_two_heap_synths_link_beside_a_static_one(self):
        with tempfile.TemporaryDirectory(prefix='voice-heap-') as folder:
            second = Path(folder) / 'voiceheapb'
            shutil.copytree(FOLDER / 'voiceheap', second)
            for name in ('definition.json', 'core.h', 'engine.cpp', 'display.h', 'display.cpp', 'panel.svg.fragment'):
                path = second / name
                text = path.read_text(encoding='utf-8').replace('voiceheap', 'voiceheapb')
                path.write_text(text.replace('"Voice Heap"', '"Voice Heap B"'), encoding='utf-8')
            out = Path(folder) / 'module'
            result = subprocess.run([sys.executable, str(ROOT / 'sdk.py'), 'compile', '--machine', str(FOLDER / 'probesynth'),
                                     '--machine', str(FOLDER / 'voiceheap'), '--machine', str(second),
                                     '--toolchain', str(TOOLS), '--compiler', str(TOOLS.parent), '--out', str(out)],
                                    capture_output=True, text=True, timeout=1200, cwd=ROOT, **HIDDEN)
            self.assertEqual(result.returncode, 0, result.stdout[-4000:] + result.stderr[-4000:])
            report = json.loads((out / 'compile-report.json').read_text(encoding='utf-8'))
            pool = voice_heap.pool_bytes(fixture('voiceheap').definition['resources'])
            self.assertEqual(report['heap_voices'], {'voiceheap': pool + 3, 'voiceheapb': pool + 3})
            self.assertIn('Heap voices, reserved from the stock heap at boot: voiceheap 144.1 KiB', result.stdout)
            for name in ('voiceheap', 'voiceheapb'):
                with self.subTest(machine=name):
                    # Module memory holds the bookkeeping and handles, not the voices.
                    self.assertLess(report['memory'][name]['state_bytes'], pool // 10)
            objdump = TOOLS / ('bfin-elf-objdump' + ('.exe' if os.name == 'nt' else ''))
            symbols = subprocess.run([str(objdump), '-t', str(out / 'module.elf')], capture_output=True, text=True,
                                     **HIDDEN).stdout
            for name in ('voiceheap', 'voiceheapb'):
                self.assertRegex(symbols, rf'(?m) g +F \.text\s+\S+ _?{name}_voice_heap_reserve$')
            self.assertNotIn('probesynth_voice_heap', symbols)


if __name__ == '__main__':
    unittest.main()
