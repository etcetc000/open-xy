"""Stock release floor the release_floor capability raises, audited site by site.

Stock 1.2.1's per-block envelope routine (8d50e110), which the normal voice
renderer calls for each voice's amplitude and filter envelopes, ends a release
(stage 4) once the Q31 level falls to 1,073,741 (2^31 / 2000, -66 dB) by
entering stage 0, idle; the renderer then retires a voice whose amplitude
envelope is idle. The release_floor capability raises that floor to -40 dB
(21,474,836, stock's own 0.01 constant), so quiet release tails stop sooner
and free their voice, and enters stage 5 instead of stage 0. Stage 5 is stock's
forced fade (linear to silence over one 64-sample block, then idle), so the
raised floor fades rather than clicks.

The floor is the envelope's own level, before velocity, filter, track volume
and mixing, so -40 dB is below the envelope's peak rather than true dBFS. Held
notes (stage 3), stolen-voice fades (stages 6 and 7) and the drum renderer's
envelopes stay stock. The filter envelope shares the routine: its release also
ends at the floor, fading its last 1% of depth to zero over the same block. A
note whose sustain is already below the floor loses its whole release.

Below the floor the stock path stores R0 as the stage and the per-sample slope
(and 0x70000000 as the coefficient), so the first faded block carries slope +5,
a negligible rise; the next block's stage-5 handler (8d50e262) sets the slope to
-level/64 and the coefficient to unity, fading to silence, then idles at stock's
-66 dB floor.
"""
from .formats import require
from .stock_tables import decode

SERVICE = 'release_floor'
STOCK_FLOOR = 1073741  # 2^31 / 2000, stock's -66 dB release floor
FLOOR = 21474836  # -40 dB: round(2^31 / 100), stock's own 0.01 constant
FLOOR_SITE = 0x8d50e226  # stage 4: R1 = floor; CC = level <= R1
STAGE_SITE = 0x8d50e232  # stage 4 below the floor: R0 = 0 (stage 0, slope 0) -> 5
# Stock code and data the edits rely on: the whole envelope routine (stage handlers,
# including stage 5's fade) and its stage dispatch table. An image from another SDK
# must leave them stock (tooling/settings.compare_with_stock).
DEPENDS = ((0x8d50e110, 0x1cc), (0x8404caa8, 0x20))


def check_edits(patches):
    """Exactly the two audited edits: the floor immediate -66 -> -40 dB, and stage 0 ->
    stock forced fade 5 below it, each changing only its immediate."""
    require(len(patches) == 2, 'Unexpected release-floor edit count')
    floor_edit, stage_edit = patches
    for patch in patches:
        require(patch['kind'] == 'limit' and patch.get('service') == SERVICE, 'A release-floor edit belongs to its service')
    require(int(floor_edit['address'], 16) == FLOOR_SITE and floor_edit['length'] == 8, 'Unexpected release-floor site')
    opcode, value = decode('imm32', bytes.fromhex(floor_edit['before']))
    require(value == STOCK_FLOOR, 'Release floor preimage differs from the audited stock value')
    require(decode('imm32', bytes.fromhex(floor_edit['after'])) == (opcode, FLOOR),
            'The release-floor edit changes only the floor, to -40 dB')
    require(int(stage_edit['address'], 16) == STAGE_SITE and stage_edit['length'] == 2, 'Unexpected release-stage site')
    opcode, stage = decode('imm7', bytes.fromhex(stage_edit['before']))
    require(stage == 0 and decode('imm7', bytes.fromhex(stage_edit['after'])) == (opcode, 5),
            'The release-stage edit changes only stage 0 to stock forced fade 5')


def check_profile(profile):
    require(profile['services'].get(SERVICE) == [SERVICE], 'Expected the release_floor service')
    check_edits([p for p in profile['patches'] if p.get('service') == SERVICE])
