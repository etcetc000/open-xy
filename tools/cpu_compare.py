"""Compare hardware CPU benchmark runs of two builds, such as the same machines on two OS releases.

Each side is a profiling package and its benchmark captures (docs/profiling.md). For every
benchmark case the two sides share, by name, it prints each recorded stage's median share of
the stock block budget and the change. Offline; reads private captures, prints no device identity.

    python tools/cpu_compare.py --a <private>/cpu-build-1.2.0 <run-001> [<run-002> ...] \\
                                --b <private>/cpu-build-1.2.1 <run-001> [<run-002> ...]
"""
import argparse
import json
from pathlib import Path
import statistics
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tooling.benchmark import case_key, decode  # noqa: E402
from tooling.formats import require, sha  # noqa: E402
from tooling.profile_report import package_data  # noqa: E402

# The stock diagnostic's stages that the whole-voice synth benchmark reports (METHODS), and
# the per-block total; other indices are TE's internal stage timers, printed with --all.
NAMED = {5: 'whole voice', 15: 'block total'}


def medians(package, runs):
    """{(case, stage index): median budget percent} over the runs, a case named by its machine
    and, for a synth's runs, what it ran (a sweep build runs each synth many times). Each capture must be a
    benchmark of exactly this package, used once (as tooling.profile_report requires). A case is
    kept only where every run sounded it, and a stage only where every run recorded it."""
    _, report, identity = package_data(package)
    expected, seen, values = report['benchmark_cases'], set(), {}
    for run in runs:
        capture = json.loads((Path(run) / 'capture.json').read_text(encoding='utf-8'))
        fingerprint = sha(json.dumps(capture, sort_keys=True).encode())
        require(fingerprint not in seen, f'{run}: the same capture given twice')
        seen.add(fingerprint)
        require(capture.get('mode') == 'benchmark' and capture.get('package') == identity and
                capture.get('expected_cases') == expected, f'{run}: a capture of another build or workload')
        for result in decode(capture, expected):
            case = result['machine']
            name = case_key(case)
            for metric in result['metrics']:
                stage = values.setdefault((name, metric['index']), [])
                stage.append(None if result['insufficient_voice_activity'] or not metric['encoded_average']
                             else metric['average_budget_percent'])
    kept = {key: statistics.median(v) for key, v in values.items() if len(v) == len(runs) and None not in v}
    require(kept, 'No stage was recorded by every run')
    return kept, report.get('benchmark_sweep', False)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--a', nargs='+', required=True, metavar='PATH', help='package, then its run folders')
    parser.add_argument('--b', nargs='+', required=True, metavar='PATH', help='package, then its run folders')
    parser.add_argument('--all', action='store_true', help='every stage index, not only whole voice and block total')
    args = parser.parse_args(argv)
    require(len(args.a) > 1 and len(args.b) > 1, 'Give each side a package and at least one run')
    (a, swept_a), (b, swept_b) = medians(args.a[0], args.a[1:]), medians(args.b[0], args.b[1:])
    if swept_a != swept_b:
        print('Note: only one side is a sweep build; its voices also check the sweep override, '
              'so shared runs differ slightly by build, not by machine.')
    shared = sorted(set(a) & set(b), key=lambda k: (k[0], k[1]))
    stages = sorted({i for _, i in shared}) if args.all else sorted(NAMED)
    width = max([12] + [len(n) for n, _ in shared])
    print(f'{"case":{width}} {"stage":14} {"A %":>8} {"B %":>8} {"change":>9}')
    for name in dict.fromkeys(n for n, _ in shared):
        for stage in stages:
            if (name, stage) in a and (name, stage) in b:
                x, y = a[(name, stage)], b[(name, stage)]
                print(f'{name:{width}} {NAMED.get(stage, str(stage)):14} {x:8.3f} {y:8.3f} {100 * (y - x) / x:+8.1f}%')
    only = sorted({n for n, _ in a} ^ {n for n, _ in b})
    if only:
        print('Not on both sides: ' + ', '.join(only))


if __name__ == '__main__':
    main()
