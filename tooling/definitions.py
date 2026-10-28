"""Strict schema for a machine's definition.json, shared by every kind."""
import math
from pathlib import Path
import re

from .formats import require

ROOT = Path(__file__).resolve().parents[1]


def keys(value, expected, label):
    require(isinstance(value, dict) and set(value) == set(expected), 'Unexpected ' + label + ' fields')


def number(value, low, high, label):
    require(type(value) in (int, float) and low <= value <= high and math.isfinite(value),
            'Invalid ' + label)


def identifier(value):
    require(isinstance(value, str) and re.fullmatch(r'[a-z][a-z0-9_-]{0,31}', value), 'Invalid identifier')


# The stock firmware's own engine and effect types (stock 1.2.1 catalog).
STOCK_TYPES = frozenset({'sampler', 'drum', 'drumline', 'organ', 'epiano', 'prism', 'hardsync', 'dissolve', 'axis',
                         'wavetable', 'simple', 'midi', 'multisampler',
                         'delay', 'reverb', 'chorus', 'phaser', 'dist', 'lofi'})
# Symbol namespaces the SDK and firmware glue use; a machine's name prefixes its symbols.
RESERVED_PREFIXES = ('extensions', 'sdk', 'kit', 'stock', 'local')
# A machine's name is also its C++ namespace.
CXX_KEYWORDS = frozenset('''alignas alignof and and_eq asm auto bitand bitor bool break case catch char char16_t
    char32_t class compl const constexpr const_cast continue decltype default delete do double dynamic_cast else
    enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not
    not_eq nullptr operator or or_eq private protected public register reinterpret_cast return short signed sizeof
    static static_assert static_cast struct switch template this thread_local throw true try typedef typeid
    typename union unsigned using virtual void volatile wchar_t while xor xor_eq opxy std'''.split())


def stock_name(value):
    """A machine's name: its folder, stock engine type, preset and screen file, and symbol prefix."""
    require(isinstance(value, str) and re.fullmatch(r'[a-z][a-z0-9_]{0,14}', value),
            'Machine names are a lowercase letter, then letters, digits or _, at most 15 characters')
    require(value not in STOCK_TYPES, 'Machine name is a stock engine or effect type: ' + value)
    require(value not in CXX_KEYWORDS, 'Machine name is a C++ keyword or SDK namespace: ' + value)
    require(not any(value == p or value.startswith(p + '_') for p in RESERVED_PREFIXES),
            'Machine name uses a reserved symbol prefix: ' + value)


def label(value):
    require(isinstance(value, str) and re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9 _-]{0,63}', value),
            'Labels must be short plain text')


def control_label(value):
    # The stock parameter page shows control names in lower case, at most eight characters.
    require(isinstance(value, str) and re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9 ]{0,7}', value),
            'Control labels are at most eight letters, digits or spaces')


# Resources each kind declares; the SDK owns everything else about its storage.
# A sample engine may add `display` to draw its own page instead of the stock sample page.
RESOURCES = {
    'synth': ('voiceBytes', 'voiceAlignment', 'voiceCapacity', 'sharedBytes', 'outputPeakQ31', 'display'),
    'sample': ('voiceCapacity',),
    'effect': (),
}
# A synth's own screen: one drawing contract (opxy/draw.hpp); the machine
# declares its frame's budget within these limits.
DISPLAY_FIELDS = ('stateBytes', 'stateAlignment', 'paths', 'points', 'nodes', 'texts', 'palette')
DISPLAY_LIMITS = dict(paths=(0, 4), points=(0, 64), nodes=(1, 256), texts=(0, 16), palette=(1, 16))
# More text slots than this need SDK 1.3 (tooling.machines).
SDK_1_2_TEXTS = 4
# Analysis storage is allocated from the stock heap at runtime, which also holds
# stock samples; a collection's analysis budgets together take at most this much.
ANALYSIS_HEAP_LIMIT = 2 * 1024 * 1024
# A sample engine's optional services: integer 0/1 flags and the analysis budget.
SAMPLE_FLAGS = ('chokeSameKey', 'padSettings', 'drumControls', 'stereoOutput', 'singleSample')
SAMPLE_OPTIONS = ('sampleAnalysis', *SAMPLE_FLAGS, 'display')
# An effect's optional delay memory (SDK 1.2): stock's per-send scratch, at most
# 512 KiB; and transport 1 for the tempo in process (SDK 1.2).
EFFECT_OPTIONS = ('bufferBytes', 'transport')
EFFECT_BUFFER_LIMIT = 524288
DISPLAY_FORMATS = ('ratio', 'percent', 'bipolar', 'duration', 'semitones', 'enum', 'division')
# Every kind's parameters occupy the stock eight-word parameter vector in order.
# A synth's first four fill the four-control page; up to four more are alternates
# (see control_positions). Sample engines and effects use one to eight.
CONTROLS = {'synth': (4, 8), 'sample': (1, 8), 'effect': (1, 8)}
# Stock voice indices: a voice object for each, 0..255.
STOCK_VOICES = 256
SYNTH_POSITIONS = 4


def own_screen(value, kind):
    """Whether a definition draws its own page: every synth, and a sample engine declaring a display."""
    return kind == 'synth' or (kind == 'sample' and isinstance(value.get('resources'), dict) and
                               'display' in value['resources'])


def control_positions(definition):
    """The synth page position (encoder) that edits each parameter, in parameter order.

    The first four parameters are the positions' own; each later one names the
    position it alternates on with `control`. Pressing an encoder cycles its
    position through its own parameter, then its alternates in parameter order.
    """
    return [parameter.get('control', i) for i, parameter in enumerate(definition['parameters'])]


def paged(definition):
    """Whether a press switches the synth's whole page (`"press": "page"`, SDK 1.3)."""
    return definition.get('press') == 'page'


def validate_definition(value, kind):
    keys(value, ('schema', 'id', 'name', 'parameters', 'presets', 'resources', *(('press',) if 'press' in value else ())),
         'definition')
    require(type(value['schema']) is int and value['schema'] == 1, 'Unknown definition schema')
    stock_name(value['id']); label(value['name'])
    resources = value['resources']
    options = SAMPLE_OPTIONS if kind == 'sample' else EFFECT_OPTIONS if kind == 'effect' else ()
    optional = tuple(k for k in options if isinstance(resources, dict) and k in resources)
    keys(resources, (*RESOURCES[kind], *optional), 'resources')
    require(all(type(v) is int for k, v in resources.items() if k != 'display'),
            'Numeric resource requirements must be integers')
    if 'voiceCapacity' in resources:
        require(1 <= resources['voiceCapacity'] <= 256, 'Voice capacity must be 1..256')
    # Stock creates a voice object for every index 0..255 and never checks for a
    # missing one. Synth voices are shared out to notes as they start (voice.cpp.in);
    # a sample engine's voice is bound to its sample when created, so it needs all 256.
    if kind == 'sample':
        require(resources['voiceCapacity'] == STOCK_VOICES,
                'A sample engine needs voiceCapacity 256: stock creates a voice for every one of its 256 indices')
    if kind == 'sample':
        heap = resources.get('sampleAnalysis', 0)
        require(type(heap) is int and (heap == 0 or (4096 <= heap <= ANALYSIS_HEAP_LIMIT and heap % 4 == 0)),
                'sampleAnalysis is 0 or the analysis heap budget in bytes, up to %d' % ANALYSIS_HEAP_LIMIT)
        for flag in SAMPLE_FLAGS:
            require(resources.get(flag, 0) in (0, 1), flag + ' must be 0 or 1')
        require(resources.get('padSettings', 0) or not resources.get('drumControls', 0),
                'drumControls edits pad settings: it needs padSettings')
        if 'display' in resources:
            require(not resources.get('padSettings', 0), 'padSettings and drumControls require a stock drum page')
            validate_display(resources['display'])
    if kind == 'effect' and 'bufferBytes' in resources:
        size = resources['bufferBytes']
        require(4 <= size <= EFFECT_BUFFER_LIMIT and size % 4 == 0,
                'bufferBytes is whole words, at most %d: the per-send scratch memory stock gives an effect'
                % EFFECT_BUFFER_LIMIT)
    if kind == 'effect':
        require(resources.get('transport', 0) in (0, 1), 'transport must be 0 or 1')
    if kind == 'synth':
        validate_synth_resources(resources)
    parameters = value['parameters']
    # A machine with its own page fills the four encoder positions; later parameters are alternates.
    screen = own_screen(value, kind)
    low, high = CONTROLS['synth' if screen else kind]
    require(isinstance(parameters, list) and low <= len(parameters) <= high,
            f'Control count outside {low}..{high} for this kind')
    names = set()
    for i, parameter in enumerate(parameters):
        alternate = screen and i >= SYNTH_POSITIONS
        validate_parameter(parameter, alternate)
        require(parameter['id'] not in names, 'Duplicate parameter identity')
        names.add(parameter['id'])
    # Two pages share positions with no alternate; no duplicate saved parameters.
    if 'press' in value:
        alternates = control_positions(value)[SYNTH_POSITIONS:]
        require(kind == 'synth' and value['press'] == 'page' and 5 <= len(parameters) <= 8 and
                len(set(alternates)) == len(alternates),
                '"press": "page" needs five to eight synth parameters and at most one alternate per position')
    require(isinstance(value['presets'], dict) and len(value['presets']) <= 64, 'Invalid presets')
    for name, preset in value['presets'].items():
        label(name); keys(preset, names, 'preset')
        for default in preset.values(): number(default, 0, 1, 'preset value')
    return value


def validate_synth_resources(resources):
    # Sizes are bounded by the shared module memory, checked when the collection links.
    require(4 <= resources['voiceBytes'] and resources['voiceBytes'] % 4 == 0,
            'Voice state must occupy at least one whole word')
    require(resources['voiceAlignment'] in (4, 8, 16) and
            resources['voiceBytes'] % resources['voiceAlignment'] == 0, 'Unsupported voice alignment')
    require(0 <= resources['sharedBytes'] and resources['sharedBytes'] % 4 == 0,
            'Shared state must occupy whole words')
    require(0 < resources['outputPeakQ31'] <= 0x7fffffff, 'Invalid oscillator output headroom')
    validate_display(resources['display'])


def validate_display(display):
    require(not (isinstance(display, dict) and 'kind' in display),
            'This display uses a contract from before SDK 1.0: see docs/display.md#migrating-from-before-sdk-10')
    keys(display, DISPLAY_FIELDS, 'display')
    for name, (low, high) in DISPLAY_LIMITS.items():
        require(type(display[name]) is int and low <= display[name] <= high,
                'Display %s must be %d..%d' % (name, low, high))
    require((display['points'] >= 2) == (display['paths'] > 0),
            'Paths have 2..64 points; a display without paths declares 0 points')
    require(display['paths'] <= display['nodes'], 'Each path is also a node')
    require(type(display['stateBytes']) is int and type(display['stateAlignment']) is int and
            4 <= display['stateBytes'] and display['stateAlignment'] in (4, 8, 16) and
            display['stateBytes'] % display['stateAlignment'] == 0,
            'Display state must occupy aligned whole words')


def validate_parameter(parameter, alternate=False):
    # A synth's parameters after the first four are alternates of one control position.
    keys(parameter, ('id', 'label', 'default', 'mapping', 'display', *(('control',) if alternate else ())), 'parameter')
    if alternate:
        require(type(parameter['control']) is int and 0 <= parameter['control'] < SYNTH_POSITIONS,
                'An alternate parameter names its control position 0..3')
    identifier(parameter['id']); control_label(parameter['label'])
    number(parameter['default'], 0, 1, 'normalized default')
    require(parameter['display'] in DISPLAY_FORMATS, 'Unsupported display format')
    require(parameter['display'] != 'division' or parameter['mapping'] == {'kind': 'linear', 'minimum': 0, 'maximum': 1},
            'A division control maps linearly from 0 to 1')
    mapping = parameter['mapping']
    require(isinstance(mapping, dict), 'Invalid mapping')
    kind = mapping.get('kind')
    require((parameter['display'] == 'enum') == (kind == 'enum'), 'Enum mapping/display must agree')
    if kind == 'enum':
        keys(mapping, ('kind', 'labels'), 'enum mapping')
        labels = mapping['labels']
        require(isinstance(labels, list) and 2 <= len(labels) <= 256 and
                all(isinstance(s, str) and re.fullmatch(r'[A-Za-z0-9 .:*+_-]{1,15}', s) for s in labels) and
                len(set(labels)) == len(labels) and parameter['display'] == 'enum', 'Invalid enum labels')
    elif kind == 'linear':
        keys(mapping, ('kind', 'minimum', 'maximum'), 'linear mapping')
        number(mapping['minimum'], -1e9, 1e9, 'mapping minimum')
        number(mapping['maximum'], -1e9, 1e9, 'mapping maximum')
        require(mapping['minimum'] < mapping['maximum'], 'Linear mapping must increase')
    elif kind == 'anchors':
        # Two anchors are a linear mapping; declare those as linear.
        keys(mapping, ('kind', 'values'), 'anchor mapping')
        anchors = mapping['values']
        require(isinstance(anchors, list) and 3 <= len(anchors) <= 256, 'Anchors need 3..256 values; use linear for two')
        for anchor in anchors: number(anchor, -1e9, 1e9, 'anchor')
        require(all(a < b for a, b in zip(anchors, anchors[1:])), 'Anchors must increase')
    elif kind == 'power':
        keys(mapping, ('kind', 'maximum', 'exponent'), 'power mapping')
        number(mapping['maximum'], 0, 1e9, 'mapping maximum')
        number(mapping['exponent'], 0.01, 32, 'mapping exponent')
        require(mapping['exponent'] != 1, 'A power mapping with exponent 1 is linear')
    elif kind == 'exponential':
        keys(mapping, ('kind', 'minimum', 'maximum'), 'exponential mapping')
        number(mapping['minimum'], 1e-9, 1e9, 'mapping minimum')
        number(mapping['maximum'], mapping['minimum'], 1e9, 'mapping maximum')
    else:
        raise ValueError('Unsupported mapping kind')
