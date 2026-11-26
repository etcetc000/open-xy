"""Check machine and extension folders anywhere, as their repositories' CI does.

For each machine: its records and sources against this SDK, a browser preview
build of a synth or drum kit, its own host tests (tests/*.cpp, compiled against the SDK
headers and run) and an estimate of its CPU cost next to Open-XY's core
machines'. For each extension: its extension.json and sources against the SDK's
registry and source rules, and its own host tests, which it must have.
Optionally a native compile of every folder together. No stock files and no device.
"""
import argparse
from concurrent.futures import ProcessPoolExecutor
import contextlib
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from . import cpu
from .formats import require
from .machines import machine_at
from .native import check_sources, host_units
from .pipeline import NO_WINDOW, ROOT


def host_tests(m, host_cxx, scratch):
    """Compiles and runs each tests/*.cpp of a machine or extension; its own checks."""
    tests = sorted((m.root / 'tests').glob('*.cpp'))
    for test in tests:
        binary = scratch / (m.id + '-' + test.stem + ('.exe' if os.name == 'nt' else ''))
        # The folder's include paths, as its preview and native builds use them.
        includes = [ROOT / 'sdk/include', m.root, *(m.root / extra for extra in getattr(m, 'includes', ()))]
        command = [host_cxx, '-std=c++11', '-O2', '-Wall', '-Werror', *('-I' + str(path) for path in includes),
                   # Only a drum kit's tests link its extra sources: other machines' tests
                   # include what they need, and linking them as well defines it twice.
                   str(test), *(str(m.root / source) for source in (m.sources if getattr(m, 'kind', None) == 'drum' else ())),
                   # The host build of an SDK unit the machine's own C++ uses (the sample kernel).
                   *(str(path) for path in host_units(m)),
                   '-o', str(binary)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=300, creationflags=NO_WINDOW)
        require(result.returncode == 0, f'{m.id}: {test.name} does not compile:\n{result.stdout}{result.stderr}')
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=300, creationflags=NO_WINDOW)
        require(result.returncode == 0, f'{m.id}: {test.name} failed:\n{result.stdout}{result.stderr}')
    return len(tests)


def drum_storage(m, host_cxx, scratch):
    """A drum kit's models, as its parameter_count names them, fit its storage: one of
    more than 16 models holds at most 15 that use all eight controls (drum_storage.hpp)."""
    count = m.definition['resources']['modelCount']
    probe = scratch / (m.id + '-drum-storage.cpp')
    probe.write_text(f'''#include "core.h"
#include "engine.cpp"
#include <cstdio>
int main() {{
    unsigned full = 0, over = 0;
    for (opxy::u32 model = 0; model < {count}; ++model) {{
        const opxy::u32 controls = {m.definition['id']}::parameter_count(model);
        full += controls == 8;
        over += controls > 8;
    }}
    std::printf("%u %u\\n", full, over);
    return 0;
}}
''', encoding='utf-8')
    binary = scratch / (m.id + '-drum-storage' + ('.exe' if os.name == 'nt' else ''))
    includes = [ROOT / 'sdk/include', m.root, *(m.root / extra for extra in getattr(m, 'includes', ()))]
    command = [host_cxx, '-std=c++11', '-O2', *('-I' + str(path) for path in includes), str(probe),
               *(str(m.root / source) for source in m.sources), '-o', str(binary)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=300, creationflags=NO_WINDOW)
    require(result.returncode == 0, f'{m.id}: its models\' control counts do not compile:\n{result.stdout}{result.stderr}')
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60, creationflags=NO_WINDOW)
    full, over = map(int, result.stdout.split())
    require(not over, f'{m.id}: {over} models claim more than eight controls')
    require(count <= 16 or full <= 15,
            f'{m.id}: {full} of its {count} models use all eight controls; a kit of more than 16 models '
            'stores at most 15 such (docs/api.md, synthesis drum kits)')
    return full


def drawn_player(m, host_cxx, scratch):
    """A note player's own drawn page (SDK 1.26): its artwork against its declared display,
    then its display on the host with player.cpp, as the device unit compiles them, over a
    held key's frames: every frame valid. Returns the frames drawn."""
    from .adapters import catalog_defaults, drawing_layout, player_instrument
    from .native import PLAYER_INSTRUMENT_DEFINE, validate_panel
    validate_panel(m)
    display = m.definition['resources']['display']
    layout = drawing_layout(display)
    words = ', '.join(str(word << 16) for word in catalog_defaults(m.definition))
    paged = 'true' if m.definition.get('press') == 'page' else 'false'
    probe = scratch / (m.id + '-drawn-page.cpp')
    probe.write_text(f'''// Generated by sdk.py test: {m.id}'s own page over a held key's frames.
#include "player.cpp"
#include "display.cpp"
#include <cstdio>
namespace {{
typedef opxy::draw::Frame<{layout['PATHS']}, {layout['POINTS']}, {layout['NODES']}, {layout['TEXTS']}, {layout['PALETTE']}> Frame;
template<class S> auto next_update(S& s, const opxy::player::Controls& c, opxy::player::Update& u, int)
    -> decltype(update(s, c, u)) {{ return update(s, c, u); }}
template<class S> bool next_update(S&, const opxy::player::Controls&, opxy::player::Update&, long) {{ return false; }}
{m.definition['id']}::State state;
{m.definition['id']}::DisplayState display_state;
Frame frame;
// As the device unit asserts them (player_drawing.cpp.in).
static_assert(sizeof({m.definition['id']}::DisplayState) == {display['stateBytes']}, "Declared display size differs from C++ type");
static_assert(alignof({m.definition['id']}::DisplayState) == {display['stateAlignment']}, "Declared display alignment differs from C++ type");
static_assert(__is_trivial({m.definition['id']}::DisplayState) && __is_standard_layout({m.definition['id']}::DisplayState),
              "Display state must be trivial standard-layout data");
}}
int main() {{
    using namespace {m.definition['id']};
    opxy::player::Controls controls = {{}};
    const opxy::i32 defaults[8] = {{{words}}};
    for (int i = 0; i < 8; ++i) controls.value[i] = defaults[i];
    controls.lowest_key = 53;
    controls.bpm_x10 = 1200;
    display_reset(display_state);
    opxy::draw::Input input = {{}};
    input.elapsed = 17;
    input.held_notes = opxy::draw::notes_unavailable;
    input.amp.attack = input.amp.decay = input.amp.sustain = input.amp.release = opxy::draw::envelope_unavailable;
    int invalid = 0;
    for (int n = 0; n < 720; ++n) {{
        opxy::player::Plan plan;
        if (n == 30) press(state, controls, 60, 100, plan);
        if (n == 600) release(state, 60);
        // 120 BPM at 60 frames a second: 8 player steps a frame.
        opxy::player::Update change;
        for (int step = 0; step < 8; ++step)
            for (int k = 0; k < 8 && next_update(state, controls, change, 0); ++k) {{}}
        const unsigned row = {paged} && n >= 360 && n < 480 ? 4 : 0;
        for (unsigned i = 0; i < 8; ++i) input.slots[i] = controls.value[i];
        for (unsigned i = 0; i < 4; ++i) {{
            input.shown[i] = static_cast<opxy::u8>(row + i);
            input.parameters[i] = controls.value[row + i];
        }}
        display(display_state, static_cast<const State&>(state), static_cast<const opxy::player::Controls&>(controls),
                static_cast<const opxy::draw::Input&>(input), frame);
        invalid += !opxy::draw::valid(frame);
    }}
    std::printf("%d\\n", invalid);
    return 0;
}}
''', encoding='utf-8')
    binary = scratch / (m.id + '-drawn-page' + ('.exe' if os.name == 'nt' else ''))
    # The device unit's warnings, and its Controls.instrument (every drawn player declares SDK 1.25 or later).
    command = [host_cxx, '-std=c++11', '-O2', '-Wall', '-Werror', '-Wno-unused-variable',
               *((PLAYER_INSTRUMENT_DEFINE,) if player_instrument(m) else ()),
               '-I' + str(ROOT / 'sdk/include'), '-I' + str(m.root), str(probe), '-o', str(binary)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=300, creationflags=NO_WINDOW)
    require(result.returncode == 0, f'{m.id}: its own page does not compile with player.cpp, as the device unit '
            f'compiles it (display(DisplayState&, const State&, const player::Controls&, const draw::Input&, Frame&)):\n'
            f'{result.stdout}{result.stderr}')
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60, creationflags=NO_WINDOW)
    require(result.returncode == 0, f'{m.id}: its own page stopped on the host:\n{result.stdout}{result.stderr}')
    invalid = int(result.stdout.split()[0])
    require(not invalid, f'{m.id}: {invalid} of its 720 frames are invalid (opxy::draw::valid); the device skips them')
    return 720


def check_machine(root, cxx, host_cxx, no_cpu, scratch):
    """A machine's preview build, host tests and CPU estimate; its report."""
    m = machine_at(root)
    scratch.mkdir(parents=True)
    done = [f'{m.kind} {m.id} ({m.root})']
    if m.kind in ('synth', 'drum'):
        from .preview import build
        build(cxx, m, scratch / ('preview-' + m.id))
        done.append('preview built')
    if m.kind == 'player' and m.display == 'drawing':
        frames = drawn_player(m, host_cxx, scratch)
        done.append(f'own page drew {frames} valid frames')
    if m.kind == 'drum':
        full = drum_storage(m, host_cxx, scratch)
        done.append(f'{full} of {m.definition["resources"]["modelCount"]} models use all eight controls')
    count = host_tests(m, host_cxx, scratch)
    done.append(f'{count} host test{"s" * (count != 1)} passed')
    report = 'PASS: ' + '; '.join(done)
    return report if no_cpu else report + '\n' + cpu.summary(m, host_cxx, scratch)


def check_extension(root, host_cxx, scratch):
    """An extension's host tests, which it must have; its report."""
    from .extensions import extension_at
    e = extension_at(root)
    if not e.linked:
        return f'PASS: settings extension {e.id} ({e.root}): capabilities {", ".join(e.capabilities)}; no code to test'
    scratch.mkdir(parents=True)
    count = host_tests(e, host_cxx, scratch)
    require(count, f'{e.id}: an extension carries host tests in tests/*.cpp')
    return f'PASS: extension {e.id} ({e.root}); {count} host test{"s" * (count != 1)} passed'


def captured(work):
    """Runs one check in a worker process: what it printed and reported, and its error if it failed."""
    check, *rest = work
    output = io.StringIO()
    with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
        try:
            print(check(*rest))
            return output.getvalue(), None
        except (ValueError, OSError, KeyError, TypeError, ET.ParseError, subprocess.SubprocessError) as error:
            return output.getvalue(), f'{Path(rest[0]).name}: {error}'


def main(argv=None):
    parser = argparse.ArgumentParser(prog='sdk.py test', description=__doc__)
    parser.add_argument('--machine', type=Path, action='append', default=[], help='machine folder; repeatable')
    parser.add_argument('--extension', type=Path, action='append', default=[], help='extension folder; repeatable')
    parser.add_argument('--cxx', default='clang++', help='Clang++ with wasm32/wasm-ld, for synth and drum previews')
    parser.add_argument('--host-cxx', default='c++', help='host C++11 compiler for tests/*.cpp')
    parser.add_argument('--no-cpu', action='store_true', help='skip the CPU estimate')
    parser.add_argument('--jobs', type=int, default=1,
                        help='folders checked at once (default: 1); CPU estimates are host-timed, so use --no-cpu with more')
    parser.add_argument('--firmware', action='store_true',
                        help='also compile these folders together with the pinned Blackfin tools')
    args = parser.parse_args(argv)
    if not args.machine and not args.extension:
        parser.error('pass --machine or --extension')
    try:
        from .extensions import check_collection, extension_at
        machines = [machine_at(folder) for folder in args.machine]
        extensions = check_collection([extension_at(folder) for folder in args.extension])
        require(len({m.id for m in machines}) == len(machines), 'Each machine folder needs its own ID')
        check_sources([m for m in machines if m.kind not in ('synth', 'drum')])  # previews check their own sources
        require(args.jobs >= 1, '--jobs is at least 1')
        host_cxx = shutil.which(args.host_cxx) or args.host_cxx
        with tempfile.TemporaryDirectory(prefix='opxy-machine-test-') as scratch:
            scratch = Path(scratch)
            work = [(check_machine, m.root, args.cxx, host_cxx, args.no_cpu, scratch / m.id) for m in machines]
            work += [(check_extension, e.root, host_cxx, scratch / ('extension-' + e.id)) for e in extensions]
            if args.jobs == 1:
                for check, *rest in work:
                    print(check(*rest))
            else:
                # Each folder in its own process, its report printed whole and in order.
                with ProcessPoolExecutor(max_workers=args.jobs) as pool:
                    for output, error in pool.map(captured, work):
                        print(output, end='')
                        if error:
                            raise ValueError(error)
            if args.firmware:
                command = [sys.executable, str(ROOT / 'sdk.py'), 'compile', '--out', str(scratch / 'module')]
                for m in machines:
                    command += ['--machine', str(m.root)]
                for e in extensions:
                    command += ['--extension', str(e.root)]
                subprocess.run(command, check=True, cwd=ROOT, timeout=1800, creationflags=NO_WINDOW)
        print('PASS: machine and extension checks; no stock files or device operations.')
        return 0
    except (ValueError, OSError, KeyError, TypeError, ET.ParseError, subprocess.SubprocessError) as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        return 1
