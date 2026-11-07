"""Revision-bound hardware measurements required for changed catalog submissions."""
import math
import re

from .benchmark import drum_label, position
from .formats import require

# What a new or updated submission measures, per kind. A synth's record is its control
# sweep: block_percent its heaviest run, midpoint_percent stock's midpoint run,
# combined_percent the run with every control at its heaviest position, and
# worst_setting the heaviest single swept setting.
# A drum kit's: block_percent its heaviest single voice, at any model and level, as
# worst_model says; midpoint_percent its heaviest at the middle; kit_percent its own
# keys at full polyphony; choke_percent retriggers on one key; ceiling_percent full
# polyphony of the heaviest. Those three are whole kits, against the same budget.
# Optional for a kit: voice_peak_percent, its worst block over the single-voice runs
# only, which the builder's CPU-high label uses (whole-kit runs are left out).
BENCHMARKS = {'synth': 'opxy-synth-c8-sweep-v1', 'effect': 'opxy-effect-callback-v1',
              'sample': 'opxy-sample-callback-v1', 'drum': 'opxy-drum-callback-v1'}
# Records from before the sweep, kept by entries that have not changed since.
LEGACY = {'synth': 'opxy-synth-c8-v1'}
FIELDS = {'benchmark', 'commit', 'firmware_sha256', 'runs', 'block_percent'}
SWEEP_FIELDS = FIELDS | {'midpoint_percent', 'combined_percent', 'worst_setting'}
DRUM_FIELDS = FIELDS | {'midpoint_percent', 'kit_percent', 'choke_percent', 'ceiling_percent', 'worst_model'}
# The share of the stock block budget a machine should stay within (docs/profiling.md).
# Above it a machine is still accepted, with a warning.
RECOMMENDED_PERCENT = 3.5
# Above this share of a block, a machine's reported worst case (its worst observed block
# peak, else its average) gets the builder's CPU-high label (builder/core.mjs
# CPU_HIGH_PERCENT, which tests compare). The label is derived, never written in an entry.
HIGH_PERCENT = 4.5


def worst_case(value):
    """The reported worst case: (percent, 'peak') when the record has a peak, else (percent, 'average').
    A drum kit is judged by its worst single sound, never its whole-kit runs: its
    single-voice peak when recorded, else its heaviest single voice's average."""
    peak = 'voice_peak_percent' if value.get('benchmark') == BENCHMARKS['drum'] else 'peak_percent'
    return (value[peak], 'peak') if peak in value else (value['block_percent'], 'average')


def high(value):
    """The builder's CPU-high note for a record whose worst case exceeds HIGH_PERCENT, else None."""
    percent, metric = worst_case(value)
    if percent <= HIGH_PERCENT:
        return None
    drum = value.get('benchmark') == BENCHMARKS['drum']
    what = (('worst single sound peak' if drum else 'worst observed block peak') if metric == 'peak' else
            'worst single sound average (no single-sound peak recorded)' if drum else 'average (no peak reported)')
    return f"the builder labels it CPU-high: {what} {percent:.3f}% is above {HIGH_PERCENT}%"


def above_recommended(value):
    """A warning when a measurement exceeds the recommended share, else None."""
    where = ''
    if value.get('midpoint_percent') == value['block_percent']:
        where = 'at the midpoint'
    elif value.get('combined_percent') == value['block_percent']:
        where = 'with every control at its heaviest position'
    elif 'worst_setting' in value:
        where = 'at ' + position(value['worst_setting'])
    elif 'worst_model' in value:
        where = 'for ' + drum_label(value['worst_model'])
    warnings = [recommendation(value['block_percent'], where, 'Mean')]
    if 'peak_percent' in value:
        warnings.append(recommendation(value['peak_percent'], 'across all measured workloads', 'Worst observed block peak'))
    return '; '.join(warning for warning in warnings if warning) or None


def recommendation(percent, where='', metric=''):
    """The warning for a share above the recommended one, else None; `where` names the run."""
    if percent <= RECOMMENDED_PERCENT:
        return None
    return (f"{metric + ' ' if metric else ''}{percent:.3f}% of the block budget{' ' + where if where else ''} is above the recommended "
            f"{RECOMMENDED_PERCENT}%; make it lighter where you can (docs/profiling.md)")
# An optional public evidence URL, never a local path or a URL with credentials.
REPORT_URL = r'https://[A-Za-z0-9.-]+(?::[0-9]+)?/[^\s<>"\\]*'


def validate(value, kind, commit=None, legacy=True):
    """A measurement's fields; with a commit, also that it measured that commit. Without
    `legacy`, average-only records and records from before the synth sweep are refused."""
    require(isinstance(value, dict), 'cpu: unexpected measurement fields')
    allowed = {BENCHMARKS.get(kind)} | ({LEGACY[kind]} if legacy and kind in LEGACY else set())
    require(kind in BENCHMARKS and isinstance(value.get('benchmark'), str) and value['benchmark'] in allowed,
            'cpu: benchmark must match the machine kind' +
            ('; synths submit a control sweep (docs/profiling.md)'
             if kind in LEGACY and value.get('benchmark') == LEGACY[kind] else ''))
    sweep = value['benchmark'] == BENCHMARKS['synth']
    drum = kind == 'drum'
    require(legacy or 'peak_percent' in value,
            'cpu: average and worst observed block peak are required; rerun sdk.py profile report '
            'with the original captures (docs/profiling.md)')
    require(set(value) - {'report', 'peak_percent'} - ({'voice_peak_percent'} if drum else set()) ==
            (SWEEP_FIELDS if sweep else DRUM_FIELDS if drum else FIELDS),
            'cpu: unexpected measurement fields')
    require(isinstance(value['commit'], str) and re.fullmatch(r'[0-9a-f]{40}', value['commit']),
            'cpu: commit is the full SHA of the measured machine source')
    require(commit is None or value['commit'] == commit, 'cpu: measurement must match the catalog commit')
    require(isinstance(value['firmware_sha256'], str) and re.fullmatch(r'[0-9a-f]{64}', value['firmware_sha256']),
            'cpu: firmware_sha256 must identify the measured build')
    require(type(value['runs']) is int and 1 <= value['runs'] <= 100, 'cpu: use 1 to 100 independent runs')
    fields = (('block_percent', 'midpoint_percent', 'combined_percent') if sweep else
              ('block_percent', 'midpoint_percent', 'kit_percent', 'choke_percent', 'ceiling_percent') if drum else
              ('block_percent',))
    for field in fields:
        number = value[field]
        require(type(number) in (int, float) and math.isfinite(number) and 0 < number < 100,
                f'cpu: {field} must be an unsaturated, positive percentage of the stock block budget')
    if sweep:
        setting = value['worst_setting']
        require(isinstance(setting, dict) and set(setting) - {'option'} == {'control', 'label', 'value'} and
                type(setting['control']) is int and 0 <= setting['control'] < 8 and
                isinstance(setting['label'], str) and 0 < len(setting['label']) <= 32 and
                type(setting['value']) is int and 0 <= setting['value'] <= 0x7fffffff and
                ('option' not in setting or isinstance(setting['option'], str) and 0 < len(setting['option']) <= 32),
                'cpu: worst_setting names the heaviest control, its label and Q31 value '
                '(and a list control\'s option)')
        require(max(value['midpoint_percent'], value['combined_percent']) <= value['block_percent'],
                'cpu: block_percent is the heaviest run, the midpoint and combined runs included')
    if drum:
        worst = value['worst_model']
        require(isinstance(worst, dict) and set(worst) == {'run', 'voices', 'model', 'level'} and
                worst['run'] == 'voice' and worst['voices'] == 1 and
                type(worst['model']) is int and 0 <= worst['model'] < 64 and worst['level'] in (0, 64, 127),
                'cpu: worst_model names the heaviest single voice\'s model and level')
        require(value['midpoint_percent'] <= value['block_percent'],
                'cpu: block_percent is the heaviest single voice, the midpoint included')
    if 'peak_percent' in value:
        peak = value['peak_percent']
        require(type(peak) in (int, float) and math.isfinite(peak) and 0 < peak < 100,
                'cpu: peak_percent must be an unsaturated, positive percentage of the stock block budget')
        require(peak >= max(value[field] for field in fields),
                'cpu: peak_percent must cover every measured run, including whole-kit runs')
    if 'voice_peak_percent' in value:
        voice = value['voice_peak_percent']
        require('peak_percent' in value and type(voice) in (int, float) and math.isfinite(voice) and
                value['block_percent'] <= voice <= value['peak_percent'],
                'cpu: voice_peak_percent is the worst block of the single-voice runs: at least block_percent, '
                'at most peak_percent')
    if 'report' in value:
        require(isinstance(value['report'], str) and len(value['report']) <= 2048 and
                re.fullmatch(REPORT_URL, value['report']), 'cpu: report must be a public HTTPS evidence URL')
    return value
