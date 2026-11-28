"""A machine's audio snapshots for its own screen (docs/display.md#audio-snapshots).

A synth or an effect declaring audioWords needs audioTransport: the native transport, the
profile's 'telemetry' service, which carries snapshots on the device. Nothing here applies
to a machine that declares neither.

On the wire a snapshot is keyed by an owner and a slot: a synth's engine number and its
track (0..7), or an effect's number with EFFECT_KEY set and its send (0 or 1), so the two
kinds never share a key.
"""
from pathlib import Path

from .definitions import ROOT
from .formats import require

# Set in an effect's wire key (audio_transport_native.hpp effect_key); synth engine and
# effect numbers are both below it.
EFFECT_KEY = 0x8000
KINDS = ('synth', 'effect')


def display(definition):
    return definition['resources'].get('display', {})


def words(definition):
    return display(definition).get('audioWords', 0)


def enabled(definition):
    """Whether the machine uses the native transport."""
    return bool(display(definition).get('audioTransport'))


def wide(definition):
    """Whether its screen takes draw::TelemetryInput: more than 16 words, or the reset handshake."""
    d = display(definition)
    return enabled(definition) and (d.get('audioWords', 0) > 16 or bool(d.get('audioReset')))


def capacity(definition):
    return 256 if wide(definition) else 16


def input_type(definition):
    return 'TelemetryInput' if wide(definition) else 'AudioInput' if words(definition) else 'Input'


def shape(definition):
    """The bank's template arguments: words, mode, reset, accumulators."""
    d = display(definition)
    return (f"{d['audioWords']}, {d['audioMode']}, {str(bool(d.get('audioReset'))).lower()}, "
            f"{d.get('audioAccumulators', 1)}")


def users(machines):
    return [m for m in machines if m.kind in KINDS and enabled(m.definition)]


def wire_key(m):
    """The owner half of the machine's wire key."""
    return EFFECT_KEY | m.identity['effect'] if m.kind == 'effect' else m.identity['engine']


def number_symbol(m):
    """The link-time symbol holding the machine's engine or effect number."""
    return m.prefix + ('_fx_id' if m.kind == 'effect' else '_engine_id')


def bank_symbol(m):
    """The machine adapter's bank accessor, which the registry calls."""
    return m.prefix + '_audio_bank'


def owner_row(m):
    """A declaring synth's or effect's row in the audio snapshot registry (audio_owner.hpp Owner),
    as adapters.Row, which its record names (tooling/records.py); None for any other machine. Its
    number is named by link-time symbol, so the row is the same whatever else a build holds."""
    from .adapters import Row
    if m.kind not in KINDS or not enabled(m.definition):
        return None
    d = display(m.definition)
    return Row((f'extern const char {number_symbol(m)}[] __asm__("{number_symbol(m)}");',
                f'opxy_transport::telemetry::Bank* {bank_symbol(m)}(opxy::u32) __asm__("{bank_symbol(m)}");'),
               f'{{{number_symbol(m)}, {str(m.kind == "effect").lower()}, {bank_symbol(m)}, {d["audioWords"]}, '
               f'{d["audioMode"]}, {str(bool(d.get("audioReset"))).lower()}, {d.get("audioAccumulators", 1)}}}')


def generate_registry(directory, machines):
    """For host checks: the registry these machines' records build (owner_row) and its lookup
    (telemetry_registry.cpp), as one telemetry_registry.cpp in `directory`."""
    from .adapters import host_tables
    require(users(machines), 'An audio snapshot registry needs a machine that declares audioTransport')
    path = host_tables(Path(directory) / 'telemetry_registry.cpp', ('audio_transport_native.hpp',),
                       [('opxy::platform::telemetry::Owner', 'sdk_audio_owners', 'sdk_audio_owner_count', owner_row)],
                       machines)
    lookup = (ROOT / 'sdk/platform/opxy-1.2.1/telemetry_registry.cpp').read_text(encoding='utf-8')
    path.write_text(path.read_text(encoding='utf-8') + lookup, encoding='utf-8', newline='\n')
    return path
