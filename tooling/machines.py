"""Machine discovery: each machine folder's machine.json declares what the SDK builds.

A machine lives in machines/<id>/ or in a folder of its own anywhere, such as
its own repository. Its definition's id is its stock name: the engine type the
device lists, its catalog and preset entries, its screen file and its symbol
prefix. machine.json declares the kind, the SDK version it targets and build
inputs; a sample engine also names the stock sampler it forks. Each build's
layout gives the machine its engine, effect and page numbers. Saved presets and
Open-XY layouts refer to the name and numbers, so they never change once
published; the SDK checks them against the stock tables and the collection.
"""
from dataclasses import dataclass, replace
import json
import os
from pathlib import Path
import re

from .definitions import (AUDIO_SDK, AUDIO_SDK_KEYS, DEVICE_IDENTITY_SDK, DRUM_KIT_CONTROLS, EXTENDED_SCREENS_SDK, EXTENDED_SDK, KIT_CONTROLS_SDK, SDK_1_25, extended_display, ROOT, SDK_1_2_TEXTS, SDK_1_6_TEXTS, SDK_1_7_NODES, STOCK_VOICES, TRACK_INSTRUMENT_SDK, paged,
                          shift_parameters, stock_name, synth_shift, validate_definition)
from .contributors import validate_native_assembly
from .formats import require
from . import track_stage, voice_heap
from .pipeline import config

KINDS = ('synth', 'drum', 'sample', 'effect', 'player')
FIELDS = {'schema', 'sdk', 'kind', 'description', 'identity', 'base', 'sources', 'includes', 'icon', 'presetDefaults',
          'presets', 'samples', 'dryParameter', 'dryLevel', 'graphics'}
# The public machine contract: headers, definition schema, artwork rules and
# callbacks. A machine names the version it was written for; the SDK builds a
# machine of the same major version and an equal or older minor version.
# 1.1 adds display note activity; 1.2 an effect's delay buffer (resources.bufferBytes),
# its tempo (resources.transport) and division captions; 1.3 the machine's own presets,
# a press that switches a synth's whole page and up to 16 display text slots.
# 1.4 adds the reusable header and partial second pages sharing unassigned controls.
# 1.5 drops identity numbers: the layout gives every number, and a sample engine
# names its base instead of a stock page.
# 1.5 also supports optional developer-authored grayscale artwork.
# 1.6 extends PadSettings to 18 bits and supplies held-pad and multi-edit services.
# 1.7 supplies a frozen source-audition clock for tempo-aware sample slicing, lets an
# effect page its controls, have a shift row, draw its own page and fix its source dry
# (dryLevel), and adds a saturating Q31 add and up to 32 display text slots.
# 1.8 adds note players and their input/output keyboard page, synthesis drum kits, and lets
# a sample engine forking the sampler draw its own screen and read its sample's overview.
# 1.9 adds a synth's track control (resources.trackControl) and hidden stock pages (hidePages).
# 1.10 gives display code the track's amp envelope (draw::Input amp); lets Shift on a
# sample engine's own screen give any encoder to one of the stock sampler page's own
# controls (resources.stockShift); lets a sample engine ship its own samples
# (machine.json `samples`), installed in the content folder, and its presets point at
# them; gives each key of a drum kit a mixer channel of its own on Shift: pan,
# level, small controls and sends, which lock per hit (resources.padMix and
# resources.padControls); runs every kit's voices through stock's track filter (M3);
# and calls a kit's drum_transport with the tempo before every render.
# 1.11 lets a track control play notes on instrument tracks, take its own encoder turns
# and pass sixteen words to its display.
# 1.12 adds stereo synths (resources.stereoOutput: 64 interleaved left/right frames)
# and a synth's voice release callback (resources.releaseCallback).
# 1.13 gives a synth a Shift row: up to eight more controls under its pages ("shift"),
# kept in the instrument track's words 36 and 37, and opxy/synth_shift.hpp for its display.
# 1.16 gives an effect a bus shift: its send's audio raised by a fixed number of
# bits around process (resources.busShift), and lets a note player page eight
# controls ("press": "page"); and gives a note player a shift row ("shift"),
# Shift turns and encoder clicks in its Controls, and its step clock's tempo.
# 1.17 lets a sample engine declare fewer than 256 voices, 27 at least
# (resources.voiceCapacity); and adds the sample kernel
# (opxy/sample_kernel.hpp): a sample voice's interpolation, level and crossfade
# as one SDK routine.
# 1.18 adds saturating 1.15 helpers to opxy/q15.hpp and saturate, subtract and
# shift_right to opxy/q31.hpp.
# 1.19 keeps a synth's voices in an arena reserved from the stock heap at boot
# when its voice pool exceeds 128 KiB (voice_heap.py).
# 1.20 adds extended drawing for synth and effect screens (docs/display.md#extended-drawing):
# affine nodes, direct colour, a whole-screen canvas, curves, open, segmented and counted
# paths and larger path budgets; the encoders' activity, and a synth's last control
# touched (opxy/control_interaction.hpp).
# 1.21 adds per-model drum value editing, played-pad display snapshots and sample-free kit presets.
# 1.22 adds the idle capability's machine API: an effect's declared tail
# (resources.tailFrames) for its send hold, and an engine's settled() for the idle cut
# (asked only of machines declaring 1.22 or later). IDLE_SDK is the one place to
# renumber it, with the "SDK 1.22" text of docs/api.md.
IDLE_SDK = (1, 22)
# 1.23 adds the drum-kit controls (resources.padVolume, padRandom, ampEnvelope) and gives a
# kit's screen its selected key (draw::Input::selected_key; KIT_CONTROLS_SDK in definitions.py).
# 1.24 adds a synth's track stage (resources.trackStage, track_stage.py): one state per
# instrument track, processed in place on the track's voice sum once per block.
# 1.25 gives a note player its track's kind of instrument (Controls.instrument) and lets a
# drum kit's value editor move the key to another model (resources.modelEdit; SDK_1_25 in definitions.py).
# From 1.31 the instrument is only a declaring player's (below).
# 1.26 lets a note player draw its own page (resources.display): a draw::Frame from its
# State and Controls each GUI frame, as a synth draws its screen. PLAYER_DRAWING_SDK is
# the one place to renumber it, with the "SDK 1.26" text of docs/api.md and docs/display.md.
PLAYER_DRAWING_SDK = (1, 26)
# 1.27 adds a screen's own audio (audio snapshots, for synths and effects, and for a synth's
# track stage), stock's output and input levels on a screen, and glyph atlases
# (docs/display.md). A renumber changes AUDIO_SDK in definitions.py, this line and the
# "SDK 1.27" text of the docs.
# 1.28 extends extended drawing to a drum kit's screen, with a whole-screen kit whose
# header shows only while Shift is held, and to a note player's own page
# (EXTENDED_SCREENS_SDK in definitions.py).
# 1.29 adds more one-instruction helpers to opxy/q15.hpp (half, subtract_wrapped and the pair
# helpers pack, low, high, multiply_pair, add_pair) and an arithmetic shift by a signed count,
# shift, to opxy/q31.hpp. A renumber changes this line, SDK_VERSION and the "SDK 1.29" text of
# those headers, docs/api.md (its CPU notes and the sdk row) and tests/test_fixed_point_helpers.py.
# 1.30 lets a machine of any kind read the device identity (resources.deviceIdentity,
# opxy/device_identity.hpp; DEVICE_IDENTITY_SDK in definitions.py).
# 1.31 makes a note player's track instrument opt-in: Controls.instrument exists only for a
# player declaring resources.trackInstrument (TRACK_INSTRUMENT_SDK in definitions.py).
SDK_VERSION = (1, 31)
# The stock samplers a sample engine forks (machine.json `base`), by their stock pages:
# the keyboard sampler's and the drum sampler's, the profile's sample_pages.
SAMPLE_BASES = {'sampler': 2, 'drumSampler': 7}
# Builder card glyph: SVG path data in a 46 x 48 box, drawn as a stroke.
ICON = re.compile(r'[MLHVCSQTAZmlhvcsqtaz][MLHVCSQTAZmlhvcsqtaz0-9 .,-]{0,511}')
# A machine's own presets (machine.json `presets`): <name>.json files the builder
# installs beside its init preset; default.json replaces the generated one.
# builder/core.mjs checks them again. Stock's preset browser lists a folder of any length
# (its scan adds each preset to a growing list, with no count; stock's own factory folders
# hold up to 28): PRESET_COUNT only bounds a kit's and a machine pack's size (64 x 16 KiB).
PRESET_NAME = re.compile(r'[a-z0-9][a-z0-9_-]{0,23}')
PRESET_COUNT = 64
PRESET_BYTES = 16 * 1024
# A sample engine's own samples (machine.json `samples`): <name>.wav files, 16-bit PCM at
# 44.1 kHz, mono or stereo, that sdk.py build installs as content/samples/<id>/<name>.wav.
# They live on the OP-XY's storage, outside the module memory; stock loads one into its
# sample memory when a preset pointing at it is loaded, as it loads any sample.
SAMPLE_NAME = re.compile(r'[a-z0-9][a-z0-9_-]{0,23}')
SAMPLE_COUNT = 4
SAMPLE_BYTES = 2 * 1024 * 1024
SAMPLE_RATE = 44100
WAVE_FORMAT_PCM, WAVE_FORMAT_EXTENSIBLE = 1, 0xfffe
# The keys a sampler region holds, as stock saves a sample engine's preset; the first
# three are required.
REGION_REQUIRED = {'sample', 'framecount', 'sample.end'}
REGION_INTEGERS = {'framecount', 'sample.start', 'sample.end', 'loop.start', 'loop.end', 'loop.crossfade',
                   'hikey', 'pitch.keycenter', 'gain', 'tune', 'pan', 'fade.in', 'fade.out'}
REGION_FLAGS = {'loop.onrelease', 'loop.enabled', 'reverse'}
# Saved gain in dB: the stock pad record's signed level byte (platform stock_sample.hpp).
REGION_GAIN = (-30, 20)
# Fields whose saved units and range the SDK doesn't record: a preset keeps them at 0,
# as a newly loaded sample has them. pitch.keycenter tunes a region in semitones.
REGION_NEUTRAL = ('tune', 'pan', 'fade.in', 'fade.out', 'loop.crossfade')
# Folders inside a machine that are never its source: version control, caches, tools.
IGNORED = {'.git', '__pycache__', 'node_modules', '.venv'}


def tree(root):
    """Every file under a machine folder, in name order, without walking ignored folders."""
    found = []
    for folder, folders, files in os.walk(root):
        folders[:] = sorted(f for f in folders if f not in IGNORED)
        found += [Path(folder) / name for name in files]
    return sorted(found)


@dataclass(frozen=True)
class Machine:
    id: str
    kind: str
    name: str
    description: str
    display: str
    sources: tuple
    includes: tuple
    definition: dict
    identity: dict  # its numbers; None where machine.json asks for none, until a layout gives them
    preset_defaults: dict
    root: Path = None
    presets: dict = None  # name -> preset document, from machine.json `presets`
    samples: dict = None  # name -> Sample, from machine.json `samples` (SDK 1.10)
    icon: str = ''
    dry_parameter: int | None = None
    dry_level: int | None = None  # a fixed source dry, Q31 (SDK 1.7)
    requested: dict = None  # the numbers machine.json asks for, when a layout gave others
    grayscale: str | None = None
    native: tuple = ()  # its NativeUnits (native/<name>.s), in name order
    sdk: tuple = (1, 0)  # the SDK version machine.json declares, (major, minor)

    @property
    def sample_page(self):
        """The stock page a sample engine forks. One drawing its own screen forks the
        sampler, and its `page` is that screen's, as a synth's is."""
        return SAMPLE_BASES['sampler'] if self.display != 'stock' else self.identity['page']

    # The stock name and the symbol prefix are the machine's name.
    @property
    def native_name(self):
        return self.id

    @property
    def prefix(self):
        return self.id

    def source(self, relative=''):
        """A machine file as the build names it: its absolute path."""
        return self.root.as_posix() + ('/' + relative if relative else '')

    def files(self):
        """Every authored file, keyed as the build's provenance names it."""
        return {'external/' + self.id + '/' + p.relative_to(self.root).as_posix(): p
                for p in tree(self.root) if p.suffix != '.md'}

    def contains(self, path):
        """Whether a path lies inside this machine's folder, where build output must never go."""
        return Path(os.path.abspath(path)).is_relative_to(self.root)


def relative_path(value):
    """A path inside a folder, written with forward slashes: no '..' and no root."""
    return (isinstance(value, str) and re.fullmatch(r'[A-Za-z0-9_./-]+', value) is not None and
            '..' not in value.split('/') and not value.startswith('/'))


def _relative(machine_root, value):
    require(relative_path(value), 'Invalid machine path: ' + str(value))
    path = (machine_root / value).resolve()
    require(path.is_relative_to(machine_root.resolve()) and path.exists(), 'Missing machine path: ' + value)
    return value


def graphics_path(record):
    """The optional grayscale source, explicitly opted into by SDK 1.5 synths
    and by SDK 1.7 effects that draw their own page."""
    if 'graphics' not in record:
        return None
    kind = record.get('kind')
    require_sdk(sdk_version(record.get('sdk')), (1, 7) if kind == 'effect' else (1, 5), 'Developer grayscale artwork')
    graphics = record['graphics']
    require(kind in ('synth', 'effect') and isinstance(graphics, dict) and set(graphics) == {'grayscale'},
            'graphics requires a synth or an effect with its own page, and exactly one grayscale artwork path')
    path = graphics['grayscale']
    require(relative_path(path) and path.endswith('.svg.fragment') and path != 'panel.svg.fragment',
            'graphics.grayscale names a separate local .svg.fragment file')
    return path


@dataclass(frozen=True)
class Sample:
    path: Path
    frames: int
    channels: int


def sample_path(identity, name):
    """Where a machine's own sample is installed, and how its presets name it."""
    return f'content/samples/{identity}/{name}.wav'


def wav_format(data):
    """(format tag, channels, rate, bits per sample, data bytes, block align) of a RIFF
    WAVE file, from its chunks. Python's wave module isn't used: whether it reads
    WAVE_FORMAT_EXTENSIBLE depends on the Python version (3.12 added it)."""
    require(len(data) >= 12 and data[:4] == b'RIFF' and data[8:12] == b'WAVE', 'no RIFF WAVE header')
    fmt = size = None
    at = 12
    while at + 8 <= len(data):
        tag, length = data[at:at + 4], int.from_bytes(data[at + 4:at + 8], 'little')
        require(at + 8 + length <= len(data), f'its {tag.decode("latin-1")!r} chunk runs past the end of the file')
        if tag == b'fmt ':
            require(fmt is None and length >= 16, 'one fmt chunk of at least 16 bytes')
            body = data[at + 8:at + 24]
            fmt = tuple(int.from_bytes(body[a:b], 'little') for a, b in ((0, 2), (2, 4), (4, 8), (14, 16), (12, 14)))
        elif tag == b'data':
            require(fmt is not None and size is None, 'one data chunk, after the fmt chunk')
            size = length
        at += 8 + length + (length & 1)
    require(fmt is not None and size is not None, 'no fmt or data chunk')
    return (*fmt[:4], size, fmt[4])


def read_samples(root, folder):
    """Each <name>.wav in the machine's samples folder, by name: 16-bit PCM (format 1) at
    44.1 kHz, mono or stereo, at most SAMPLE_BYTES each."""
    path = root / _relative(root, folder)
    require(path.is_dir(), 'samples names a folder: ' + folder)
    files = sorted(p for p in path.iterdir() if p.suffix != '.md' and not p.name.startswith('.'))
    require(0 < len(files) <= SAMPLE_COUNT, f'A samples folder holds 1 to {SAMPLE_COUNT} samples')
    samples = {}
    for file in files:
        require(file.is_file() and file.resolve() == file and file.suffix == '.wav' and SAMPLE_NAME.fullmatch(file.stem),
                f'Invalid sample file {file.name}: <name>.wav, the name lowercase letters, digits, - and _, at most 24')
        require(file.stat().st_size <= SAMPLE_BYTES, f'{file.name}: a sample is at most {SAMPLE_BYTES} bytes')
        try:
            tag, channels, rate, bits, size, align = wav_format(file.read_bytes())
        except ValueError as error:
            raise ValueError(f'{file.name}: not a PCM WAV file: {error}') from error
        require(tag != WAVE_FORMAT_EXTENSIBLE,
                f'{file.name}: WAVE_FORMAT_EXTENSIBLE; save it as plain 16-bit PCM (format 1)')
        require(tag == WAVE_FORMAT_PCM and bits == 16 and rate == SAMPLE_RATE and channels in (1, 2) and
                align == channels * 2 and 0 < size and size % align == 0,
                f'{file.name}: a sample is 16-bit PCM at {SAMPLE_RATE} Hz, mono or stereo')
        samples[file.stem] = Sample(file, size // align, channels)
    return samples


def check_region(region, identity, samples):
    """The sampler region of a machine's own preset, as stock saves it, pointing at one of
    the machine's own samples by its installed path."""
    require(isinstance(region, dict) and REGION_REQUIRED <= set(region) <=
            REGION_REQUIRED | REGION_INTEGERS | REGION_FLAGS, 'A sample region holds stock region fields only')
    require(all(type(region[k]) is int for k in REGION_INTEGERS & set(region)) and
            all(type(region[k]) is bool for k in REGION_FLAGS & set(region)), 'Invalid sample region values')
    named = {sample_path(identity, name): sample for name, sample in samples.items()}
    require(region['sample'] in named, f'A region names one of the machine\'s own samples, as '
            f'{sample_path(identity, "<name>")}')
    frames = named[region['sample']].frames
    start, end = region.get('sample.start', 0), region['sample.end']
    require(region['framecount'] == frames and 0 <= start < end <= frames,
            f'A region\'s framecount is its sample\'s ({frames}) and its range lies inside it')
    if 'loop.start' in region or 'loop.end' in region:
        require(start <= region.get('loop.start', start) <= region.get('loop.end', end) <= end,
                f'A region\'s loop lies inside its sample.start..sample.end ({start}..{end})')
    require(all(0 <= region[k] <= 127 for k in ('hikey', 'pitch.keycenter') if k in region), 'Region keys are 0..127')
    low, high = REGION_GAIN
    require(low <= region.get('gain', 0) <= high, f'A region\'s gain is {low}..{high} dB')
    require(all(region.get(k, 0) == 0 for k in REGION_NEUTRAL),
            'A region keeps ' + ', '.join(REGION_NEUTRAL) + ' at 0: the SDK doesn\'t record their saved ranges; '
            'pitch.keycenter tunes it')
    return region


def drum_preset_regions(regions):
    """Sample-free Drumline records carrying the public kit's packed pad words."""
    return isinstance(regions, list) and len(regions) == 24 and all(
        isinstance(region, dict) and {'model', 'params'} <= set(region) and
        set(region) <= {'model', 'params', 'playmode', 'hikey'} and region['model'] == 'fm slap' and
        isinstance(region['params'], list) and len(region['params']) == 4 and
        all(type(n) is int and 0 <= n <= 32767 for n in region['params']) and
        region.get('playmode', 'oneshot') in ('gate', 'oneshot', 'mute1', 'loop', 'mute2', 'mute3') and
        ('hikey' not in region or type(region['hikey']) is int and region['hikey'] == 53 + index)
        for index, region in enumerate(regions))


def check_preset(value, identity, samples=None, shift=False, kind='synth', pad_mix=False):
    """A preset as the device saves it (version 4), for this machine, pointing at no samples
    or, for a sample engine shipping its own (SDK 1.10), at one of them. A synth with a
    Shift row (SDK 1.13) saves it as Drumline saves its keys, in sample-less regions
    (docs/abi-1.2.1.md#a-synths-shift-row), which it may keep. A drum kit without padMix
    (SDK 1.21) may keep its 24 pad records; a padMix kit's records also carry each key's
    level and pad word (`gain`, `random`), whose saved form the SDK does not check yet, so
    its presets keep empty regions."""
    engine = value.get('engine') if isinstance(value, dict) else None
    params = engine.get('params') if isinstance(engine, dict) else None
    found = value.get('regions') if isinstance(value, dict) else None
    require(isinstance(value, dict) and value.get('version') == 4 and value.get('platform') == 'OP-XY' and
            value.get('type') == identity and isinstance(found, list) and isinstance(params, list) and
            len(params) == 8 and all(type(n) is int and 0 <= n <= 32767 for n in params),
            f'A preset is a version 4 OP-XY preset of {identity}, with eight engine params')
    if kind == 'drum' and found:
        require(not pad_mix, f"A preset of the padMix kit {identity} has no regions: its keys' level, "
                'pad controls and sends are not checked in shipped presets')
        require(drum_preset_regions(found), f'A drum preset of {identity} holds 24 sample-free packed pad records')
    elif samples:
        require(len(found) <= 1, f'A preset of {identity} has at most 1 sample region')
        for region in found:
            check_region(region, identity, samples)
    elif shift:
        require(len(found) <= 24 and all(isinstance(r, dict) and not r.get('sample') for r in found),
                f'A preset of {identity} keeps its Shift row in at most 24 regions, none pointing at a sample')
    else:
        require(found == [], f'A preset of {identity} has no sample regions: only a sample engine '
                'shipping its own samples ("samples") points at them')
    return value


def reject_constant(name):
    raise ValueError(f'{name} is not JSON')


def read_presets(root, folder, identity, samples=None, shift=False, kind='synth', pad_mix=False):
    """Each <name>.json in the machine's presets folder, by name."""
    path = root / _relative(root, folder)
    require(path.is_dir(), 'presets names a folder: ' + folder)
    # Notes and hidden files, such as a Finder .DS_Store, are not presets.
    files = sorted(p for p in path.iterdir() if p.suffix != '.md' and not p.name.startswith('.'))
    require(0 < len(files) <= PRESET_COUNT, f'A presets folder holds 1 to {PRESET_COUNT} presets')
    presets = {}
    for file in files:
        require(file.is_file() and file.suffix == '.json' and PRESET_NAME.fullmatch(file.stem),
                f'Invalid preset file {file.name}: <name>.json, the name lowercase letters, digits, - and _, '
                'at most 24')
        data = file.read_bytes()
        require(len(data) <= PRESET_BYTES, f'{file.name}: a preset is at most {PRESET_BYTES} bytes')
        try:
            value = json.loads(data.decode('utf-8'), parse_constant=reject_constant)
        except ValueError as error:
            raise ValueError(f'{file.name}: invalid JSON: {error}') from error
        presets[file.stem] = check_preset(value, identity, samples, shift, kind=kind, pad_mix=pad_mix)
    return presets


def check_identity(rules, kind, value, display=None):
    """IDs a machine may claim under the profile's identity rules (the browser builder applies the same rules).

    Engines take a free entry of the relocated table, never the stock "none" value; a
    synth draws its own page above the stock "none" page, a sample engine an ID only
    the relocated table holds and a stock sample page or, drawing its own screen (display
    "drawing"), a relocated page, an effect a free FX ID on the FX page or, drawing its
    own, on a relocated page.
    """
    require(isinstance(value, dict), 'machine.json needs an identity')
    if kind == 'player':
        require(set(value) == {'player', 'page'} and all(type(v) is int for v in value.values()),
                'Players need a player type and their own page')
        require(value['player'] in rules['players'], 'Players use a type stock leaves unused')
        require(rules['page_none'] < value['page'] < rules['pages'], 'Players need a relocated page')
        return dict(value)
    slot = 'effect' if kind == 'effect' else 'engine'
    require(set(value) == {slot, 'page'}, 'Unexpected identity fields')
    number, page = value[slot], value['page']
    require(type(number) is int and type(page) is int, 'IDs must be integers')
    if kind == 'effect':
        own = display == 'drawing' or display is None and page != rules['effect_page']
        require(number in rules['effects'] and (rules['page_none'] < page < rules['pages'] if own
                                                 else page == rules['effect_page']),
                'Effects use an FX ID stock leaves unregistered, on the stock FX page or, drawing their own, '
                'a relocated page')
        return {slot: number, 'page': page}
    require(0 < number < rules['engines'] and number != rules['engine_none'] and number not in rules['stock_engines'],
            'Engine ID outside the relocated table, the stock "none" value or registered by stock')
    if kind == 'sample':
        own = display == 'drawing' or display is None and page not in rules['sample_pages']
        require(rules['engine_none'] < number and (rules['page_none'] < page < rules['pages'] if own
                                                   else page in rules['sample_pages']),
                'Sample engines need a relocated-table engine ID and a stock sample page or, drawing their own '
                'screen, a relocated page')
    else:
        require(rules['page_none'] < page < rules['pages'], 'A machine with its own screen needs a relocated page')
    return {slot: number, 'page': page}


def requested_numbers(rules, kind, value, version, display='stock'):
    """The numbers machine.json asks for. Before SDK 1.5 it lists them, as preferences a
    layout gives when free; from 1.5 it lists none (None) and a sample engine names its
    base, the stock sampler whose page it shares, or (SDK 1.8, display "drawing", only
    forking the sampler) asks for a page of its own as a synth does. An effect uses the
    stock FX page, or (SDK 1.7, display "drawing") asks for a page of its own too."""
    if kind == 'player':
        require_sdk(version, (1, 8), 'Note players')
        require('identity' not in value and 'base' not in value, 'Players leave their type and page to the layout')
        return {'player': None, 'page': None}
    if version < (1, 5):
        if 'base' in value:
            require_sdk(version, (1, 5), 'A sample base ("base")')
        return check_identity(rules, kind, value.get('identity'), display)
    bases = SAMPLE_BASES
    names = ' or '.join('"%s"' % name for name in bases)
    require('identity' not in value, 'From SDK 1.5 machine.json has no identity: each build gives the numbers. '
            'Remove "identity"; a sample engine names its base instead ("base": %s)' % names)
    if kind != 'sample':
        require('base' not in value, 'Only a sample engine names a base')
        if kind == 'effect':
            return {'effect': None, 'page': None if display == 'drawing' else rules['effect_page']}
        return {'engine': None, 'page': None}
    # A list or object is no name; checked first, since it cannot be looked up.
    require(isinstance(value.get('base'), str) and value['base'] in bases, 'A sample engine names the stock sampler it forks: "base": ' + names)
    if display == 'drawing':
        require(value['base'] == 'sampler', 'Only a sample engine forking the sampler ("base": "sampler") draws its own screen')
        return {'engine': None, 'page': None}
    return {'engine': None, 'page': bases[value['base']]}


def check_request(rules, kind, value):
    """Numbers a machine may ask for, as a builder kit lists them (builder/core.mjs
    validRequest): valid ones, or the SDK 1.5 forms that leave them to the layout."""
    if kind == 'player':
        forms = [{'player': None, 'page': None}]
    elif kind == 'effect':
        forms = [{'effect': None, 'page': rules['effect_page']}, {'effect': None, 'page': None}]
    else:
        forms = [{'engine': None, 'page': None}]
        if kind == 'sample':
            forms += [{'engine': None, 'page': page} for page in SAMPLE_BASES.values()]
    return value if value in forms else check_identity(rules, kind, value)


def fits(requested, numbers):
    """Whether numbers are what a machine asks for: the same fields, equal where it names one."""
    return set(requested) == set(numbers) and all(v is None or numbers[k] == v for k, v in requested.items())


def sdk_version(text):
    """A machine's declared SDK version, checked against this SDK."""
    require(text is not None, 'machine.json needs "sdk": "%d.%d". A machine from before SDK 1.0: '
            'see docs/display.md#migrating-from-before-sdk-10' % SDK_VERSION)
    require(isinstance(text, str) and re.fullmatch(r'\d+\.\d+', text), 'machine.json needs "sdk": "<major>.<minor>"')
    major, minor = map(int, text.split('.'))
    current = '%d.%d' % SDK_VERSION
    require(major == SDK_VERSION[0] and minor <= SDK_VERSION[1],
            f'The machine targets SDK {text}; this SDK is {current} and builds {SDK_VERSION[0]}.0 to {current}')
    return major, minor


def require_sdk(version, minimum, feature):
    """A feature newer than the SDK version the machine declares is refused, not misread."""
    require(version >= minimum, f'{feature} needs "sdk": "%d.%d" in machine.json' % minimum)


# A machine's own Blackfin routines (docs/api.md, "Native routines"): native/<name>.s,
# described by native/<name>.json. A firmware build assembles each into the machine;
# every other build runs the C reference its header pairs with it (opxy/native.hpp).
NATIVE = 'native'
NATIVE_NAME = re.compile(r'[a-z][a-z0-9_]{0,31}')
NATIVE_FIELDS = {'symbol', 'stackBytes', 'stateBytes', 'range', 'reference'}
NATIVE_UNITS = 32
NATIVE_SOURCE_BYTES = 256 * 1024
# The most stack one routine may declare: an audio callback runs on stock's stack.
NATIVE_STACK_BYTES = 2048
NATIVE_STATE_BYTES = 64 * 1024
ASSEMBLY_SUFFIXES = ('.s', '.asm', '.sx')


@dataclass(frozen=True)
class NativeUnit:
    name: str          # native/<name>.s
    path: Path
    symbol: str        # its one exported routine, as C names it: <id>_native_...
    stack_bytes: int   # the most stack it takes below its caller's SP, callees aside
    state_bytes: int   # writable .data and .bss it may hold (0: none)
    range: str         # the inputs it supports, for its readers
    reference: str     # the C reference that defines it, for its readers


def read_native(root, identity):
    """A machine's native units, each checked against its manifest. Assembly lives only
    in native/ (vendored code, never built, aside)."""
    folder = root / NATIVE
    for path in tree(root):
        parts = path.relative_to(root).parts
        if path.suffix.lower() in ASSEMBLY_SUFFIXES and parts[0] != 'vendor':
            require(len(parts) == 2 and parts[0] == NATIVE and path.suffix == '.s',
                    f'{identity}: assembly lives in native/<name>.s, not {"/".join(parts)}')
    if not folder.exists():
        return ()
    require(folder.is_dir() and folder.resolve() == folder, f'{identity}: native/ is a folder')
    names = sorted(p.stem for p in folder.iterdir() if p.suffix == '.s')
    manifests = sorted(p.stem for p in folder.iterdir() if p.suffix == '.json')
    require(names == manifests, f'{identity}: each native/<name>.s has its manifest native/<name>.json, and only those')
    require(len(names) <= NATIVE_UNITS, f'{identity}: at most {NATIVE_UNITS} native units')
    units = []
    for name in names:
        source, manifest = folder / (name + '.s'), folder / (name + '.json')
        where = f'{identity}/native/{name}'
        require(NATIVE_NAME.fullmatch(name) is not None, where + ': native unit names are lowercase C identifiers')
        require(source.resolve() == source and manifest.resolve() == manifest and
                0 < source.stat().st_size <= NATIVE_SOURCE_BYTES, where + ': missing, redirected or oversized')
        record = json.loads(manifest.read_text(encoding='utf-8'))
        require(isinstance(record, dict) and {'symbol', 'stackBytes', 'range'} <= set(record) <= NATIVE_FIELDS,
                where + '.json: symbol, stackBytes and range, and optionally stateBytes and reference')
        symbol, stack, state = record['symbol'], record['stackBytes'], record.get('stateBytes', 0)
        require(isinstance(symbol, str) and re.fullmatch(identity + r'_native_[a-z0-9_]+', symbol) and len(symbol) <= 64,
                where + f'.json: symbol starts with {identity}_native_, its machine\'s own prefix')
        require(type(stack) is int and 0 <= stack <= NATIVE_STACK_BYTES and stack % 4 == 0,
                where + f'.json: stackBytes is a multiple of 4 from 0 to {NATIVE_STACK_BYTES}')
        require(type(state) is int and 0 <= state <= NATIVE_STATE_BYTES,
                where + f'.json: stateBytes is 0 to {NATIVE_STATE_BYTES}')
        for field in ('range', 'reference'):
            require(isinstance(record.get(field, ''), str) and len(record.get(field, '')) <= 400,
                    where + f'.json: {field} is text of at most 400 characters')
        require(record['range'].strip(), where + '.json: range says which inputs the routine supports')
        validate_native_assembly(source.read_text(encoding='utf-8'), where + '.s', identity, symbol, state > 0)
        units.append(NativeUnit(name=name, path=source, symbol=symbol, stack_bytes=stack, state_bytes=state,
                                range=record['range'], reference=record.get('reference', '')))
    require(len({u.symbol for u in units}) == len(units), f'{identity}: each native unit exports its own symbol')
    return tuple(units)


def reads_sample_overview(root, header='sample_overview'):
    """Whether a machine's own C++, its tests and tools aside, includes the overview header
    (or another SDK header named by `header`)."""
    include = re.compile(r'^\s*#\s*include\s*[<"]opxy/' + header + r'\.hpp[>"]', re.M)
    return any(include.search(path.read_text(encoding='utf-8', errors='replace')) for path in tree(root)
               if path.suffix in ('.cc', '.cpp', '.h', '.hpp') and path.relative_to(root).parts[0] not in ('tests', 'tools'))


# The kinds of Controls.instrument (opxy/player.hpp), outside comments. Reading the member
# itself without the declaration does not compile: the member exists only with it.
TRACK_INSTRUMENT = re.compile(r'\b(?:Other|Drum)Instrument\b')
COMMENTS = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)


def reads_track_instrument(root):
    """Whether a machine's own C++, its tests and tools aside, names the kinds of
    Controls.instrument, which only a player declaring resources.trackInstrument has."""
    return any(TRACK_INSTRUMENT.search(COMMENTS.sub(' ', path.read_text(encoding='utf-8', errors='replace')))
               for path in tree(root)
               if path.suffix in ('.cc', '.cpp', '.h', '.hpp') and path.relative_to(root).parts[0] not in ('tests', 'tools'))


# The device identity's reader by name: code that declares it itself, without the header.
DEVICE_IDENTITY_READER = re.compile(r'\bopxy_device_identity\b')


def reads_device_identity(root):
    """Whether a machine's own C++, its tests and tools aside, names the device identity's reader."""
    return any(DEVICE_IDENTITY_READER.search(path.read_text(encoding='utf-8', errors='replace')) for path in tree(root)
               if path.suffix in ('.cc', '.cpp', '.h', '.hpp') and path.relative_to(root).parts[0] not in ('tests', 'tools'))


def machine_at(root, identity=None):
    """The machine in a folder; its definition names it."""
    root = Path(os.path.abspath(root))
    path = root / 'machine.json'
    require(path.is_file() and path.resolve() == path.absolute() and root.resolve() == root,
            'Unknown or redirected machine: ' + str(identity or root))
    value = json.loads(path.read_text(encoding='utf-8'))
    if identity is None:
        named = json.loads((root / 'definition.json').read_text(encoding='utf-8'))
        identity = named.get('id') if isinstance(named, dict) else None
        stock_name(identity)
    require(isinstance(value, dict) and value.get('schema') == 1 and
            set(value) <= FIELDS,
            'Unexpected machine.json fields: ' + identity)
    version = sdk_version(value.get('sdk'))
    kind = value.get('kind')
    require(kind in KINDS, 'Unknown machine kind: ' + str(kind))
    if kind == 'drum':
        require_sdk(version, (1, 8), 'A synthesis drum kit')
    require(isinstance(value.get('description'), str) and 0 < len(value['description']) <= 160,
            'Machine needs a short description')
    sources = tuple(_relative(root, s) for s in value.get('sources', ()))
    includes = tuple(_relative(root, s) for s in value.get('includes', ()))
    require(all(s.endswith(('.cc', '.cpp')) for s in sources), 'Extra sources must be C++ translation units')
    icon = value.get('icon', '')
    require(icon == '' or isinstance(icon, str) and ICON.fullmatch(icon), 'icon is SVG path data')
    definition = validate_definition(json.loads((root / 'definition.json').read_text(encoding='utf-8')), kind)
    if kind == 'drum' and any(definition['resources'].get(k) for k in ('padMix', 'padControls')):
        # One gate for a kit's per-key mixer: pan, level, pad controls and sends.
        require_sdk(version, (1, 10), "A drum kit's per-key mixer (padMix, padControls)")
    if kind == 'drum' and any(definition['resources'].get(k) for k in DRUM_KIT_CONTROLS):
        require_sdk(version, KIT_CONTROLS_SDK, 'The drum-kit controls (padVolume, padRandom, ampEnvelope)')
    if definition['resources'].get('parameterEdit'):
        require_sdk(version, (1, 21), 'Per-model drum value editing (parameterEdit)')
    if definition['resources'].get('modelEdit'):
        require_sdk(version, SDK_1_25, 'Drum value editing across models (modelEdit)')
    # The declaration itself, even as 0, needs 1.31.
    if 'trackInstrument' in definition['resources']:
        require_sdk(version, TRACK_INSTRUMENT_SDK, "A note player's track instrument (trackInstrument)")
    # Only a set flag needs 1.12: an explicit 0 is the mono, callback-free default.
    if kind == 'synth' and any(definition['resources'].get(k) for k in ('stereoOutput', 'releaseCallback')):
        require_sdk(version, (1, 12), 'Stereo synth output and voice release callbacks (stereoOutput, releaseCallback)')
    if definition['resources'].get('sliceTiming'):
        require_sdk(version, (1, 7), 'A sample slicing clock (sliceTiming)')
    if 'bufferBytes' in definition['resources']:
        require_sdk(version, (1, 2), 'An effect buffer (bufferBytes)')
    if definition['resources'].get('transport'):
        require_sdk(version, (1, 2), "An effect's tempo (transport)")
    if definition['resources'].get('busShift'):
        require_sdk(version, (1, 16), "An effect's bus shift (busShift)")
    if 'tailFrames' in definition['resources']:
        require_sdk(version, IDLE_SDK, "An effect's declared tail (tailFrames)")
    if 'trackStage' in definition['resources']:
        require_sdk(version, track_stage.TRACK_STAGE_SDK, "A synth's track stage (trackStage)")
        if 'audio' in definition['resources']['trackStage']:
            require_sdk(version, AUDIO_SDK, "A track stage's audio snapshots (trackStage audio)")
    if 'deviceIdentity' in definition['resources']:
        require_sdk(version, DEVICE_IDENTITY_SDK, 'The device identity (deviceIdentity)')
    if any(p['display'] == 'division' for p in definition['parameters']):
        require_sdk(version, (1, 2), 'A division control')
    if kind == 'sample' and definition['resources']['voiceCapacity'] < STOCK_VOICES:
        require_sdk(version, (1, 17), 'A sample engine with fewer than 256 voices (voiceCapacity)')
    if kind == 'sample' and 'display' in definition['resources']:
        require_sdk(version, (1, 8), "A sample engine's own screen (resources.display)")
    if 'stockShift' in definition['resources']:
        require_sdk(version, (1, 10), "Shift for the stock sampler page's controls (stockShift)")
    if paged(definition):
        require_sdk(version, (1, 3), 'A press that switches the page ("press": "page")')
        if len(definition['parameters']) < 8:
            require_sdk(version, (1, 4), 'A partially filled second page')
    if 'hidePages' in definition:
        require_sdk(version, (1, 9), 'Hidden stock pages ("hidePages")')
    if kind == 'effect' and paged(definition):
        require_sdk(version, (1, 7), 'An effect whose press switches the page ("press": "page")')
    if kind == 'player' and paged(definition):
        require_sdk(version, (1, 16), 'A note player whose press switches the page ("press": "page")')
    if kind == 'player' and definition.get('press') == 'click':
        require_sdk(version, (1, 16), 'A note player whose presses are only its clicks ("press": "click")')
    if shift_parameters(definition) and kind == 'player':
        require_sdk(version, (1, 16), 'A player\'s shift row ("shift")')
    if shift_parameters(definition):
        require_sdk(version, (1, 7), 'An effect\'s shift row ("shift")')
    if synth_shift(definition):
        require_sdk(version, (1, 13), 'A synth\'s Shift row ("shift")')
    if kind == 'synth' and definition['resources']['display']['texts'] > SDK_1_2_TEXTS:
        require_sdk(version, (1, 3), 'More than %d display text slots' % SDK_1_2_TEXTS)
    if 'display' in definition['resources'] and definition['resources']['display']['texts'] > SDK_1_6_TEXTS:
        require_sdk(version, (1, 7), 'More than %d display text slots' % SDK_1_6_TEXTS)
    if 'display' in definition['resources'] and extended_display(definition['resources']['display']):
        require_sdk(version, EXTENDED_SDK, 'Extended drawing (docs/display.md#extended-drawing)')
        if kind in ('drum', 'player'):
            require_sdk(version, EXTENDED_SCREENS_SDK, "Extended drawing on a drum kit's screen or a note player's page")
    if any(k in definition['resources'].get('display', {}) for k in AUDIO_SDK_KEYS):
        require_sdk(version, AUDIO_SDK, "Glyph atlases, a screen's own audio and output levels (docs/display.md)")
    if definition['resources'].get('display', {}).get('nodes', 0) > SDK_1_7_NODES:
        require_sdk(version, (1, 8), 'More than %d display nodes' % SDK_1_7_NODES)
    require(definition['id'] == identity, 'Machine directory and definition identity disagree')
    grayscale = graphics_path(value)
    require(not grayscale or kind == 'synth' or 'display' in definition['resources'],
            'An effect\'s grayscale artwork needs its own page (resources.display)')
    # After the feature checks, which name the SDK version each feature needs.
    requested = requested_numbers(config()['identities'], kind, value, version,
                                  'drawing' if 'display' in definition['resources'] else 'stock')
    dry_parameter = None
    if 'dryParameter' in value:
        require(kind == 'effect' and isinstance(value['dryParameter'], str),
                'dryParameter names a normalized control on an effect')
        matches = [i for i, p in enumerate(definition['parameters']) if p['id'] == value['dryParameter']]
        require(len(matches) == 1, 'dryParameter must name a declared control')
        dry_parameter = matches[0]
        require(definition['parameters'][dry_parameter]['mapping'] ==
                dict(kind='linear', minimum=0, maximum=1), 'dryParameter must map linearly from 0 to 1')
    # A fixed source dry instead: 0 takes a sent track's dry wholly into the
    # effect at full send, as an effect inline would.
    dry_level = None
    if 'dryLevel' in value:
        require_sdk(version, (1, 7), 'A fixed source dry ("dryLevel")')
        level = value['dryLevel']
        require(kind == 'effect' and 'dryParameter' not in value and type(level) in (int, float) and 0 <= level <= 1,
                'dryLevel is a number from 0 to 1 on an effect without dryParameter')
        dry_level = round(level * 0x7fffffff)
    # A synth or drum kit draws its own screen, and so may an effect (SDK 1.7), a sample
    # engine (SDK 1.8) or a note player (SDK 1.26); other sample engines and effects use
    # a stock page, and other players the SDK's dot page.
    drawn = kind in ('synth', 'drum') or 'display' in definition['resources']
    display = 'drawing' if drawn else 'player' if kind == 'player' else 'stock'
    if kind == 'effect' and display == 'drawing':
        require_sdk(version, (1, 7), "An effect's own page (resources.display)")
    if kind == 'player' and display == 'drawing':
        require_sdk(version, PLAYER_DRAWING_SDK, "A note player's own drawn page (resources.display)")
        require((root / 'display.cpp').is_file() and (root / 'display.h').is_file(),
                'A player that draws its own page supplies display.h and display.cpp')
    if reads_sample_overview(root):
        require_sdk(version, (1, 8), 'A sample overview (opxy/sample_overview.hpp)')
        require(kind == 'sample' and display == 'drawing', "Only a sample engine's own screen reads a sample overview")
    if reads_sample_overview(root, 'synth_shift'):
        require_sdk(version, (1, 13), "A synth's Shift row on its display (opxy/synth_shift.hpp)")
        require(synth_shift(definition), 'Only a synth with a Shift row ("shift") reads opxy/synth_shift.hpp')
    if reads_sample_overview(root, 'control_interaction'):
        require_sdk(version, EXTENDED_SDK, 'The last control touched (opxy/control_interaction.hpp)')
        require(kind == 'synth', "Only a synth's own screen reads opxy/control_interaction.hpp")
    # From SDK 1.31 Controls.instrument exists only for a player declaring it (opxy/player.hpp); a
    # player declaring SDK 1.25 to 1.30 has it as before (adapters.player_instrument).
    require(kind != 'player' or version < TRACK_INSTRUMENT_SDK or definition['resources'].get('trackInstrument', 0) == 1 or
            not reads_track_instrument(root),
            "A note player reading its track's instrument (Controls.instrument) declares resources.trackInstrument "
            '(SDK %d.%d)' % TRACK_INSTRUMENT_SDK)
    if reads_sample_overview(root, 'sample_kernel'):
        require_sdk(version, (1, 17), 'The sample kernel (opxy/sample_kernel.hpp)')
    # Only a firmware with a machine declaring it links the identity's device code.
    if reads_sample_overview(root, 'device_identity') or reads_device_identity(root):
        require(definition['resources'].get('deviceIdentity', 0) == 1,
                'A machine that includes opxy/device_identity.hpp declares "deviceIdentity": 1 in its resources')
    native = read_native(root, identity)
    # The native plan compiles extra translation units only into synths.
    if kind not in ('synth', 'drum'):
        require(not sources and not includes,
                'Sample engines, effects and players compile one sample.cpp, fx.cpp or player.cpp '
                '(and an effect or a drawing player its display.cpp)')
    # Pad settings live in the stock drum page's pad records, whose fade the SDK then owns.
    if kind == 'sample' and definition['resources'].get('padSettings', 0):
        require(requested['page'] == SAMPLE_BASES['drumSampler'],
                'Pad settings and drum controls need the drum sampler base ("base": "drumSampler")')
    if 'trackControl' in definition['resources']:
        require_sdk(version, (1, 9), 'Track control (trackControl)')
        require((root / 'control.cpp').is_file(), 'A synth with track control supplies control.cpp')
    defaults = value.get('presetDefaults', {})
    require(isinstance(defaults, dict) and set(defaults) <= {'amp', 'volume'}, 'Invalid preset defaults')
    require('volume' not in defaults or (type(defaults['volume']) is int and 0 <= defaults['volume'] <= 32767),
            'Invalid preset volume default')
    amp = defaults.get('amp', {})
    require(isinstance(amp, dict) and set(amp) <= {'attack', 'decay', 'release', 'sustain'} and
            all(type(v) is int and 0 <= v <= 32767 for v in amp.values()), 'Invalid preset amp defaults')
    samples = None
    if 'samples' in value:
        require_sdk(version, (1, 10), "A sample engine's own samples (samples)")
        # The sampler plays one sample on every key. The drum sampler's 24 pad regions also
        # save a play mode and a choke group, which these checks don't cover yet.
        require(kind == 'sample' and value.get('base') == 'sampler' and isinstance(value['samples'], str),
                'samples names the sample folder of a sample engine forking the sampler ("base": "sampler")')
        samples = read_samples(root, value['samples'])
    presets = None
    if 'presets' in value:
        require_sdk(version, (1, 3), "A machine's own presets")
        require(kind in ('synth', 'drum', 'sample') and isinstance(value['presets'], str), 'presets names an instrument preset folder')
        presets = read_presets(root, value['presets'], identity, samples, shift=bool(synth_shift(definition)), kind=kind,
                               pad_mix=kind == 'drum' and bool(definition['resources'].get('padMix', 0)))
        if kind == 'drum' and any(preset['regions'] for preset in presets.values()):
            require_sdk(version, (1, 21), 'Sample-free drum-kit preset records')
    if samples:
        # Every sample is there for a preset: one no preset loads only takes up the player's storage.
        loaded = {region['sample'] for preset in (presets or {}).values() for region in preset['regions']}
        unused = [name for name in samples if sample_path(identity, name) not in loaded]
        require(not unused, 'Each of a machine\'s own samples is loaded by one of its presets ("presets"); '
                'none loads ' + ', '.join(unused))
    if grayscale:
        path = root / grayscale
        require(path.resolve() == path and path.is_file() and 0 < path.stat().st_size <= 65536,
                'Missing, redirected or oversized grayscale artwork: ' + grayscale)
    result = Machine(id=identity, kind=kind, name=definition['name'], description=value['description'],
                   display=display, sources=sources, includes=includes,
                   definition=definition,
                   identity=requested, preset_defaults=defaults,
                   root=root, icon=icon, dry_parameter=dry_parameter, dry_level=dry_level, presets=presets, samples=samples,
                   grayscale=grayscale, native=native, sdk=version)
    if grayscale:
        from .native import validate_panel
        validate_panel(result)
    return result


# Machine numbers. Projects store a track's engine, effect and page numbers, so a
# machine keeps its numbers in a firmware for good. The first Open-XY firmware gave
# its own machines fixed numbers, held in every firmware whether or not it holds the
# machine (FIXED_NUMBERS). Every other machine is given numbers by a layout: the firmware's record of
# every number it has given out, including to machines since dropped, which stay
# held so an old project never opens another machine. Machines new to a layout get
# the numbers they ask for when free (only machines before SDK 1.5 ask for any), then
# each number still missing from its own place in the pool: preferred(), so a build
# without a layout gives a machine the same numbers whatever else it holds, unless
# two machines' places collide. Machines are numbered in ID order, never collection order.
# Engines never take an effect number (number_layout reserved), and a layout's engine number
# among them, which earlier SDKs gave, is let go once: the machine keeps its page.
# builder/core.mjs numberLayout applies the same rules (tests/fixtures/numbers.json).
LAYOUT_FORMAT = 'opxy-layout/1'
FIXED_NUMBERS = {'fm2op': {'engine': 34, 'page': 82}, 'macroosc': {'engine': 26, 'page': 83},
                 'ringmod': {'effect': 1, 'page': 21}, 'timestretch': {'engine': 35, 'page': 7}}


def number_pools(rules):
    """The numbers a machine new to a layout may be given, lowest first. Engines come only
    from the relocated table above stock's "none" value: the gaps in stock's own table
    below it are left for stock."""
    engines = [n for n in range(rules['engine_none'] + 1, rules['engines']) if n not in rules['stock_engines']]
    return dict(engine=engines, page=list(range(rules['page_none'] + 1, rules['pages'])), effect=list(rules['effects']), player=list(rules['players']))


def fnv1a(text):
    """FNV-1a, 32-bit, of the text's UTF-8 bytes."""
    value = 0x811c9dc5
    for byte in text.encode('utf-8'):
        value = (value ^ byte) * 0x01000193 & 0xffffffff
    return value


def preferred(pool, name):
    """A pool's numbers in the order a machine new to a layout takes the first free one:
    from the machine's own place, FNV-1a of its ID modulo the pool's size, upward and
    wrapping around."""
    start = fnv1a(name) % len(pool)
    return pool[start:] + pool[:start]


def occupied(rules, identity):
    """What an identity holds: its engine or effect number, and its own page. Effects share
    the FX page and sample engines a stock sample page. Stock reads a send track's effect
    number as its engine, so an effect also holds that engine number: no engine shares it."""
    keys = [(slot, identity[slot]) for slot in ('engine', 'effect', 'player') if slot in identity]
    if 'effect' in identity:
        keys.append(('engine', identity['effect']))
    if identity['page'] not in rules['sample_pages'] and identity['page'] != rules['effect_page']:
        keys.append(('page', identity['page']))
    return keys


def read_layout(path, build=False):
    """A layout file's numbers by machine ID. The builder's saved selection is a layout too.

    A build refuses a layout from the builder's experimental memory, which SDK
    builds do not offer; companions need only the numbers."""
    value = json.loads(Path(path).read_text(encoding='utf-8'))
    profile = config()
    require(isinstance(value, dict) and value.get('format') == LAYOUT_FORMAT and isinstance(value.get('machines'), dict),
            f'{path} is not a machine layout ({LAYOUT_FORMAT})')
    # Earlier profiles with the same identity rules number machines the same way.
    require(value.get('profile') in (profile['id'], *profile.get('layout_profiles', ())),
            f"{path} is a layout for {value.get('profile')}, not {profile['id']}")
    # The builder's experimental 4 MiB memory (profile experimental_memory) is
    # builder-only; an SDK build from its layout would quietly use 2 MiB.
    # The same shape the builder accepts (builder/core.mjs readLayout): neither member, or both,
    # reserved_bytes read as JavaScript's JSON.parse reads it (a double, e.g. 4194304.0).
    reserved = value.get('reserved_bytes')
    try:
        reserved = float(reserved) if type(reserved) in (int, float) else None
    except OverflowError:
        reserved = None  # JavaScript reads it as Infinity
    require(('memory' not in value and 'reserved_bytes' not in value) or
            (value.get('memory') == 'expanded' and reserved is not None and reserved.is_integer() and
             reserved > 0 and reserved % (1024 * 1024) == 0),
            f'{path} has an invalid memory choice')
    require(not build or 'memory' not in value, f"{path} is from a build with the builder's experimental memory; "
            'sdk.py builds use standard memory. Remove "memory" and "reserved_bytes" from a copy to build with standard memory.')
    return check_layout_numbers(value['machines'], 'the layout')


def check_layout_numbers(machines, where):
    """Layout numbers by machine ID: an engine and page, or an effect and page, each."""
    for name, identity in machines.items():
        stock_name(name)
        require(isinstance(identity, dict) and set(identity) in ({'engine', 'page'}, {'effect', 'page'}, {'player', 'page'}) and
                all(type(n) is int for n in identity.values()), f'{name}: invalid numbers in {where}')
    return machines


def layout_document(numbers):
    return {'format': LAYOUT_FORMAT, 'profile': config()['id'], 'machines': dict(sorted(numbers.items()))}


def owns_page(rules, kind, page):
    """Whether a machine that needn't draw (an effect or a sample engine) is on a page of its own."""
    if kind == 'effect':
        return page != rules['effect_page']
    return kind == 'sample' and page not in rules['sample_pages']


def number_layout(rules, fixed, layout, machines):
    """The layout after numbering `machines`: every number `layout` held, the fixed
    machines' and any given out now. Machines are (id, kind, requested identity), None
    where they ask for no number; fixed ones (id, kind, identity). A sample engine's
    stock page is its base's, part of what it is."""
    pools = number_pools(rules)
    numbers, holders = {}, {}

    def hold(name, identity):
        for key in occupied(rules, identity):
            require(holders.setdefault(key, name) == name,
                    f'{name} and {holders[key]} both hold {key[0]} {key[1]} in this layout')
        numbers[name] = dict(identity)

    # An effect number is also held as an engine number (occupied).
    keys = lambda slot, n: [(slot, n)] + ([('engine', n)] if slot == 'effect' else [])
    # The last page is reserved for an SDK overlay screen: in the pool, so every
    # other machine keeps its own place, never given out, and still valid in a
    # layout that already gave it (check_identity), as earlier SDKs did. Likewise
    # an engine is never given an effect number: stock reads a send's effect number
    # as an engine number (occupied), so effects have their pool to themselves and
    # only the fixed machines (FIXED_NUMBERS) hold engine numbers in it.
    reserved = lambda slot, n: (slot == 'page' and n == rules['pages'] - 1) or (slot == 'engine' and n in rules['effects'])
    free = lambda slot, n: not reserved(slot, n) and not any(k in holders for k in keys(slot, n))

    def take(name, identity, slot, n):
        identity[slot] = n
        for key in keys(slot, n):
            holders[key] = name

    for name, _kind, identity in fixed:
        hold(name, identity)
    fixed_engines = {identity['engine'] for _name, _kind, identity in fixed if 'engine' in identity}
    moved, released = [], {}
    for name, identity in (layout or {}).items():
        if name in numbers:
            require(numbers[name] == identity, f'{name} has fixed numbers; the layout gives it others')
        elif 'effect' in identity and identity['effect'] not in pools['effect']:
            # An effect number later found unusable on stock (its controls never
            # moved) is let go: the machine is numbered again like a new one.
            continue
        elif ('engine' in identity and identity['engine'] in pools['engine'] and identity['engine'] in rules['effects'] and
              identity['engine'] not in fixed_engines):
            # An engine number among the effect numbers, which earlier SDKs gave out
            # (from the engine pool alone), is let go too (below); a fixed machine's is
            # never another's, and a stock engine's is refused as before.
            n = identity['engine']
            require(released.setdefault(n, name) == name, f'{name} and {released[n]} both hold engine {n} in this layout')
            moved.append(name)
        else:
            hold(name, identity)
    # Such a machine, in this build or not, keeps its page and is given an engine number
    # from its own place, in ID order, before any machine new to the layout. A project
    # saved with its old number reopens through the project's companion (tooling/projects.py).
    for n, name in released.items():
        require(('engine', n) not in holders, f'{name} and {holders.get(("engine", n))} both hold engine {n} in this layout')
    for name in sorted(moved):
        n = next((n for n in preferred(pools['engine'], name) if free('engine', n)), None)
        require(n is not None, f'No free engine number for {name}: every one is held in this layout')
        hold(name, dict(layout[name], engine=n))
    new = []
    for name, kind, requested in sorted(machines, key=lambda machine: machine[0]):
        own_page = owns_page(rules, kind, requested['page'])
        if name in numbers and own_page != owns_page(rules, kind, numbers[name]['page']):
            # An effect (SDK 1.7) or sample engine (SDK 1.8) that came to draw its own page,
            # or stopped: it keeps its number and is given a page, or gives its page up. Only
            # a sample engine forking the sampler draws.
            require(kind != 'sample' or (numbers[name]['page'] if own_page else requested['page']) == SAMPLE_BASES['sampler'],
                    f'{name} now forks another stock sampler; the layout holds its numbers for the old one')
            for key in [key for key, holder in holders.items() if holder == name and key[0] == 'page']:
                del holders[key]
            if own_page:
                new.append((name, kind, dict(numbers[name], page=None), ('page',)))
            else:
                numbers[name] = dict(numbers[name], page=requested['page'])
        elif name in numbers:
            check_identity(rules, kind, numbers[name])
            require(kind != 'sample' or own_page or numbers[name]['page'] == requested['page'],
                    f'{name} now forks another stock sampler; the layout holds its numbers for the old one')
        else:
            slot = 'player' if kind == 'player' else 'effect' if kind == 'effect' else 'engine'
            slots = (slot, 'page') if kind in ('synth', 'drum', 'player') or own_page else (slot,)
            new.append((name, kind, dict(requested), slots))
    # Requested numbers first, so no machine's preferred place takes one another asks for.
    for name, _kind, identity, slots in new:
        for slot in slots:
            n = identity[slot]
            identity[slot] = None
            if n in pools[slot] and free(slot, n):
                take(name, identity, slot, n)
    for name, kind, identity, slots in new:
        for slot in slots:
            if identity[slot] is None:
                n = next((n for n in preferred(pools[slot], name) if free(slot, n)), None)
                require(n is not None, f'No free {slot} number for {name}: every one is held in this layout')
                take(name, identity, slot, n)
        hold(name, check_identity(rules, kind, identity))
    return numbers


def assign_numbers(machines, layout=None, fixed=None):
    """The machines with their numbers in this layout, and the layout after. `fixed` maps
    IDs to fixed numbers, by default FIXED_NUMBERS; a machine with fixed numbers asks for them."""
    fixed = FIXED_NUMBERS if fixed is None else fixed
    for m in machines:
        require(m.id not in fixed or fits(m.identity, fixed[m.id]),
                f'{m.id} has fixed numbers; its machine.json asks for others')
    numbers = number_layout(config()['identities'], [(name, None, identity) for name, identity in fixed.items()],
                            layout, [(m.id, m.kind, m.identity) for m in machines])
    assigned = [m if numbers[m.id] == m.identity else replace(m, identity=numbers[m.id], requested=m.identity)
                for m in machines]
    return assigned, numbers


def read_collection(path=None, extra=(), extensions=()):
    """A collection file's entries, resolved: each machine's absolute folder
    ({"path": ...}) and each extension's ({"extension": ...}), relative to the
    file. Machine folders in `extra` and extension folders in `extensions` follow."""
    entries = []
    if path is not None:
        path = Path(path)
        value = json.loads(path.read_text(encoding='utf-8'))
        require(isinstance(value, dict) and {'schema', 'machines'} <= set(value) <= {'schema', 'machines', 'extensions'}
                and value['schema'] == 1 and isinstance(value['machines'], list) and
                isinstance(value.get('extensions', []), list), 'Unsupported collection file: ' + str(path))
        for kind, key in (('machines', 'path'), ('extensions', 'extension')):
            for entry in value.get(kind, []):
                require(isinstance(entry, dict) and set(entry) == {'path'} and isinstance(entry['path'], str),
                        'Collection entries are {"path": ...}, the SDK\'s own extensions (extensions/cpu) included')
                entries.append({key: str((path.parent / entry['path']).resolve())})
    return (entries + [{'path': str(Path(folder).resolve())} for folder in extra] +
            [{'extension': str(Path(folder).resolve())} for folder in extensions])


def collection(entries=None, layout=None):
    """Machines in firmware order, with their numbers in the layout: the order sets
    startup, catalog and link layout, never numbers. Entries come from read_collection."""
    entries = [e for e in ([] if entries is None else entries) if 'path' in e]
    require(isinstance(entries, list) and entries,
            'Collection needs machines: pass --machine <folder>, or --collection local/catalog/collection.json '
            'after python sdk.py catalog fetch')
    machines = [machine_at(e['path']) for e in entries]
    identities = [m.id for m in machines]
    require(len(set(identities)) == len(identities), 'Collection needs distinct machines')
    machines, _numbers = assign_numbers(machines, layout)
    engines = [m.identity['engine'] for m in machines if 'engine' in m.identity]
    effects = [m.identity['effect'] for m in machines if 'effect' in m.identity]
    pages = [m.identity['page'] for m in machines if m.display != 'stock']
    require(len(set(engines)) == len(engines) and len(set(effects)) == len(effects) and len(set(pages)) == len(pages),
            'Conflicting engine, effect or page ID')
    # Heap voices are reserved at boot, so their budget is checked here. Sample analyses
    # come and go: the device shares one allowance among them (sample_cache.cpp).
    voice_heap.check_budget(machines)
    return machines


def by_kind(machines, *kinds):
    return [m for m in machines if m.kind in kinds]


# The kinds in the order a build lists its machines' records (machine_record.hpp machine_kind).
RECORD_KINDS = ('synth', 'drum', 'sample', 'effect', 'player')


def record_order(machines):
    """Machines as a build lists their records (sdk_machine_records, tooling/records.py): by kind,
    then ID. Startup registers them, and the shared tables hold their rows, in this order,
    whichever order a collection lists them in."""
    return sorted(machines, key=lambda m: (RECORD_KINDS.index(m.kind), m.id))
