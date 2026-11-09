"""The synth velocity hook: a note's velocity reaches an engine's note hook, on the
adapter and in the browser preview; an engine without the hook is unchanged."""
import copy
import os
import shutil
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

from tooling.adapters import generate_voice, generate_voice_preview
from tooling.preview import VOICE_EXPORTS, FLAGS, MEMORY
from tooling.preview_doctor import probe as preview_probe
from machine_fixtures import fixture
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}

_WASM = {}


def wasm_compiler():
    """A WASM compiler that builds and runs a module, or None; probed once,
    lazily, through the preview doctor (OPXY_TEST_PREVIEW_CXX, default clang++)."""
    if 'cxx' not in _WASM:
        node = shutil.which('node')
        if node is None:
            _WASM['cxx'] = None
        else:
            compiler = os.environ.get('OPXY_TEST_PREVIEW_CXX', 'clang++')
            try:
                preview_probe(compiler, node)
                _WASM['cxx'] = compiler
            except (ValueError, OSError):
                _WASM['cxx'] = None
    return _WASM['cxx']


# The same engine without the hook: its render writes a fixed level, so the
# adapter's fallback leaves a machine without one behaving as before.
PLAIN = '''#include "core.h"
namespace probevelocity {
inline void initialize(Voice& voice, Shared&, opxy::u32) { voice.velocity = 0; }
inline void start(Voice&, Shared&, opxy::u32) {}
inline void destroy(Voice&, Shared&, opxy::u32) {}
inline void render(Voice&, Shared&, opxy::u32, opxy::Output output,
                   opxy::Parameters, opxy::Pitch) {
    for (unsigned i = 0; i < opxy::block_frames; ++i)
        output[i] = static_cast<opxy::i32>(100u << 16);
}
}
'''
# An engine that counts its onsets: a start that ran before the hook leaves an
# unstored velocity, and the counts must meet one hook before every start.
COUNTED = '''#include "core.h"
namespace probevelocity {
unsigned note_calls, start_calls, unstored_starts;
inline void initialize(Voice& voice, Shared&, opxy::u32) { voice.velocity = 0; }
inline void start(Voice& voice, Shared&, opxy::u32) { ++start_calls; if (!voice.velocity) ++unstored_starts; }
inline void destroy(Voice&, Shared&, opxy::u32) {}
inline void note(Voice& voice, Shared&, opxy::u32 velocity) { voice.velocity = velocity; ++note_calls; }
inline void render(Voice& voice, Shared&, opxy::u32, opxy::Output output,
                   opxy::Parameters, opxy::Pitch) {
    for (unsigned i = 0; i < opxy::block_frames; ++i)
        output[i] = static_cast<opxy::i32>(voice.velocity << 16);
}
}
'''
# Drive the generated adapter as stock does: one note, started at two velocities.
# The driver is engine-agnostic: it prints the two rendered levels, and the test
# asserts its engine's regime.
DRIVER = r'''#include "voice.cpp"
#include <cstdio>
// Stock supplies the adapter's clock and the engine's title; stand in for both.
opxy::u32 sdk_voice_ticks() { return 0; }
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index + 4; }

namespace {
void* handle[8];
const void* const* table(unsigned index) {
    return *static_cast<const void* const* const*>(handle[index]);
}
int call(unsigned index, unsigned entry) {
    typedef int (*Method)(void*);
    return reinterpret_cast<Method>(const_cast<void*>(table(index)[entry]))(handle[index]);
}
// The widened stock note start: vtable entry 4 now carries the note and its
// velocity, as the drum constructor does.
int start(unsigned index, opxy::u32 note, opxy::u32 velocity) {
    typedef int (*Start)(void*, opxy::u32, opxy::u32);
    return reinterpret_cast<Start>(const_cast<void*>(table(index)[4]))(handle[index], note, velocity);
}
// The level a voice index renders: every sample equal, or -1 when output changes
// within the block, as in a fade.
opxy::i32 rendered(unsigned index) {
    typedef int (*Render)(void*, opxy::i32*, const opxy::i32*);
    opxy::i32 output[64], parameters[8] = {};
    reinterpret_cast<Render>(const_cast<void*>(table(index)[2]))(handle[index], output, parameters);
    for (opxy::i32 word : output)
        if (word != output[0]) return -1;
    return output[0] >> 16;
}
}

int main() {
    for (unsigned i = 0; i < 8; ++i) handle[i] = probevelocity_factory(i);
    if (!handle[0]) return 1;
    // Nothing sounds before a note starts.
    for (unsigned i = 0; i < 8; ++i)
        if (rendered(i) != 0) return 2;
    start(0, 60u, 100u);
    const opxy::i32 level1 = rendered(0);
    if (level1 == 0 || level1 == -1) return 3;
    // An in-place restart on the same handle, at the note's other velocity.
    start(0, 60u, 40u);
    const opxy::i32 level2 = rendered(0);
    if (level2 == 0 || level2 == -1) return 4;
    std::printf("%d %d\n", level1, level2);
    return 0;
}
'''
# Every voice busy, then notes that wait: the velocity of a waiting note's
# latest start is the one that begins when the voice it takes fades out, and a
# waiting note that ends never sounds.
WAITING = r'''#include "voice.cpp"
#include <cstdio>
opxy::u32 sdk_voice_ticks() { return 0; }
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index + 4; }

namespace {
void* handle[256];
const void* const* table(unsigned index) {
    return *static_cast<const void* const* const*>(handle[index]);
}
int start(unsigned index, opxy::u32 note, opxy::u32 velocity) {
    typedef int (*Start)(void*, opxy::u32, opxy::u32);
    return reinterpret_cast<Start>(const_cast<void*>(table(index)[4]))(handle[index], note, velocity);
}
void destroy(unsigned index) {
    typedef int (*Method)(void*);
    reinterpret_cast<Method>(const_cast<void*>(table(index)[0]))(handle[index]);
}
opxy::i32 rendered(unsigned index) {
    typedef int (*Render)(void*, opxy::i32*, const opxy::i32*);
    opxy::i32 output[64], parameters[8] = {};
    reinterpret_cast<Render>(const_cast<void*>(table(index)[2]))(handle[index], output, parameters);
    for (opxy::i32 word : output)
        if (word != output[0]) return -1;
    return output[0] >> 16;
}
}

int main() {
    for (unsigned i = 0; i < 256; ++i) handle[i] = probevelocity_factory(i);
    // Every voice sounds, and one more note waits at 100, then 40: the latest
    // velocity is the one that begins once the taken note has faded.
    for (unsigned i = 0; i < kVoices; ++i) start(i, 60u + i, 10u + i);
    const unsigned waiting = kVoices;
    start(waiting, 60u, 100u);
    start(waiting, 60u, 40u);
    if (rendered(0) != -1) return 1;  // the taken note fades out over its block
    const opxy::i32 promoted = rendered(waiting);
    if (promoted != 40) return 2;
    // A note that ends while it waits never sounds.
    const unsigned cancelled = kVoices + 1;
    start(cancelled, 61u, 77u);
    destroy(cancelled);
    if (rendered(cancelled) != 0) return 3;
    // The stock range reaches the hook whole, and a note on a fresh factory
    // object begins normally.
    start(2, 62u, 1u);
    if (rendered(2) != 1) return 4;
    start(2, 62u, 127u);
    if (rendered(2) != 127) return 5;
    handle[2] = probevelocity_factory(2);
    start(2, 62u, 2u);
    if (rendered(2) != 2) return 6;
    // One hook per start, each before it: the fading block took no hook, the
    // cancelled waiter none, and 9 + 1 + 3 notes began or restarted.
    std::printf("%d %u %u %u\n", promoted, probevelocity::note_calls,
                probevelocity::start_calls, probevelocity::unstored_starts);
    return 0;
}
'''


class VoiceVelocity(unittest.TestCase):
    def run_engine(self, engine=None, driver=DRIVER):
        """Compiles and runs a driver against the fixture's engine (engine None)
        or a test-local variant of it, and returns the printed tuple."""
        m = SimpleNamespace(kind='synth', definition=copy.deepcopy(fixture('probevelocity').definition),
                            prefix='probevelocity', native_name='probevelocity',
                            identity=fixture('probevelocity').identity)
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            generate_voice(folder, m)
            (folder / 'core.h').write_text((fixture('probevelocity').root / 'core.h').read_text(encoding='utf-8'),
                                           encoding='utf-8')
            if engine is None:
                (folder / 'engine.cpp').write_text((fixture('probevelocity').root / 'engine.cpp').read_text(encoding='utf-8'),
                                                   encoding='utf-8')
            else:
                (folder / 'engine.cpp').write_text(engine, encoding='utf-8')
            (folder / 'test.cpp').write_text(driver, encoding='utf-8')
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-Wno-unused-function',
                       *('-I' + str(path) for path in (ROOT / 'sdk/include', ROOT / 'sdk/platform/opxy-1.2.1', folder)),
                       str(folder / 'test.cpp'), '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, **HIDDEN)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60, **HIDDEN)
            self.assertEqual(result.returncode, 0, 'velocity driver: ' + result.stdout + result.stderr)
            return tuple(int(value) for value in result.stdout.split())

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_velocity_reaches_the_hook(self):
        # The hook fires on the begin path and on the in-place restart, and the
        # level follows the note's velocity.
        self.assertEqual(self.run_engine(), (100, 40))

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_without_the_hook_behaviour_is_unchanged(self):
        # Without the hook the fallback is a no-op: the fixed level holds for
        # every velocity, as before the hook existed.
        self.assertEqual(self.run_engine(PLAIN), (100, 100))
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_a_waiting_note_starts_with_its_latest_velocity(self):
        # Nine notes fill the pool; the tenth waits at 100, then 40. The fade
        # hands the voice over with the latest 40, a cancelled waiter never
        # sounds, the full 1..127 range reaches the hook, and every start
        # follows its hook.
        self.assertEqual(self.run_engine(COUNTED, WAITING), (40, 13, 13, 0))

    def test_preview_note_velocity_end_to_end(self):
        compiler = wasm_compiler()
        if compiler is None:
            self.skipTest('No working preview WASM compiler and Node (set OPXY_TEST_PREVIEW_CXX)')
        m = fixture('probevelocity')
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / 'core.h').write_text((m.root / 'core.h').read_text(encoding='utf-8'), encoding='utf-8')
            (folder / 'engine.cpp').write_text((m.root / 'engine.cpp').read_text(encoding='utf-8'), encoding='utf-8')
            shim = generate_voice_preview(folder, copy.deepcopy(m.definition))
            binary = folder / 'engine.wasm'
            command = [compiler, *FLAGS, '-I' + str(ROOT / 'sdk/include'), shim,
                       ROOT / 'sdk/platform/blackfin/memory.cpp', '-o', str(binary),
                       '-Wl,--no-entry', '-Wl,-z,stack-size=16384',
                       f'-Wl,--initial-memory={MEMORY}', f'-Wl,--max-memory={MEMORY}',
                       *('-Wl,--export=' + name for name in VOICE_EXPORTS)]
            result = subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=120, **HIDDEN)
            self.assertEqual(result.returncode, 0, 'Preview compilation failed:\n' + result.stdout + result.stderr)
            result = subprocess.run(['node', str(ROOT / 'tools/check_voice_velocity.mjs'), str(binary)],
                                    capture_output=True, text=True, timeout=120, **HIDDEN)
            self.assertEqual(result.returncode, 0, 'Preview velocity check:\n' + result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
