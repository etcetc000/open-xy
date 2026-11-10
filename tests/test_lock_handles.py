"""Step-lock handles: the pool, copy-on-write, garbage collection, open-xy.locks round trips
and the limit, loads with a missing or mismatched companion, handles following stock's step
operations and the step hook (tests/fixtures/lock_handles.cpp), the build wiring, and the
stock sites the design relies on, read from the profile's stock bytes."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from tooling import native
from tooling.firmware import active_services, decode_branch
from tooling.formats import ldr, mapped_offset, sha
from tooling.startup import render
from test_sample_services import HOST_CXX
import test_drum_locks

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
PROFILE = json.loads((ROOT / 'profiles/opxy-1.2.1.json').read_text(encoding='utf-8'))
# The step hook: the sequencer's call of its lock pass for a step's note.
STEP_HOOK = ('0x8d5939aa', 'sdk_lock_step', 'ffe38bfe', 0x8d5936c0)


def run(command, **kwargs):
    hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
    return subprocess.run(command, capture_output=True, text=True, **kwargs, **hidden)


def host_stock(folder):
    """stock.hpp whose addresses reach the fixture's host doubles."""
    stock = (PLATFORM / 'stock.hpp').read_text(encoding='utf-8')
    stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                          'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
    for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
        stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                              'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
    (folder / 'stock.hpp').write_text(stock, encoding='utf-8')


class LockHandles(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_handles_follow_stock_and_round_trip_through_the_companion(self):
        with tempfile.TemporaryDirectory(prefix='lock-handles-') as folder:
            folder = Path(folder)
            host_stock(folder)
            for name in ('project_companion.cpp', 'lock_handles.cpp'):
                (folder / name).write_bytes((PLATFORM / name).read_bytes())
            binary = folder / ('lock_handles.exe' if os.name == 'nt' else 'lock_handles')
            built = run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(folder),
                         '-I' + str(ROOT / 'sdk/include'), '-I' + str(PLATFORM), '-I' + str(ROOT / 'tests/fixtures'),
                         str(ROOT / 'tests/fixtures/lock_handles.cpp'), str(folder / 'lock_handles.cpp'),
                         '-o', str(binary)], timeout=180)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = run([str(binary), str(folder / 'song.xy')], timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('lock handles ok', result.stdout)

    def test_the_step_hook_and_the_handles_link_with_padmix_kits_only(self):
        address, symbol, before, target = STEP_HOOK
        patch = next(p for p in PROFILE['patches'] if p.get('symbol') == symbol)
        self.assertEqual((patch['kind'], patch['address'], patch['before'], patch['call'], patch['length'],
                          patch['service']), ('hook', address, before, True, 4, 'drum_locks'))
        self.assertEqual(decode_branch(int(address, 16), bytes.fromhex(before)), (target, True))
        source = (PLATFORM / 'lock_handles.cpp').read_text(encoding='utf-8')
        self.assertIn('(0x%08x)' % target, source)
        self.assertIn('__asm__("%s")' % symbol, (PLATFORM / 'lock_handles.hpp').read_text(encoding='utf-8'))
        self.assertEqual(native.UNIT_SERVICES['lock_handles'], 'drum_locks')
        with_mix, plain = test_drum_locks.kit(), test_drum_locks.kit(0)
        self.assertIn('drum_locks', active_services(PROFILE, native.service_users([with_mix])))
        self.assertNotIn('drum_locks', active_services(PROFILE, native.service_users([plain])))
        self.assertIn('lock_handles', native.SDK_UNITS)

    def test_startup_reaches_the_handles_only_when_linked(self):
        linked, plain = render(['probe'], locks=True), render(['probe'])
        for name in ('size', 'write', 'begin', 'byte', 'loaded'):
            self.assertIn(f'sdk_step_locks_{name}(', linked)
            self.assertNotIn(f'sdk_step_locks_{name}', plain)
            self.assertIn(f'sdk_project_locks_{name}(', plain)
        self.assertIn('u32 sdk_project_locks_size(void*, u32) { return 0; }', plain)
        self.assertIn('void sdk_project_locks_loaded(void*, u32) {}', plain)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_stock_sites_hold_what_the_handles_rely_on(self):
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(stock), PROFILE['stock_sha256'])
        image = ldr(stock)

        def at(address, length):
            offset = mapped_offset(image, address, length)
            return stock[offset:offset + length]

        def call(address):
            return decode_branch(address, at(address, 4))

        address, symbol, before, target = STEP_HOOK
        patch = next(p for p in PROFILE['patches'] if p.get('symbol') == symbol)
        self.assertEqual(mapped_offset(image, int(address, 16), 4), int(patch['file_offset'], 16))
        self.assertEqual(at(int(address, 16), 4).hex(), before)
        # 8d593980, for each step note: base into live (8d5accf8), the lock pass with the
        # track (R1 = R7) and position (R2 = [P5]), then note start (8d595738), which
        # starts a kit's pad payload at 8d595b50, already hooked.
        self.assertEqual(call(0x8d59399c), (0x8d5accf8, True))
        self.assertEqual(at(0x8d5939a0, 10).hex(), '82ce06c02a9100000f30')
        self.assertEqual(call(0x8d593a22), (0x8d595738, True))
        self.assertEqual(call(0x8d595b50), (0x8d5ad660, True))
        # The pass: the current pattern (8d586014), its step (pos + 240) / 480 by the
        # multiply at 8d59370a, union-gated, the 42-word loop (R6 = 0x2a) on rows of 0x54.
        self.assertEqual(call(0x8d5936e0), (0x8d586014, True))
        self.assertEqual(at(0x8d5936ee, 12).hex(), '23e1f00006da888889880000')
        self.assertEqual(at(0x8d5936fa, 8).hex(), '20e4160c22e4170c')   # the union words
        self.assertEqual(at(0x8d59376e, 2).hex(), '5661')               # R6 = 0x2a
        self.assertEqual(at(0x8d593748, 4).hex(), 'a0e46030')           # the slide byte +0x3060
        # The lock writer 8d586f40(track, step, index, value): the current pattern's
        # 0x258 + step * 0x54 + index * 2 (W[P2] = R6.L, R6 the fourth argument), mask
        # +0x2c58 + step * 8 and union +0x3058 OR the index's bit.
        self.assertEqual(at(0x8d586f68, 8).hex(), '04cc1c06f6a30000')  # R6 = [SP + 0x3c]
        self.assertEqual(at(0x8d586fc0, 4).hex(), '29e1582c')
        self.assertEqual(at(0x8d586fd0, 4).hex(), '2ae15802')
        self.assertEqual(at(0x8d586fda, 2).hex(), '928b')               # W[P2] = R6.L
        self.assertEqual(at(0x8d586ff2, 4).hex(), '20e6160c')           # [P4 + 0x3058] = R0
        # 8d5843ec(pattern): the 128 masks ORed into the union.
        self.assertEqual(at(0x8d5843ec, 8).hex(), '083234e1582c00e8')
        self.assertEqual(at(0x8d58440a, 8).hex(), '08e6160c09e6170c')
        # The held step: 8d5b7e4c + 16 * 8d585fb4 of the current pattern (8d5ad42c).
        self.assertEqual(call(0x8d5ad438), (0x8d5b7e4c, True))
        self.assertEqual(call(0x8d5ad440), (0x8d585fb4, True))
        self.assertEqual(at(0x8d5ad444, 4).hex(), '82c62086')           # R3 = R0 << 4
        self.assertEqual(call(0x8d5ad44c), (0x8d586014, True))
        # The editor's redraw after its write: 8d54b438's object, 8d54b3b6 sets its +0x6c.
        self.assertEqual(call(0x8d5ad4ce), (0x8d586f40, True))
        self.assertEqual(call(0x8d5ad4d2), (0x8d54b438, True))
        self.assertEqual(call(0x8d5ad4d6), (0x8d54b3b6, True))
        # Copy step (8d5824e0): P5 = 0x84ffca12, its +0x786 the step slot; values from
        # its +0x10 (0x84ffd1ac), the mask's words at 0x84ffd200 and 0x84ffd204.
        self.assertEqual(at(0x8d5824ea, 6).hex(), '0ddaff8412ca')
        self.assertEqual(at(0x8d582522, 4).hex(), '29e18607')
        self.assertEqual(at(0x8d582600, 4).hex(), '02e61600')           # [P0 + 0x58] = R2: high
        self.assertEqual(at(0x8d582608, 4).hex(), '23e61900')           # [P4 + 0x64] = R3: low
        self.assertEqual(at(0x8d582620, 4).hex(), '34e11000')           # M0 = 0x10: the values
        # Paste step (8d582218) reads the same slot: P5 = 0x84ffd200.
        self.assertEqual(at(0x8d582220, 6).hex(), '0ddaff8400d2')
        # Copy pattern (8d581a2c): memcpy(0x84ffd208, pattern, 0x53f4).
        self.assertEqual(at(0x8d581a52, 18).hex(), '22e1f45300daff8408d2000000de0204c401')
        # Nudge (8d5930e8) moves rows whole: memcpy of 0x54.
        self.assertEqual(at(0x8d5932cc, 10).hex(), '22e1540000de0204c401')
        # The snapshot: the recorder 0x860c6adc, its +0x461c (8d593d7a), rows +0x788
        # and masks +0x3188 (8d5adcf8, 8d5ae000), restored by 8d5adde4.
        self.assertEqual(at(0x8c083ef4, 6).hex(), '00da0c86dc6a')
        self.assertEqual(at(0x8d593d7a, 4).hex(), '27e11c46')
        self.assertEqual(call(0x8d593d8a), (0x8d5adaf4, True))
        self.assertEqual(at(0x8d5adcf8, 4).hex(), '28e18807')
        self.assertEqual(at(0x8d5ae000, 4).hex(), '2be18831')

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_no_stock_writer_or_absolute_address_names_words_36_or_37(self):
        # The device-check gate's traced half: word 37 (and 36) silent on every engine.
        # Every call of stock's five word setters and readers by index (8d518d78 and
        # 8d518d94 on the audio side; 8d5acf28, 8d5acea8, 8d5ad5bc on the GUI side) is
        # found, relative CALL or CALL.A, and none loads 36 or 37 into R1 or R2 in the
        # twelve halfwords before it, though such loads of 38..41 and smaller indices are
        # there to find. And no imm32 names words 36 or 37 of track 0's audio record
        # (0x84eab39c), its GUI live words (0x86bd42fc) or a pattern's stored words
        # (+0x3864). Engines that read the record by a register index are not covered;
        # on hardware, patterns with these locks copied onto a stock synth track and a
        # stock Drumline track played as the same patterns without them.
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(stock), PROFILE['stock_sha256'])
        targets = {0x8d518d78, 0x8d518d94, 0x8d5acf28, 0x8d5acea8, 0x8d5ad5bc}
        constants = {0x84eab39c + 0x90, 0x84eab39c + 0x94, 0x86bd42fc + 0x90, 0x86bd42fc + 0x94,
                     0x3864 + 0x90, 0x3864 + 0x94}
        calls, indices, named = 0, set(), []
        for block in ldr(stock):
            if block['fill'] or not block['stored']:
                continue
            body = stock[block['offset'] + 16:block['offset'] + 16 + block['stored']]
            halves = struct.unpack('<%dH' % (len(body) // 2), body[:len(body) // 2 * 2])
            for i, half in enumerate(halves):
                address = block['target'] + 2 * i
                if half >> 8 == 0xda and half & 0xff <= 13 and i + 2 < len(halves):
                    if (halves[i + 1] << 16 | halves[i + 2]) in constants:
                        named.append(hex(address))
                target = None
                if half == 0xde00 and i + 3 < len(halves) and halves[i + 3] == 0:
                    target = halves[i + 1] << 16 | halves[i + 2]
                elif half >> 8 == 0xe3 and i + 1 < len(halves):
                    target, _call = decode_branch(address, body[2 * i:2 * i + 4])
                if target not in targets:
                    continue
                calls += 1
                for before in halves[max(0, i - 12):i]:
                    if before & 0xfc07 in (0x6001, 0x6002):  # R1 or R2 = imm7
                        indices.add(before >> 3 & 0x7f)
        self.assertEqual(calls, 99)
        self.assertTrue({38, 39, 40, 41} <= indices, indices)
        self.assertFalse({36, 37} & indices)
        self.assertEqual(named, [])

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_the_audio_sides_loads_at_words_36_and_37_are_of_other_records(self):
        # Every 32-bit load at +0x90 or +0x94 (words 36, 37) from a pointer register in
        # the audio code (8d4e0000..8d520000: engines, the mixer, voices and renderers)
        # and stock's L1 code (11a00000..11a0c000), found by encoding: `[Preg + imm16 * 4]`
        # is 0xe4/0xe5 then the word index. None reads a track record (58 words, 0xe8
        # bytes): each base is another record, its own state written by the same code.
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(stock), PROFILE['stock_sha256'])
        found = set()
        for block in ldr(stock):
            if block['fill'] or not block['stored']:
                continue
            body = stock[block['offset'] + 16:block['offset'] + 16 + block['stored']]
            halves = struct.unpack('<%dH' % (len(body) // 2), body[:len(body) // 2 * 2])
            for i in range(len(halves) - 1):
                address = block['target'] + 2 * i
                if not (0x8d4e0000 <= address < 0x8d520000 or 0x11a00000 <= address < 0x11a0c000):
                    continue
                first, index = halves[i], halves[i + 1]
                if first & 0xffc0 in (0xe400, 0xe500) and index in (36, 37) and (first >> 3) & 7 < 6:  # not SP, FP
                    found.add(address)
        voice_machine = {  # a voice's machine object (+0x94), called through its method table next
            0x8d51a93e, 0x8d51a990, 0x8d51aeec, 0x8d51af20, 0x8d51b00a, 0x8d51b182, 0x8d51b53e, 0x8d51b66c,
            0x8d51b68e, 0x8d51b8a8, 0x11a0964e, 0x11a0977c, 0x11a099ae}
        voice_other = {0x8d51b1fc, 0x8d51c78c}   # a voice's +0x90, in the voice loop (8d51c708)
        own_state = {  # an object's own fields, which the same function writes
            0x8d4f4a0c, 0x8d4f5fb8, 0x8d4f6064, 0x8d4ffb8c, 0x8d4ffbaa, 0x8d5165c8, 0x8d516732, 0x8d5167a8,
            0x8d5020f4,   # 8d501cc8's this: +0x124 is past a track record
            0x8d4fd62c}   # a count at +0x90 beside its array at +0x8c
        self.assertEqual(found, voice_machine | voice_other | own_state)
        # A voice's machine object is a voice record's +0x94, as the drum renderer reads it
        # (8d51b53e, docs/abi-1.2.1.md per-key sends), and stock reads a track record's
        # words from the record itself: word 38 at 8d51af24 ([P5 + 0x98]).
        def at(address, length):
            offset = mapped_offset(ldr(stock), address, length)
            return stock[offset:offset + length].hex()
        self.assertEqual(at(0x8d51b53e, 4), '21e52500')
        self.assertEqual(at(0x8d51af24, 4), '29e42600')


if __name__ == '__main__':
    unittest.main()
