"""Generate diagnostic workloads from the same collection as the firmware."""
from pathlib import Path
import struct

from .cpu import control_defaults, sweep
from .pipeline import ROOT
from .formats import require

METHODS = {'synth': ('opxy-synth-c8-v1', 5), 'effect': ('opxy-effect-callback-v1', 7),
           'sample': ('opxy-sample-callback-v1', 3)}
# A sweep build's extra synth runs: the stock workload, one control at a time.
SWEEP = 'opxy-synth-c8-sweep-v1'

STOCK = [('prism', 18), ('simple', 32), ('hardsync', 19), ('dissolve', 20),
         ('axis', 22), ('organ', 6), ('epiano', 7), ('wavetable', 31), ('chord', 13)]


def sweep_settings(m):
    """A synth's swept settings in run order, as (control, its label, Q31 value, the
    option's name for a list control, else None): each control through its positions
    (cpu.sweep, as the host estimate times them); the firmware runs each with the other
    controls at their defaults (cpu.control_defaults)."""
    result = []
    for control, p in enumerate(m.definition['parameters']):
        options = p['mapping']['labels'] if p['mapping']['kind'] == 'enum' else None
        result += [(control, p['label'], value, options[i] if options else None)
                   for i, value in enumerate(sweep(p))]
    return result


def cases(machines, sweep=False):
    """The reports a run sends, in order: stock's nine, then each machine's; in a sweep
    build a synth's swept settings follow its standard run, then one run with every
    control at the position that ran heaviest (combined_positions)."""
    result = [dict(name=name, engine=engine, source='stock') for name, engine in STOCK]
    for m in machines:
        case = dict(name=m.id, engine=m.identity.get('engine', m.identity.get('effect')), source='sdk',
                    kind=m.kind, benchmark=METHODS[m.kind][0], metric=METHODS[m.kind][1])
        result.append(case)
        swept = sweep_settings(m) if sweep and m.kind == 'synth' else []
        if swept:
            result += [dict(case, benchmark=SWEEP, setting=dict(control=control, label=label, value=value,
                                                                **({'option': option} if option else {})))
                       for control, label, value, option in swept]
            result.append(dict(case, benchmark=SWEEP, combined=True))
    return result


def position(setting):
    """A swept setting as people read it: the control's label and its position, or the
    option's name for a list control."""
    if setting.get('option'):
        return f"{setting['label']} {setting['option']}"
    return f"{setting['label']} {100 * setting['value'] / 0x7fffffff:.1f}%"


def combined_label(positions):
    """The combined run as people read it: the positions its capture's reports point to."""
    return 'combined (as read from the reports): ' + ', '.join(map(position, positions))


def case_label(case):
    """What a case ran, as people read it: a swept setting, the combined run, a synth's
    midpoint run or a callback's defaults."""
    if case.get('setting'):
        return position(case['setting'])
    if case.get('combined'):
        return 'combined'
    return 'midpoint (stock)' if case.get('kind', 'synth') == 'synth' else 'defaults'


def case_key(case):
    """A case's name across builds and captures: a stock engine's, or an SDK machine's
    with what it ran, the control's index keeping equal labels apart."""
    if case.get('source') != 'sdk':
        return case['name']
    control = f" #{case['setting']['control']}" if case.get('setting') else ''
    return f"{case['name']}{control} {case_label(case)}"


def combined_positions(results):
    """The positions a machine's combined run used, from that capture's swept results
    ((setting, encoded average) in run order): each control's heaviest, the first of
    equal ones, as the firmware picks them from its cycles per block. Encoded averages
    round those, so on a near tie the firmware may have picked a neighbour."""
    chosen = {}
    for setting, average in results:
        control = setting['control']
        if control not in chosen or average > chosen[control][1]:
            chosen[control] = (setting, average)
    return [chosen[control][0] for control in sorted(chosen)]


def generate(directory, machines, sweep=False):
    declarations, rows, settings, defaults = [], [], [], []
    for index, m in enumerate(machines):
        swept = sweep_settings(m) if sweep and m.kind == 'synth' else []
        if swept:
            # A row per setting holds only its control and value; the defaults beside it
            # are one row per machine.
            defaults.append('    {%d, {%s}},' % (index, ', '.join(map(str, control_defaults(m.definition['parameters'])))))
            settings += ['    {%d, %d, %d},' % (index, control, value)
                         for control, _label, value, _option in swept]
        p = m.prefix
        id_symbol = p + ('_fx_id' if m.kind == 'effect' else '_engine_id')
        declarations += [f'int {p}_ready() __asm__("{p}_ready");',
                         f'extern const char {id_symbol}[] __asm__("{id_symbol}");']
        callback = 'nullptr'
        if m.kind != 'synth':
            callback = p + '_benchmark'
            declarations.append(f'opxy::benchmark::Stats {callback}() __asm__("{callback}");')
        rows.append(f'    {{"{m.id}", {id_symbol}, {p}_ready, {callback}, {METHODS[m.kind][1]}}},')
    source = (ROOT / 'sdk/platform/opxy-1.2.1/benchmark.cpp.in').read_text(encoding='utf-8')
    source = source.replace('@DECLARATIONS@', '\n'.join(declarations))
    source = source.replace('@MACHINES@', '\n'.join(rows))
    source = source.replace('@SETTINGS@', '\n'.join(settings))
    source = source.replace('@DEFAULTS@', '\n'.join(defaults))
    path = Path(directory) / 'benchmark.cpp'
    path.write_text(source, encoding='utf-8', newline='\n')
    return path


def decode(capture, expected):
    """Attribute only a complete ordered run; packets carry no machine identity."""
    frames = [f for f in capture['frames'] if f['command'] == 0x94]
    require(capture['complete'] and not capture['errors'], 'Incomplete or corrupt capture')
    require(len(frames) == len(expected), 'Report count differs from this build; refusing machine attribution')
    require(all(f['route'] == 0x12 and len(bytes.fromhex(f['payload_hex'])) == 120 for f in frames),
            'Unexpected report endpoint or size')
    scale = struct.unpack('<f', bytes.fromhex('1844e73d'))[0]
    result = []
    for case, frame in zip(expected, frames):
        payload = bytes.fromhex(frame['payload_hex'])
        metrics = []
        for index in range(20):
            maximum, average, accumulated = struct.unpack_from('>HHH', payload, index * 6)
            metrics.append(dict(index=index, encoded_maximum=maximum, encoded_average=average,
                                encoded_accumulated=accumulated, maximum_clipped=maximum == 65535,
                                average_budget_percent=100 * average / 65535,
                                approximate_average_cycles=average / scale))
        result.append(dict(machine=case, metrics=metrics,
                           insufficient_voice_activity=metrics[case.get('metric', 3)]['encoded_accumulated'] < 10))
    return result


def append_callback(path, m):
    """Compile the diagnostic beside the real callback, without duplicating author code."""
    resources = m.definition['resources']
    values = dict(PREFIX=m.prefix, NAMESPACE=m.id, DEFAULTS=', '.join(map(str, control_defaults(m.definition['parameters']))))
    if m.kind == 'effect':
        size = resources.get('bufferBytes', 0)
        values.update(BUFFER_BYTES=str(size)+'u', BUFFER=f'Buffer{{static_cast<i32*>(memory.data), {size//4}u}}, ' if size else '',
                      TRANSPORT='time, ' if resources.get('transport') else '')
    else:
        values.update(CHANNELS=2 if resources.get('stereoOutput') else 1, PREPARE='', SETTINGS='')
        if resources.get('padSettings'):
            values['SETTINGS'] = f"""    settings = {m.prefix}_sample_defaults();
    const char filename[] = \"cpu-120bpm.wav\";
    const SampleAssignment assignment{{filename, sizeof(filename)-1, sample_frames, 44100, 1200, false, settings, false}};
    // Reuse the adapter entry outside timing. An indirect setup call keeps GCC
    // from outlining a second copy of the author's inline helper into COMDAT.
    PadSettings (*volatile assign)(const SampleAssignment*) = &{m.prefix}_sample_assign;
    settings = assign(&assignment);"""
        if resources.get('sampleAnalysis'):
            values['PREPARE'] = f"""    const u32 words = {m.id}::analysis_words(sample);
    if (!words || words > {resources['sampleAnalysis']//4}u || words > 1048576u) return stats;
    Memory analysis_memory(words * 4u);
    if (!analysis_memory.data) return stats;
    static {m.id}::Preparation preparation;
    preparation = {m.id}::Preparation();
    if (!{m.id}::prepare_begin(preparation, sample, static_cast<u32*>(analysis_memory.data), words)) return stats;
    PrepareStatus status = prepare_running;
    u32 cursor = 0;
    for (u32 pass = 0; pass < 4096 && status == prepare_running; ++pass)
        status = {m.id}::prepare_step(preparation, 1024, cursor);
    SampleAnalysis completed{{}};
    if (status != prepare_done || !{m.id}::prepared(preparation, completed)) return stats;
    if (completed.words != analysis_memory.data || completed.word_count > words ||
        completed.sample.data != data || completed.sample.frames != sample_frames) return stats;
    analysis = &completed;"""
    source = (ROOT / f'sdk/platform/opxy-1.2.1/benchmark_{m.kind}.cpp.in').read_text()
    for key, value in values.items():
        source = source.replace('@' + key + '@', str(value))
    require('@' not in source, 'Unexpanded diagnostic callback')
    path.write_text(path.read_text() + source, newline='\n')
    return path
