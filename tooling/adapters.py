"""SDK adapters for every machine kind: generated bindings, registration and catalog entries.

Machine code implements plain C++ callbacks. These adapters supply the stock
object layouts, method tables, storage, registration and menu entries:

- synth: initialize/start/destroy/render over Voice and Shared state, with a
  drawing display;
- sample: flag-1 synth registration that receives the track's sample;
- effect: send-FX registry object with in-place stereo processing.
"""
import copy
import json
import math
from pathlib import Path

from .benchmark import append_callback
from .definitions import (KIT_CONTROLS_SDK, extended_display, ROOT, STOCK_VOICES, SYNTH_POSITIONS, SYNTH_SHIFT_ENTRIES, control_positions, hidden_pages, paged,
                          shift_choices, shift_field, shift_parameters, stock_shift_codes, synth_shift,
                          validate_definition)

# An effect's shift-row controls follow its eight parameter words (slots 8..11).
SHIFT_SLOT = 8
from .formats import require
from .stock_tables import REGISTRATION as RELOCATED
from .voice_heap import on_heap, placement
from . import track_stage

TEMPLATES = ROOT / 'sdk/platform/opxy-1.2.1'
# The init preset's authored settings, shared with builder/core.mjs's defaultPreset
# (which embeds this same file's content at its import). Engine params, the
# machine's type and its amp overrides are merged in at default_preset below.
DEFAULT_PRESET_TEMPLATE = json.loads((Path(__file__).parent / 'default_preset.json').read_text(encoding='utf-8'))


def expand(template, values, destination, root=TEMPLATES):
    source = (root / template).read_text(encoding='utf-8')
    for key, value in values.items():
        source = source.replace('@' + key + '@', str(value))
    require('@' not in source, 'Unexpanded template ' + template)
    destination = Path(destination)
    destination.write_text(source, encoding='utf-8', newline='\n')
    return destination


def stock_identity(m):
    """A machine's stock identity in the shape the templates expand."""
    return dict(native_name=m.native_name, symbol_prefix=m.prefix)


def voice_values(definition):
    resources = definition['resources']
    return dict(NAMESPACE=definition['id'], VOICE_BYTES=resources['voiceBytes'],
                VOICE_ALIGNMENT=resources['voiceAlignment'], SHARED_BYTES=resources['sharedBytes'],
                PEAK=resources['outputPeakQ31'], CHANNELS=2 if resources.get('stereoOutput') else 1,
                RELEASE_CALLBACK=f"{definition['id']}::release(*voice, shared, owner(voice));" if resources.get('releaseCallback') else '')


def idle_values(m, drum, idle, output_frames='opxy::block_frames', voice_of_held='slot->voice'):
    """The idle cut's pieces of a voice adapter (voice.cpp.in, drum_voice.cpp.in): only in a
    build with the idle capability. Without it every placeholder is empty and the adapter is
    the public one, whether or not its engine defines `settled`."""
    pieces = DRUM_IDLE if drum else VOICE_IDLE
    if not idle:
        return {key: '' for key in pieces}
    # settled() is SDK 1.22 API (machines.IDLE_SDK): a machine declaring an older SDK is
    # never asked, whatever its namespace happens to define.
    from .machines import IDLE_SDK, SDK_VERSION
    asked = getattr(m, 'sdk', SDK_VERSION) >= IDLE_SDK
    settles = 'sdk_settles::has<Voice, Shared>::value' if asked else 'false'
    if not asked:
        # Not even instantiated: the engine's own settled, whatever it returns, is never named.
        pieces = {key: text.replace('sdk_settles::query(@VOICE_OF_HELD@, shared)', 'false')
                  .replace('sdk_settles::query(slot->voice, shared)', 'false') for key, text in pieces.items()}
    # voice_of_held: the held slot's voice, in module memory or a heap arena (voice_heap.py).
    return {key: text.replace('@PREFIX@', m.prefix).replace('@OUTPUT_FRAMES@', output_frames)
            .replace('@VOICE_OF_HELD@', voice_of_held).replace('@SETTLES@', settles) for key, text in pieces.items()}


def generate_voice(directory, m, sweep=False, idle=False):
    """The native machine object, method table and voice pool for a synth. In a sweep
    build (benchmark.cpp.in) its voices render with the swept controls while a sweep run
    of this machine is active; other builds' voices never look."""
    if sweep:
        declarations = ('// A profiling build\'s control sweep (benchmark.cpp.in): while set for this machine\'s\n'
                        '// engine, its voices render with these controls instead of their track\'s.\n'
                        'extern const opxy::i32* volatile sdk_benchmark_parameters __asm__("sdk_benchmark_parameters");\n'
                        'extern const char* volatile sdk_benchmark_engine __asm__("sdk_benchmark_engine");\n')
        controls = (f'        const opxy::i32* swept = sdk_benchmark_engine == {m.prefix}_engine_id ? '
                    'sdk_benchmark_parameters : nullptr;\n'
                    '        const opxy::i32* controls = swept ? swept : parameters;\n')
    else:
        declarations, controls = '', '        const opxy::i32* controls = parameters;\n'
    shift_row = ''
    if synth_shift(m.definition):
        # Parameters 8..15: the Shift row, from words 36 and 37 of the track record stock
        # renders the voice with, which `parameters` points at (synth_shift.hpp).
        shift_row = SHIFT_ROW.format(**shift_row_tables(m.definition))
        controls = (controls.replace('const opxy::i32* controls', 'const opxy::i32* stock_controls', 1) +
                    '        opxy::i32 controls[2 * opxy::parameter_slots];\n'
                    '        for (unsigned i = 0; i < opxy::parameter_slots; ++i) controls[i] = stock_controls[i];\n'
                    '        static opxy::i32 shift_kept[kVoices][2];\n'
                    '        shift_words(parameters, controls + opxy::parameter_slots, shift_kept[slot - voices]);\n')
    values = dict(voice_values(m.definition), SWEEP_DECLARATIONS=declarations, SWEEP_CONTROLS=controls,
                  SHIFT_ROW=shift_row,
                  PREFIX=m.prefix, NAME=m.native_name, LABELS=stock_labels(m),
                  CAPACITY=m.definition['resources']['voiceCapacity'])
    values.pop('PEAK')
    values.pop('RELEASE_CALLBACK')
    # Where a synth's voices live: module memory, or a heap arena for a large pool (voice_heap.py).
    values.update(placement(m))
    # A synth's track stage (track_stage.py): nothing for a synth without one.
    values.update(track_stage.adapter_values(m, bool(synth_shift(m.definition))))
    release = m.definition['resources'].get('releaseCallback', 0)
    values.update(RELEASE_METHOD=f'{m.prefix}_release' if release else 'noop',
                  RELEASE_FUNCTION=f'''int {m.prefix}_release(void* self) {{
    Handle* handle = static_cast<Handle*>(self);
    if (Slot* slot = held(handle))
        {m.definition['id']}::release({values['VOICE_OF_HELD']}, shared, slot->owner);
    else if (pending(handle) || handle->wants) handle->released = 1;
    return 0;
}}
''' if release else '')
    values.update(RELEASE_STATE=', released' if release else '',
                  RELEASE_CLEAR='    handle->released = 0;' if release else '',
                  RELEASE_PENDING=f"    if (handle->released) {m.definition['id']}::release({values['VOICE_OF_SLOT']}, shared, id);" if release else '')
    # Expand to the original mono text when omitted, preserving its native code.
    values.update(OUTPUT_FRAMES='opxy::block_frames' if values['CHANNELS'] == 1 else '2 * opxy::block_frames',
                  STEREO_METHOD='zero' if values['CHANNELS'] == 1 else 'stereo',
                  STEREO_FUNCTION='' if values['CHANNELS'] == 1 else 'static int stereo() { return 1; }\n')
    values['FADE_OUTPUT'] = ('            for (opxy::i32 i = 0; i < frames; ++i) output[i] = opxy::q31::multiply(output[i], step * (frames - 1 - i));' if values['CHANNELS'] == 1 else
        '            for (opxy::i32 i = 0; i < frames; ++i) for (opxy::i32 channel = 0; channel < 2; ++channel)\n'
        '                output[2 * i + channel] = opxy::q31::multiply(output[2 * i + channel], step * (frames - 1 - i));')
    if m.kind == 'drum':
        values['MODEL_COUNT'] = m.definition['resources']['modelCount']
        values['CHOKE'] = 'true' if m.definition['resources'].get('chokeSameKey', 0) else 'false'
        values['PAD_MIX'] = 'true' if m.definition['resources'].get('padMix', 0) else 'false'
        pads = m.definition['resources'].get('padControls', [])
        values['PAD_COUNT'] = len(pads)
        values['PAD_BITS'] = ', '.join(str(f['bits']) for f in pads) or '1'
        values['PAD_SIGNS'] = sum(1 << i for i, f in enumerate(pads) if f.get('signed'))
        values['TRANSPORT'] = 'true' if drum_transport(m) else 'false'
        values['PARAMETER_EDIT'] = m.definition['resources'].get('parameterEdit', 0)
        values['KIT_CONTROLS'] = kit_controls(m)
        values['AMP_DEFAULTS'] = ', '.join(f'{w:#x}' for w in kit_amp_defaults(m))
    values.update(idle_values(m, m.kind == 'drum', idle, values.get('OUTPUT_FRAMES', 'opxy::block_frames'),
                              values.get('VOICE_OF_HELD', 'slot->voice')))
    path = expand('drum_voice.cpp.in' if m.kind == 'drum' else 'voice.cpp.in', values, Path(directory) / 'voice.cpp')
    return append_callback(path, m) if m.kind == 'drum' else path


# The idle cut's pieces of the synth voice adapter (voice.cpp.in), in builds with idle.
VOICE_IDLE = {
    'IDLE_INCLUDE': '#include "idle_cut.hpp"\n#include "settles.hpp"\n',
    'IDLE_DECLARATIONS': '''// The idle cut's threshold and its registry of machines it asks (idle_cut.cpp).
opxy::i32 sdk_idle_floor() __asm__("sdk_idle_floor");
bool sdk_idle_register(const void* methods, int (*settled)(void*)) __asm__("sdk_idle_register");
''',
    # follow, idle: the idle cut's follower and its answer (engines with `settled`).
    'IDLE_SLOT': ' opxy::platform::idle_cut::Follower follow; bool idle;',
    'IDLE_DEFINITIONS': '''const bool kSettles = @SETTLES@;
static bool idle_asked;
// The idle cut's own symbols (idle_cut.cpp), reached only by a machine whose engine
// settles: the others reference nothing.
template <bool On> struct IdleLinks {
    static opxy::i32 floor() { return 0; }
    static bool add(const void*, int (*)(void*)) { return true; }
};
template <> struct IdleLinks<true> {
    static opxy::i32 floor() { return sdk_idle_floor(); }
    static bool add(const void* methods, int (*settled)(void*)) { return sdk_idle_register(methods, settled); }
};
''',
    'IDLE_BEGIN': '''    opxy::platform::idle_cut::restart(slot.follow);
    slot.idle = false;
''',
    'IDLE_SETTLED': '''// The idle cut's question (idle_cut.cpp): whether the voice may go.
static int @PREFIX@_settled(void* self) {
    const Slot* slot = held(static_cast<Handle*>(self));
    return slot && !slot->next && slot->idle;
}
''',
    'IDLE_START': '''            opxy::platform::idle_cut::restart(slot->follow);
            slot->idle = false;
''',
    'IDLE_FOLLOW': '''        // The idle cut: the engine's own output, before stock's envelope, against
        // the note's peak. Stock's kill fades the voice once its note is released
        // (idle_cut.cpp asks @PREFIX@_settled).
        if (kSettles) {
            // Asked once: a full registry (idle_cut.cpp) leaves this machine uncut, not rescanned.
            if (!idle_asked) {
                idle_asked = true;
                IdleLinks<kSettles>::add(@PREFIX@_vtable, &@PREFIX@_settled);
            }
            slot->idle = opxy::platform::idle_cut::follow(slot->follow, opxy::platform::idle_cut::peak(output, @OUTPUT_FRAMES@),
                                                          sdk_settles::query(@VOICE_OF_HELD@, shared), IdleLinks<kSettles>::floor());
        }
''',
}
# The idle cut's pieces of the kit voice adapter (drum_voice.cpp.in), in builds with idle:
# a settled voice below the threshold fades out as a choked one does, then is freed.
DRUM_IDLE = {
    'IDLE_INCLUDE': '#include "idle_cut.hpp"\n#include "settles.hpp"\n',
    'IDLE_DECLARATIONS': '''// The idle cut's threshold (idle_cut.cpp).
opxy::i32 sdk_idle_floor() __asm__("sdk_idle_floor");
''',
    'IDLE_SLOT': ' opxy::platform::idle_cut::Follower follow;',
    'IDLE_DEFINITIONS': '''const bool kSettles = @SETTLES@;
// The idle cut's own symbols (idle_cut.cpp), reached only by a machine whose engine
// settles: the others reference nothing.
template <bool On> struct IdleLinks {
    static opxy::i32 floor() { return 0; }
};
template <> struct IdleLinks<true> {
    static opxy::i32 floor() { return sdk_idle_floor(); }
};
''',
    'IDLE_START': '    opxy::platform::idle_cut::restart(slot->follow);\n',
    'IDLE_FOLLOW': '''        // The idle cut: the engine's own output, before any gain, against the hit's
        // peak; once it may go it fades out as a choked voice does.
        if (kSettles && !slot->fade &&
            opxy::platform::idle_cut::follow(slot->follow, opxy::platform::idle_cut::peak(output, opxy::block_frames),
                                             sdk_settles::query(slot->voice, shared), IdleLinks<kSettles>::floor()))
            slot->fade = kChokeFrames;
''',
    # kChoke | kSettles && slot->fade: | binds before &&, so the condition reads
    # (kChoke || kSettles) && fade and the text without idle is the plain kit's.
    'IDLE_FADE': ' | kSettles',
}


# A synth's Shift row in its voice adapter (SDK 1.13): each entry's default field, its
# highest one and, for an enum, its bucket, so render's words cost no division.
SHIFT_ROW = '''#include "synth_shift.hpp"
// The Shift row (SDK 1.13): each entry's default field, highest field (0 when
// absent) and, for an enum, its bucket (0x7fffffff / choices; 0 for an amount).
static const opxy::u8 kShiftDefaults[8] = {{{defaults}}};
static const opxy::u8 kShiftTops[8] = {{{tops}}};
static const opxy::u32 kShiftBuckets[8] = {{{buckets}}};
// The row's eight words as render receives them, from the track record's words 36 and
// 37. A word without the mark that isn't 0 is a step-lock handle stock's lock pass has
// just written there, which the SDK replaces with the step's row before the note
// starts (synth_controls.cpp sdk_synth_shift_play): the voice keeps playing the last
// row it read until then.
static void shift_words(const opxy::i32* record, opxy::i32* out, opxy::i32* kept) {{
    namespace row = opxy::platform::synth_shift;
    opxy::i32 words[2];
    for (opxy::u32 w = 0; w < 2; ++w) {{
        words[w] = record[row::kFirstWord + w];
        if (words[w] & row::kMark) kept[w] = words[w];
        else if (words[w]) words[w] = kept[w];
    }}
    for (opxy::u32 i = 0; i < row::kEntries; ++i) {{
        opxy::u32 field = row::field(words[i / row::kPerWord], i, kShiftDefaults);
        if (field > kShiftTops[i]) field = kShiftTops[i];
        out[i] = kShiftBuckets[i] ? row::choice(field, kShiftBuckets[i]) : kShiftTops[i] ? row::amount(field) : 0;
    }}
}}
'''


def shift_row_tables(definition):
    """A synth's Shift row as the runtime's SynthShift and the voice adapter's tables hold it."""
    shift = synth_shift(definition)
    pad = lambda values, fill: values + [fill] * (SYNTH_SHIFT_ENTRIES - len(values))
    choices = pad([shift_choices(p) for p in shift], 1)
    defaults = pad([shift_field(p, p['default']) for p in shift], 0)
    tops = [n - 1 if n >= 2 else 0 if n == 1 else 100 for n in choices]
    buckets = [0x7fffffff // n if n >= 2 else 0 for n in choices]
    return dict(count=len(shift), choices=', '.join(map(str, choices)), defaults=', '.join(map(str, defaults)),
                tops=', '.join(map(str, tops)), buckets=', '.join(f'{b}u' for b in buckets))


def drum_transport(m):
    """Whether a drum kit's adapter passes the tempo to its drum_transport: a kit declaring
    SDK 1.10 or later. Older kits never get the call, whatever their source defines."""
    return m.kind == 'drum' and getattr(m, 'sdk', (1, 0)) >= (1, 10)


# The SDK version from which a drum kit's screen reads the track's note serial and the
# played pad's snapshot; an older kit keeps the selected-pad strike filter (screen_drawing.hpp).
PLAYED_PAD_SDK = (1, 21)


def played_pad(m):
    """Whether a drum kit's screen reads the played-pad snapshot (SDK 1.21): it alone calls
    sdk_drum_trigger, so only a build with such a kit links that unit (native.UNIT_SERVICES)."""
    return getattr(m, 'kind', 'synth') == 'drum' and getattr(m, 'sdk', (1, 0)) >= PLAYED_PAD_SDK


def kit_controls(m):
    """A drum kit's drum-kit controls (SDK 1.23) as drum_kit_controls.hpp's bits: padVolume 1,
    padRandom 2, ampEnvelope 4; 0 for a kit declaring none, whose generated code is as before."""
    if getattr(m, 'kind', 'synth') != 'drum':
        return 0
    resources = m.definition['resources']
    return sum(bit for bit, key in ((1, 'padVolume'), (2, 'padRandom'), (4, 'ampEnvelope')) if resources.get(key, 0))


def kit_amp_defaults(m):
    """The amp words 8..11 an ampEnvelope kit's key plays when it keeps none: its init preset's amp
    attack, decay, sustain and release (the shared template's, under the kit's presetDefaults), << 16.
    A kit's M2 page (the drum sampler's) names those words start, attack, hold and decay."""
    amp = dict(DEFAULT_PRESET_TEMPLATE['envelope']['amp'])
    amp.update((getattr(m, 'preset_defaults', None) or {}).get('amp', {}))
    return [amp[key] << 16 for key in ('attack', 'decay', 'sustain', 'release')]


def selected_key(m):
    """Whether a drum kit's screen fills draw::Input::selected_key: a kit declaring SDK 1.23 or later."""
    return getattr(m, 'kind', 'synth') == 'drum' and getattr(m, 'sdk', (1, 0)) >= KIT_CONTROLS_SDK


def pad_mix(m):
    """Whether a drum kit's Shift row is each key's mixer channel (resources.padMix), whose
    pan, level, pad controls and sends also lock per hit (drum_locks.hpp)."""
    return m.kind == 'drum' and bool(m.definition['resources'].get('padMix', 0))


def sends_per_key(m):
    """Whether a drum kit's keys keep their own share of the track's sends: padMix, whose
    Shift row edits FX I and FX II."""
    return m.kind == 'drum' and bool(m.definition['resources'].get('padMix', 0))


def kit_table(machines):
    """drum_callbacks.cpp's table of kits declaring the drum-kit controls (drum_kit_controls.hpp
    sdk_drum_kits); empty for a build without one, whose source is then as before."""
    kits = [m for m in machines if kit_controls(m)]
    if not kits:
        return ''
    declarations, rows = [], []
    for m in kits:
        amp = kit_controls(m) & 4
        if amp:
            declarations.append(f'static const opxy::i32 {m.prefix}_amp[] = {{{", ".join(f"{w:#x}" for w in kit_amp_defaults(m))}}};')
        rows.append(f'    {{{m.prefix}_engine_id, {kit_controls(m)}, {m.prefix + "_amp" if amp else "nullptr"}}},')
    return ('#include "drum_kit_controls.hpp"\n' + ''.join(d + '\n' for d in declarations) +
            'const opxy::platform::DrumKit sdk_drum_kits[] = {\n' + '\n'.join(rows) + '\n};\n'
            f'const opxy::u32 sdk_drum_kit_count = {len(kits)};\n')


def generate_drum_callbacks(directory, machines):
    declarations, rows, sends = [], [], []
    for m in machines:
        if m.kind != 'drum':
            continue
        p = m.prefix
        declarations += [f'extern const char {p}_engine_id[] __asm__("{p}_engine_id");',
                         f'int {p}_record_ready() __asm__("{p}_record_ready");',
                         f'void {p}_drum_defaults(opxy::u32, opxy::u8*, opxy::u8*) __asm__("{p}_drum_defaults");',
                         f'const char* {p}_drum_label(opxy::u32, opxy::u32) __asm__("{p}_drum_label");',
                         f'const char* {p}_drum_model_name(opxy::u32) __asm__("{p}_drum_model_name");',
                         f'opxy::u32 {p}_drum_count(opxy::u32) __asm__("{p}_drum_count");',
                         f'void {p}_drum_scramble_range(opxy::u32, opxy::u32, opxy::u8*, opxy::u8*) __asm__("{p}_drum_scramble_range");']
        groups = m.definition['resources'].get('modelGroups', [])
        pads = m.definition['resources'].get('padControls', [])
        if pads:
            declarations += [f'bool {p}_drum_pad_text(opxy::u32, opxy::i32, char*) __asm__("{p}_drum_pad_text");',
                             f'static const opxy::u8 {p}_pad_bits[] = {{{", ".join(str(f["bits"]) for f in pads)}}};',
                             f'static const char* const {p}_pad_labels[] = {{{", ".join(json.dumps(f["label"]) for f in pads)}}};']
        edit = m.definition['resources'].get('parameterEdit', 0)
        if edit:
            declarations.append(f'opxy::u32 {p}_drum_edit_value(opxy::u32, opxy::u32, opxy::u32, opxy::i32, opxy::u32) '
                                f'__asm__("{p}_drum_edit_value");')
        if groups:
            declarations += [f'static const opxy::u8 {p}_group_sizes[] = {{{", ".join(str(g["count"]) for g in groups)}}};',
                             f'static const char* const {p}_group_names[] = {{{", ".join(json.dumps(g["label"]) for g in groups)}}};']
        # Audio classification must not require a screen constructed on the GUI role.
        rows.append(f'    {{{p}_engine_id, {p}_record_ready, {m.definition["resources"]["modelCount"]}, '
                    f'{p}_drum_defaults, {p}_drum_label, {p}_drum_model_name, {p}_drum_count, {len(groups)}, '
                    f'{p + "_group_sizes" if groups else "nullptr"}, {p + "_group_names" if groups else "nullptr"}, '
                    f'{1 if m.definition["resources"].get("padMix", 0) else 0}, {len(pads)}, '
                    f'{p + "_pad_bits" if pads else "nullptr"}, {sum(1 << i for i, f in enumerate(pads) if f.get("signed"))}, '
                    f'{p + "_pad_labels" if pads else "nullptr"}, {p + "_drum_pad_text" if pads else "nullptr"}, '
                    f'{p}_drum_scramble_range, '
                    f'{p + "_drum_edit_value" if edit else "nullptr"}}},')
        if sends_per_key(m):
            # The mixer's send hooks (drum_send.cpp) know a kit's voices by its method table.
            declarations += [f'extern const void* const {p}_vtable[];',
                             f'opxy::u32 {p}_drum_send(const void*) __asm__("{p}_drum_send");']
            sends.append(f'    {{{p}_vtable, {p}_drum_send}},')
    source = '#include "drum_machines.hpp"\n#include "drum_send.hpp"\nextern "C" {\n' + '\n'.join(declarations) + '\n}\n'
    source += 'const opxy::platform::DrumMachine sdk_drum_machines[] = {\n' + ('\n'.join(rows) or '    {},') + '\n};\n'
    source += f'const opxy::u32 sdk_drum_machine_count = {len(rows)};\n'
    source += kit_table(machines)
    source += 'const opxy::platform::DrumSendKit sdk_drum_send_kits[] = {\n' + ('\n'.join(sends) or '    {},') + '\n};\n'
    source += f'const opxy::u32 sdk_drum_send_kit_count = {len(sends)};\n'
    destination = Path(directory) / 'drum_callbacks.cpp'
    destination.write_text(source, encoding='utf-8', newline='\n')
    return destination


# A drum kit's keys and names, as the device's adapter reads them (drum_voice.cpp.in),
# for the builder's audition and screen header: a key's default model and eight
# controls (nine bytes), a model's name, a slot's label and the slots it uses.
DRUM_KEYS = '''static opxy::u8 drum_key[9];
const opxy::u8* opxy_drum_defaults(opxy::u32 key) {
    drum_key[0] = 0; drum_key[1] = 0; drum_key[2] = 0; drum_key[3] = 0; drum_key[4] = 0;
    drum_key[5] = 0; drum_key[6] = 0; drum_key[7] = 0; drum_key[8] = 0;
    @NAMESPACE@::drum_defaults(key, &drum_key[0], &drum_key[1]);
    return drum_key;
}
const char* opxy_drum_model_name(opxy::u32 model) { return @NAMESPACE@::model_name(model); }
const char* opxy_drum_label(opxy::u32 model, opxy::u32 slot) { return @NAMESPACE@::parameter_name(model, slot); }
opxy::u32 opxy_drum_count(opxy::u32 model) { return @NAMESPACE@::parameter_count(model); }
'''


def generate_player_preview(directory, m):
    """A drawing note player's browser preview (SDK 1.24): its display and player.cpp in one
    module, as its device unit compiles them, exporting the drawing transport display.mjs
    reads and a key's press and release and a player step, which run its callbacks."""
    namespace, display = m.definition['id'], m.definition['resources']['display']
    extension = (f'opxy::u32 opxy_display_extension() {{ return {display_extension(m.definition)}; }}\n'
                 if extended_display(display) else '')
    source = f'''// Generated by the SDK: {m.native_name}'s own page in the browser, drawn as on the device.
#include "player.cpp"
#include "display.cpp"
namespace {{
typedef {player_frame(display)} Frame;
typedef opxy::draw::{display_input_type(m.definition)} Input;
{namespace}::State state;
{namespace}::DisplayState display_state;
Input input;
Frame frame;
opxy::player::Controls controls;
template <class S> auto next_update(S& s, const opxy::player::Controls& c, opxy::player::Update& u, int)
    -> decltype(update(s, c, u)) {{ return update(s, c, u); }}
template <class S> bool next_update(S&, const opxy::player::Controls&, opxy::player::Update&, long) {{ return false; }}
// The callbacks see the eight controls the page shows, at 120 BPM from middle C's octave.
void take_controls() {{
    for (opxy::u32 i = 0; i < 8; ++i) controls.value[i] = input.slots[i];
    controls.lowest_key = 53;
    controls.bpm_x10 = 1200;
}}
// At most eight updates drain per event or step, as on the sequencer role.
void drain() {{
    opxy::player::Update change;
    for (opxy::u32 k = 0; k < 8 && next_update(state, controls, change, 0); ++k) {{}}
}}
}}
extern "C" {{
opxy::u32 opxy_display_abi() {{ return 7; }}
{extension}opxy::u32 opxy_display_state_bytes() {{ return sizeof(display_state); }}
opxy::u32 opxy_display_input_bytes() {{ return sizeof(input); }}
opxy::u32 opxy_display_frame_bytes() {{ return sizeof(frame); }}
{namespace}::DisplayState* opxy_display_state() {{ return &display_state; }}
Input* opxy_display_input() {{ return &input; }}
Frame* opxy_display_frame() {{ return &frame; }}
void opxy_display_reset() {{
    {namespace}::display_reset(display_state);
    for (opxy::u32 i = 0; i < Frame::path_count; ++i) frame.paths[i].key = 0;
}}
void opxy_display_render() {{
    take_controls();
    {namespace}::display(display_state, static_cast<const {namespace}::State&>(state),
                         static_cast<const opxy::player::Controls&>(controls), static_cast<const Input&>(input), frame);
}}
opxy::u32 opxy_display_valid() {{ return opxy::draw::valid(frame); }}
void opxy_player_press(opxy::u32 note, opxy::u32 velocity) {{
    take_controls();
    opxy::player::Plan plan;
    plan.count = 0;
    if ({namespace}::press(state, controls, note, velocity, plan)) drain();
}}
void opxy_player_release(opxy::u32 note) {{
    {namespace}::release(state, note);
    drain();
}}
void opxy_player_step() {{ drain(); }}
}}
'''
    destination = Path(directory) / 'player_preview.cpp'
    destination.write_text(source, encoding='utf-8', newline='\n')
    return destination


def generate_voice_preview(directory, definition, *, kind='synth'):
    """Browser preview voices for a synth or drum definition (curated or local)."""
    require(kind in ('synth', 'drum'), 'Browser previews audition synths and drums')
    values = voice_values(validate_definition(definition, kind))
    # A synth with a Shift row renders sixteen words: its eight controls, then the row.
    values['PARAMETER_COUNT'] = '2 * opxy::parameter_slots' if synth_shift(definition) else 'opxy::parameter_slots'
    count = definition['resources'].get('modelCount', 0)
    values['DRUM_SELECT'] = (f"{values['NAMESPACE']}::drum_select(*voice, shared, owner(voice), drum_model < {count} ? drum_model : 0);\n    "
                             if kind == 'drum' else '')
    values['DRUM_KEYS'] = DRUM_KEYS.replace('@NAMESPACE@', values['NAMESPACE']) if kind == 'drum' else ''
    return expand('voice_preview.cpp.in', values, Path(directory) / 'voice_preview.cpp', ROOT / 'sdk/host')


def drawing_layout(display):
    """Frame byte layout shared by the C++ frame and the browser."""
    if extended_display(display):
        return extended_layout(display)
    paths, points, nodes, texts, palette = (display[k] for k in ('paths', 'points', 'nodes', 'texts', 'palette'))
    path_bytes = 4 + 8 * points
    # An empty path or text array keeps one unused element (draw.hpp).
    nodes_at = max(paths, 1) * path_bytes
    texts_at = nodes_at + 28 * nodes
    header_at = texts_at + 36 * max(texts, 1)
    return dict(PATHS=paths, POINTS=points, NODES=nodes, TEXTS=texts, PALETTE=palette, PATH_BYTES=path_bytes,
                FRAME_NODES_AT=nodes_at, FRAME_TEXTS_AT=texts_at, FRAME_HEADER_AT=header_at,
                FRAME_BYTES=header_at + 32)


def extended_layout(display):
    """An extended frame's byte layout (draw.hpp ExtendedFrame): its path and node types."""
    paths, points, nodes, texts, palette = (display[k] for k in ('paths', 'points', 'nodes', 'texts', 'palette'))
    commands = display.get('commands', 0)
    if display.get('quadratic'):
        path_bytes, points_at = 8 + 20 * commands, 8
    else:
        points_at = 8 if display.get('countedPaths') else 4
        path_bytes = points_at + 8 * points
    node_bytes = 32 if display.get('affine') else 28
    nodes_at = max(paths, 1) * path_bytes
    texts_at = nodes_at + node_bytes * nodes
    header_at = texts_at + 36 * max(texts, 1)
    return dict(PATHS=paths, POINTS=points, COMMANDS=commands, NODES=nodes, TEXTS=texts, PALETTE=palette,
                PATH_BYTES=path_bytes, POINTS_AT=points_at, NODE_BYTES=node_bytes, FRAME_NODES_AT=nodes_at,
                FRAME_TEXTS_AT=texts_at, FRAME_HEADER_AT=header_at, FRAME_BYTES=header_at + 32)


def extended_frame(display):
    """The C++ type of an extended display's frame, spelled as draw.hpp's aliases resolve."""
    layout, flag = extended_layout(display), lambda k: 'true' if display.get(k) else 'false'
    size = layout['COMMANDS'] if display.get('quadratic') else layout['POINTS']
    frame = (f"opxy::draw::ExtendedFrame<{layout['PATHS']}, {size}, {layout['NODES']}, {layout['TEXTS']}, "
             f"{layout['PALETTE']}, {flag('affine')}, {flag('cubic')}, {display.get('openPaths', 0)}u, "
             f"{display.get('segmentPaths', 0)}u, {flag('quadratic')}, {flag('countedPaths')}>")
    if display.get('rgba'):
        frame = f'opxy::draw::RgbaFrame<{frame} >'
    return frame


def player_frame(display):
    """The C++ type of a note player's frame, as its display.h names it."""
    if extended_display(display):
        return extended_frame(display)
    return 'opxy::draw::Frame<{PATHS}, {POINTS}, {NODES}, {TEXTS}, {PALETTE}>'.format(**drawing_layout(display))


def display_extension(definition):
    """What an extended display's input appends to draw::Input, as the browser reads it
    (display.mjs): 4 the encoders' activity. Bits 1 and 2 are left for later inputs."""
    return 4 if definition['resources']['display'].get('encoderActivity') else 0


def display_input_type(definition):
    return 'EncoderInput' if definition['resources']['display'].get('encoderActivity') else 'Input'


def screen_bytes(m):
    """Renderer object size: the 76-byte stock screen, then the renderer's words
    (screen_drawing.hpp). The generated renderer asserts its C++ object has this size."""
    display = m.definition['resources']['display']
    # Nodes; text leaves and each slot's applied 24 characters; palette; frame
    # delta; then validated geometry and applied key per path (one word each when absent).
    texts, paths = max(display['texts'], 1), max(display['paths'], 1)
    words = display['nodes'] + 7 * texts + display['palette'] + 1 + 2 * paths
    if extended_display(display):
        # extended_drawing.hpp: a palette word even without a palette, and each node's
        # authored visibility byte (whole words).
        words += (display['palette'] == 0) + (display['nodes'] + 3) // 4
    return 76 + 4 * words


def generate_display(directory, *, machine=None, definition=None, kind="synth", namespace=None, symbol_prefix=None, binding_namespace="binding"):
    """Same authored display callback for host, WebAssembly and native wrappers."""
    if definition is None:
        definition, assignment = machine.definition, stock_identity(machine)
    else:
        validate_definition(definition, kind)
        assignment = {'symbol_prefix': 'local_preview'}
    namespace, prefix = namespace or definition['id'], symbol_prefix or assignment['symbol_prefix']
    display = definition['resources']['display']
    if extended_display(display):
        return generate_extended_display(Path(directory), definition, namespace, prefix)
    layout = drawing_layout(display)
    frame = 'opxy::draw::Frame<{PATHS}, {POINTS}, {NODES}, {TEXTS}, {PALETTE}>'.format(**layout)
    header = f'''// Generated by the SDK; no renderer addresses in author code.
#pragma once
#include "display.h"
namespace opxy {{ namespace binding {{
using DisplayState = {namespace}::DisplayState;
typedef {frame} Frame;
const unsigned display_bytes = {display['stateBytes']};
const unsigned display_alignment = {display['stateAlignment']};
}}}}
extern "C" {{
void {prefix}_display_reset(opxy::binding::DisplayState*, opxy::binding::Frame*);
void {prefix}_display_render(opxy::binding::DisplayState*, const opxy::draw::Input*, opxy::binding::Frame*);
unsigned {prefix}_display_validate(const opxy::binding::Frame*);
}}
namespace opxy {{ namespace binding {{
static inline void display_reset(DisplayState* state, Frame* frame) {{ {prefix}_display_reset(state, frame); }}
static inline void display_render(DisplayState* state, const draw::Input* input, Frame* frame) {{
    {prefix}_display_render(state, input, frame);
}}
static inline bool display_valid(const Frame* frame) {{ return {prefix}_display_validate(frame) != 0; }}
}}}}
'''
    source = f'''// Generated translation unit. One display implementation, bounded SDK frame.
#include "display_binding.hpp"
#include "display.cpp"
using opxy::binding::Frame;
static_assert(sizeof(opxy::binding::DisplayState) == opxy::binding::display_bytes,
              "Declared display size differs from C++ type");
static_assert(alignof(opxy::binding::DisplayState) == opxy::binding::display_alignment,
              "Declared display alignment differs from C++ type");
static_assert(__is_trivial(opxy::binding::DisplayState) && __is_standard_layout(opxy::binding::DisplayState),
              "Display state must be trivial standard-layout data");
static_assert(sizeof(Frame) == {layout['FRAME_BYTES']} &&
              sizeof(Frame::path_type) == {layout['PATH_BYTES']} && sizeof(opxy::draw::Node) == 28 &&
              __builtin_offsetof(Frame::path_type, points) == 4 &&
              __builtin_offsetof(Frame, nodes) == {layout['FRAME_NODES_AT']} &&
              __builtin_offsetof(Frame, texts) == {layout['FRAME_TEXTS_AT']} &&
              __builtin_offsetof(Frame, header) == {layout['FRAME_HEADER_AT']} &&
              __builtin_offsetof(opxy::draw::Node, scale) == 8 &&
              __builtin_offsetof(opxy::draw::Node, cos) == 12 &&
              __builtin_offsetof(opxy::draw::Node, shade) == 20 &&
              __builtin_offsetof(opxy::draw::Node, paint) == 24 &&
              __builtin_offsetof(opxy::draw::Text, chars) == 12,
              "Drawing frame layout differs from the declared display");
extern "C" void {prefix}_display_reset(opxy::binding::DisplayState* state, Frame* frame) {{
    {namespace}::display_reset(*state);
    for (opxy::u32 i = 0; i < Frame::path_count; ++i) frame->paths[i].key = 0;
}}
extern "C" void {prefix}_display_render(opxy::binding::DisplayState* state,
                                       const opxy::draw::Input* input, Frame* frame) {{
    {namespace}::display(*state, *input, *frame);
}}
extern "C" unsigned {prefix}_display_validate(const Frame* frame) {{
    return opxy::draw::valid(*frame);
}}
'''
    directory = Path(directory)
    header = header.replace('namespace binding', 'namespace '+binding_namespace).replace('opxy::binding', 'opxy::'+binding_namespace)
    source = source.replace('opxy::binding', 'opxy::'+binding_namespace)
    (directory / 'display_binding.hpp').write_text(header, encoding='utf-8', newline='\n')
    (directory / 'display_binding.cpp').write_text(source, encoding='utf-8', newline='\n')


def generate_extended_display(directory, definition, namespace, prefix):
    """An extended display's binding (docs/display.md#extended-drawing): its frame and input
    types, and the encoder activity the renderer gives its input."""
    display = definition['resources']['display']
    layout, frame, input_type = extended_layout(display), extended_frame(display), display_input_type(definition)
    header = f'''// Generated by the SDK; no renderer addresses in author code.
#pragma once
#include "display.h"
#include <opxy/encoder_activity.hpp>
namespace opxy {{ namespace binding {{
using DisplayState = {namespace}::DisplayState;
using DisplayInput = draw::{input_type};
typedef {frame} Frame;
const unsigned display_bytes = {display['stateBytes']};
const unsigned display_alignment = {display['stateAlignment']};
const bool full_canvas = {'true' if display.get('fullCanvas') else 'false'};
const unsigned display_extension = {display_extension(definition)};
}}}}
extern "C" {{
void {prefix}_display_reset(opxy::binding::DisplayState*, opxy::binding::Frame*);
void {prefix}_display_render(opxy::binding::DisplayState*, const opxy::binding::DisplayInput*, opxy::binding::Frame*);
unsigned {prefix}_display_validate(const opxy::binding::Frame*);
}}
namespace opxy {{ namespace binding {{
static inline void display_reset(DisplayState* state, Frame* frame) {{ {prefix}_display_reset(state, frame); }}
static inline void display_render(DisplayState* state, const DisplayInput* input, Frame* frame) {{
    {prefix}_display_render(state, input, frame);
}}
static inline bool display_valid(const Frame* frame) {{ return {prefix}_display_validate(frame) != 0; }}
}}}}
''' + encoder_binding(display)
    member = 'commands' if display.get('quadratic') else 'points'
    source = f'''// Generated translation unit. One display implementation, bounded SDK frame.
#include "display_binding.hpp"
#include "display.cpp"
using opxy::binding::Frame;
static_assert(sizeof(opxy::binding::DisplayState) == opxy::binding::display_bytes,
              "Declared display size differs from C++ type");
static_assert(alignof(opxy::binding::DisplayState) == opxy::binding::display_alignment,
              "Declared display alignment differs from C++ type");
static_assert(__is_trivial(opxy::binding::DisplayState) && __is_standard_layout(opxy::binding::DisplayState),
              "Display state must be trivial standard-layout data");
static_assert(sizeof(Frame) == {layout['FRAME_BYTES']} &&
              sizeof(Frame::path_type) == {layout['PATH_BYTES']} && sizeof(Frame::node_type) == {layout['NODE_BYTES']} &&
              __builtin_offsetof(Frame::path_type, {member}) == {layout['POINTS_AT']} &&
              __builtin_offsetof(Frame, nodes) == {layout['FRAME_NODES_AT']} &&
              __builtin_offsetof(Frame, texts) == {layout['FRAME_TEXTS_AT']} &&
              __builtin_offsetof(Frame, header) == {layout['FRAME_HEADER_AT']} &&
              __builtin_offsetof(opxy::draw::Text, chars) == 12,
              "Drawing frame layout differs from the declared display");
extern "C" void {prefix}_display_reset(opxy::binding::DisplayState* state, Frame* frame) {{
    {namespace}::display_reset(*state);
    for (opxy::u32 i = 0; i < Frame::path_count; ++i) frame->paths[i].key = 0;
}}
extern "C" void {prefix}_display_render(opxy::binding::DisplayState* state,
                                       const opxy::binding::DisplayInput* input, Frame* frame) {{
    {namespace}::display(*state, *input, *frame);
}}
extern "C" unsigned {prefix}_display_validate(const Frame* frame) {{
    return opxy::draw::valid(*frame);
}}
'''
    (directory / 'display_binding.hpp').write_text(header, encoding='utf-8', newline='\n')
    (directory / 'display_binding.cpp').write_text(source, encoding='utf-8', newline='\n')


def encoder_binding(display):
    if not display.get('encoderActivity'):
        return 'namespace opxy { namespace binding { inline void snapshot_encoders(DisplayInput&) {} }}\n'
    return '''#if defined(__bfin__)
#include "encoder_activity.hpp"
namespace opxy { namespace binding {
inline void snapshot_encoders(DisplayInput& input) { input.encoders = platform::encoder_activity(); }
}}
#else
namespace opxy { namespace binding { inline void snapshot_encoders(DisplayInput&) {} }}
#endif
'''


def generate_registration(directory, m):
    """A machine's stock record (and own screen) for the SDK's checked registration."""
    p, effect = m.prefix, m.kind == 'effect'
    name = m.native_name.encode('ascii')
    require(1 <= len(name) <= 15 and b'\0' not in name, 'Unsupported inline engine name')
    own = 'fx_' if effect else ''
    addresses = [f'{p}_{own}factory', f'{p}_{own}parameter_name', f'{p}_{own}name',
                 f'{p}_fx_id' if effect else f'{p}_engine_id', f'{p}_{own}page_id']
    declarations = [f'extern const char {symbol}[] __asm__("{symbol}");' for symbol in addresses]
    objects = [f'const opxy::platform::Record record = {{{", ".join(addresses)}, {int(m.kind in ("sample", "drum"))}}};']
    entries, screen = {}, 'nullptr'
    if m.display != 'stock':
        declarations += [f'extern char {p}_screen_state[] __asm__("{p}_screen_state");',
                         *(f'extern const char {p}_screen_{part}[] __asm__("{p}_screen_{part}");'
                           for part in ('vtable', 'asset')),
                         f'char* {p}_screen_ctor(char*, opxy::u32) __asm__("{p}_screen_ctor");']
        objects.append(f'const opxy::platform::Screen screen = {{{p}_screen_state, {screen_bytes(m)}, '
                       f'{p}_screen_vtable, {p}_screen_asset, {p}_screen_ctor}};')
        if m.kind == 'sample':
            # A sample engine keeps its stock page; its own screen has a page of its
            # own, registered and checked apart from the engine.
            declarations.append(f'extern const char {p}_screen_page_id[] __asm__("{p}_screen_page_id");')
            objects.append(f'const opxy::platform::Record screen_record = {{{", ".join(addresses[:4])}, '
                           f'{p}_screen_page_id, 1}};')
            entries[p + '_screen_register()'] = 'sdk_register_screen(&screen_record, &screen)'
            entries[p + '_screen_ready()'] = 'sdk_screen_ready(&screen_record, &screen)'
        else:
            screen = '&screen'
            entries[p + '_screen_register()'] = 'sdk_register_screen(&record, &screen)'
    if effect:
        entries[p + '_fx_register()'] = 'sdk_register_effect(&record)'
        entries[p + '_ready()'] = f'sdk_effect_ready(&record, {screen})'
    else:
        entries[p + '_register()'] = 'sdk_register_engine(&record)'
        if on_heap(m):
            # A synth with heap voices reserves their arena as it first registers on the audio
            # role at boot (voice_heap.hpp); an unselected synth in a browser kit never reserves.
            declarations.append(f'opxy::u32 {p}_voice_heap_reserve() __asm__("{p}_voice_heap_reserve");')
            entries[p + '_register()'] = f'({p}_voice_heap_reserve(), sdk_register_engine(&record))'
        entries[p + '_record_ready()'] = 'sdk_engine_ready(&record, nullptr)'
        entries[p + '_name_matches(const void* string)'] = 'sdk_name_matches(&record, string)'
        entries[p + '_ready()'] = f'sdk_engine_ready(&record, {screen})'
    declarations += [f'int {signature} __asm__("{signature.split("(")[0]}");' for signature in entries]
    lines = [f"// Generated: {m.id}'s stock identity for the SDK's checked registration.",
             '#include "registration.hpp"', 'extern "C" {', *declarations, '}', 'namespace {', *objects, '}',
             *(f'int {signature} {{ return {call}; }}' for signature, call in entries.items())]
    destination = Path(directory) / 'registration.cpp'
    destination.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    return destination


def generate_registration_stock(directory):
    """The stock tables behind checked registration, from the audited relocated layout."""
    return expand('registration_stock.cpp.in', RELOCATED, Path(directory) / 'registration_stock.cpp')


def generate_screen_order(directory):
    """The compositor traversal over the relocated screen slots."""
    return expand('screen_order.cpp.in', RELOCATED, Path(directory) / 'screen_order.cpp')


def generate_screen(directory, m):
    """The machine's renderer: its display shape and artwork IDs as data for the
    SDK renderer (screen_drawing.hpp), and its exported screen symbols. Compiled
    after the display binding it includes."""
    from .native import display_ids
    ids = display_ids(m.prefix, m.definition['resources']['display'])
    table = ''.join(f'    "{name}",\n' for name in ids)
    if extended_display(m.definition['resources']['display']):
        return generate_extended_screen(Path(directory), m, ids, table)
    drum = getattr(m, 'kind', 'synth') == 'drum'
    source = f'''// Generated by the SDK: {m.native_name}'s declared display as renderer data.
#include "display_binding.hpp"
#include "screen_drawing.hpp"
namespace {{
struct Machine {{
    static const char* const ids[{len(ids)}];
    // A drum kit's note serial and played-pad snapshot (screen_drawing.hpp): from SDK 1.21
    // the track's serial and the snapshot, before it the selected pad's strikes only.
    static const bool played_pad = {'true' if played_pad(m) else 'false'};
    static const bool selected_pad = {'true' if drum and not played_pad(m) else 'false'};
    static const bool sample = {'true' if getattr(m, 'kind', 'synth') == 'sample' else 'false'};   // reads its sample's overview
    static const bool percent_sends = {'true' if getattr(m, 'sdk', (1, 0)) >= (1, 15) else 'false'};   // pad[2..3] as percent + 1 (SDK 1.15)
    // A drum kit's Shift cells and choices of the drum-kit controls, and its selected key (SDK 1.23).
    static const bool kit_controls = {'true' if kit_controls(m) & 3 else 'false'};
    static const bool selected_key = {'true' if selected_key(m) else 'false'};
}};
const char* const Machine::ids[{len(ids)}] = {{
{table}}};
}}
OPXY_SCREEN(opxy::screen::Drawing<Machine>, "{m.prefix}", "{m.native_name}.svg", {RELOCATED['PAGE_LIMIT']}, {screen_bytes(m)})
'''
    destination = Path(directory) / 'screen.cpp'
    destination.write_text(source, encoding='utf-8', newline='\n')
    return destination


def generate_extended_screen(directory, m, ids, table):
    """An extended display's renderer (extended_drawing.hpp): a synth's, an effect's or, from
    SDK 1.26, a drum kit's, which keeps a kit's played-pad snapshot, selected key and Shift cells."""
    flag = lambda value: 'true' if value else 'false'
    drum = getattr(m, 'kind', 'synth') == 'drum'
    source = f'''// Generated by the SDK: {m.native_name}'s declared display as renderer data.
#include "display_binding.hpp"
#include "extended_drawing.hpp"
namespace {{
struct Machine {{
    static const char* const ids[{len(ids)}];
    static const bool percent_sends = {'true' if getattr(m, 'sdk', (1, 0)) >= (1, 15) else 'false'};
    // A drum kit's screen (SDK 1.26): its played-pad snapshot, selected key and Shift cells.
    static const bool kit = {flag(drum)};
    static const bool played_pad = {flag(drum and played_pad(m))};
    static const bool selected_key = {flag(drum and selected_key(m))};
    static const bool kit_controls = {flag(drum and kit_controls(m) & 3)};
}};
const char* const Machine::ids[{len(ids)}] = {{
{table}}};
}}
OPXY_SCREEN(opxy::screen::ExtendedDrawing<Machine>, "{m.prefix}", "{m.native_name}.svg", {RELOCATED['PAGE_LIMIT']}, {screen_bytes(m)})
'''
    destination = directory / 'screen.cpp'
    destination.write_text(source, encoding='utf-8', newline='\n')
    return destination


# Each optional sample service and the C entry points (prefix + suffix) its
# machine exports, in sample_machines.hpp's SampleMachine order.
SAMPLE_SERVICES = (
    ('padSettings', (('_sample_defaults', 'opxy::PadSettings', ''),
                     ('_sample_assign', 'opxy::PadSettings', 'const opxy::SampleAssignment*'))),
    ('drumControls', (('_drum_control', 'void', 'opxy::PadSettings*, const opxy::DrumControl*'),
                      ('_drum_panel', 'void', 'opxy::PadSettings, const opxy::SampleRegion*, const opxy::i32*, opxy::DrumPanel*'))),
    ('sampleAnalysis', (('_analysis_words', 'opxy::u32', 'const opxy::SampleView*'),
                        ('_prepare_begin', 'opxy::u32', 'const opxy::SampleView*, opxy::u32*, opxy::u32'),
                        ('_prepare_step', 'opxy::u32', 'opxy::u32, opxy::u32*'),
                        ('_prepared', 'opxy::u32', 'opxy::SampleAnalysis*'))),
    ('sliceTiming', (('_slice_clock', 'void', 'const opxy::SampleSlice*, opxy::SampleSliceRate*'),)),
)


def sample_callbacks(m):
    """The C entry points a sample machine exports for the services it declares."""
    resources = m.definition['resources']
    return [m.prefix + suffix for flag, entries in SAMPLE_SERVICES if resources.get(flag, 0) for suffix, *_ in entries]


def sample_refusal(prefix, stereo):
    """The inert voice object and refusal counter of a sample adapter below 256 voices."""
    source = (TEMPLATES / 'sample_voice_refusal.cpp.in').read_text(encoding='utf-8')
    return source.replace('@PREFIX@', prefix).replace('@STEREO@', str(int(bool(stereo))))


def generate_sample_voice(directory, m, benchmark=False):
    resources = m.definition['resources']
    values = dict(PREFIX=m.prefix, NAMESPACE=m.definition['id'], NAME=m.native_name,
                  LABELS=stock_labels(m), CAPACITY=resources['voiceCapacity'],
                  ANALYSIS=resources.get('sampleAnalysis', 0), STEREO=resources.get('stereoOutput', 0),
                  CHOKE=resources.get('chokeSameKey', 0), DRUM_KEYS=int(m.sample_page == 7),
                  PAD_SETTINGS=resources.get('padSettings', 0), DRUM_CONTROLS=resources.get('drumControls', 0),
                  # The drum page edits outer markers only; an engine with its own screen spans them.
                  OUTER_MARKERS=int(bool(resources.get('drumControls', 0)) or 'display' in resources),
                  SLICE_TIMING=resources.get('sliceTiming', 0))
    # Below stock's 256 a refused index gets a shared inert object, not null
    # (sample_voice_refusal.cpp.in); at 256 nothing is ever refused and the
    # adapter stays as it was.
    refusing = resources['voiceCapacity'] < STOCK_VOICES
    values['REFUSED'] = 'refused()' if refusing else 'nullptr'
    values['REFUSAL_DEFINITIONS'] = sample_refusal(m.prefix, values['STEREO']) if refusing else ''
    path = expand('sample_voice.cpp.in', values, Path(directory) / f'sample_voice_{m.prefix}.cpp')
    return append_callback(path, m) if benchmark else path


def generate_sample_callbacks(directory, machines):
    """The collection's sample machines as one table (sample_machines.hpp) and the stock-page bridge."""
    declarations, rows = [], []
    for m in machines:
        if m.kind != 'sample':
            continue
        p, resources = m.prefix, m.definition['resources']
        declarations.append(f'int {p}_ready() __asm__("{p}_ready");')
        # The engine ID is an absolute symbol the linker supplies; the stock page is the machine's own.
        declarations.append(f'extern const char {p}_engine_id[] __asm__("{p}_engine_id");')
        fields = [p + '_engine_id', str(m.sample_page), p + '_ready']
        for flag, entries in SAMPLE_SERVICES:
            declared = bool(resources.get(flag, 0))
            if flag == 'sampleAnalysis':
                fields.append(f'{resources.get(flag, 0)}u')
            for suffix, result, arguments in entries:
                if declared:
                    declarations.append(f'{result} {p}{suffix}({arguments}) __asm__("{p}{suffix}");')
                fields.append(p + suffix if declared else 'nullptr')
        rows.append('    {' + ', '.join(fields) + '},')
    return expand('sample_callbacks.cpp.in', dict(CALLBACK_DECLS='\n'.join(declarations), MACHINES='\n'.join(rows)),
                  Path(directory) / 'sample_callbacks.cpp')


def control_cycles(definition):
    """next[slot] for a synth's alternate controls: each position's parameter, then its alternates in
    parameter order, then back. None when the definition lists no alternates."""
    positions = control_positions(definition)
    if len(positions) <= 4:
        return None
    following = list(range(8))
    for position in range(4):
        # An absent own slot still starts its position's cycle, so the press reaches the alternates.
        cycle = [slot for slot, owner in enumerate(positions) if owner == position or (slot == position and owner is None)]
        for i, slot in enumerate(cycle):
            following[slot] = cycle[(i + 1) % len(cycle)]
    return following


def enum_steps(parameter):
    """An enum's choice count, for stock's stepped encoder edits; 0 for any other control and for an
    enum too large for stock's step table (fewer than 256 steps)."""
    labels = parameter['mapping'].get('labels', []) if parameter['mapping']['kind'] == 'enum' else []
    return len(labels) if len(labels) < 256 else 0


def generate_synth_controls(directory, machines):
    """Synth ownership, alternate selection and continuous fine controls."""
    declarations, rows, formatters, lfo_rows = [], [], [], []
    for m in machines:
        if m.kind in ('synth', 'drum', 'sample'):
            declarations.append(f'int {m.prefix}_record_ready() __asm__("{m.prefix}_record_ready");')
            declarations.append(f'extern const char {m.prefix}_engine_id[] __asm__("{m.prefix}_engine_id");')
            controls = ', '.join(str((i if position is None else position) % 4) for i, position in enumerate(control_positions(m.definition)))
            lfo_rows.append(f'    {{{m.prefix}_engine_id, {m.prefix}_record_ready, {len(m.definition["parameters"])}, {{{controls}}}}},')
        # A sample engine with its own screen pages its eight controls as a paged synth does.
        own_screen = m.kind == 'sample' and m.display != 'stock'
        if m.kind not in ('synth', 'drum', 'effect') and not own_screen:
            continue
        effect = m.kind == 'effect'
        count = len(m.definition['parameters'])
        if own_screen:
            following = [(i + 4) % 8 if count > 4 and (i + 4) % 8 < count else i for i in range(8)]
        else:
            following = control_cycles(m.definition) or list(range(8))
        continuous = sum(1 << i for i, p in enumerate(m.definition['parameters']) if p['mapping']['kind'] != 'enum' and not p.get('absent'))
        absent = sum(1 << i for i, p in enumerate(m.definition['parameters']) if p.get('absent'))
        steps = [enum_steps(p) for p in m.definition['parameters']]
        steps += [0] * (8 - len(steps))
        declarations.append(f'int {m.prefix}_ready() __asm__("{m.prefix}_ready");')
        # The engine or effect ID is an absolute symbol the linker supplies.
        id_symbol = m.prefix + ('_fx_id' if effect else '_engine_id')
        if effect:
            declarations.append(f'extern const char {id_symbol}[] __asm__("{id_symbol}");')
        body = [f'static bool {m.prefix}_format(opxy::u32 slot, opxy::i32 value, char* text) {{', '    switch (slot) {']
        # An effect's shift-row controls caption as slots 8..11.
        captioned = list(enumerate(m.definition['parameters']))
        captioned += [(SHIFT_SLOT + i, p) for i, p in enumerate(shift_parameters(m.definition) or synth_shift(m.definition))]
        for i, p in captioned:
            mapping = p['mapping']
            if p.get('absent'):
                continue
            # The selected choice's label, on synth and send pages alike.
            if mapping['kind'] == 'enum':
                labels = ', '.join(json.dumps(label) for label in mapping['labels'])
                body.extend([f'    case {i}: {{', f'        static const char* const labels[] = {{{labels}}};',
                             f'        control_label(text, value, labels, {len(mapping["labels"])});',
                             '        return true;', '    }'])
                continue
            # Stock delay's note-length names. Send pages keep stock captions
            # for every other control.
            if p['display'] == 'division':
                body.extend([f'    case {i}:', '        control_division(text, value);', '        return true;'])
                continue
            if effect:
                continue
            # Compact native ratio captions; unusual/signed ranges retain the
            # stock normalized display, with an extra decimal place.
            if p['display'] == 'ratio' and mapping['kind'] == 'anchors' and 0 <= mapping['values'][0] < mapping['values'][-1] <= 1000:
                anchors = ', '.join(str(math.floor(v * 10 + 0.5)) for v in mapping['values'])
                body.extend([f'    case {i}: {{', f'        static const opxy::u32 anchors[] = {{{anchors}}};',
                             f'        control_tenths(text, control_ratio(value, anchors, {len(mapping["values"])}), ":1");',
                             '        return true;', '    }'])
            else:
                body.extend([f'    case {i}:', '        control_tenths(text, value < 0 ? 0 : ((static_cast<opxy::u32>(value) >> 10) * 1000 + (1u << 20)) >> 21, "");',
                             '        return true;'])
        body.extend(['    default: return false;', '    }', '}'])
        formatters.append('\n'.join(body))
        # A drum kit's press pages its whole row, on a key that has a second row
        # (synth_controls.cpp second_row).
        page = m.kind == 'drum' or own_screen and count > 4 or paged(m.definition)
        # An effect's shift row: its choice counts and titles; other rows leave the fields zero.
        extra = ''
        shift = shift_parameters(m.definition)
        if shift:
            titles = ', '.join(json.dumps(p['label'].lower()) for p in shift)
            declarations.append(f'static const char* const {m.prefix}_shift_titles[] = {{{titles}}};')
            choices = [len(p['mapping']['labels']) for p in shift] + [1] * (SYNTH_POSITIONS - len(shift))
            extra = f", {len(shift)}, {{{', '.join(map(str, choices))}}}, {m.prefix}_shift_titles"
        elif synth_shift(m.definition):
            # A synth's Shift row (SDK 1.13, synth_controls.hpp SynthShift).
            tables = shift_row_tables(m.definition)
            titles = ', '.join(json.dumps('' if p.get('absent') else p['label'].lower()) for p in synth_shift(m.definition))
            declarations.append(f'static const char* const {m.prefix}_shift_titles[] = {{{titles}}};')
            declarations.append(f'static const opxy::platform::SynthShift {m.prefix}_shift_row = '
                                f"{{{tables['count']}, {{{tables['choices']}}}, {{{tables['defaults']}}}, {m.prefix}_shift_titles}};")
            extra = f', 0, {{1, 1, 1, 1}}, nullptr, {{0, 0, 0, 0}}, &{m.prefix}_shift_row'
        elif own_screen and 'stockShift' in m.definition['resources']:
            # Shift gives these encoders to the stock sampler page (drum_controls_stock.cpp).
            codes = ', '.join(map(str, stock_shift_codes(m.definition)))
            extra = f', 0, {{1, 1, 1, 1}}, nullptr, {{{codes}}}'
        rows.append(f"    {{{id_symbol}, {m.prefix}_ready, {{{', '.join(map(str, following))}}}, {continuous}, "
                    f"{{{', '.join(map(str, steps))}}}, {m.prefix}_format, "
                    f"{'true' if effect else 'false'}, {'true' if page else 'false'}, {hidden_pages(m.definition)}, {absent}{extra}}},")
    return expand('synth_control_table.cpp.in', dict(READY_DECLS='\n'.join(declarations), MACHINES='\n'.join(rows), FORMATTERS='\n'.join(formatters), LFO_MACHINES='\n'.join(lfo_rows)),
                  Path(directory) / 'synth_control_table.cpp')



def generate_project_companion_table(directory, machines):
    """Each machine's ID and its number symbols, sorted by ID as a companion lists them
    (docs/projects.md). Numbers are link-time symbols; a kit's unselected machines are 0."""
    declarations, rows = [], []
    for m in sorted((m for m in machines if m.kind != 'player'), key=lambda m: m.id):
        number, page = (m.prefix + '_fx_id', m.prefix + '_fx_page_id') if m.kind == 'effect' else \
            (m.prefix + '_engine_id', m.prefix + '_page_id')
        declarations += [f'extern const char {name}[] __asm__("{name}");' for name in (number, page)]
        rows.append(f'    {{"{m.id}", {number}, {page}, {"true" if m.kind == "effect" else "false"}}},')
    return expand('project_companion_table.cpp.in', dict(DECLARATIONS='\n'.join(declarations), MACHINES='\n'.join(rows)),
                  Path(directory) / 'project_companion_table.cpp')

# A send slot's stock scratch memory: scratch(slot, bytes) (0x8d50e048), as stock
# delay and reverb use it, only for the two send slots (docs/abi-1.2.1.md).
FX_BUFFER_INIT = '''    // Stock's per-send scratch memory, which stock delay and reverb use the same way.
    object->buffer = slot == 14 || slot == 15
        ? opxy::platform::stock<opxy::i32*(opxy::u32, opxy::u32)>(0x8d50e048)(slot, {bytes}u) : nullptr;'''


def track_control(m):
    """A synth's declared track control (resources.trackControl), or {}."""
    return (m.definition['resources'].get('trackControl') if m.kind == 'synth' else None) or {}


def generate_control(directory, m):
    """A synth's track control callbacks behind C entry points, with its state."""
    control, namespace, p = track_control(m), m.definition['id'], m.prefix
    copies = 1 if control.get('unique') else 16  # a copy per track, aux tracks included
    source = f'''// Generated: {m.native_name}'s track control behind C entry points.
#include "control.cpp"
static_assert(sizeof({namespace}::ControlState) == {control['stateBytes']},
              "Declared track control state size differs from C++ type");
static_assert(alignof({namespace}::ControlState) == {control['stateAlignment']},
              "Declared track control state alignment differs from C++ type");
static_assert(__is_trivial({namespace}::ControlState) && __is_standard_layout({namespace}::ControlState),
              "Track control state must be trivial standard-layout data");
// One state per copy: a unique machine has one, any other one per track.
namespace {{ {namespace}::ControlState states[{copies}]; }}
extern "C" void {p}_control_reset(opxy::u32) __asm__("{p}_control_reset");
extern "C" void {p}_control(opxy::u32, const opxy::control::Input*, opxy::control::Overlay*) __asm__("{p}_control");
extern "C" void {p}_control_reset(opxy::u32 copy) {{ {namespace}::control_reset(states[copy]); }}
extern "C" void {p}_control(opxy::u32 copy, const opxy::control::Input* input, opxy::control::Overlay* overlay) {{
    {namespace}::control(states[copy], *input, *overlay);
}}
'''
    if 'savedBytes' in control:
        source += f'''extern "C" opxy::u32 {p}_control_save(opxy::u8*) __asm__("{p}_control_save");
extern "C" void {p}_control_load(const opxy::u8*, opxy::u32) __asm__("{p}_control_load");
extern "C" opxy::u32 {p}_control_save(opxy::u8* data) {{
    const opxy::u32 bytes = {namespace}::control_save(states[0], data, {control['savedBytes']});
    return bytes <= {control['savedBytes']} ? bytes : 0;
}}
extern "C" void {p}_control_load(const opxy::u8* data, opxy::u32 bytes) {{
    {namespace}::control_load(states[0], data, bytes);
}}
'''
    destination = Path(directory) / 'control_binding.cpp'
    destination.write_text(source, encoding='utf-8', newline='\n')
    return destination


def control_callbacks(m):
    """The C entry points a synth with track control exports."""
    control = track_control(m)
    if not control:
        return []
    suffixes = ('_control_reset', '_control') + (('_control_save', '_control_load') if 'savedBytes' in control else ())
    return [m.prefix + suffix for suffix in suffixes]


def generate_track_controls(directory, machines):
    """The collection's synths with track control, as one table (track_control.hpp)."""
    # Shift + M1 on an aux track lists its stock role and every machine that may take
    # it over (aux_tracks.cpp, kChoices): refuse a build whose list would not fit.
    for m in machines:
        require(not track_control(m).get('aux') or m.id not in AUX_ROLES.values(),
                f'A machine that takes over aux tracks is not named as a stock role ({m.id})')
    for track in AUX_ROLES:
        holders = [m.id for m in machines if track_control(m) and track in track_control(m).get('aux', ())]
        require(len(holders) <= AUX_CHOICES - 1,
                f'At most {AUX_CHOICES - 1} machines may take over aux track {track} in one build, not {len(holders)}')
    declarations, rows = [], []
    for m in machines:
        if not track_control(m):
            continue
        p, saved = m.prefix, track_control(m).get('savedBytes', 0)
        declarations += [f'int {p}_ready() __asm__("{p}_ready");',
                         f'extern const char {p}_engine_id[] __asm__("{p}_engine_id");',
                         f'void {p}_control_reset(opxy::u32) __asm__("{p}_control_reset");',
                         f'void {p}_control(opxy::u32, const opxy::control::Input*, opxy::control::Overlay*) '
                         f'__asm__("{p}_control");']
        if saved:
            declarations += [f'opxy::u32 {p}_control_save(opxy::u8*) __asm__("{p}_control_save");',
                             f'void {p}_control_load(const opxy::u8*, opxy::u32) __asm__("{p}_control_load");']
        callbacks = f'{p}_control_save, {p}_control_load' if saved else 'nullptr, nullptr'
        unique = 'true' if track_control(m).get('unique') else 'false'
        shared_lfo = 'true' if track_control(m).get('sharedLfo') else 'false'
        # The aux tracks it may take over, by index; there it shows its own page
        # and starts from its defaults, as stock applies them on tracks 1-8.
        aux = sum(1 << (t - 1) for t in track_control(m).get('aux', ()))
        require(not aux or m.display != 'stock', 'A machine that takes over aux tracks needs its own display')
        page = f'{p}_page_id' if m.display != 'stock' else 'nullptr'
        if m.display != 'stock':
            declarations.append(f'extern const char {p}_page_id[] __asm__("{p}_page_id");')
        defaults = ', '.join(str(d << 16) for d in catalog_defaults(m.definition))
        # Its presets become files in content/aux_presets (aux_preset_files), named by them.
        require(not aux or all(len(name) <= 15 for name in m.definition['presets']),
                'A machine that takes over aux tracks names its presets in at most 15 characters')
        rows.append(f'    {{{p}_engine_id, {p}_ready, {p}_control_reset, {p}_control, "{m.id}", {saved}, {callbacks}, '
                    f'{unique}, {aux:#x}u, {page}, {{{defaults}}}, {shared_lfo}}},')
    return expand('track_control_table.cpp.in', dict(CONTROL_DECLS='\n'.join(declarations), MACHINES='\n'.join(rows)),
                  Path(directory) / 'track_control_table.cpp')


# An effect's shift row (SDK 1.7): its choices, packed into the send track's
# word shift_row::kWord, reach the DSP as enum words 8..11 after the eight.
SHIFT_DECLS = '''static const opxy::u8 kShiftChoices[{count}] = {{{choices}}};
static void shift_words(opxy::u32 slot, const opxy::i32* parameters, opxy::i32* words) {{
    namespace row = opxy::platform::shift_row;
    for (opxy::u32 i = 0; i < 8; ++i) words[i] = parameters[i];
    const opxy::i32 packed = slot == 14 || slot == 15
        ? static_cast<opxy::i32>(opxy::platform::word(row::kAudioWords + slot * row::kTrackBytes + row::kWord * 4)) : 0;
    opxy::u32 fields[{count}];
    row::decode(packed, kShiftChoices, {count}, fields);
    for (opxy::u32 i = 0; i < row::kControls; ++i)
        words[8 + i] = i < {count} ? row::midpoint(fields[i], kShiftChoices[i]) : 0;
}}'''
SHIFT_PRELUDE = '''    opxy::i32 words[12];
    shift_words(object->slot, parameters, words);'''


def generate_fx_object(directory, m, benchmark=False, idle=False):
    resources = m.definition['resources']
    bytes_ = resources.get('bufferBytes')
    if bytes_ is None:
        values = dict(OBJECT='struct Object { const void* const* vtable; State state; };\n'
                             'static_assert(sizeof(State) <= opxy::effect_state_bytes, "Effect state exceeds the stock FX slot");',
                      INIT='    (void)slot;', BUFFER='', RESET='')
    else:
        values = dict(OBJECT='struct Object { const void* const* vtable; State state; opxy::i32* buffer; };\n'
                             'static_assert(sizeof(State) <= opxy::effect_buffered_state_bytes,\n'
                             '              "Effect state exceeds the stock FX slot beside its buffer address");',
                      INIT=FX_BUFFER_INIT.format(bytes=bytes_),
                      BUFFER=f'opxy::Buffer{{object->buffer, object->buffer ? {bytes_ // 4}u : 0u}}, ',
                      RESET='    object->buffer = nullptr;')
    # A tempo-aware effect reads the audio clock's transport once per block.
    values['TRANSPORT'] = 'opxy::platform::audio_transport(), ' if resources.get('transport') else ''
    values.update(SHIFT_DECLS='', SHIFT_PRELUDE='', PARAMETERS='parameters')
    shift = shift_parameters(m.definition)
    if shift:
        # The slot finds the send track's shift-row word on the audio side.
        values['OBJECT'] = values['OBJECT'].replace(' };\n', ' opxy::u32 slot; };\n', 1).replace(
            'sizeof(State) <=', 'sizeof(State) + 4 <=', 1)
        values['INIT'] = '    object->slot = slot;\n' + values['INIT'].replace('    (void)slot;', '')
        values['RESET'] += '\n    object->slot = 0;'
        choices = ', '.join(str(len(p['mapping']['labels'])) for p in shift)
        values['SHIFT_DECLS'] = SHIFT_DECLS.format(count=len(shift), choices=choices)
        values['SHIFT_PRELUDE'] = SHIFT_PRELUDE
        values['PARAMETERS'] = 'words'
    # The buffer's scratch call, the transport and the shift row use stock addresses.
    if bytes_ is not None or resources.get('transport') or shift:
        values['OBJECT'] = '#include "stock.hpp"\n' + ('#include "shift_row.hpp"\n' if shift else '') + values['OBJECT']
    values.update(effect_bus_values(m, idle))
    if resources.get('busShift') or idle:
        values['OBJECT'] = '#include "send_bus.hpp"\n' + values['OBJECT']
    path = expand('fx_object.cpp.in', dict(values, PREFIX=m.prefix, NAMESPACE=m.definition['id'],
                                           NAME=m.native_name, LABELS=stock_labels(m),
                                           DRY_LEVEL=f'parameters[{m.dry_parameter}]' if m.dry_parameter is not None
                                           else f'{m.dry_level:#x}' if m.dry_level is not None else '0x7fffffff'),
                  Path(directory) / f'fx_object_{m.prefix}.cpp')
    return append_callback(path, m) if benchmark else path


def effect_bus_values(m, idle=False):
    """An effect's bus shift (resources.busShift, send_bus.hpp): the block raised by
    N bits, saturating, ahead of the shift row, and the result lowered by N, rounded,
    after process. In a build with the idle capability its init also records, per send,
    its vtable and the hold its tail needs (send_hold) for the send hook
    (send_fx.cpp). Empty, leaving the object's source as it was, for an effect without a
    shift in a build without idle."""
    n = m.definition['resources'].get('busShift', 0)
    values = dict(BUS_INIT='', BUS_RAISE='', BUS_LOWER='')
    if idle:
        values['BUS_INIT'] = (f'\n    opxy::platform::send_bus::declare(slot, {m.prefix}_fx_vtable, '
                              f'{send_hold(m.definition):#x}u);')
    if n:
        values.update(BUS_RAISE=f'    opxy::platform::send_bus::raise<{n}>(left, right);\n',
                      BUS_LOWER=f'\n    opxy::platform::send_bus::lower<{n}>(left, right);')
    return values


# The idle capability's send hold (send_fx.cpp), in 64-frame blocks: the effect's
# tail plus 64 blocks of margin for parameter smoothing. The tail is what the
# effect declares (resources.tailFrames), or else the longest a plain line in its
# memory holds: 16-bit mono samples, in bufferBytes or in its state, and for an effect that
# declares nothing never less than a fixed 3.07 s (SEND_HOLD_UNDECLARED): the derivation
# may lengthen an undeclared effect's hold, never shorten it below that. "unbounded"
# (a looper, a freeze, anything that sounds without send input) never idles.
SEND_HOLD_MARGIN = 64
SEND_HOLD_UNDECLARED = 2048 + 64   # 3.07 s at 44.1 kHz
SEND_HOLD_ENDLESS = 0xffffffff
EFFECT_STATE_BYTES = 476   # opxy/effect.hpp effect_state_bytes


def send_hold(definition):
    resources = definition['resources']
    tail = resources.get('tailFrames')
    if tail == 'unbounded':
        return SEND_HOLD_ENDLESS
    if tail is None:
        derived = -(-(resources.get('bufferBytes', EFFECT_STATE_BYTES) // 2) // 64) + SEND_HOLD_MARGIN
        return max(derived, SEND_HOLD_UNDECLARED)
    return -(-tail // 64) + SEND_HOLD_MARGIN


def stock_labels(m):
    """Control names as the stock parameter page shows them."""
    return ', '.join(json.dumps(p['label'].lower()) for p in m.definition['parameters'])


# The stock role of each aux track a machine can take over (1-based), as presets name it.
AUX_ROLES = {12: 'cv'}
# The entries of an aux track's Shift + M1 list: its stock role, then machines (aux_tracks.cpp kChoices).
AUX_CHOICES = 32
# The aux presets the device lists, in every folder together, and its folders
# (aux_tracks.cpp kPresets, kFolders).
AUX_PRESETS, AUX_FOLDERS = 64, 16


def check_aux_presets(machines):
    """Refuses a build whose aux preset list the device cannot show whole."""
    paths = {path for m in machines for path in aux_preset_files(m)}
    require(len(paths) <= AUX_PRESETS, f'The aux machines in one build have at most {AUX_PRESETS} presets '
                                       f'together, the role inits included, not {len(paths)}')
    folders = {path.split('/')[2] for path in paths}
    require(len(folders) <= AUX_FOLDERS, f'The aux machines in one build have at most {AUX_FOLDERS} preset '
                                         f'folders together, the roles included, not {len(folders)}')


def aux_preset_files(m):
    """A machine that takes over aux tracks: its definition's presets as the files the
    aux preset list reads (aux_tracks.cpp), by path. Each names the machine in "type"
    and holds its eight controls as instrument presets store them (0..32767). With
    them, an `init` for the stock role of each of its tracks, without controls: it
    gives the track back to that role, so the role is listed for those who only
    choose presets."""
    if not (track_control(m) and track_control(m).get('aux')):
        return {}
    parameters = m.definition['parameters']
    # The device lists at most 64 aux presets (aux_tracks.cpp kPresets), the role's init among them.
    names = [name.lower() for name in m.definition['presets']]
    require(len(set(names)) == len(names), 'A machine that takes over aux tracks names its presets apart, ignoring case')
    require(len(names) + len(track_control(m)['aux']) <= AUX_PRESETS,
            f'A machine that takes over aux tracks has at most {AUX_PRESETS - 1} presets')
    files = {}
    for track in track_control(m)['aux']:
        role = AUX_ROLES[track]
        document = {'platform': 'OP-XY', 'type': role, 'version': 1}
        files[f'content/aux_presets/{role}/init.json'] = (json.dumps(document, indent=2) + '\n').encode()
    for name, preset in m.definition['presets'].items():
        params = [math.floor(preset.get(p['id'], p['default']) * 32767 + 0.5) for p in parameters]
        params += [0] * (8 - len(params))
        document = {'params': params, 'platform': 'OP-XY', 'type': m.id, 'version': 1}
        files[f'content/aux_presets/{m.id}/{name.lower()}.json'] = (json.dumps(document, indent=2) + '\n').encode()
    return files


def catalog_defaults(definition):
    """Eight Q15 stock parameter defaults from normalized definition defaults."""
    defaults = [math.floor(p['default'] * 32767 + 0.5) for p in definition['parameters']]
    return defaults + [0] * (8 - len(defaults))


def catalog_entry(m):
    """The stock catalog text inserted for a machine, in the reviewed byte layout. A machine
    that takes over aux tracks has none: it is chosen there, never in tracks 1-8's engine list."""
    if track_control(m) and track_control(m).get('aux'):
        return ''
    if m.kind in ('effect', 'player'):
        return (',\n        {\n            "type": ' + json.dumps(m.native_name) +
                ',\n            "id": ' + str(m.identity['effect'] if m.kind == 'effect' else m.identity['player'] + PLAYER_CATALOG_OFFSET) +
                ',\n            "default_params": ' + json.dumps(catalog_defaults(m.definition)) + '\n        }')
    return (',\n        {\n            "type": ' + json.dumps(m.native_name) +
            ',\n            "default_params": ' + json.dumps([16384] * 8 if m.kind == 'drum' else catalog_defaults(m.definition)) + '\n        }\n')



def generate_player_object(directory, m):
    """A player's stock object methods around its press and release, and its own page: the SDK's
    dot page (screen_player.hpp), or the one its display draws (SDK 1.24, screen_player_drawing.hpp)."""
    from .native import player_header_ids, player_page_ids
    values = dict(PREFIX=m.prefix, NAMESPACE=m.definition['id'], NAME=m.native_name,
                  PAGE='nullptr', PAGE_READY='nullptr', PAGE_DECLARATIONS='', PAGE_UNIT='',
                  PAGED='true' if paged(m.definition) else 'false',
                  CLICKS='true' if m.definition.get('press') == 'click' else 'false')
    # One choice marks an absent slot: its encoder turns nothing (players.cpp).
    # So do the second page's slots a player leaves out.
    steps = [1 if p.get('absent') else enum_steps(p) for p in m.definition['parameters']]
    values['STEPS'] = ', '.join(map(str, steps + [1] * (8 - len(steps))))
    values.update(player_shift_values(m))
    if m.display in ('player', 'drawing'):
        p = m.prefix
        titles = [json.dumps(parameter['label'].lower()) for parameter in m.definition['parameters']]
        titles += ['""'] * (8 - len(titles))
        fields = dict(PAGED=values['PAGED'], TITLES=', '.join(titles), PAGE_LIMIT=RELOCATED['PAGE_LIMIT'], PREFIX=p,
                      NAMESPACE=m.definition['id'], NAME=m.native_name)
        if m.display == 'drawing':
            # Its own drawn page (SDK 1.24): display.cpp in this unit, after player.cpp; from
            # SDK 1.26 an extended display's frame, input and renderer (extended_retained.hpp).
            from .native import display_ids
            display = m.definition['resources']['display']
            page = (TEMPLATES / 'player_drawing.cpp.in').read_text(encoding='utf-8')
            ids = display_ids(p, display)
            extended, encoders = extended_display(display), bool(display.get('encoderActivity'))
            fields.update(FRAME=player_frame(display), INPUT=display_input_type(m.definition),
                          RETAINED='ExtendedRetained' if extended else 'RetainedDrawing',
                          BIND='Retained::bind<Page>(object);' if extended else 'Retained::bind(object, ids);',
                          FULL_CANVAS='true' if display.get('fullCanvas') else 'false',
                          SNAPSHOT='input.encoders = opxy::platform::encoder_activity();' if encoders else '(void)input;',
                          INCLUDES=''.join(f'#include "{name}"\n' for name in
                                           ('extended_retained.hpp',) * extended + ('encoder_activity.hpp',) * encoders),
                          STATE_BYTES=str(display['stateBytes']), STATE_ALIGNMENT=str(display['stateAlignment']),
                          SCREEN_BYTES=str(screen_bytes(m)))
        else:
            page = (TEMPLATES / 'player_page.cpp.in').read_text(encoding='utf-8')
            # A paged player's page also drives the two-row header in its artwork (native.page_artwork).
            ids = player_page_ids(p) + (player_header_ids(p + '_header') if paged(m.definition) else [])
        fields.update(IDS='\n'.join(f'    "{name}",' for name in ids), ID_COUNT=str(len(ids)))
        for key, value in fields.items():
            page = page.replace('@' + key + '@', value)
        values.update(PAGE=f'{p}_page_id', PAGE_READY=f'{p}_screen_ready', PAGE_UNIT=page,
                      PAGE_DECLARATIONS='\n'.join([f'extern const char {p}_page_id[] __asm__("{p}_page_id");',
                                                    f'int {p}_screen_register() __asm__("{p}_screen_register");',
                                                    f'int {p}_screen_ready() __asm__("{p}_screen_ready");']))
    return expand('player_object.cpp.in', values, Path(directory) / f'player_object_{m.prefix}.cpp')


def player_shift_values(m):
    """A player's shift row (SDK 1.16) as its Player's table, or nothing, which leaves the
    generated source of a player without one as it was."""
    shift = shift_parameters(m.definition)
    if not shift:
        return dict(SHIFT_TABLE='', SHIFT='')
    choices, defaults = [], []
    for parameter in shift:
        count = len(parameter['mapping']['labels'])
        # The stored default (catalog_defaults' Q15) and its choice, as the SDK steps an enum.
        bucket = min((math.floor(parameter['default'] * 32767 + 0.5) * count) >> 15, count - 1)
        width = 0x7fffffff // count
        choices.append(count)
        defaults.append(bucket * width + width // 2)
    choices += [0] * (8 - len(shift))
    defaults += [0] * (8 - len(shift))
    table = (f'static const opxy::platform::PlayerShift player_shift = {{{len(shift)}, '
             f'{{{", ".join(map(str, choices))}}}, {{{", ".join(map(str, defaults))}}}}};\n')
    return dict(SHIFT_TABLE=table, SHIFT=', &player_shift')


def generate_player_table(directory, machines):
    """The collection's players, which the stock player hooks look up."""
    players = [m for m in machines if m.kind == 'player']
    declarations = [f'const opxy::platform::Player* {m.prefix}_player() __asm__("{m.prefix}_player");' for m in players]
    rows = [f'    {{{m.prefix}_player}},' for m in players]
    return expand('player_table.cpp.in', dict(DECLS='\n'.join(declarations), ROWS='\n'.join(rows)),
                  Path(directory) / 'player_table.cpp')


# Stock lists Arpeggio, Hold and Maestro (types 2..4) with catalog IDs 6..8.
PLAYER_CATALOG_OFFSET = 4


def preset_path(m):
    """content/presets/<id>/default.json for an instrument; None for an effect, a
    player or a machine on an aux track (builder/core.mjs presetPath). An aux machine
    has no catalog entry and keeps its presets under content/aux_presets/ instead
    (aux_preset_files)."""
    return (None if m.kind in ('effect', 'player') or track_control(m).get('aux')
            else f'content/presets/{m.id}/default.json')


def default_preset(m):
    """The generated init preset: catalog defaults merged into the shared template
    (tooling/default_preset.json, also embedded into builder/core.mjs's defaultPreset).
    Engine controls come from the catalog, never from a copied factory sound."""
    entry = json.loads(catalog_entry(m).strip().lstrip(','))
    amp = m.preset_defaults.get('amp', {})
    preset = copy.deepcopy(DEFAULT_PRESET_TEMPLATE)
    preset['engine']['params'] = list(entry['default_params'])
    # Match a device-saved minimum amp release: the stored endpoint is 32767.
    preset['envelope']['amp'].update(amp)
    if 'volume' in m.preset_defaults:
        preset['engine']['volume'] = m.preset_defaults['volume']
    preset['type'] = entry['type']
    return preset


def preset_files(m):
    """The machine's init preset, then its own presets (tooling.machines.read_presets):
    each a version 4 preset of this machine, pointing at no samples or, for a sample
    engine shipping its own (SDK 1.10), at one of those. Its own default replaces the
    generated init preset (builder/core.mjs presetFiles)."""
    path = preset_path(m)
    if not path:
        return {}
    presets = m.presets or {}
    files = {} if 'default' in presets else {path: default_preset(m)}
    for name, preset in presets.items():
        files[f'content/presets/{m.id}/{name}.json'] = preset
    return files

