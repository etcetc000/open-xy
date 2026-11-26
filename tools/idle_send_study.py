"""Effects under idle sends, on the host (docs/cpu-capabilities.md, idle): for each effect folder given, every
setting of a small grid (defaults, all controls at 0, at 1 and at 0.5, and each control at 1 with the rest at its
default), three inputs (a 2 s noise burst at two bus levels then silence, or silence throughout) and the transport
stopped or playing. The send would idle once the input and the effect's output stay below the floor (64 raw) for
the effect's hold (tooling/adapters.py send_hold); the study reports whether that happens and whether the output,
had stock kept calling the effect, rises to the floor or above afterwards. The effect's own process() runs as the
adapter would call it, compiled for the host; nothing of the device is modelled.

    python tools/idle_send_study.py [--cxx g++] [--json out.json] <effect folder>...

An effect folder is a machine folder with definition.json and fx.cpp (the catalog's effects at their catalog
commits are the ones docs/cpu-capabilities.md reports)."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tooling.adapters import send_hold  # noqa: E402

FLOOR = 64
HARNESS = r'''
#include "fx.cpp"
#include <cstdio>
#include <cstdlib>
using namespace opxy;
static i32 words[@WORDS@ + 1];
static u32 seed = 1;
static i32 noise(i32 shift) { seed = seed * 1664525u + 1013904223u; return static_cast<i32>(seed) >> shift; }
int main(int, char** argv) {
    const u32 hold = @HOLD@, burst = 1378;  // 2 s of blocks
    const u32 total = burst + 3 * hold + 2000;
    i32 p[8];
    for (int i = 0; i < 8; ++i) p[i] = static_cast<i32>(std::strtoll(argv[1 + i], 0, 0));
    const i32 shift = std::atoi(argv[9]);   // input level: noise >> shift; 99 = silence
    const bool playing = std::atoi(argv[10]) != 0;
    for (u32 i = 0; i < @WORDS@; ++i) words[i] = 0;
    static @NS@::State state;
    @NS@::initialize(state);
    Transport transport{1200, 0, playing};
    i32 l[64], r[64];
    u32 quiet = 0, idled = 0, woke = 0;
    long long loudest_after = 0;
    for (u32 b = 0; b < total; ++b) {
        const bool input = shift != 99 && b < burst;
        for (int i = 0; i < 64; ++i) { l[i] = input ? noise(shift) : 0; r[i] = input ? noise(shift) : 0; }
        @CALL@
        if (playing) transport.ticks96 = static_cast<u32>((static_cast<unsigned long long>(b + 1) * 64 * 96 * 1200 / 600) / 44100);
        i32 peak = 0;
        for (int i = 0; i < 64; ++i) {
            const i32 a = l[i] < 0 ? -l[i] : l[i], c = r[i] < 0 ? -r[i] : r[i];
            if (a > peak) peak = a;
            if (c > peak) peak = c;
        }
        const bool below = !input && peak < @FLOOR@ && peak >= 0;
        if (!idled) {
            quiet = below ? quiet + 1 : 0;
            if (quiet >= hold) idled = b + 1;
        } else if (peak >= @FLOOR@ && !woke) {
            woke = b;
        }
        if (idled && peak > loudest_after) loudest_after = peak;
    }
    std::printf("%u %u %lld\n", idled, woke, loudest_after);
    return 0;
}
'''
INPUTS = {13: 'burst 2^18', 3: 'burst 2^28', 99: 'silence'}


def study(folder, cxx, work):
    """One row per run of the effect in `folder`."""
    definition = json.loads((folder / 'definition.json').read_text(encoding='utf-8'))
    resources = definition['resources']
    hold = send_hold(definition)
    ns = definition['id']
    args = (['buffer'] if 'bufferBytes' in resources else []) + (['transport'] if resources.get('transport') else [])
    call = f'{ns}::process(state, ' + ''.join(a + ', ' for a in args) + 'l, r, p);'
    words = resources.get('bufferBytes', 0) // 4
    text = (HARNESS.replace('@WORDS@', str(max(words, 1))).replace('@HOLD@', str(hold)).replace('@NS@', ns)
            .replace('@FLOOR@', str(FLOOR))
            .replace('@CALL@', ('Buffer buffer{words, %d}; ' % words if 'buffer' in args else '') + call))
    source, binary = work / f'{ns}.cpp', work / f'{ns}.exe'
    source.write_text(text, encoding='utf-8')
    subprocess.run([cxx, '-std=c++11', '-O2', '-w', '-I' + str(ROOT / 'sdk/include'), '-I' + str(folder),
                    str(source), '-o', str(binary)], check=True, capture_output=True, text=True)
    count = len(definition['parameters'])
    defaults = [round(p['default'] * 0x7fffffff) for p in definition['parameters']] + [0] * (8 - count)
    settings = {'defaults': defaults, 'all 0': [0] * 8, 'all 1': [0x7fffffff] * count + [0] * (8 - count),
                'all 0.5': [0x3fffffff] * count + [0] * (8 - count)}
    for i, parameter in enumerate(definition['parameters']):
        settings[f'{parameter["id"]} at 1'] = defaults[:i] + [0x7fffffff] + defaults[i + 1:]
    rows = []
    for name, values in settings.items():
        for level in INPUTS:
            for playing in (0, 1):
                run = subprocess.run([str(binary), *map(str, values), str(level), str(playing)], capture_output=True,
                                     text=True, timeout=900, check=True)
                idled, woke, loudest = map(int, run.stdout.split())
                rows.append(dict(effect=ns, hold=hold, setting=name, input=INPUTS[level], playing=playing,
                                 idled=idled, woke=woke, loudest_after=loudest))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--cxx', default='g++', help='host C++ compiler (default g++)')
    parser.add_argument('--json', type=Path, help='also write every run here')
    parser.add_argument('folders', type=Path, nargs='+', help='effect folders (definition.json and fx.cpp)')
    args = parser.parse_args()
    rows = []
    with tempfile.TemporaryDirectory(prefix='idle-study-') as work:
        for folder in args.folders:
            rows += study(folder.resolve(), args.cxx, Path(work))
    print('| Effect | Hold (blocks) | Runs | Idles and stays below the floor | Does not idle within the run | '
          'Rises again after idling |')
    print('| --- | --- | --- | --- | --- | --- |')
    for effect in dict.fromkeys(r['effect'] for r in rows):
        mine = [r for r in rows if r['effect'] == effect]
        settled = sum(1 for r in mine if r['idled'] and not r['woke'])
        never = sum(1 for r in mine if not r['idled'])
        woke = sorted({f"{r['setting']}: {r['loudest_after']} raw" for r in mine if r['woke']})
        print(f"| {effect} | {mine[0]['hold']} | {len(mine)} | {settled} | {never} | "
              f"{len(woke) and sum(1 for r in mine if r['woke'])}{' (' + ', '.join(woke) + ')' if woke else ''} |")
    print(f"{len(rows)} runs; {sum(1 for r in rows if r['idled'] and not r['woke'])} idle and stay below the floor; "
          f"{sum(1 for r in rows if r['woke'])} rise again after idling.")
    if args.json:
        args.json.write_text(json.dumps(rows, indent=1), encoding='utf-8')


if __name__ == '__main__':
    main()
