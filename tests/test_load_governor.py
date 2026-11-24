"""The load_governor capability: its audited edits, what they mean (what the smoother returns, and the smoother itself
run instruction by instruction from its bytes, off and on), the mean's arithmetic against a model, the cut and
recovery lines decoded from the image's bytes, what it does and does not cut, that it is off (stock bytes) unless a
settings extension declares it, that it has no setting, and that only sdk.py build chooses it."""
import copy
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

from tooling import browser_kit, extensions, load_governor as gov, native, settings as image_settings
import load_governor_model as model
from tooling.extensions import capabilities, build_capabilities, capability_settings, check_collection, extension_at
from tooling.firmware import active_services, check_edit, patch_active
from tooling.formats import header
from tooling.pipeline import ROOT, check_patch_boundaries, config, document

REGISTRY = document(ROOT / 'profiles/extensions.json')['capabilities']


def edits(profile):
    return [p for p in profile['patches'] if p.get('service') == gov.SERVICE]


def folder(test, **fields):
    root = Path(tempfile.mkdtemp())
    test.addCleanup(shutil.rmtree, root)
    data = {'id': 'governor', 'name': 'Load governor', 'description': 'Changes how stock sheds voices under load.',
            'claims': [], 'actions': [], 'capabilities': ['load_governor'], **fields}
    (root / 'extension.json').write_text(json.dumps(data), encoding='utf-8')
    return root


def stock_smooth(smoothed, hold, load):
    """8d51c480 as its listing reads, written apart from the module's model: a rise of a quarter of the gap and a
    256-block hold, else the hold counts down, else a fall of 8 to a floor of 0."""
    if smoothed < load:
        return smoothed + (load - smoothed) // 4, 256
    if hold != 0:
        return smoothed, hold - 1
    return (smoothed - 8 if smoothed > 8 else 0), 0


def stock_returns(smoothed, hold, load):
    """What 8d51c480 leaves in R0, as its listing reads: the rise path ends `R0 = 0x100; [P1+0xe4] = R0;
    RTS`; the hold path returns the smoothed load it loaded into R0; the decay path returns the fallen value; the
    floor path stores 0 and returns the old smoothed load."""
    if smoothed < load:
        return 256
    if hold != 0:
        return smoothed
    return smoothed - 8 if smoothed > 8 else smoothed


def mean_model(smoothed, load):
    """The rewritten smoother as specified, written apart from the module: the figure moves an eighth of the way to
    this block's load, up or down, rounded toward minus infinity; no hold, no fixed fall."""
    return smoothed + (load - smoothed) // 8


def blackfin_call_target(at, raw):
    """The destination of a 32-bit CALL (0xe3 page, pcrel24 halfwords), decoded independently of the module."""
    hi, lo = int.from_bytes(raw[0:2], 'little'), int.from_bytes(raw[2:4], 'little')
    assert hi >> 8 == 0xe3
    field = (hi & 0xff) << 16 | lo
    if field & 0x800000:
        field -= 1 << 24
    return at + 2 * field


# Stock 1.2.1's smoother 8d51c480..8d51c4d6 as loaded (86 bytes; the listing: P1 = R0; LINK 0; R2 = 8;
# R0 = [P1 + 0xe0]; CC = R0 < R1 (IU); IF !CC JUMP c4a8; R1 = R1 - R0; R1 >>= 2; R0 = R0 + R1; [P1 + 0xe0] = R0;
# R0 = 0x100 (X); [P1 + 0xe4] = R0; UNLINK; RTS; c4a8: R1 = [P1 + 0xe4]; CC = R1 == 0; IF CC JUMP c4bc; R1 += -1;
# [P1 + 0xe4] = R1; UNLINK; RTS; c4bc: CC = R2 < R0 (IU); IF !CC JUMP c4cc; R0 += -8; [P1 + 0xe0] = R0; UNLINK; RTS;
# c4cc: [P1 + 0xe0] = R1; UNLINK; RTS). The profile's `before` bytes at each smoother site must be slices of it.
SMOOTHER_AT = 0x8d51c480
STOCK_SMOOTHER = bytes.fromhex(
    '083200e80000426008e4380088090d104152114e085008e6380020e1000108e6390001e800001000'
    '09e43900010c0718f96709e6390001e80000100082090710c06708e6380001e80000100009e6380001e800001000')
POOL = 0x84c69838  # the voice manager's pool (any address: the routine reads and writes only pool+0xe0 / +0xe4)
WIDE = (0xe1, 0xe4, 0xe6, 0xe8, 0xc6)  # first bytes (high) of the 32-bit forms this routine uses


def site_bytes(at, on):
    """The bytes an image built with the capability on (or off: stock) holds at one of its sites."""
    patch = next(p for p in edits(config()) if int(p['address'], 16) == at)
    return bytes.fromhex(patch['after'] if on else patch['before'])


def smoother_bytes(on):
    """The routine's bytes as an image built with the capability on, or off (stock), holds them."""
    code = bytearray(STOCK_SMOOTHER)
    for patch in edits(config()):
        at = int(patch['address'], 16) - SMOOTHER_AT
        if 0 <= at < len(code) and on:
            code[at:at + patch['length']] = bytes.fromhex(patch['after'])
    return bytes(code)


def execute(code, smoothed, hold, load):
    """Run the routine's bytes on a tiny Blackfin model written apart from the module (only the forms this routine
    uses; anything else is an error): R0 = pool, R1 = L. Returns (R0, smoothed, hold) as the routine leaves them."""
    mask = 0xffffffff
    r = [0] * 8
    p = [0] * 6
    r[0], r[1] = POOL, load
    memory = {POOL + 0xe0: smoothed, POOL + 0xe4: hold}
    cc = None
    pc = 0
    for _ in range(64):
        word = int.from_bytes(code[pc:pc + 2], 'little')
        low, size = (int.from_bytes(code[pc + 2:pc + 4], 'little'), 4) if word >> 8 in WIDE else (None, 2)
        nxt = pc + size
        if word == 0x0000:                                    # NOP
            pass
        elif word == 0x0010:                                  # RTS
            return r[0], memory[POOL + 0xe0], memory[POOL + 0xe4]
        elif word >> 6 == 0b0011001000:                       # P(dst) = R(src): REGMV, gd = P, gs = R
            p[word >> 3 & 7] = r[word & 7]
        elif word >> 10 == 0b011000:                          # Rd = imm7 (X)
            imm = word >> 3 & 0x7f
            r[word & 7] = (imm - 0x80 if imm & 0x40 else imm) & mask
        elif word >> 10 == 0b011001:                          # Rd += imm7
            imm = word >> 3 & 0x7f
            r[word & 7] = (r[word & 7] + (imm - 0x80 if imm & 0x40 else imm)) & mask
        elif word >> 11 == 0b00001:                           # CC = compare (CCflag), D registers
            immediate, opc, x, y = word >> 10 & 1, word >> 7 & 7, r[word & 7], word >> 3 & 7
            assert not word >> 6 & 1 and opc in (0, 3, 4), hex(word)
            y = (y - 8 if y & 4 else y) & mask if immediate else r[y]
            cc = {0: x == y, 3: x < y, 4: x <= y}[opc]
        elif word >> 12 == 1:                                 # IF [!]CC JUMP pcrel10
            assert cc is not None
            offset = word & 0x3ff
            if bool(word >> 11 & 1) == cc:
                nxt = pc + 2 * (offset - 0x400 if offset & 0x200 else offset)
        elif word >> 12 == 0b0101 and word >> 9 & 7 in (0, 1):  # Rd = Rs0 + / - Rs1 (comp3op)
            dst, a, b = word >> 6 & 7, r[word & 7], r[word >> 3 & 7]
            r[dst] = (a + b if word >> 9 & 7 == 0 else a - b) & mask
        elif word >> 11 == 0b01001 and word >> 8 & 7 in (5, 6):  # R >>>= imm (5) / R >>= imm (6), LOGI2op
            amount, reg = word >> 3 & 0x1f, word & 7
            if word >> 8 & 7 == 5:
                signed = r[reg] - (1 << 32) if r[reg] >> 31 else r[reg]
                r[reg] = (signed >> amount) & mask
            else:
                r[reg] >>= amount
        elif word in (0xe800, 0xe801):                        # LINK / UNLINK: no data register or pool word
            pass
        elif word >> 8 in (0xe4, 0xe6):                       # [P + uimm16 * 4] load / store, 32-bit
            store, size_bits, ptr, reg = word >> 9 & 1, word >> 6 & 3, word >> 3 & 7, word & 7
            assert size_bits == 0
            address = p[ptr] + 4 * low
            if store:
                memory[address] = r[reg]
            else:
                r[reg] = memory[address]
        elif word >> 8 == 0xe1:                               # LDIMMhalf: Rd = imm16 (Z) / (X)
            z, h, sign, group, reg = word >> 7 & 1, word >> 6 & 1, word >> 5 & 1, word >> 3 & 3, word & 7
            assert group == 0 and not h and z != sign
            r[reg] = low if z else (low - 0x10000 if low & 0x8000 else low) & mask
        elif word == 0xc682:                                  # dsp32shiftimm Rd = Rs >>> imm (sop 0) / >> imm (sop 2)
            sop, hls, dst, immag, src = low >> 14, low >> 12 & 3, low >> 9 & 7, low >> 3 & 0x3f, low & 7
            assert hls == 0 and sop in (0, 2) and immag & 0x20, hex(low)
            amount = 0x40 - immag
            value = r[src] - (1 << 32) if sop == 0 and r[src] >> 31 else r[src]
            r[dst] = (value >> amount) & mask
        else:
            raise AssertionError('not modelled: %#06x at %#x' % (word, SMOOTHER_AT + pc))
        pc = nxt
    raise AssertionError('no RTS')


SITES = (0x8d51c430, 0x8d51c48c, 0x8d51c492, 0x8d51c49a, 0x8d51e4f0, 0x8d51e4fe)


class Profile(unittest.TestCase):
    def test_profile_holds_the_audited_edits(self):
        gov.check_profile(config(), REGISTRY)
        self.assertEqual(tuple(int(p['address'], 16) for p in edits(config())), SITES)
        self.assertEqual(SITES, (gov.CUT_LINE_SITE, gov.BRANCH_SITE, gov.RISE_SITE, gov.RETURN_SITE, gov.TEST_SITE,
                                 gov.RECOVERY_SITE))
        # Fixed edits only: no setting, no choices; every one changes stock's bytes.
        for patch in edits(config()):
            with self.subTest(site=patch['address']):
                self.assertNotIn('choices', patch)
                self.assertNotIn('setting', patch)
                self.assertNotEqual(patch['before'], patch['after'])

    def test_the_smoother_sites_are_stocks_routine(self):
        # Each smoother edit's `before` is a slice of stock's routine as loaded, and off holds it whole.
        for patch in edits(config()):
            at = int(patch['address'], 16) - SMOOTHER_AT
            if 0 <= at < len(STOCK_SMOOTHER):
                with self.subTest(site=patch['address']):
                    self.assertEqual(STOCK_SMOOTHER[at:at + patch['length']].hex(), patch['before'])
        self.assertEqual(smoother_bytes(False), STOCK_SMOOTHER)

    def test_the_encodings(self):
        # The test edit: R4 = R0 (MOVE 0x3020), then the threshold call moved two bytes on, still to 8d51c450,
        # then stock's CC = R0 <= R4 (IU); the branch that follows (8d51e4f8) is untouched and keeps its target.
        mark = bytes.fromhex(gov.MARK)
        self.assertEqual(mark[0:2], bytes.fromhex('2030'))
        self.assertEqual(blackfin_call_target(gov.TEST_SITE + 2, mark[2:6]), 0x8d51c450)
        self.assertEqual(mark[6:8], bytes.fromhex(gov.STOCK[gov.TEST_SITE])[4:6])
        self.assertEqual(blackfin_call_target(gov.TEST_SITE, bytes.fromhex(gov.STOCK[gov.TEST_SITE])[0:4]), 0x8d51c450)
        self.assertEqual(len(mark), 8)
        # The smoother: ASHIFT R1 >>>= 3 (LOGI2op opc 5) for stock's LSHIFT R1 >>= 2; R2 = R1 >> 1 (dsp32shiftimm);
        # R0 = 0x6680 (Z); R0 = R0 + R2; NOP.
        self.assertEqual((gov.lshift_right(1, 2), gov.ashift_right(1, 3)), ('114e', '194d'))
        self.assertEqual({hex(a): b for a, b in gov.ON.items()},
                         {'0x8d51c430': '81e170fd', '0x8d51c48c': '82c6f985', '0x8d51c492': '194d',
                          '0x8d51c49a': '80e1806610500000', '0x8d51e4f0': '2030ffe3afef200a',
                          '0x8d51e4fe': 'ffe39def'})
        self.assertEqual({hex(a): b for a, b in gov.STOCK.items()},
                         {'0x8d51c430': '81e100f2', '0x8d51c48c': '88090d10', '0x8d51c492': '114e',
                          '0x8d51c49a': '20e1000108e63900', '0x8d51e4f0': 'ffe3b0ef200a0660',
                          '0x8d51e4fe': 'ffe3b5ef'})
        # The recovery's call: stock to 8d51c468 (0xd900), on to 8d51c438 (0xee00).
        self.assertEqual(blackfin_call_target(gov.RECOVERY_SITE, bytes.fromhex(gov.STOCK[gov.RECOVERY_SITE])), 0x8d51c468)
        self.assertEqual(blackfin_call_target(gov.RECOVERY_SITE, bytes.fromhex(gov.ON[gov.RECOVERY_SITE])), 0x8d51c438)

    def test_rejects_other_bytes_and_sites(self):
        profile = config()
        for at, change in ((gov.RISE_SITE, dict(after='214e')),
                           (gov.RISE_SITE, dict(after='214d')),
                           (gov.BRANCH_SITE, dict(after='88090d10')),
                           (gov.RETURN_SITE, dict(after='80e1006610500000')),
                           (gov.CUT_LINE_SITE, dict(after='81e100ff')),
                           (gov.CUT_LINE_SITE, dict(after='81e100f2')),
                           (gov.TEST_SITE, dict(after='2030ffe3b0ef200a')),
                           (gov.RECOVERY_SITE, dict(after='ffe3b5ef')),
                           (gov.RECOVERY_SITE, dict(after='ffe3a9ef')),
                           (gov.RECOVERY_SITE, dict(before='ffe3b5ee')),
                           (gov.CUT_LINE_SITE, dict(address='0x8d51c434')),
                           (gov.TEST_SITE, dict(setting='load_governor')),
                           (gov.TEST_SITE, dict(choices={'0': gov.STOCK[gov.TEST_SITE], '1': gov.MARK})),
                           (gov.RETURN_SITE, dict(build=False)),
                           (gov.RETURN_SITE, dict(kind='hook'))):
            altered = copy.deepcopy(profile)
            index = next(i for i, p in enumerate(altered['patches'])
                         if p.get('service') == gov.SERVICE and int(p['address'], 16) == at)
            altered['patches'][index].update(change)
            with self.subTest(at=hex(at), change=change), self.assertRaises(ValueError):
                gov.check_profile(altered, REGISTRY)
        # One edit fewer, or one more (stock's single-block branch made an edit), is refused.
        for drop in (True, False):
            altered = copy.deepcopy(profile)
            if drop:
                altered['patches'] = [p for p in altered['patches']
                                      if not (p.get('service') == gov.SERVICE and int(p['address'], 16) == gov.RETURN_SITE)]
            else:
                extra = dict(edits(profile)[0], address=hex(model.SPIKE_SITE), before='6818', after='0000', length=2)
                altered['patches'].append(extra)
            with self.subTest(drop=drop), self.assertRaises(ValueError):
                gov.check_profile(altered, REGISTRY)
        # A setting in the registry is refused.
        altered = copy.deepcopy(REGISTRY)
        altered['load_governor'] = {'build': True, 'setting': {'label': 'Governor', 'unit': 'on', 'minimum': 0,
                                                               'maximum': 1, 'default': 0}}
        with self.assertRaises(ValueError):
            gov.check_profile(profile, altered)

    def test_an_inactive_build_leaves_the_sites_stock(self):
        profile = copy.deepcopy(config())
        for patch in edits(profile):
            patch['after'] = patch['before']
        check_patch_boundaries(profile, len(profile['patches']))


class Routine(unittest.TestCase):
    """The smoother's bytes, run instruction by instruction off and on, against the models."""

    LOADS = [0, 8, 9, 0x1000, 0x9a00, 0xb300, 0xcc00, 0xd900, 0xe600, 0xee00, 0xf200, 0xf600, 0xff00]

    def states(self):
        for smoothed in (0, 5, 8, 9, 0x9a00, 0xcc00, 0xe5ff, 0xe600, 0xf200, 0xff00):
            for hold in (0, 1, 256):
                for load in self.LOADS:
                    yield smoothed, hold, load

    def test_off_runs_stocks_routine(self):
        code = smoother_bytes(False)
        for smoothed, hold, load in self.states():
            with self.subTest(smoothed=smoothed, hold=hold, load=load):
                self.assertEqual(execute(code, smoothed, hold, load),
                                 (stock_returns(smoothed, hold, load), *stock_smooth(smoothed, hold, load)))
                self.assertEqual(model.smooth(smoothed, hold, load), stock_smooth(smoothed, hold, load))

    def test_on_is_a_short_mean_with_no_hold(self):
        # Up or down by the same fraction, the hold word untouched (never read, never written), and R0 = 0x6680 + L/2.
        code = smoother_bytes(True)
        for smoothed, hold, load in self.states():
            with self.subTest(smoothed=smoothed, hold=hold, load=load):
                self.assertEqual(execute(code, smoothed, hold, load),
                                 (0x6680 + load // 2, mean_model(smoothed, load), hold))
                self.assertEqual(model.mean(smoothed, load), mean_model(smoothed, load))
                self.assertEqual(model.mean_returned(load), 0x6680 + load // 2)

    def test_the_mean_falls_as_fast_as_it_rises(self):
        code = smoother_bytes(True)
        up = down = 0xcc00
        rise, fall = [], []
        for _ in range(40):
            up = execute(code, up, 0, 0xff00)[1]
            down = execute(code, down, 0, 0x9900)[1]
            rise.append(0xff00 - up)
            fall.append(down - 0x9900)
        # The gap shrinks by 7/8 a block either way; stock's fall is 8 a block after a 256-block hold.
        for k in (5, 10, 20):
            self.assertLess(abs(rise[k] - fall[k]), 16, k)
        self.assertLess(fall[5], 0x3300 * 0.55)
        self.assertLess(fall[39], 0x3300 // 100)  # under 1 % of the step after 40 blocks (58 ms)

    def test_the_single_block_test_needs_the_ceiling(self):
        # The test edit's R4 = R0 meets 8d51c450 (0xe600 minus USB audio's term T) and, on its way to 8d51e3fc,
        # 8d51c438 (0xee00 - T). Without USB audio: one tail only at L = 0xff00 (byte 255), never two.
        code = smoother_bytes(True)
        for byte in range(256):
            r0 = execute(code, 0xcc00, 0, byte << 8)[0]
            with self.subTest(byte=byte):
                self.assertEqual(r0 >= model.SINGLE_BLOCK, byte == 255)
                self.assertLess(r0, 0xee00)
                self.assertLess(r0, 1 << 31)  # never negative: the test is unsigned (IU)
        # With USB audio's term for eight routed streams (0x26c + 8 * 0xa3): from byte 240 (93.8 %); stock 223.
        term = model.overhead(8)
        self.assertEqual(term, 0x26c + 8 * 0xa3)
        first_byte = min(b for b in range(256) if execute(code, 0, 0, b << 8)[0] >= model.SINGLE_BLOCK - term)
        self.assertEqual(first_byte, 240)

    def test_the_figure_stays_in_stocks_range(self):
        code = smoother_bytes(True)
        smoothed = 0
        for byte in [255] * 300 + [255, 0] * 200 + [0] * 400:
            smoothed = execute(code, smoothed, 0, byte << 8)[1]
            self.assertTrue(0 <= smoothed <= 0xff00)
        self.assertEqual(smoothed, 0)


def cut_line(on):
    """The 16-bit value 8d51c420 loads (`Rd = imm16 (Z)`, LDIMMhalf), decoded apart from the module from the bytes an
    image built with the capability on (or off) holds."""
    raw = site_bytes(0x8d51c430, on)
    word, imm = int.from_bytes(raw[:2], 'little'), int.from_bytes(raw[2:], 'little')
    assert word >> 7 == 0xe180 >> 7 and word & 0x78 == 0 and word & 7 == 1, hex(word)  # R1 = imm16 (Z)
    return imm


# Stock's threshold routines as their listings read (8d51c420..c47e: each `R1 = imm (Z); R0 = R1 - R0` after loading
# the overhead term into R0): what each call target returns without USB audio.
THRESHOLDS = {0x8d51c420: 0xf200, 0x8d51c438: 0xee00, 0x8d51c450: 0xe600, 0x8d51c468: 0xd900}


def recovery_line(on):
    """The threshold the recovery test (8d51e502: CC = S < it) compares with, from the CALL an image built with the
    capability on (or off) holds at 8d51e4fe, decoded apart from the module."""
    return THRESHOLDS[blackfin_call_target(0x8d51e4fe, site_bytes(0x8d51e4fe, on))]


def governor(loads, on, smoothed=0xcc00, hold=0):
    """Per block (figure, single-block kill fires, budget cut fires, recovery acts) for byte loads without USB audio,
    the smoother run from the image's bytes; off: stock's test of the block's own load and stock's lines."""
    code, cut, recovery = smoother_bytes(on), cut_line(on), recovery_line(on)
    out = []
    for byte in loads:
        load = byte << 8
        r0, smoothed, hold = execute(code, smoothed, hold, load)
        tested = r0 if on else load
        out.append((smoothed, tested >= model.SINGLE_BLOCK, smoothed >= cut, smoothed < recovery))
    return out


def first(rows, column):
    return next((i for i, row in enumerate(rows) if row[column]), None)


class Lines(unittest.TestCase):
    """The cut at 99.0 % and the recovery under 93.0 % of the load byte's scale, read from the image's bytes."""

    def test_the_lines(self):
        self.assertEqual((cut_line(False), cut_line(True)), (0xf200, 0xfd70))
        self.assertEqual(round(cut_line(True) * 1000 / 65536), 990)
        self.assertEqual((recovery_line(False), recovery_line(True)), (0xd900, 0xee00))
        # No band where both the cut and the recovery act: from 93 % to 99 % neither moves the budget.
        self.assertLess(recovery_line(True), cut_line(True))
        # The pacing of stolen notes keeps its own call to 8d51c468, and no threshold routine's constant is edited.
        self.assertFalse({0x8d51c468, 0x8d51c478, 0x8d51c448, 0x8d51c460} & set(SITES))
        # In real block terms (byte 255 at 550,000 of 580,352 cycles): cut 93.8 %, recovery 88.1 %; stock 89.6 / 80.3.
        self.assertEqual([round(model.block_percent(v), 1) for v in (0xfd70, 0xee00, 0xf200, 0xd900, 0xff00)],
                         [93.8, 88.1, 89.6, 80.3, 94.4])

    def test_saturation_reaches_the_line_in_stated_time(self):
        # The load byte's ceiling is 255 (L = 0xff00); the figure settles at least 0xfef9 under it, above the line.
        rows = governor([255] * 200, True)
        self.assertGreaterEqual(min(r[0] for r in rows[150:]), 0xfef9)
        self.assertEqual(first(rows, 2) + 1, 27)                                 # from 80 %: the 27th saturated block
        self.assertLess(first(governor([255] * 200, True, smoothed=0), 2), 50)   # from silence
        self.assertIsNotNone(first(governor([254] * 10000, True), 2))            # a constant 99.2 % of the scale

    def test_what_it_does_not_cut(self):
        # A constant 98.8 % of the scale or less, and saturated blocks mixed with lighter ones, never reach the line.
        for pattern in ([253], [251], [248], [246], [255, 243], [255, 255, 255, 230], [255, 230]):
            with self.subTest(pattern=pattern):
                self.assertIsNone(first(governor(pattern * (10000 // len(pattern)), True), 2))
        # Stock cuts the 255 / 95 % alternation and the constant 96 % (its held peaks).
        for pattern in ([255, 243], [246]):
            self.assertIsNotNone(first(governor(pattern * 2000, False), 2), pattern)

    def test_isolated_and_repeated_saturated_blocks_do_not_cut(self):
        for burst in (1, 3):
            rows = governor([204] * 400 + [255] * burst + [204] * 600, True)[400:]
            with self.subTest(burst=burst):
                self.assertIsNone(first(rows, 2))
                self.assertLess(max(r[0] for r in rows), 0xe600)
        for every, base in ((8, 179), (4, 204), (2, 230)):
            rows = governor([base] * 400 + ([255] + [base] * (every - 1)) * (10000 // every), True)[400:]
            with self.subTest(every=every, base=base):
                self.assertIsNone(first(rows, 2))
        # Stock cuts the every-4th pattern (the peak-hold ratchet).
        self.assertIsNotNone(first(governor([204] * 400 + ([255] + [204] * 3) * 2500, False)[400:], 2))

    def test_one_tail_only_on_a_saturated_block(self):
        rows = governor([231, 235, 239, 242, 250, 254] * 500, True)
        self.assertFalse(any(r[1] for r in rows))
        rows = governor([204, 255] * 500, True)
        self.assertFalse(any(r[1] for r in rows[0::2]))
        self.assertTrue(all(r[1] for r in rows[1::2]))
        # Stock fires on every block from 89.8 % of the scale (byte 230).
        self.assertTrue(all(r[1] for r in governor([230] * 100, False)))

    def test_the_recovery(self):
        # After a cut (the figure at the ceiling), a steady 92 % of the scale (byte 236) falls under 93 % within
        # blocks and the voices come back; a steady 95 % (243) keeps the budget down. Stock's figure holds the peak
        # 256 blocks and falls 8 a block, so stock is under its own line only after more than 1,000 blocks.
        self.assertLess(first(governor([236] * 400, True, smoothed=0xfef9), 3), 30)
        self.assertIsNone(first(governor([243] * 3000, True, smoothed=0xfef9), 3))
        stock = governor([179] * 400 + [255] * 400 + [154] * 3000, False, smoothed=0)[800:]
        self.assertGreater(first(stock, 3), 1000)
        mine = governor([179] * 400 + [255] * 400 + [154] * 400, True, smoothed=0)[800:]
        self.assertLess(first(mine, 3), 8)


class Switch(unittest.TestCase):
    def test_registry(self):
        self.assertIn('load_governor', capabilities())
        self.assertIn('load_governor', build_capabilities())
        self.assertNotIn('load_governor', capability_settings())
        self.assertEqual(REGISTRY['load_governor'], {'build': True})

    def test_only_a_declaring_extension_turns_it_on(self):
        class Machines(list):
            extensions = (extension_at(folder(self)),)
        profile = config()
        services = active_services(profile, native.service_users(Machines()))
        self.assertEqual({patch_active(p, services) for p in edits(profile)}, {True})
        self.assertEqual(native.build_settings(Machines()), {})
        for patch in edits(profile):
            built = dict(patch, after=native.fixed_after(patch, services, True, {}) or patch['after'])
            self.assertEqual(built['after'], gov.ON[int(patch['address'], 16)])
            check_edit(built, patch, services, True, 0, 1)
        idle = active_services(profile, native.service_users([]))
        self.assertNotIn(gov.SERVICE, idle)
        self.assertEqual([native.fixed_after(p, idle, True, {}) for p in edits(profile)], [p['before'] for p in edits(profile)])

    def test_it_has_no_setting(self):
        # No value in an extension is accepted, whatever the number.
        for bad in ({'load_governor': 1}, {'load_governor': 4}, {'load_governor': 0}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                extension_at(folder(self, settings=bad))

    def test_two_extensions_may_both_name_it(self):
        # No setting, so two settings extensions naming it agree: it is simply on.
        a = extension_at(folder(self, id='one'))
        b = extension_at(folder(self, id='two'))
        check_collection([a, b])


def image(profile):
    """A synthetic loader holding stock's bytes at each load-governor site, and a profile pointing at them."""
    records, fake, offset = [header(0x5004, 0, 0, 0)], copy.deepcopy(profile), 16
    for patch in edits(fake):
        records.append(header(1, int(patch['address'], 16), patch['length'], 0) + bytes.fromhex(patch['before']))
        patch['file_offset'] = hex(offset + 16)
        offset += 16 + patch['length']
    records.append(header(0x8004, 0, 0, 0))
    data = b''.join(records)
    fake['stock_bytes'], fake['stock_records'] = len(data), len(records)
    return data, fake


def written(data, profile):
    """The image with the capability on, as sdk.py build writes it."""
    out = bytearray(data)
    for patch in edits(profile):
        at = int(patch['file_offset'], 16)
        out[at:at + patch['length']] = bytes.fromhex(patch['after'])
    return bytes(out)


class OnlyWhenBuilt(unittest.TestCase):
    """Chosen only when the firmware is built (sdk.py build or the builder) from a collection that names it."""

    def test_settings_apply_refuses_it(self):
        data, profile = image(config())
        e = extension_at(folder(self))
        for source in (data, written(data, profile)):
            with self.assertRaisesRegex(ValueError, 'Chosen when the firmware is built.*load_governor'):
                image_settings.apply(source, [e], {}, profile)
        with self.assertRaises(ValueError):
            image_settings.apply(data, [e], {'load_governor': 1}, profile)

    def test_a_browser_kit_carries_it_as_image_edits(self):
        # Fixed bytes and no code: the builder writes them when its extension is selected.
        e = extension_at(folder(self))
        self.assertNotIn('load_governor', extensions.adapter_capabilities())
        browser_kit.refuse_adapter_capabilities([e])

    def test_each_edit_is_marked_as_the_builds(self):
        # The mark says in the profile that the edit is a build capability's; the registry refuses the
        # capability with an edit unmarked, and a mark on an image capability's edit.
        self.assertEqual({p.get('build') for p in edits(config())}, {True})
        real = extensions.document
        for change in ('unmarked', 'marked image capability'):
            def fake(path, change=change):
                data = real(path)
                if Path(path).name == Path(extensions.FIRMWARE_PROFILE).name:
                    data = copy.deepcopy(data)
                    for p in data['patches']:
                        if change == 'unmarked' and p.get('service') == gov.SERVICE:
                            p.pop('build')
                            break
                        if change == 'marked image capability' and p.get('service') == 'release_floor':
                            p['build'] = True
                return data
            with self.subTest(change=change), mock.patch.object(extensions, 'document', fake), \
                    self.assertRaises(ValueError):
                extensions.capabilities()

    def test_the_registry_refuses_a_governor_with_a_setting(self):
        # A switch: neither the registry entry nor one of its edits may carry a setting or choices again.
        spec = dict(label='Governor', unit='0 off, 1 on', minimum=0, maximum=1, default=0)
        real = extensions.document
        for change in ('registry setting', 'edit setting', 'edit choices'):
            def fake(path, change=change):
                data = copy.deepcopy(real(path))
                if change == 'registry setting' and Path(path).name == 'extensions.json':
                    data['capabilities'][gov.SERVICE] = {'build': True, 'setting': spec}
                if change != 'registry setting' and Path(path).name == Path(extensions.FIRMWARE_PROFILE).name:
                    patch = next(p for p in data['patches'] if p.get('service') == gov.SERVICE)
                    if change == 'edit setting':
                        patch['setting'] = gov.SERVICE
                    else:
                        patch['choices'] = {'1': patch['after']}
                return data
            with self.subTest(change=change), mock.patch.object(extensions, 'document', fake), \
                    self.assertRaisesRegex(ValueError, 'Invalid build capability: load_governor'):
                extensions.capabilities()

    def test_settings_show_reads_on_or_off(self):
        # This image holds only the load-governor sites: the other build capabilities are left out of built().
        data, profile = image(config())
        only = mock.patch.object(image_settings, 'build_capabilities', lambda: ('load_governor',))
        only.start()
        self.addCleanup(only.stop)
        self.assertEqual(image_settings.built(data, profile), {'load_governor': False})
        self.assertEqual(image_settings.built(written(data, profile), profile), {'load_governor': True})
        # Part of it on, or other bytes at a site, is refused.
        for change in ('partly', 'other'):
            mixed = bytearray(written(data, profile))
            rise = next(p for p in edits(profile) if int(p['address'], 16) == gov.RISE_SITE)
            at = int(rise['file_offset'], 16)
            mixed[at:at + 2] = bytes.fromhex(rise['before'] if change == 'partly' else '294e')
            with self.subTest(change=change), self.assertRaises(ValueError):
                image_settings.built(bytes(mixed), profile)


if __name__ == '__main__':
    unittest.main()
