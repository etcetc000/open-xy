"""Revision-bound hardware measurements required for changed catalog submissions."""
import math
import re

from .benchmark import position
from .formats import require

# What a new or updated submission measures, per kind. A synth's record is its control
# sweep: block_percent its heaviest run, midpoint_percent stock's midpoint run,
# combined_percent the run with every control at its heaviest position, and
# worst_setting the heaviest single swept setting.
BENCHMARKS = {'synth': 'opxy-synth-c8-sweep-v1', 'effect': 'opxy-effect-callback-v1',
              'sample': 'opxy-sample-callback-v1'}
# Records from before the sweep, kept by entries that have not changed since.
LEGACY = {'synth': 'opxy-synth-c8-v1'}
FIELDS = {'benchmark', 'commit', 'firmware_sha256', 'runs', 'block_percent'}
SWEEP_FIELDS = FIELDS | {'midpoint_percent', 'combined_percent', 'worst_setting'}
# The share of the stock block budget a machine should stay within (docs/profiling.md).
# Above it a machine is still accepted, with a warning.
RECOMMENDED_PERCENT = 3.5


def above_recommended(value):
    """A warning when a measurement exceeds the recommended share, else None."""
    where = ''
    if value.get('midpoint_percent') == value['block_percent']:
        where = 'at the midpoint'
    elif value.get('combined_percent') == value['block_percent']:
        where = 'with every control at its heaviest position'
    elif 'worst_setting' in value:
        where = 'at ' + position(value['worst_setting'])
    return recommendation(value['block_percent'], where)


def recommendation(percent, where=''):
    """The warning for a share above the recommended one, else None; `where` names the run."""
    if percent <= RECOMMENDED_PERCENT:
        return None
    return (f"{percent:.3f}% of the block budget{' ' + where if where else ''} is above the recommended "
            f"{RECOMMENDED_PERCENT}%; make it lighter where you can (docs/profiling.md)")
# An optional public evidence URL, never a local path or a URL with credentials.
REPORT_URL = r'https://[A-Za-z0-9.-]+(?::[0-9]+)?/[^\s<>"\\]*'


def validate(value, kind, commit=None, legacy=True):
    """A measurement's fields; with a commit, also that it measured that commit. Without
    `legacy`, a record from before the synth sweep is refused (new or updated entries)."""
    require(isinstance(value, dict), 'cpu: unexpected measurement fields')
    allowed = {BENCHMARKS.get(kind)} | ({LEGACY[kind]} if legacy and kind in LEGACY else set())
    require(kind in BENCHMARKS and isinstance(value.get('benchmark'), str) and value['benchmark'] in allowed,
            'cpu: benchmark must match the machine kind' +
            ('; synths submit a control sweep (docs/profiling.md)'
             if kind in LEGACY and value.get('benchmark') == LEGACY[kind] else ''))
    sweep = value['benchmark'] == BENCHMARKS['synth']
    require(set(value) - {'report'} == (SWEEP_FIELDS if sweep else FIELDS), 'cpu: unexpected measurement fields')
    require(isinstance(value['commit'], str) and re.fullmatch(r'[0-9a-f]{40}', value['commit']),
            'cpu: commit is the full SHA of the measured machine source')
    require(commit is None or value['commit'] == commit, 'cpu: measurement must match the catalog commit')
    require(isinstance(value['firmware_sha256'], str) and re.fullmatch(r'[0-9a-f]{64}', value['firmware_sha256']),
            'cpu: firmware_sha256 must identify the measured build')
    require(type(value['runs']) is int and 1 <= value['runs'] <= 100, 'cpu: use 1 to 100 independent runs')
    for field in ('block_percent', 'midpoint_percent', 'combined_percent') if sweep else ('block_percent',):
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
    if 'report' in value:
        require(isinstance(value['report'], str) and len(value['report']) <= 2048 and
                re.fullmatch(REPORT_URL, value['report']), 'cpu: report must be a public HTTPS evidence URL')
    return value
