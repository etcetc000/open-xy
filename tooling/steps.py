"""Step components: a step machine replaces one stock sequencer step component.

Stock's sequencer calls each step component's object through its method table:
on reset, when a step is entered, at its trig, for each note and when the
playhead leaves it (docs/abi-1.2.1.md#step-components). A step machine takes
over one of the fourteen keys, its slot: the profile's `step` edits for that
slot point an object component's called methods at the machine's adapter, and
any key's picker name and ten setting labels at the machine's strings. The
three condition keys have no object: the `steps` service registers the
machine's own and routes their decisions to it (step_conditions.cpp). A build
whose collection leaves a slot unassigned leaves that slot's edits stock.

builder/core.mjs computes the same bytes (stepWords).
"""
import struct

from .formats import require

SETTINGS = 10            # black keys 1-9 then 0, stock setting numbers 0-9
LABEL_BYTES = 16         # each label line, NUL-terminated, in the adapter's table
LABEL_CHARACTERS = 14    # stock's longest setting line, "force velocity"
NAME_CHARACTERS = 10     # stock's longest component name, "portamento"
# Edit words: a literal stock address, or one of the machine's adapter methods,
# its name or its labels (<prefix>_step_<word>).
METHODS = ('reset_all', 'reset_track', 'condition', 'apply', 'mute', 'enter', 'next', 'trigs')
SYMBOLS = {word: '_step_' + word for word in (*METHODS, 'name', 'settings')}
# The machine callbacks a definition may declare, beside the methods that call them.
CALLBACKS = ('reset', 'condition', 'enter', 'trigs', 'mute', 'apply', 'next', 'plays')


def service(slot):
    """The pseudo-service that activates a slot's edits, beside the profile's own services."""
    return 'step:' + slot


def slots(profile):
    """Replaceable slots in profile order."""
    found = []
    for patch in profile['patches']:
        if patch['kind'] == 'step' and patch['slot'] not in found:
            found.append(patch['slot'])
    return found


def assigned(machines):
    """Slot -> machine for the step machines of a collection."""
    taken = {}
    for m in machines:
        if m.kind == 'step':
            slot = m.identity['step']
            require(slot not in taken, f'{m.id} and {taken.get(slot) and taken[slot].id} both replace {slot}')
            taken[slot] = m
    return taken


def services(machines):
    return {service(slot) for slot in assigned(machines)}


def words(patch):
    """The edit's words: literals as integers, symbols by token; settings stands for the 20 label pointers."""
    out = []
    for word in patch['words']:
        if word == 'settings':
            out += [('settings', i) for i in range(2 * SETTINGS)]
        elif word in SYMBOLS:
            out.append((word, 0))
        else:
            require(isinstance(word, str) and word.startswith('0x'), 'Invalid step edit word')
            out.append((None, int(word, 16)))
    return out


def check_patch(patch):
    """Shape of a profile step edit; the build computes its bytes."""
    require(isinstance(patch.get('slot'), str) and patch['slot'] and isinstance(patch.get('words'), list) and
            patch['words'], 'A step edit names its slot and words')
    require(len(words(patch)) * 4 == patch['length'] and int(patch['address'], 16) % 4 == 0,
            'Step edit words must cover its aligned bytes')
    require('service' not in patch and 'profiling' not in patch, 'Step edits take no service or profiling flag')


def target(token, index, prefix, symbol):
    """Address a symbolic word holds: the adapter, the name, or one label line."""
    address = symbol(prefix + SYMBOLS[token])
    return address + index * LABEL_BYTES if token == 'settings' else address


def after(patch, prefix, symbol):
    """The edit's bytes for the machine with this prefix; symbol(name) -> address."""
    return b''.join(struct.pack('<I', value if token is None else target(token, value, prefix, symbol))
                    for token, value in words(patch))


def check_after(patch, data, low, high):
    """Literal words unchanged; every symbolic word points inside the module."""
    require(len(data) == patch['length'], 'Step edit length mismatch')
    for (token, value), (word,) in zip(words(patch), struct.iter_unpack('<I', data)):
        require(word == value if token is None else low <= word < high, 'Step edit outside the module')


def label_lines(definition):
    """The 20 label lines in table order: each setting's first then second line."""
    return [line for setting in definition['settings'] for line in setting]
