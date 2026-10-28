"""A host estimate of audio cost, in calibrated instruction-equivalents per block.

The OP-XY renders every voice and effect in 64-sample blocks on one Blackfin
core, so a machine's cost per block adds up across its voices and the tracks
using it. There is no Blackfin simulator in the SDK, so this times the
machine's own render (or process) on the host against a reference loop timed
in the same run, and scales by the reference's Blackfin instruction count,
which reduces host variation without modeling target execution.
Each control is swept from its default setting, through nine positions or
every option of a list, and the slowest setting counts: a machine whose cost
depends on a mode or model is judged by its heaviest one.

It is an estimate, not a device measurement: the host divides and multiplies
far faster than the Blackfin, so a machine heavy in division costs more on the
device than here, and pipeline stalls add cycles to instructions.
"""
import os
from pathlib import Path
import subprocess

from .formats import require
from .pipeline import NO_WINDOW, ROOT

GUIDE = 'docs/api.md#cpu'
# Host screening thresholds, not cycle budgets or qualified polyphony limits.
# A synth's cost is per voice; an effect runs once per track.
LIMITS = {'synth': (15000, 24000), 'effect': (30000, 48000)}
# Open-XY's core machines for scale, as this estimate measured them.
EXAMPLES = {'synth': '2op FM about 14,000, Macro Osc (Braids) about 9,000', 'effect': 'Ring Mod about 10,500'}
# The reference loop's Blackfin instructions per block, from the pinned compiler
# (-O2 -mcpu=bf707): 9 per sample in a zero-overhead hardware loop, plus setup.
REFERENCE_INSTRUCTIONS = 590

# Shared by both benchmarks: the settings to time and the timing loop. RUN is the
# code timed for one block, with `parameters` set.
HARNESS = r'''
#include <chrono>
#include <cstdio>
// The defaults, then each control at each of its positions with the others at
// their defaults.
static const opxy::i32 defaults[8] = {@DEFAULTS@};
static const opxy::i32 positions[] = {@POSITIONS@};
static const int counts[8] = {@COUNTS@};
const int setting_count = 1 + @POSITION_COUNT@;
static int settings(opxy::i32 (*out)[8]) {
    int count = 0, at = 0;
    for (int i = 0; i < 8; ++i) out[count][i] = defaults[i];
    ++count;
    for (int control = 0; control < 8; ++control)
        for (int step = 0; step < counts[control]; ++step, ++count) {
            for (int i = 0; i < 8; ++i) out[count][i] = defaults[i];
            out[count][control] = positions[at++];
        }
    return count;
}
// Nanoseconds per block: the best of five runs per setting, the worst setting.
template <class Setup, class Block> double worst_block(Setup setup, Block block) {
    static opxy::i32 all[setting_count][8];
    const int count = settings(all);
    double worst = 0;
    for (int s = 0; s < count; ++s) {
        double best = 1e30;
        for (int run = 0; run < 5; ++run) {
            setup();
            const auto begin = std::chrono::steady_clock::now();
            for (int b = 0; b < @BLOCKS@; ++b) block(all[s], b);
            const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count();
            if (ns < best) best = ns;
        }
        if (best > worst) worst = best;
    }
    return worst / @BLOCKS@;
}
'''

SYNTH = r'''
#include "engine.cpp"
''' + HARNESS + r'''
// One voice at 440 Hz, its note started again before each run.
int main() {
    static @NS@::Voice voice;
    static @NS@::Shared shared;
    opxy::u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = 42852281u;
    static opxy::i32 output[opxy::block_frames];
    volatile opxy::i32 sink = 0;
    @NS@::initialize(voice, shared, 1);
    std::printf("%.1f\n", worst_block([&] { @NS@::start(voice, shared, 1); }, [&](const opxy::i32* parameters, int b) {
        @NS@::render(voice, shared, 1, output, parameters, pitch);
        sink = sink + output[b & (opxy::block_frames - 1)];
    }));
    return 0;
}
'''

EFFECT = r'''
#include "fx.cpp"
''' + HARNESS + r'''
// A stereo noise burst through the effect.
int main() {
    static @NS@::State state;
@BUFFER_STORAGE@
    static opxy::i32 noise[opxy::block_frames], left[opxy::block_frames], right[opxy::block_frames];
    opxy::u32 seed = 1;
    for (unsigned i = 0; i < opxy::block_frames; ++i)
        noise[i] = static_cast<opxy::i32>(seed = seed * 1664525u + 1013904223u) >> 2;
    volatile opxy::i32 sink = 0;
    @NS@::initialize(state);
    std::printf("%.1f\n", worst_block([] {}, [&](const opxy::i32* parameters, int b) {
        for (unsigned i = 0; i < opxy::block_frames; ++i) left[i] = right[i] = noise[i];
        @NS@::process(state, @BUFFER@@TRANSPORT@left, right, parameters);
        sink = sink + left[b & (opxy::block_frames - 1)];
    }));
    return 0;
}
'''


# The reference loop: a table oscillator through a one-pole low-pass, 64 samples
# a block, whose Blackfin instruction count is known. Timing it in the same run
# as the machine makes the estimate about the same on any computer.
REFERENCE = r'''
#include <opxy/types.hpp>
#include <chrono>
#include <cstdio>
int main() {
    static opxy::i32 table[256], output[64];
    for (int i = 0; i < 256; ++i) table[i] = (i < 128 ? i : 255 - i) * 16777216 - 1073741824;
    opxy::u32 phase = 0;
    opxy::i32 low = 0;
    volatile opxy::i32 sink = 0;
    double best = 1e30;
    for (int run = 0; run < 7; ++run) {
        const auto begin = std::chrono::steady_clock::now();
        for (int block = 0; block < 20000; ++block) {
            for (int i = 0; i < 64; ++i) {
                phase += 42852281u;
                low += (table[phase >> 24] - low) >> 4;
                output[i] = low;
            }
            sink = sink + output[block & 63];
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count();
        if (ns < best) best = ns;
    }
    std::printf("%.3f\n", best / 20000);
    return 0;
}
'''
_reference = {}


def reference_cost(host_cxx, scratch):
    """Host nanoseconds per block of the reference voice, once per compiler and run."""
    if host_cxx not in _reference:
        folder = Path(scratch) / 'cpu-reference'
        folder.mkdir(parents=True, exist_ok=True)
        (folder / 'reference.cpp').write_text(REFERENCE, encoding='utf-8')
        binary = folder / ('reference.exe' if os.name == 'nt' else 'reference')
        result = subprocess.run([host_cxx, '-std=c++11', '-O2', '-I' + str(ROOT / 'sdk/include'),
                                 str(folder / 'reference.cpp'), '-o', str(binary)],
                                capture_output=True, text=True, timeout=600, creationflags=NO_WINDOW)
        require(result.returncode == 0, 'The CPU reference does not compile:\n' + result.stdout + result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=600, creationflags=NO_WINDOW)
        require(result.returncode == 0, 'The CPU reference failed:\n' + result.stdout + result.stderr)
        _reference[host_cxx] = float(result.stdout.strip())
    return _reference[host_cxx]


def sweep(parameter):
    """A control's positions to time, as Q31 values: each option of a list, at the middle
    of its range, or nine positions from 0 to full scale."""
    if parameter['mapping']['kind'] == 'enum':
        options = len(parameter['mapping']['labels'])
        return [(2 * i + 1) * 0x7fffffff // (2 * options) for i in range(options)]
    return [0x7fffffff * step // 8 for step in range(9)]


def block_cost(m, host_cxx, scratch):
    """Host nanoseconds per block for one voice of a synth or one effect, at its slowest
    setting; None for sample engines, whose cost depends on the sample they play."""
    template = {'synth': SYNTH, 'effect': EFFECT}.get(m.kind)
    if template is None:
        return None
    parameters = m.definition['parameters']
    defaults = [round(p['default'] * 0x7fffffff) for p in parameters]
    # A buffered effect runs with a send slot's buffer of its declared size.
    words = m.definition['resources'].get('bufferBytes', 0) // 4
    buffer_storage = f'    static opxy::i32 words[{words}];' if words else ''
    buffer_argument = f'opxy::Buffer{{words, {words}u}}, ' if words else ''
    # A tempo-aware effect runs at 120 BPM, playing.
    transport_argument = 'opxy::Transport{1200u, 0u, true}, ' if m.definition['resources'].get('transport') else ''
    positions = [sweep(p) for p in parameters]
    counts = [len(values) for values in positions] + [0] * (8 - len(positions))
    source = (template.replace('@NS@', m.definition['id']).replace('@BLOCKS@', '400')
              .replace('@POSITIONS@', ', '.join(str(v) for values in positions for v in values))
              .replace('@COUNTS@', ', '.join(map(str, counts)))
              .replace('@POSITION_COUNT@', str(sum(counts)))
              .replace('@DEFAULTS@', ', '.join(map(str, defaults + [0] * (8 - len(defaults)))))
              .replace('@BUFFER_STORAGE@', buffer_storage).replace('@BUFFER@', buffer_argument)
              .replace('@TRANSPORT@', transport_argument))
    folder = Path(scratch) / ('cpu-' + m.id)
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'bench.cpp').write_text(source, encoding='utf-8')
    binary = folder / ('bench.exe' if os.name == 'nt' else 'bench')
    includes = [ROOT / 'sdk/include', m.root, *(m.root / extra for extra in m.includes)]
    command = [host_cxx, '-std=c++11', '-O2', *('-I' + str(path) for path in includes),
               str(folder / 'bench.cpp'), *(str(m.root / s) for s in m.sources), '-o', str(binary)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=600, creationflags=NO_WINDOW)
    require(result.returncode == 0, f'{m.id}: the CPU estimate does not compile:\n{result.stdout}{result.stderr}')
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=600, creationflags=NO_WINDOW)
    require(result.returncode == 0, f'{m.id}: the CPU estimate failed:\n{result.stdout}{result.stderr}')
    return float(result.stdout.strip())


def summary(m, host_cxx, scratch):
    """Host estimate or explicit unavailability, with screening thresholds for scale."""
    try:
        own = block_cost(m, host_cxx, scratch)
        if own is None:
            return (f'WARNING: {m.name}: CPU cost per sounding note is UNESTIMATED for sample engines. '
                    f'Include every internal grain/layer and release tail; compile/test success does not qualify polyphony ({GUIDE}).')
        cost = round(own / reference_cost(host_cxx, scratch) * REFERENCE_INSTRUCTIONS, -2)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        # Advice only: a benchmark that cannot build or run never fails the machine's checks.
        return f'CPU: not estimated ({str(error).splitlines()[0]}); --no-cpu skips it ({GUIDE})'
    aim, most = LIMITS[m.kind]
    unit = 'voice' if m.kind == 'synth' else 'effect'
    line = (f'CPU estimate (host-timed; not measured on the device): {m.name} about {cost:,.0f} instruction-equivalents '
            f'per {unit} block at its heaviest setting. Aim for at most {aim:,}; for scale, {EXAMPLES[m.kind]}')
    if cost > most:
        article = 'a voice' if unit == 'voice' else 'an effect'
        return 'WARNING: ' + line + (f'. Above {most:,}: {article} this heavy may drop audio with several notes or tracks. '
                       f'Look for division and per-sample work that could be per block ({GUIDE})')
    if cost > aim:
        return 'WARNING: ' + line + f'. Above the aim: make it lighter where you can ({GUIDE})'
    return line + f' ({GUIDE})'


def planning_notice():
    """A planning allocation, deliberately separate from host instruction-equivalents."""
    return ('CPU planning: at 400 MHz / 44.1 kHz, a 64-frame block has about 580,500 cycles. '
            'For 24 simultaneous sounding notes and a provisional 50% reserve for other work, '
            'target about 12,000 cycles per note/block, INCLUDING all grains/layers and release tails. '
            'Host instruction-equivalents are not device cycles; 24-note polyphony requires device validation.')


def native_notice(machines):
    samples = ', '.join(m.name for m in machines if m.kind == 'sample')
    return (planning_notice() + '\nWARNING: Native compilation verifies structure and memory, not real-time CPU cost. '
            'Run python sdk.py test --machine <folder> for available host estimates.' +
            (f' Sample-engine cost is UNESTIMATED: {samples}.' if samples else ''))
