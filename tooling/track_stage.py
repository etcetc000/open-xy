"""A synth's track stage (resources.trackStage, SDK 1.24; docs/api.md#track-stage).

A synth may declare a stage: one state per instrument track that holds it, which
the SDK processes once per block, in place, on the track's stereo voice sum, after
stock's voice loop and before the track's gain and buses. It runs from the shared
handler of stock's track call (track_call.s), which the profile's track call record
names for the `track_stage` service; track_stage.cpp is its runtime and
track_stage_table.cpp, generated here, lists the declaring synths. A build without
such a synth links none of it, and its voice adapters carry none of it.
"""
from pathlib import Path

from .cpu import control_defaults
from .definitions import ROOT, shift_words
from .formats import require

# The first SDK version with track stages. A renumber changes this line (and the 1.24
# line of SDK_VERSION's history in machines.py and the "SDK 1.24" text of docs/api.md).
TRACK_STAGE_SDK = (1, 24)
# The hold of a stage that never sleeps (track_stage.hpp kUnbounded).
UNBOUNDED = 0xffffffff
# Blocks of margin after a stage's tail before it sleeps, as for idle sends.
MARGIN = 64
# The adapter's stage code (voice.cpp.in's TRACK_STAGE).
TEMPLATE = ROOT / 'sdk/platform/opxy-1.2.1/track_stage_voice.cpp.in'


def track_stage(m):
    """A synth's trackStage declaration, or None."""
    return m.definition['resources'].get('trackStage') if m.kind == 'synth' else None


def hold(m):
    """Blocks in a row without voices and with its output below the floor after which a
    stage sleeps: its tail in 64-frame blocks plus MARGIN, or UNBOUNDED, never."""
    tail = track_stage(m)['tailFrames']
    return UNBOUNDED if tail == 'unbounded' else (tail + 63) // 64 + MARGIN


def adapter_values(m, shift_row=False):
    """voice.cpp.in's TRACK_STAGE: empty for a synth without a stage, so its adapter keeps
    its text; else the states, their checks and the two entries the table names."""
    stage = track_stage(m)
    if stage is None:
        return dict(TRACK_STAGE='')
    if shift_row:
        # Parameters 8..15, the Shift row, from the track's words 36 and 37 as a voice decodes them.
        words = ('    opxy::i32 controls[2 * opxy::parameter_slots];\n'
                 '    for (unsigned i = 0; i < opxy::parameter_slots; ++i) controls[i] = parameters[i];\n'
                 '    static opxy::i32 shift_kept[opxy::platform::track_stage::kTracks][2];\n'
                 '    shift_words(parameters, controls + opxy::parameter_slots, shift_kept[track]);\n')
        given = 'controls'
    else:
        words, given = '', 'parameters'
    source = TEMPLATE.read_text(encoding='utf-8')
    # The diagnostic's words: the controls at their defaults, then a Shift row's at its own.
    defaults = ', '.join(map(str, control_defaults(m.definition['parameters'])[:8] +
                             (shift_words(m.definition) or [0] * 8)))
    for key, value in (('PREFIX', m.prefix), ('NAMESPACE', m.definition['id']), ('STATE_BYTES', stage['stateBytes']),
                       ('STATE_ALIGNMENT', stage['stateAlignment']), ('STAGE_WORDS', words), ('STAGE_PARAMETERS', given),
                       ('STAGE_DEFAULTS', defaults), *audio_values(m).items()):
        require(('@%s@' % key) in source, 'Track stage template placeholder missing')
        source = source.replace('@%s@' % key, str(value))
    # After the template's last line, which keeps its own line end.
    return dict(TRACK_STAGE='\n' + source.rstrip('\n'))


def audio_values(m):
    """A stage declaring audio (SDK 1.27): track_process takes the synth's AudioContext for
    its track, on the bank its voices publish to (adapters.audio_voice_values), as one more
    update after the voice loop; the diagnostic's run publishes into scratch words no screen
    reads, so its publishing is timed. Empty for a stage without it, which keeps its text."""
    if not track_stage(m).get('audio'):
        return dict(STAGE_AUDIO_BEGIN='', STAGE_AUDIO_ARGUMENT='', STAGE_AUDIO_END='', STAGE_BENCHMARK_AUDIO='')
    display = m.definition['resources']['display']
    count = display['audioWords']
    handshake = ('audio_handshake, audio_handshake + 1, audio_handshake + 2' if display.get('audioReset')
                 else 'nullptr, nullptr, nullptr')
    return dict(
        STAGE_AUDIO_BEGIN=('    auto* audio_bank = sdk_audio.resolve(track);\n'
                           '    const opxy::AudioContext audio_context = sdk_audio.context(track, audio_bank);\n'
                           '    sdk_audio.begin(audio_bank);\n'),
        STAGE_AUDIO_ARGUMENT=', audio_context',
        STAGE_AUDIO_END='\n    sdk_audio.end(audio_bank);',
        STAGE_BENCHMARK_AUDIO=(f'    static volatile u32 audio_words[{count}], audio_handshake[3];\n'
                               f'    const AudioContext audio_context(0, audio_words, {count}, {handshake}, 0, true);\n'))


def stage_symbols(m):
    """The entries a declaring synth's adapter defines, which the table names."""
    return (m.prefix + '_track_reset', m.prefix + '_track_process')


def benchmark_symbol(m):
    """The diagnostic's stage run a declaring synth's adapter defines (benchmark.py STAGE)."""
    return m.prefix + '_stage_benchmark'


def absent_symbols(m):
    """An unselected declaring synth's entries, each kit_absent; its ready check (also
    kit_absent) declines first, so none is called."""
    if track_stage(m) is None:
        return {}
    return dict.fromkeys((*stage_symbols(m), benchmark_symbol(m)), 'kit_absent')


def generate_table(directory, machines):
    """track_stage_table.cpp: the declaring synths' stages, in collection order."""
    declarations, rows = [], []
    for m in machines:
        if track_stage(m) is None:
            continue
        p = m.prefix
        reset, process = stage_symbols(m)
        declarations += [f'extern const char {p}_engine_id[] __asm__("{p}_engine_id");',
                         f'int {p}_record_ready() __asm__("{p}_record_ready");',
                         f'void {reset}(opxy::u32) __asm__("{reset}");',
                         f'void {process}(opxy::u32, opxy::i32*, const opxy::i32*, opxy::u32) __asm__("{process}");']
        rows.append(f'    {{{p}_engine_id, {p}_record_ready, {reset}, {process}, {hold(m)}u}},')
    require(rows, 'A track stage table needs a synth that declares one')
    source = ('#include "track_stage.hpp"\nextern "C" {\n' + '\n'.join(declarations) + '\n'
              'extern const opxy::platform::track_stage::Stage sdk_track_stages[] __asm__("sdk_track_stages");\n'
              'extern const opxy::u32 sdk_track_stage_count __asm__("sdk_track_stage_count");\n}\n'
              'const opxy::platform::track_stage::Stage sdk_track_stages[] = {\n' + '\n'.join(rows) + '\n};\n'
              f'const opxy::u32 sdk_track_stage_count = {len(rows)};\n')
    destination = Path(directory) / 'track_stage_table.cpp'
    destination.write_text(source, encoding='utf-8', newline='\n')
    return destination
