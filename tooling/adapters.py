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

from .benchmark import append_callback
from .definitions import ROOT, SYNTH_POSITIONS, control_positions, hidden_pages, paged, shift_parameters, validate_definition

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
                PEAK=resources['outputPeakQ31'])


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
    return expand('voice.cpp.in', values, Path(directory) / 'voice.cpp')


def generate_voice_preview(directory, definition):
    """Browser preview voices for a synth definition (curated or local)."""
    return expand('voice_preview.cpp.in', voice_values(validate_definition(definition, 'synth')),
                  Path(directory) / 'voice_preview.cpp', ROOT / 'sdk/host')


def drawing_layout(display):
    """Frame byte layout shared by the C++ frame and the browser."""
    paths, points, nodes, texts, palette = (display[k] for k in ('paths', 'points', 'nodes', 'texts', 'palette'))
    path_bytes = 4 + 8 * points
    # An empty path or text array keeps one unused element (draw.hpp).
    nodes_at = max(paths, 1) * path_bytes
    texts_at = nodes_at + 28 * nodes
    header_at = texts_at + 36 * max(texts, 1)
    return dict(PATHS=paths, POINTS=points, NODES=nodes, TEXTS=texts, PALETTE=palette, PATH_BYTES=path_bytes,
                FRAME_NODES_AT=nodes_at, FRAME_TEXTS_AT=texts_at, FRAME_HEADER_AT=header_at,
                FRAME_BYTES=header_at + 32)


def screen_bytes(m):
    """Renderer object size: the 76-byte stock screen, then the renderer's words
    (screen_drawing.hpp). The generated renderer asserts its C++ object has this size."""
    display = m.definition['resources']['display']
    # Nodes; text leaves and each slot's applied 24 characters; palette; frame
    # delta; then validated geometry and applied key per path (one word each when absent).
    texts, paths = max(display['texts'], 1), max(display['paths'], 1)
    words = display['nodes'] + 7 * texts + display['palette'] + 1 + 2 * paths
    return 76 + 4 * words


def generate_display(directory, *, machine=None, definition=None):
    """Same authored display callback for host, WebAssembly and native wrappers."""
    if definition is None:
        definition, assignment = machine.definition, stock_identity(machine)
    else:
        validate_definition(definition, 'synth')
        assignment = {'symbol_prefix': 'local_preview'}
    namespace, prefix = definition['id'], assignment['symbol_prefix']
    display = definition['resources']['display']
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
    (directory / 'display_binding.hpp').write_text(header, encoding='utf-8', newline='\n')
    (directory / 'display_binding.cpp').write_text(source, encoding='utf-8', newline='\n')


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


# The SDK's own marks (track_control_overlay.svg): four capture outlines, then the line.
DEFAULT_MARK_IDS = ('overlay_capture_0', 'overlay_capture_1', 'overlay_capture_2', 'overlay_capture_3', 'overlay_line')


def mark_ids(m):
    """A track control machine's own mark node IDs (marks.svg.fragment), capture then line, or ()."""
    marks = track_control(m).get('marks')
    if not marks:
        return ()
    return (tuple(f'{m.id}_capture_{i}' for i in range(marks['capture'])) +
            tuple(f'{m.id}_line_{i}' for i in range(marks['line'])))


def generate_track_control_overlay(directory, machines):
    """Track control's marks over every page, in the last page slot: the SDK's
    marks, then each machine's own, with each machine's capture and line nodes
    in the collection's track control order (generate_track_controls)."""
    ids, rows = list(DEFAULT_MARK_IDS), []
    for m in machines:
        if not track_control(m):
            continue
        marks = track_control(m).get('marks')
        if marks:
            first = len(ids)
            ids += mark_ids(m)
            rows.append(f'    {{{first}, {marks["capture"]}, {first + marks["capture"]}, {marks["line"]}}},  // {m.id}')
        else:
            rows.append(f'    {{0, 4, 4, 1}},  // {m.id}: the SDK\'s')
    values = dict(RELOCATED, MARK_NODES=str(len(ids)), MARK_IDS=''.join(f'    "{i}",\n' for i in ids),
                  MARK_MACHINES='\n'.join(rows))
    return expand('track_control_overlay.cpp.in', values, Path(directory) / 'track_control_overlay.cpp')


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
    source = f'''// Generated by the SDK: {m.native_name}'s declared display as renderer data.
#include "display_binding.hpp"
#include "screen_drawing.hpp"
namespace {{
struct Machine {{
    static const char* const ids[{len(ids)}];
}};
const char* const Machine::ids[{len(ids)}] = {{
{table}}};
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
                    f"{'true' if effect else 'false'}, {'true' if page else 'false'}, {hidden_pages(m.definition)}, {absent}{extra}}},")
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


def track_control(m):
    """A synth's declared track control state, or None."""
    return m.definition['resources'].get('trackControl') if m.kind == 'synth' else None


def generate_control(directory, m):
    """A synth's track control callbacks behind C entry points, with its state."""
    control, namespace, p = track_control(m), m.definition['id'], m.prefix
    copies = 1 if control.get('unique') else 8
    source = f'''// Generated: {m.native_name}'s track control behind C entry points.
#include "control.cpp"
static_assert(sizeof({namespace}::ControlState) == {control['stateBytes']},
              "Declared track control state size differs from C++ type");
static_assert(alignof({namespace}::ControlState) == {control['stateAlignment']},
              "Declared track control state alignment differs from C++ type");
static_assert(__is_trivial({namespace}::ControlState) && __is_standard_layout({namespace}::ControlState),
              "Track control state must be trivial standard-layout data");
// One state per copy: a unique machine has one, any other one per instrument track.
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
                    f'{unique}, {aux:#x}u, {page}, {{{defaults}}}}},')
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


# The stock role of each aux track a machine can take over (1-based), as presets name it.
AUX_ROLES = {12: 'cv'}
# The entries of an aux track's Shift + M1 list: its stock role, then machines (aux_tracks.cpp kChoices).
AUX_CHOICES = 32


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
    if m.kind == 'effect':
        return (',\n        {\n            "type": ' + json.dumps(m.native_name) +
                ',\n            "id": ' + str(m.identity['effect']) +
                ',\n            "default_params": ' + json.dumps(catalog_defaults(m.definition)) + '\n        }')
    return (',\n        {\n            "type": ' + json.dumps(m.native_name) +
            ',\n            "default_params": ' + json.dumps(catalog_defaults(m.definition)) + '\n        }\n')

