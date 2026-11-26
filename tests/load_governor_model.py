"""Host models of stock's load governor and of the load_governor capability's mean, for tests/test_load_governor.py
(the arithmetic each routine does, written apart from the bytes tooling/load_governor.py audits)."""
from tooling.load_governor import MEAN_SHIFT

BLOCK = 580352            # cycles in one audio block period (the simulator's and the device's)
STOCK_SCALE = 550000      # stock's limit and divisor: byte 255 at 94.8 % of a block period
TERM_BASE, TERM_STREAM = 0x26c, 0xa3  # stock's USB audio overhead term: TERM_BASE + TERM_STREAM per routed stream
SPIKE_SITE = 0x8d51e4f8   # 8d51e4c4: IF CC JUMP 8d51e5c8 (to the single-block kill 8d51e3fc), not edited
SINGLE_BLOCK = 0xe600     # 8d51c450's threshold (without USB audio's overhead term)
RETURN_BASE = 0x6680      # on, the smoother returns RETURN_BASE + L / 2: 0xe600 exactly at L = 0xff00, the byte's ceiling


def smooth(smoothed, hold, load):
    """Stock's 8d51c480 for one block, as the host model of the routine: (smoothed, hold) after a block whose L is
    `load`: a rise of a quarter of the gap and a 256-block hold, else the hold counts down, else a fall of 8."""
    if smoothed < load:
        return smoothed + ((load - smoothed) >> 2), 0x100
    if hold:
        return smoothed, hold - 1
    return (smoothed - 8, 0) if smoothed > 8 else (0, 0)


def mean(smoothed, load, shift=MEAN_SHIFT):
    """The rewritten 8d51c480 for one block: the figure after a block whose L is `load` (the hold word is left
    alone). An arithmetic shift of the signed gap: floor((L - S) / 2**shift), so it settles at L from above and up to
    2**shift - 1 under L from below."""
    return smoothed + ((load - smoothed) >> shift)


def mean_returned(load):
    """What the rewritten 8d51c480 leaves in R0 (the test edit's R4): 0x6680 + L / 2, at most 0xe600."""
    return RETURN_BASE + (load >> 1)


def overhead(streams=None):
    """USB audio's overhead term [0803ba84] as 8d51e4c4 makes it each block: 0 without USB audio (None), else the
    base plus one step per routed stream."""
    return 0 if streams is None else TERM_BASE + TERM_STREAM * streams


def block_percent(value):
    """A value on the governor's scale (L, of 65,536) as percent of a real block period (byte 255 at 550,000
    cycles)."""
    return value * STOCK_SCALE * 100 / 65536 / BLOCK
