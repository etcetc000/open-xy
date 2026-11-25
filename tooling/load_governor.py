"""Stock's load governor, which the load_governor capability makes act on a short mean of the block load, with its
budget cut moved up to 99 % and its recovery to 93 % of the load byte's scale; audited byte by byte. It is a plain
switch: off, every site holds stock's bytes; on, the six edits below.

Stock 1.2.1 ends every audio block with 8d50ec6c, which turns the block's cycle count into a load byte
(cycles * 256 / 550,000, at most 255: 255 from 547,852 cycles, 94.4 % of the 580,352-cycle period) and calls the
voice manager's governor 8d51e4c4(pool, load, 0). The pool is the voice manager, the audio engine (0x84c696fc) plus
0x13c: 8d50ec6c passes that at 8d50ed1c..8d50ed26, so pool+0xe0 is 0x84c69918. The governor (L = load << 8, of 65,536):
- smooths L into pool+0xe0 with 8d51c480: a rise moves it a quarter of the gap (`LSHIFT R1 >>= 2` at 8d51c492)
  and holds it 256 blocks; then it falls 8 a block;
- when this block's own L reaches 0xe600 (89.8 %; 8d51e4f4 against the raw L in R4) calls 8d51e3fc, which kills
  one (two from 0xee00, 93.0 %) released voice at or below -26 dB (Q31 0x06666666 at 840532ec) and at least 8
  blocks old with stock's kill 8d51a998 (one-block fade);
- when the smoothed load is under 0xd900 (84.8 %) returns one voice of budget every 32 blocks (pool+0xec up to 24,
  then pool+0xf0 up to 3);
- when it is at or above 0xf200 (94.5 %; the branch at 8d51e566) drops the budget to max(12, active - 2) and kills
  every voice over it (8d51cf3c).
Every threshold is lowered by an overhead term while USB audio is configured (0x26c + 0xa3 per routed stream).

On:
- 8d51c480 is rewritten inside its own bytes (three edits, `MEAN_BYTES`): the load figure moves 1/8 of the way to
  each block's L whether the block is above or below it (`R1 = L - S; R1 >>>= 3; S += R1`, an arithmetic shift, so
  a fall is the same fraction as a rise), with no hold and no fixed fall: a short mean of the block load (time
  constant 8 blocks, about 12 ms). The rise/hold branch at 8d51c48c becomes `R2 = L >> 1`, and the routine returns
  R0 = 0x6680 + L / 2 where it used to return 0x100 (its hold reload, which it no longer stores). The hold word
  (pool+0xe4) is no longer read or written; the hold, decay and floor paths (8d51c4a8..c4d4) are left in place but
  unreachable.
- The single-block test compares 0xe600 with that R0 instead of the block's own L (`MARK`: R4 = R0 before the
  test), so it fires only on a block at the byte's ceiling (L = 0xff00, 547,852 cycles or more: 0x6680 + 0x7f80
  = 0xe600; with USB audio's overhead term T, from L >= 0xff00 - 2T), and its two-voice test (0xee00) never without
  USB audio (0x6680 + L / 2 <= 0xe600; with it, on a saturated block from an overhead of 0x800, 9 routed entries).
  This edit marks the capability on.
- The budget cut's threshold routine 8d51c420 (its only caller is the 94.5 % test at 8d51e560) returns 0xfd70 (99.0 %
  of the load byte's scale) instead of 0xf200: `R1 = 0xf200 (Z)` at 8d51c430 becomes `R1 = 0xfd70 (Z)`. The load
  byte saturates at 255 (L = 0xff00, 99.6 %), and the mean settles 0 to 7 under L from below, so under saturation
  on every block it reads at least 0xfef9: 0xfd70 is reached after 27 saturated blocks from 80 %, and a constant
  byte of 254 (0xfe00) reaches it, 253 (0xfd00) never.
- The recovery (one voice per 32 blocks) starts under 93.0 % instead of 84.8 %: its routine 8d51c468 (0xd900) is
  shared with the pacing of stolen notes (8d51c778), so its constant stays; instead the recovery's own call at
  8d51e4fe goes to 8d51c438, which returns 0xee00 (93.0 %) minus the same overhead term (its only other caller is the
  two-tail test 8d51e5c8, which the mean's return never passes without USB audio). The pacing stays at 84.8 %.
The single-block kill branch (8d51e4f8), the 94.5 % branch (8d51e566), the thresholds 8d51c450 and 8d51c468, the
kill routine and the load byte's scale stay stock and read the new figure.

A build capability (profiles/extensions.json "build": true, each edit "build": true): chosen only when the
firmware is built (sdk.py build, or the builder, which writes its fixed bytes) from a collection that names it;
`sdk.py settings apply` refuses it, and `sdk.py settings show` reads from an image whether it was built with it.
"""
from .formats import require

SERVICE = 'load_governor'
HIGH_CUT = 0xfd70         # the budget-cut line: 99.0 % of the load byte's scale (stock 0xf200, 94.5 %)
MEAN_SHIFT = 3            # the mean's time constant: 1 << 3 = 8 blocks

CUT_LINE_SITE = 0x8d51c430  # 8d51c420 (the 94.5 % test's threshold, one caller 8d51e560): R1 = 0xf200 (Z)
BRANCH_SITE = 0x8d51c48c  # 8d51c480: CC = R0 < R1 (IU); IF !CC JUMP 8d51c4a8 (to the hold / decay / floor paths)
RISE_SITE = 0x8d51c492    # 8d51c480: LSHIFT R1 >>= 2 (the rise: a quarter of the gap)
RETURN_SITE = 0x8d51c49a  # 8d51c480: R0 = 0x100 (X); [P1 + 0xe4] = R0 (the hold reload, also its return value)
TEST_SITE = 0x8d51e4f0    # 8d51e4c4: CALL 8d51c450; CC = R0 <= R4 (IU); R6 = 0
RECOVERY_SITE = 0x8d51e4fe  # 8d51e4c4: CALL 8d51c468 (the recovery's threshold, 0xd900; shared with the pacing)
TWO_TAILS = 0x8d51c438    # returns 0xee00 minus the overhead term (callers: 8d51e5c8; on, also 8d51e4fe)
RECOVER = 0x8d51c468      # returns 0xd900 minus the overhead term (callers: 8d51e4fe, 8d51c778)
THRESHOLD = 0x8d51c450    # returns 0xe600 minus the overhead term

NOP = '0000'


def lshift_right(reg, amount):
    """Blackfin `LSHIFT Rreg >>= amount` (LOGI2op 0x4e00 | amount << 3 | reg), little-endian."""
    require(0 <= reg < 8 and 0 < amount < 32, 'Invalid shift')
    return (0x4e00 | amount << 3 | reg).to_bytes(2, 'little').hex()


def ashift_right(reg, amount):
    """Blackfin `ASHIFT Rreg >>>= amount` (LOGI2op 0x4d00 | amount << 3 | reg), little-endian: an arithmetic shift."""
    require(0 <= reg < 8 and 0 < amount < 32, 'Invalid shift')
    return (0x4d00 | amount << 3 | reg).to_bytes(2, 'little').hex()


def load_zero_extended(reg, value):
    """Blackfin `Rreg = value (Z)` (LDIMMhalf 0xe180 | reg, then the 16-bit value), little-endian."""
    require(0 <= reg < 8 and 0 <= value < 0x10000, 'Invalid immediate')
    return ((0xe180 | reg).to_bytes(2, 'little') + value.to_bytes(2, 'little')).hex()


def call(at, target):
    """Blackfin 32-bit `CALL target` placed at `at` (pcrel24 in halfwords)."""
    offset = (target - at) // 2
    require((target - at) % 2 == 0 and -(1 << 23) <= offset < (1 << 23), 'Call out of range')
    field = offset & 0xffffff
    return ((0xe300 | field >> 16).to_bytes(2, 'little') + (field & 0xffff).to_bytes(2, 'little')).hex()


# Stock's bytes at each edited site (the profile's `before`).
STOCK = {
    CUT_LINE_SITE: load_zero_extended(1, 0xf200),               # R1 = 0xf200 (Z)
    BRANCH_SITE: '8809' + '0d10',                               # CC = R0 < R1 (IU); IF !CC JUMP 8d51c4a8
    RISE_SITE: lshift_right(1, 2),
    RETURN_SITE: '20e10001' + '08e63900',                       # R0 = 0x100 (X); [P1 + 0xe4] = R0
    TEST_SITE: call(TEST_SITE, THRESHOLD) + '200a' + '0660',    # CALL; CC = R0 <= R4 (IU); R6 = 0
    RECOVERY_SITE: call(RECOVERY_SITE, RECOVER),                # CALL 8d51c468
}
# The test edit: R4 = R0 (what the rewritten 8d51c480 left in R0: 0x6680 + L / 2), then the same call and test,
# two bytes later. R6's zero goes: R6 is read only when the governor's third argument is non-zero, and its one
# caller passes 0.
MARK = '2030' + call(TEST_SITE + 2, THRESHOLD) + '200a'
# The smoother's three edits (8d51c480 as it then runs: P1 = R0; LINK 0; R2 = 8; R0 = [P1 + 0xe0];
# R2 = R1 >> 1; R1 = R1 - R0; R1 >>>= 3; R0 = R0 + R1; [P1 + 0xe0] = R0; R0 = 0x6680 (Z); R0 = R0 + R2; NOP;
# UNLINK; RTS): R2 = R1 >> 1 is dsp32shiftimm 0xc682 0x85f9; R0 = 0x6680 (Z) is LDIMMhalf 0xe180 0x6680;
# R0 = R0 + R2 is comp3op 0x5010.
MEAN_BYTES = {
    BRANCH_SITE: '82c6' + 'f985',                               # R2 = R1 >> 1 (L / 2, for the return)
    RISE_SITE: ashift_right(1, MEAN_SHIFT),                     # R1 >>>= 3: (L - S) / 8, signed
    RETURN_SITE: '80e18066' + '1050' + NOP,                     # R0 = 0x6680 (Z); R0 = R0 + R2; NOP
}
# Each site's bytes when on (the profile's `after`).
ON = {
    CUT_LINE_SITE: load_zero_extended(1, HIGH_CUT),             # R1 = 0xfd70 (Z): the cut at 99.0 %
    **MEAN_BYTES,
    TEST_SITE: MARK,
    RECOVERY_SITE: call(RECOVERY_SITE, TWO_TAILS),              # CALL 8d51c438: the recovery under 93.0 %
}


def check_edits(patches):
    """Exactly the six audited edits, in address order: the cut line, the smoother's branch, rise shift and return,
    the test edit that marks it on, and the recovery's threshold call; each fixed, with stock's bytes as `before`
    and the reviewed bytes as `after`."""
    require([int(p['address'], 16) for p in patches] == sorted(STOCK), 'Unexpected load-governor edit sites')
    for patch in patches:
        at = int(patch['address'], 16)
        require(patch['kind'] == 'limit' and patch.get('service') == SERVICE and patch.get('build') is True,
                'A load-governor edit belongs to its service and is chosen by sdk.py build')
        require(patch['before'] == STOCK[at] and patch['length'] * 2 == len(STOCK[at]),
                'Load-governor preimage differs from the audited stock bytes: %#x' % at)
        require('choices' not in patch and 'setting' not in patch and patch['after'] == ON[at],
                'Load-governor bytes differ from the audited encodings: %#x' % at)


def check_profile(profile, registry):
    require(profile['services'].get(SERVICE) == [SERVICE], 'Expected the load_governor service')
    require(registry.get(SERVICE) == dict(build=True), 'Load-governor registry entry differs')
    check_edits([p for p in profile['patches'] if p.get('service') == SERVICE])
