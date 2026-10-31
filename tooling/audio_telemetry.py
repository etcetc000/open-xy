"""Explicit audio/display transport contracts; no implicit native enablement."""


def display(definition):
    return definition['resources']['display']


def enabled(definition):
    return bool(display(definition).get('audioTransport'))


def extended(definition):
    d = display(definition)
    return enabled(definition) and (d.get('audioWords', 0) > 16 or bool(d.get('audioReset')))


def capacity(definition):
    return 256 if extended(definition) else 16


def input_type(definition):
    return 'TelemetryInput' if extended(definition) else 'AudioInput' if display(definition).get('audioWords') else 'Input'


# Authenticated OP-XY1.2.1 call sites. The baseline profile/hash is untouched.
HOOKS = (
    dict(kind='hook', address='0x8c0ba4f4', file_offset='0xf1730', length=8,
         before='00de4d8d106a0000', symbol='sdk_telemetry_tick', call=True, service='telemetry'),
    dict(kind='hook', address='0x8d5b1af4', file_offset='0x1e8d4c', length=4,
         before='f9e3d525', symbol='sdk_telemetry_receive_hook', call=True, service='telemetry'),
)


def extend_profile(profile, machines):
    from copy import deepcopy
    from .pipeline import check_patch_boundaries
    if not any(m.kind == 'synth' and enabled(m.definition) for m in machines):
        return profile
    result = deepcopy(profile)
    result['services']['telemetry'] = ['synth']
    result['patches'].extend(deepcopy(HOOKS))
    check_patch_boundaries(result, len(profile['patches']) + 2)
    return result


def generate_registry(directory, machines):
    from pathlib import Path
    selected = [m for m in machines if m.kind == 'synth' and enabled(m.definition)]
    lines = ['#include "audio_transport_native.hpp"', '#include "stock.hpp"',
             'namespace opxy { namespace platform { namespace telemetry {']
    for m in selected:
        lines += [f'extern "C" wire::Bank* {m.prefix}_audio_bank(u32) __asm__("{m.prefix}_audio_bank");']
    lines += ['wire::Bank* resolve_bank(void*, wire::u16 engine, wire::u8 track) {',
              '    if (track >= 8 || word(0x11b01d48u + track * 4) != engine) return nullptr;',
              '    switch (engine) {']
    for m in selected:
        lines += [f'    case {m.identity["engine"]}: return {m.prefix}_audio_bank(track);']
    lines += ['    default: return nullptr;', '    }', '}',
              'bool configuration(u32 engine, u32& words, u32& mode, bool& reset, u32& prefix) {',
              '    switch (engine) {']
    for m in selected:
        d = display(m.definition)
        lines += [f'    case {m.identity["engine"]}: words={d["audioWords"]}; mode={d["audioMode"]}; reset={str(bool(d.get("audioReset"))).lower()}; prefix={d.get("audioAccumulators", 1)}; return true;']
    lines += ['    default: return false;', '    }', '}', '}}}']
    path = Path(directory) / 'telemetry_registry.cpp'
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    return path
