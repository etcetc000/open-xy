"""Stock release floor the release_floor capability raises, audited site by site.

Stock 1.2.1's per-block envelope routine (8d50e110), which the normal voice
renderer calls for each voice's amplitude and filter envelopes, ends a release
(stage 4) once the Q31 level falls to 1,073,741 (2^31 / 2000, -66 dB) by
entering stage 0, idle; the renderer then retires a voice whose amplitude
envelope is idle. The release_floor capability raises that floor to a chosen
value from -66 to -20 dB, so quiet release tails stop sooner and free their
voice, and enters stage 5 instead of stage 0. Stage 5 is stock's forced fade
(linear to silence over one 64-sample block, then idle), so a raised floor
fades rather than clicks.

The floor is the envelope's own level, before velocity, filter, track volume
and mixing, so a setting is a level below the envelope's peak rather than true
dBFS. Held notes (stage 3), stolen-voice fades (stages 6 and 7) and the drum
renderer's envelopes stay stock. The filter envelope shares the routine: its
release also stops at the floor, holding the last 0.03..10% of its depth.

Each value has its own reviewed bytes (the floor edit's `choices`), so the
builder recognizes a setting in an image exactly, without arithmetic.
"""
from .formats import require
from .stock_tables import decode

SERVICE = 'release_floor'
STOCK_FLOOR = 1073741  # 2^31 / 2000, stock's -66 dB release floor
MINIMUM, MAXIMUM, DEFAULT = -66, -20, -48
FLOOR_SITE = 0x8d50e226  # stage 4: R1 = floor; CC = level <= R1
STAGE_SITE = 0x8d50e232  # stage 4 below the floor: R0 = 0 (stage 0, slope 0) -> 5
SPEC = dict(label='Idle threshold', unit='dB', minimum=MINIMUM, maximum=MAXIMUM, default=DEFAULT)


def floor(db):
    """The Q31 level a setting ends releases at: stock's exact value at -66 dB."""
    require(type(db) is int and MINIMUM <= db <= MAXIMUM, f'Idle threshold is a whole number of dB, {MINIMUM}..{MAXIMUM}')
    return STOCK_FLOOR if db == MINIMUM else round((1 << 31) * 10 ** (db / 20))


def floor_bytes(opcode, db):
    value = floor(db)
    return bytes.fromhex('%04x' % opcode)[::-1] + (value >> 16).to_bytes(2, 'little') + (value & 0xffff).to_bytes(2, 'little') + bytes(2)


def choices(before):
    """Every setting's reviewed bytes for the floor edit, keyed by its dB value."""
    opcode, value = decode('imm32', bytes.fromhex(before))
    require(value == STOCK_FLOOR, 'Release floor preimage differs from the audited stock value')
    return {str(db): floor_bytes(opcode, db).hex() for db in range(MAXIMUM, MINIMUM - 1, -1)}


def check_edits(patches):
    """Exactly the two audited edits: the floor immediate, with one reviewed encoding per
    setting, and stage 0 -> stage 5 below it."""
    require(len(patches) == 2, 'Unexpected release-floor edit count')
    floor_edit, stage_edit = patches
    for patch in patches:
        require(patch['kind'] == 'limit' and patch.get('service') == SERVICE, 'A release-floor edit belongs to its service')
    require(int(floor_edit['address'], 16) == FLOOR_SITE and floor_edit['length'] == 8 and
            floor_edit.get('setting') == SERVICE, 'Unexpected release-floor site')
    require(floor_edit.get('choices') == choices(floor_edit['before']), 'Release-floor choices differ from the audited encodings')
    require(floor_edit['after'] == floor_edit['choices'][str(DEFAULT)], 'Release-floor default differs')
    require(int(stage_edit['address'], 16) == STAGE_SITE and stage_edit['length'] == 2 and 'choices' not in stage_edit,
            'Unexpected release-stage site')
    opcode, stage = decode('imm7', bytes.fromhex(stage_edit['before']))
    require(stage == 0 and decode('imm7', bytes.fromhex(stage_edit['after'])) == (opcode, 5),
            'The release-stage edit changes only stage 0 to stock forced fade 5')


def check_profile(profile, registry):
    require(profile['services'].get(SERVICE) == [SERVICE], 'Expected the release_floor service')
    require(registry.get(SERVICE) == dict(setting=SPEC), 'Release-floor registry setting differs')
    check_edits([p for p in profile['patches'] if p.get('service') == SERVICE])

