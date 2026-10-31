"""SDK adapters for every machine kind: generated bindings, registration and catalog entries.

Machine code implements plain C++ callbacks. These adapters supply the stock
object layouts, method tables, storage, registration and menu entries:

- synth: initialize/start/destroy/render over Voice and Shared state, with a
  drawing display;
- sample: flag-1 synth registration that receives the track's sample;
- effect: send-FX registry object with in-place stereo processing.
"""
import json
import math
from pathlib import Path

from . import audio_telemetry
from .benchmark import append_callback
from .definitions import ROOT, SYNTH_POSITIONS, control_positions, paged, shift_parameters, validate_definition

# An effect's shift-row controls follow its eight parameter words (slots 8..11).
SHIFT_SLOT = 8
from .formats import require
from .stock_tables import REGISTRATION as RELOCATED

TEMPLATES = ROOT / 'sdk/platform/opxy-1.2.1'


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
                AUDIO_WORDS=resources['display'].get('audioWords', 0),
                RELEASE_CALLBACK=f"{definition['id']}::release(*voice, shared, owner(voice));" if resources.get('releaseCallback') else '',
                AUDIO_ARGUMENT=', audio_context' if resources['display'].get('audioWords') else '')


def generate_voice(directory, m, sweep=False):
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
    values = dict(voice_values(m.definition), SWEEP_DECLARATIONS=declarations, SWEEP_CONTROLS=controls,
                  PREFIX=m.prefix, NAME=m.native_name, LABELS=stock_labels(m),
                  CAPACITY=m.definition['resources']['voiceCapacity'])
    values.pop('PEAK')
    values.pop('RELEASE_CALLBACK')
    release = m.definition['resources'].get('releaseCallback', 0)
    values.update(RELEASE_METHOD=f'{m.prefix}_release' if release else 'noop',
                  RELEASE_FUNCTION=f'''int {m.prefix}_release(void* self) {{
    Handle* handle = static_cast<Handle*>(self);
    if (Slot* slot = held(handle))
        {m.definition['id']}::release(slot->voice, shared, slot->owner);
    else if (pending(handle) || handle->wants) handle->released = 1;
    return 0;
}}
''' if release else '')
    values.update(RELEASE_STATE=', released' if release else '',
                  RELEASE_CLEAR='    handle->released = 0;' if release else '',
                  RELEASE_PENDING=f"    if (handle->released) {m.definition['id']}::release(slot.voice, shared, id);" if release else '')
    # Expand to the original mono text when omitted, preserving its native code.
    values.update(OUTPUT_FRAMES='opxy::block_frames' if values['CHANNELS'] == 1 else '2 * opxy::block_frames',
                  STEREO_METHOD='zero' if values['CHANNELS'] == 1 else 'stereo',
                  STEREO_FUNCTION='' if values['CHANNELS'] == 1 else 'static int stereo() { return 1; }\n')
    words = values['AUDIO_WORDS']
    values.update(AUDIO_INCLUDE='#include "audio_telemetry.hpp"\n' if words else '',
                  AUDIO_CONTEXT='        const opxy::AudioContext audio_context = audio.context(opxy::platform::synth_audio_track(parameters));\n' if words else '',
                  AUDIO_STORAGE=f'''static opxy::TelemetryBank<{words}> audio;
extern "C" unsigned {m.prefix}_take_audio(opxy::u32, opxy::u32*) __asm__("{m.prefix}_take_audio");
extern "C" unsigned {m.prefix}_take_audio(opxy::u32 track, opxy::u32* out) {{
    return opxy::platform::take_audio(audio, track, out);
}}
''' if words else '')
    values['FADE_OUTPUT'] = ('            for (opxy::i32 i = 0; i < frames; ++i) output[i] = opxy::q31::multiply(output[i], step * (frames - 1 - i));' if values['CHANNELS'] == 1 else
        '            for (opxy::i32 i = 0; i < frames; ++i) for (opxy::i32 channel = 0; channel < 2; ++channel)\n'
        '                output[2 * i + channel] = opxy::q31::multiply(output[2 * i + channel], step * (frames - 1 - i));')
    values['AUDIO_END'] = ''
    if audio_telemetry.enabled(m.definition):
        d = audio_telemetry.display(m.definition)
        mode, reset = d['audioMode'], str(bool(d.get('audioReset'))).lower()
        values.update(AUDIO_INCLUDE='#include "audio_transport_native.hpp"\n',
                      AUDIO_STORAGE=f'''static opxy::platform::telemetry::NativeBank<{words}, {mode}, {reset}, {d.get('audioAccumulators', 1)}> audio;
extern "C" opxy::platform::telemetry::wire::Bank* {m.prefix}_audio_bank(opxy::u32) __asm__("{m.prefix}_audio_bank");
extern "C" opxy::platform::telemetry::wire::Bank* {m.prefix}_audio_bank(opxy::u32 track) {{ return audio.resolve(track); }}
''', AUDIO_CONTEXT='''        const opxy::u32 audio_track = opxy::platform::synth_audio_track(parameters);
        auto* audio_bank = audio.resolve(audio_track);
        const opxy::AudioContext audio_context = audio.context(audio_track, audio_bank, opxy::platform::synth_prepared_amplitude(parameters));
        audio.begin(audio_bank);
''', AUDIO_END='        audio.end(audio_bank);\n')
    return expand('voice.cpp.in', values, Path(directory) / 'voice.cpp')


def generate_voice_preview(directory, definition):
    """Browser preview voices for a synth definition (curated or local)."""
    values = voice_values(validate_definition(definition, 'synth'))
    values.update(AUDIO_BANK=f'opxy::TelemetryBank<{values["AUDIO_WORDS"]}>', AUDIO_TAKE='audio.take(track, out)',
                  AUDIO_EXTRA='')
    if audio_telemetry.enabled(definition):
        d = audio_telemetry.display(definition)
        cap = audio_telemetry.capacity(definition)
        values.update(AUDIO_BANK=f'opxy::HostTelemetryBank<{d["audioWords"]}, {d["audioMode"]}, {str(bool(d.get("audioReset"))).lower()}, {d.get("audioAccumulators", 1)}>',
                      AUDIO_TAKE=f'audio.take(track, out, {cap})', AUDIO_EXTRA=f'''
opxy::u32 opxy_audio_capacity() {{ return {cap}; }}
opxy::u32 opxy_audio_sequence(opxy::u32 track) {{ return audio.sequence(track); }}
opxy::u32 opxy_audio_requested(opxy::u32 track) {{ return audio.requested_command(track); }}
opxy::u32 opxy_audio_applied(opxy::u32 track) {{ return audio.applied_command(track); }}
opxy::u32 opxy_audio_reset_supported() {{ return {int(bool(d.get('audioReset')))}; }}
opxy::u32 opxy_audio_reset(opxy::u32 track, opxy::u32 target) {{ return audio.request_reset(track, target); }}
''')
    return expand('voice_preview.cpp.in', values, Path(directory) / 'voice_preview.cpp', ROOT / 'sdk/host')


def drawing_layout(display):
    """Frame byte layout shared by the C++ frame and the browser."""
    paths, points, nodes, texts, palette = (display[k] for k in ('paths', 'points', 'nodes', 'texts', 'palette'))
    commands = display.get('commands', 0)
    path_bytes = 8 + 20 * commands if display.get('quadratic') else (8 if display.get('countedPaths') else 4) + 8 * points
    # An empty path or text array keeps one unused element (draw.hpp).
    nodes_at = max(paths, 1) * path_bytes
    texts_at = nodes_at + (32 if display.get('affine') else 28) * nodes
    header_at = texts_at + 36 * max(texts, 1)
    return dict(PATHS=paths, POINTS=points, COMMANDS=commands, NODES=nodes, TEXTS=texts, PALETTE=palette, PATH_BYTES=path_bytes,
                FRAME_NODES_AT=nodes_at, FRAME_TEXTS_AT=texts_at, FRAME_HEADER_AT=header_at,
                FRAME_GLYPHS_AT=header_at + 32, FRAME_BYTES=header_at + 32 + 4 * display.get('glyphs', 0))


def screen_bytes(m):
    """Renderer object size: the 76-byte stock screen, then the renderer's words
    (screen_drawing.hpp). The generated renderer asserts its C++ object has this size."""
    display = m.definition['resources']['display']
    # Nodes; text leaves and each slot's applied 24 characters; palette; frame
    # delta; then validated geometry and applied key per path (one word each when absent).
    texts, paths = max(display['texts'], 1), max(display['paths'], 1)
    words = display['nodes'] + 7 * texts + max(display['palette'], 1) + 1 + 2 * paths
    glyph_bytes = (1032 * display['glyphDefinitions'] + 28 * display['glyphs'] + 4) if display.get('glyphs') else 0
    return 76 + 4 * words + glyph_bytes + ((display['nodes'] + 3) // 4) * 4


def generate_display(directory, *, machine=None, definition=None):
    """Same authored display callback for host, WebAssembly and native wrappers."""
    if definition is None:
        definition, assignment = machine.definition, stock_identity(machine)
    else:
        validate_definition(definition, 'synth')
        assignment = {'symbol_prefix': 'local_preview'}
    namespace, prefix = definition['id'], assignment['symbol_prefix']
    display = definition['resources']['display']
    audio_words = display.get('audioWords', 0)
    encoder_activity = bool(display.get('encoderActivity'))
    display_input = audio_telemetry.input_type(definition)
    if encoder_activity: display_input = 'Encoder' + display_input
    layout = drawing_layout(display)
    frame_kind = 'AffineCountedFrame' if display.get('countedPaths') else 'AffineQuadraticFrame' if display.get('quadratic') else ('Affine' if display.get('affine') else '') + 'CubicFrame' if display.get('cubic') else 'AffineFrame' if display.get('affine') else 'Frame'
    extra = ''
    if not display.get('cubic') and (display.get('openPaths') or display.get('segmentPaths')):
        extra = ('' if display.get('affine') else ', false, false') + f", {display.get('openPaths', 0)}u, {display.get('segmentPaths', 0)}u"
    capacity = '{COMMANDS}' if display.get('quadratic') else '{POINTS}'
    frame = ('opxy::draw::' + frame_kind + '<{PATHS}, ' + capacity + ', {NODES}, {TEXTS}, {PALETTE}' + extra + '>').format(**layout)
    if display.get('rgba'): frame = 'opxy::draw::RgbaFrame<' + frame + '>'
    if display.get('glyphs'):
        frame = f"opxy::draw::GlyphFrame<{frame}, {display['glyphs']}, {display['glyphDefinitions']}, {display['glyphStart']}>"
    header = f'''// Generated by the SDK; no renderer addresses in author code.
#pragma once
#include "display.h"
#include <opxy/audio_transport.hpp>
#include <opxy/encoder_activity.hpp>
namespace opxy {{ namespace binding {{
using DisplayState = {namespace}::DisplayState;
using DisplayInput = draw::{display_input};
typedef {frame} Frame;
const unsigned display_bytes = {display['stateBytes']};
const unsigned display_alignment = {display['stateAlignment']};
const bool full_canvas = {'true' if display.get('fullCanvas') else 'false'};
const unsigned audio_words = {audio_words};
const unsigned display_abi = {(5 if audio_telemetry.extended(definition) else 4 if audio_words else 3) + (3 if encoder_activity else 0)};
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
              sizeof(Frame::path_type) == {layout['PATH_BYTES']} && sizeof(Frame::node_type) == {32 if display.get('affine') else 28} &&
              __builtin_offsetof(Frame::path_type, {'commands' if display.get('quadratic') else 'points'}) == {8 if display.get('quadratic') or display.get('countedPaths') else 4} &&
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
                                       const opxy::binding::DisplayInput* input, Frame* frame) {{
    {namespace}::display(*state, *input, *frame);
}}
extern "C" unsigned {prefix}_display_validate(const Frame* frame) {{
    return opxy::draw::valid(*frame);
}}
'''
    directory = Path(directory)
    if audio_telemetry.enabled(definition):
        header += native_audio_binding(definition, prefix)
    elif audio_words:
        header += f'''extern "C" unsigned {prefix}_take_audio(opxy::u32, opxy::u32*) __asm__("{prefix}_take_audio");
namespace opxy {{ namespace binding {{
inline void snapshot_audio(draw::AudioInput& input) {{
    input.audio_count = {prefix}_take_audio(input.track, input.audio_words);
}}
}}}}
'''
    else:
        header += 'namespace opxy { namespace binding { inline void snapshot_audio(draw::Input&) {} }}\n'
    if not audio_telemetry.enabled(definition):
        header += 'namespace opxy { namespace binding { inline void audio_screen_reset() {} inline void finish_audio(DisplayInput&) {} }}\n'
    if encoder_activity:
        header += '''#if defined(__bfin__)
#include "encoder_activity.hpp"
namespace opxy { namespace binding {
inline void snapshot_encoders(DisplayInput& input) { input.encoders = platform::encoder_activity(); }
}}
#else
namespace opxy { namespace binding { inline void snapshot_encoders(DisplayInput&) {} }}
#endif
'''
    else:
        header += 'namespace opxy { namespace binding { inline void snapshot_encoders(DisplayInput&) {} }}\n'
    (directory / 'display_binding.hpp').write_text(header, encoding='utf-8', newline='\n')
    (directory / 'display_binding.cpp').write_text(source, encoding='utf-8', newline='\n')


def native_audio_binding(definition, prefix):
    """Native calls are platform-only; host display snapshots enter through WASM."""
    extended = audio_telemetry.extended(definition)
    capacity = audio_telemetry.capacity(definition)
    metadata = '''    input.audio_sequence = snapshot.sequence;
    input.audio_requested_command = snapshot.requested;
    input.audio_applied_command = snapshot.applied;
    input.audio_reset_request = 0;
''' if extended else ''
    if extended:
        metadata += f'    input.audio_reset_supported = {int(bool(audio_telemetry.display(definition).get("audioReset")))};\n'
    return f'''#if defined(__bfin__)
#include "audio_transport_native.hpp"
extern "C" const char {prefix}_engine_id[] __asm__("{prefix}_engine_id");
namespace opxy {{ namespace binding {{
inline u32 audio_engine() {{ return reinterpret_cast<u32>({prefix}_engine_id); }}
inline void audio_screen_reset() {{ platform::telemetry::reset_screen(audio_engine()); }}
inline void snapshot_audio(DisplayInput& input) {{
    const auto snapshot = platform::telemetry::capture(audio_engine(), input.track, input.audio_words, {capacity});
    input.audio_count = snapshot.count;
{metadata}}}
inline void finish_audio(DisplayInput& input) {{
    platform::telemetry::frame_end(audio_engine(), input.track, {'input.audio_reset_request' if extended else '0'});
}}
}}}}
#else
namespace opxy {{ namespace binding {{
inline void audio_screen_reset() {{}}
inline void snapshot_audio(DisplayInput&) {{}}
inline void finish_audio(DisplayInput&) {{}}
}}}}
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
    objects = [f'const opxy::platform::Record record = {{{", ".join(addresses)}, {int(m.kind == "sample")}}};']
    entries, screen = {}, 'nullptr'
    if m.display != 'stock':
        declarations += [f'extern char {p}_screen_state[] __asm__("{p}_screen_state");',
                         *(f'extern const char {p}_screen_{part}[] __asm__("{p}_screen_{part}");'
                           for part in ('vtable', 'asset')),
                         f'char* {p}_screen_ctor(char*, opxy::u32) __asm__("{p}_screen_ctor");']
        objects.append(f'const opxy::platform::Screen screen = {{{p}_screen_state, {screen_bytes(m)}, '
                       f'{p}_screen_vtable, {p}_screen_asset, {p}_screen_ctor}};')
        screen = '&screen'
        entries[p + '_screen_register()'] = 'sdk_register_screen(&record, &screen)'
    if effect:
        entries[p + '_fx_register()'] = 'sdk_register_effect(&record)'
        entries[p + '_ready()'] = f'sdk_effect_ready(&record, {screen})'
    else:
        entries[p + '_register()'] = 'sdk_register_engine(&record)'
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
    from .native import display_ids, glyph_ids
    ids = display_ids(m.prefix, m.definition['resources']['display'])
    table = ''.join(f'    "{name}",\n' for name in ids)
    glyphs = glyph_ids(m.prefix, m.definition['resources']['display'])
    glyph_decl = f'static const char* const glyph_ids[{len(glyphs)}];' if glyphs else ''
    glyph_table = ('const char* const Machine::glyph_ids[] = {' + ', '.join(json.dumps(x) for x in glyphs) + '};') if glyphs else ''
    source = f'''// Generated by the SDK: {m.native_name}'s declared display as renderer data.
#include "display_binding.hpp"
#include "screen_drawing.hpp"
namespace {{
struct Machine {{
    static const char* const ids[{len(ids)}];
    {glyph_decl}
}};
const char* const Machine::ids[{len(ids)}] = {{
{table}}};
{glyph_table}
}}
OPXY_SCREEN(opxy::screen::Drawing<Machine>, "{m.prefix}", "{m.native_name}.svg", {RELOCATED['PAGE_LIMIT']}, {screen_bytes(m)})
'''
    destination = Path(directory) / 'screen.cpp'
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


def generate_sample_voice(directory, m, benchmark=False):
    resources = m.definition['resources']
    values = dict(PREFIX=m.prefix, NAMESPACE=m.definition['id'], NAME=m.native_name,
                  LABELS=stock_labels(m), CAPACITY=resources['voiceCapacity'],
                  ANALYSIS=resources.get('sampleAnalysis', 0), STEREO=resources.get('stereoOutput', 0),
                  CHOKE=resources.get('chokeSameKey', 0), DRUM_KEYS=int(m.identity['page'] == 7),
                  PAD_SETTINGS=resources.get('padSettings', 0), DRUM_CONTROLS=resources.get('drumControls', 0),
                  SLICE_TIMING=resources.get('sliceTiming', 0))
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
        fields = [p + '_engine_id', str(m.identity['page']), p + '_ready']
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
        if m.kind != 'effect':
            declarations.append(f'int {m.prefix}_record_ready() __asm__("{m.prefix}_record_ready");')
            declarations.append(f'extern const char {m.prefix}_engine_id[] __asm__("{m.prefix}_engine_id");')
            controls = ', '.join(str((i if position is None else position) % 4) for i, position in enumerate(control_positions(m.definition)))
            lfo_rows.append(f'    {{{m.prefix}_engine_id, {m.prefix}_record_ready, {len(m.definition["parameters"])}, {{{controls}}}}},')
        if m.kind not in ('synth', 'effect'):
            continue
        effect = m.kind == 'effect'
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
        captioned += [(SHIFT_SLOT + i, p) for i, p in enumerate(shift_parameters(m.definition))]
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
        page = paged(m.definition)
        # An effect's shift row: its choice counts and titles; other rows leave the fields zero.
        extra = ''
        shift = shift_parameters(m.definition)
        if shift:
            titles = ', '.join(json.dumps(p['label'].lower()) for p in shift)
            declarations.append(f'static const char* const {m.prefix}_shift_titles[] = {{{titles}}};')
            choices = [len(p['mapping']['labels']) for p in shift] + [1] * (SYNTH_POSITIONS - len(shift))
            extra = f", {len(shift)}, {{{', '.join(map(str, choices))}}}, {m.prefix}_shift_titles"
        rows.append(f"    {{{id_symbol}, {m.prefix}_ready, {{{', '.join(map(str, following))}}}, {continuous}, "
                    f"{{{', '.join(map(str, steps))}}}, {m.prefix}_format, "
                    f"{'true' if effect else 'false'}, {'true' if page else 'false'}, {absent}{extra}}},")
    return expand('synth_control_table.cpp.in', dict(READY_DECLS='\n'.join(declarations), MACHINES='\n'.join(rows), FORMATTERS='\n'.join(formatters), LFO_MACHINES='\n'.join(lfo_rows)),
                  Path(directory) / 'synth_control_table.cpp')



def generate_project_companion_table(directory, machines):
    """Each machine's ID and its number symbols, sorted by ID as a companion lists them
    (docs/projects.md). Numbers are link-time symbols; a kit's unselected machines are 0."""
    declarations, rows = [], []
    for m in sorted(machines, key=lambda m: m.id):
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


def generate_fx_object(directory, m, benchmark=False):
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
    path = expand('fx_object.cpp.in', dict(values, PREFIX=m.prefix, NAMESPACE=m.definition['id'],
                                           NAME=m.native_name, LABELS=stock_labels(m),
                                           DRY_LEVEL=f'parameters[{m.dry_parameter}]' if m.dry_parameter is not None
                                           else f'{m.dry_level:#x}' if m.dry_level is not None else '0x7fffffff'),
                  Path(directory) / f'fx_object_{m.prefix}.cpp')
    return append_callback(path, m) if benchmark else path


def stock_labels(m):
    """Control names as the stock parameter page shows them."""
    return ', '.join(json.dumps(p['label'].lower()) for p in m.definition['parameters'])


def catalog_defaults(definition):
    """Eight Q15 stock parameter defaults from normalized definition defaults."""
    defaults = [math.floor(p['default'] * 32767 + 0.5) for p in definition['parameters']]
    return defaults + [0] * (8 - len(defaults))


def catalog_entry(m):
    """The stock catalog text inserted for a machine, in the reviewed byte layout."""
    if m.kind == 'effect':
        return (',\n        {\n            "type": ' + json.dumps(m.native_name) +
                ',\n            "id": ' + str(m.identity['effect']) +
                ',\n            "default_params": ' + json.dumps(catalog_defaults(m.definition)) + '\n        }')
    return (',\n        {\n            "type": ' + json.dumps(m.native_name) +
            ',\n            "default_params": ' + json.dumps(catalog_defaults(m.definition)) + '\n        }\n')

