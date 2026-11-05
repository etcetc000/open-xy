"""Per-hit locks of a padMix kit's Shift fields: the note-start hooks, the stock sites they
and the neutral-base fixes (words 36..38) rely on, and the build wiring. Routing, per-key
writes to the step-lock handles, the Shift row's display and the neutral base run in the
drum GUI fixture (test_drum_services); the handles themselves in test_lock_handles."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from tooling.adapters import default_preset, generate_synth_controls
from tooling.definitions import hidden_pages, validate_definition
from tooling.firmware import active_services, decode_branch
from tooling.formats import ldr, mapped_offset, sha
from tooling import native
from machine_fixtures import fixture
import test_drum_preview
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
PROFILE = json.loads((ROOT / 'profiles/opxy-1.2.1.json').read_text(encoding='utf-8'))
# The two reviewed sites: note start's call of the pad payload builder, and the
# builder's call of its publisher, each a relative CALL the hook replaces.
SITES = {'0x8d595b50': ('sdk_drum_lock_note', '00e388bd', 0x8d5ad660),
         '0x8d5ad776': ('sdk_drum_lock_publish', 'ffe30ffd', 0x8d5ad194)}


def kit(pad_mix=1, **resources):
    definition = test_drum_preview.DrumPreviewTests.definition(None, 'drum')
    definition['resources'].update(padMix=pad_mix, **resources)
    return SimpleNamespace(kind='drum', id='probe', prefix='probe', native_name='probe', definition=definition,
                           sdk='1.10', preset_defaults={}, engine=None)


def run(command, **kwargs):
    hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
    return subprocess.run(command, capture_output=True, text=True, **kwargs, **hidden)


def words(before):
    data = bytes.fromhex(before)
    return [int.from_bytes(data[i:i + 2], 'little') for i in range(0, len(data), 2)]


class DrumLocks(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_note_start_puts_the_steps_offsets_into_its_pad_payload(self):
        with tempfile.TemporaryDirectory(prefix='drum-locks-') as folder:
            folder = Path(folder)
            # Stock's addresses reach host doubles, as test_drum_services maps them.
            stock = (PLATFORM / 'stock.hpp').read_text()
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock)
            for header in ('drum_values.hpp', 'drum_lock_store.hpp'):
                (folder / header).write_text((PLATFORM / header).read_text())
            sources = []
            for name in ('drum_locks.cpp', 'drum_machines.cpp'):
                (folder / name).write_text((PLATFORM / name).read_text())
                sources.append(str(folder / name))
            binary = folder / ('drum_locks.exe' if os.name == 'nt' else 'drum_locks')
            built = run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(folder),
                         '-I' + str(ROOT / 'sdk/include'), '-I' + str(PLATFORM),
                         str(ROOT / 'tests/fixtures/drum_locks.cpp'), *sources, '-o', str(binary)], timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = run([str(binary)], timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_the_profile_hooks_the_builder_and_its_publisher_for_padmix_kits_only(self):
        self.assertEqual(PROFILE['services']['drum_locks'], ['padMix'])
        # The service's third hook, the step hook, is the handles' (test_lock_handles).
        hooks = {p['address']: p for p in PROFILE['patches']
                 if p.get('service') == 'drum_locks' and p['symbol'] != 'sdk_lock_step'}
        self.assertEqual(set(hooks), set(SITES))
        source = (PLATFORM / 'drum_locks.cpp').read_text(encoding='utf-8')
        for address, (symbol, before, target) in SITES.items():
            patch = hooks[address]
            self.assertEqual((patch['kind'], patch['symbol'], patch['before'], patch['call'], patch['length']),
                             ('hook', symbol, before, True, 4))
            # Each hook is a C function in place of the CALL: it makes the call itself.
            self.assertEqual(decode_branch(int(address, 16), bytes.fromhex(before)), (target, True))
            self.assertIn('(0x%08x)' % target, source)
            self.assertIn('__asm__("%s")' % symbol, source)
        self.assertEqual(native.UNIT_SERVICES['drum_locks'], 'drum_locks')
        with_mix, plain = kit(), kit(0)
        self.assertIn('padMix', native.service_users([with_mix]))
        self.assertIn('drum_locks', active_services(PROFILE, native.service_users([with_mix])))
        self.assertNotIn('drum_locks', active_services(PROFILE, native.service_users([plain])))

    def test_a_padmix_kit_shows_m3_and_may_hide_pages_itself(self):
        # M3 is every kit's track filter (drum_filter.hpp): no kit hides it unless it says so.
        self.assertEqual(hidden_pages(kit().definition), 0)
        self.assertEqual(hidden_pages(kit(0).definition), 0)
        self.assertEqual(hidden_pages(dict(kit().definition, hidePages=['envelope'])), 2)
        self.assertNotIn('hidePages', native.service_users([kit()]))
        self.assertNotIn('control', active_services(PROFILE, native.service_users([kit()])))
        drum = kit(0).definition
        validate_definition(dict(drum, hidePages=['filter']), 'drum')
        self.assertEqual(hidden_pages(dict(drum, hidePages=['filter'])), 4)
        with self.assertRaises(ValueError):
            validate_definition(dict(drum, hidePages=['lfo']), 'drum')
        machine = kit()
        with tempfile.TemporaryDirectory() as folder:
            table = generate_synth_controls(folder, [machine]).read_text()
        self.assertRegex(table, r'probe_format, false, true, 0[,}]')

    def test_a_padmix_kits_init_preset_has_stocks_filter(self):
        # M3 off (fx.active false) and stock's filter values; the lock words have no preset key.
        synth = fixture('probesynth')
        entry = lambda m: json.dumps({'type': m.id, 'default_params': [0] * 8})
        with patch('tooling.adapters.catalog_entry', entry):
            mixed, plain = default_preset(kit()), default_preset(kit(0))
        self.assertEqual(mixed, plain)
        self.assertEqual(mixed['fx'], default_preset(synth)['fx'])
        self.assertIs(mixed['fx']['active'], False)

    def test_the_load_hook_replays_what_it_replaces(self):
        # 8d588ec8: R0 = [P1 + 0x18]; CC = R0 == R1 (88a1 0808), with the alias between.
        patch = next(p for p in PROFILE['patches'] if p.get('symbol') == 'extensions_drum_load_parameters')
        self.assertEqual((patch['address'], patch['before']), ('0x8d588ec8', '88a10808'))
        stubs = (PLATFORM / 'drum_hooks.s').read_text(encoding='utf-8')
        body = stubs[stubs.index('extensions_drum_load_parameters:\n'):]
        body = body[:body.index('.size')]
        self.assertTrue(body.startswith('extensions_drum_load_parameters:\n    R0 = [P1 + 0x18];\n    HOOK_SAVE\n'))
        self.assertIn('    R1 = P1;\n    CALL sdk_drum_load_engine;\n    HOOK_RESULT R0\n    HOOK_RESTORE\n'
                      '    CC = R0 == R1;\n    RTS;\n', body)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_stock_sites_hold_what_the_locks_rely_on(self):
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        self.assertEqual(sha(stock), PROFILE['stock_sha256'])
        image = ldr(stock)

        def at(address, length):
            offset = mapped_offset(image, address, length)
            return stock[offset:offset + length]

        # The hooked calls reach the builder and its publisher.
        for address, (symbol, before, target) in SITES.items():
            patch = next(p for p in PROFILE['patches'] if p.get('symbol') == symbol)
            self.assertEqual(mapped_offset(image, int(address, 16), 4), int(patch['file_offset'], 16))
            self.assertEqual(at(int(address, 16), 4).hex(), before)
            self.assertEqual(decode_branch(int(address, 16), at(int(address, 16), 4)), (target, True))

        # Blackfin+ `reg = imm32` is 0xdaNN (NN: 0..7 R0..R7, 8..13 P0..P5), high half, low half.
        def load(address):
            w = words(at(address, 8).hex())
            self.assertEqual(w[0] & 0xff00, 0xda00, hex(address))
            return w[0] & 0xff, w[1] << 16 | w[2]

        # The key a turn edits (stock.hpp drum_edit_key), which the Shift row shows and
        # locks: the controller's held key 0x8400dfc8 (read at 8c0b1344 and 8c0b1448,
        # latched from the last played key 8d4d6cb0 by the first turn, 8c0b167a..8c0b168a;
        # Shift and a pad set it, 8c0b29ce), handed to the per-key editor; not the track's
        # selected key 8d4d6c98, another byte.
        self.assertEqual(load(0x8c0b1344), (9, 0x8400dfc8))
        self.assertEqual(at(0x8c0b1448, 8).hex(), '03c800180891eca0')
        self.assertEqual(at(0x8c0b167a, 6).hex(), '00de4d8db06c')
        self.assertEqual(load(0x8c0b1682), (9, 0x8400dfc8))
        self.assertEqual(at(0x8c0b168a, 2).hex(), '08b0')
        self.assertEqual(at(0x8c0b29ce, 6).hex(), '00d80084c8df')
        self.assertEqual(load(0x8d4d6cb0), (1, 0x080379f0))
        self.assertEqual(load(0x8d4d6c98), (1, 0x08037a00))
        self.assertIn('0x8400dfc8', (PLATFORM / 'stock.hpp').read_text(encoding='utf-8'))
        # The turn reaches the last played key twice when none is held: to latch it
        # (8c0b1678..8c0b168a) and for the per-key editor (8c0b14b2..8c0b14bc, after the
        # check at 8c0b1450). Both calls are hooked for owned kits, so the SDK never
        # writes the held key: the latch keeps -1, the editor gets the selected key.
        call = '00de4d8db06c0000'   # CALL.A 0x8d4d6cb0
        self.assertEqual(at(0x8c0b1450, 4).hex(), '380c3018')   # CC = R0 == -1; IF CC JUMP 8c0b14b2
        for site in (0x8c0b14b2, 0x8c0b1678):
            self.assertEqual(at(site, 2).hex(), '1899')             # R0 = B[P3] (Z), the selected track
            self.assertEqual(at(site + 2, 8).hex(), call)
        self.assertEqual(at(0x8c0b14bc, 2).hex(), 'cc2f')           # back to 8c0b1454 with the key
        hooks = {p['address']: p for p in PROFILE['patches'] if p.get('symbol') in ('sdk_drum_unheld_key', 'sdk_drum_latch_key')}
        self.assertEqual({a: (p['symbol'], p['before'], p['length'], p['call'], p['service']) for a, p in hooks.items()},
                         {'0x8c0b14b4': ('sdk_drum_unheld_key', call, 8, True, 'drums'),
                          '0x8c0b167a': ('sdk_drum_latch_key', call, 8, True, 'drums')})
        values = (PLATFORM / 'drum_values_stock.cpp').read_text(encoding='utf-8')
        self.assertIn('opxy::u32 sdk_drum_latch_key(opxy::u32 track) {', values)
        self.assertIn('opxy::u32 sdk_drum_unheld_key(opxy::u32 track) {', values)
        self.assertNotIn('kDrumEditKey) =', (PLATFORM / 'stock.hpp').read_text(encoding='utf-8'))
        # The turn event (8d5ad2e8): word 1 its detents ([P5 + 0x4], 8d5ad346), word 2 its
        # speed ([P5 + 0x8], 8d5ad31a); word 0, the continuous motion (8d5ad28e: R0 = [P1]),
        # is added to a Q31 word.
        self.assertEqual(at(0x8d5ad31a, 2).hex(), 'a8a0')
        self.assertEqual(at(0x8d5ad28e, 2).hex(), '0891')

        # The builder's Drumline path, whose words drum_locks.hpp recomputes: the model
        # byte's bin (8d5ad6ee, 8d5ad6fa), pan (8d5ad71e, 8d5ad72a) into word 5 (+0x14,
        # 8d5ad736), the pad word into word 6 (+0x18, 8d5ad73e) and level into word 7
        # through 8d5ad604 (8d5ad744; +0x1c at 8d5ad74a), then the publication.
        self.assertEqual(load(0x8d5ad6ee), (2, 0x01f07c1f))
        self.assertEqual(load(0x8d5ad6fa), (1, 0x00f83e0f))
        self.assertEqual(load(0x8d5ad71e), (3, 0x00a3d70a))
        self.assertEqual(load(0x8d5ad72a), (2, 0x40000000))
        self.assertEqual(at(0x8d5ad736, 2).hex(), '58b1')   # [P3 + 0x14] = R0
        self.assertEqual(at(0x8d5ad73e, 2).hex(), '98b1')   # [P3 + 0x18] = R0
        self.assertEqual(decode_branch(0x8d5ad744, at(0x8d5ad744, 4)), (0x8d5ad604, True))
        source = (PLATFORM / 'drum_locks.hpp').read_text(encoding='utf-8')
        for constant in ('0x01f07c1fu', '0x00f83e0fu', '0x00a3d70a', '0x40000000'):
            self.assertIn(constant, source)
        self.assertIn('stock<i32(i32)>(0x8d5ad604)', (PLATFORM / 'drum_locks.cpp').read_text(encoding='utf-8'))

        # Scramble (8c0b52fc) writes 16..19 through stock's set: R6 = 16 (8c0b53ac) up to
        # 19 (8c0b53ca), each from its generator, before its last write, 13 (8c0b541c, hooked).
        self.assertEqual(at(0x8c0b53ac, 2).hex(), '8660')   # R6 = 0x10
        self.assertEqual(at(0x8c0b53ca, 2).hex(), '9b60')   # R3 = 0x13
        self.assertEqual(decode_branch(0x8c0b53b2, at(0x8c0b53b2, 4)), (0x8c0b5230, True))
        self.assertEqual(at(0x8c0b53c0, 8).hex(), '00de5a8d28cf0000')   # CALL.A 0x8d5acf28
        self.assertEqual(at(0x8c0b541c, 8).hex(),
                         next(p for p in PROFILE['patches'] if p.get('symbol') == 'sdk_synth_scramble')['before'])

        # Project load's track check stores neutral words 0..7 at the object's +0x3864
        # (8d588dfe..8d588e22) just after the hooked test, the object at [SP + 0x34].
        self.assertEqual(load(0x8d588dfe), (0, 0x40000000))
        self.assertEqual(at(0x8d588e06, 4).hex(), '08e6190e')   # [P1 + 0x3864] = R0
        self.assertEqual(at(0x8d588ec4, 2).hex(), '71af')       # P1 = [SP + 0x34]

        # A held step: 8d5b7e34 tests the step word at 0x86bda0c8 against -1, and
        # 8d5ac93c reads its lock (or the stored word) as the Shift row shows it.
        self.assertEqual(at(0x8d5b7e38, 8).hex(), '00d8bd86c8a00000')
        self.assertEqual(at(0x8d5b7e44, 2).hex(), '380c')       # CC = R0 == -1
        self.assertEqual(decode_branch(0x8d5ac950, at(0x8d5ac950, 4)), (0x8d5b7e34, True))
        self.assertEqual(decode_branch(0x8d5ac970, at(0x8d5ac970, 4)), (0x8d5ac910, True))

        # Stock's step 8d5ad2e8 moves count * scale buckets (count from the hooked 8d5ad306)
        # and stores a bucket's centre: 0x7fffffff / buckets * k + half of it.
        self.assertEqual(load(0x8d5ad366), (0, 0x7fffffff))
        self.assertEqual(at(0x8d5ad35a, 2).hex(), 'b84d')       # R0 >>>= 0x17: the bucket
        self.assertEqual(at(0x8d5ad384, 4).hex(), '82c6f805')   # R2 = R0 >>> 1: half a bucket

        # Stock's editor writes a held step's lock, as the handles do (lock_handles.cpp)
        # (8d5ad42c): the step is 8d5b7e4c() + 16 x 8d585fb4(track) (8d5ad438..8d5ad44a), the
        # value its stepped word's top half (8d5ad4be), through 8d586f40(track, step, index,
        # value) (8d5ad4ce), then asks for a redraw: 8d54b3b6(8d54b438()) (8d5ad4d2, 8d5ad4d6).
        self.assertEqual(decode_branch(0x8d5ad438, at(0x8d5ad438, 4)), (0x8d5b7e4c, True))
        self.assertEqual(decode_branch(0x8d5ad440, at(0x8d5ad440, 4)), (0x8d585fb4, True))
        self.assertEqual(at(0x8d5ad444, 4).hex(), '82c62086')   # R3 = R0 << 4
        self.assertEqual(at(0x8d5ad4be, 4).hex(), '80c60092')   # R1.L = R0.H
        self.assertEqual(decode_branch(0x8d5ad4ce, at(0x8d5ad4ce, 4)), (0x8d586f40, True))
        self.assertEqual(decode_branch(0x8d5ad4d2, at(0x8d5ad4d2, 4)), (0x8d54b438, True))
        self.assertEqual(decode_branch(0x8d5ad4d6, at(0x8d5ad4d6, 4)), (0x8d54b3b6, True))
        # The writer stores the halfword at +0x258 + step x 0x54 + index x 2 (8d586fd0..8d586fda).
        self.assertEqual(at(0x8d586fd0, 4).hex(), '2ae15802')   # P2 = 0x258
        self.assertEqual(at(0x8d586fda, 2).hex(), '928b')       # W[P2] = R6.L
        # The kit voice's constructor reads stock's note-start pan term, word 38, once
        # (8d51af24: [P5 + 0x98]), with the pad's pan word (8d51af28), into the voice's
        # pan through 8d51a688 (8d51af2e): kept centred, it leaves the pad's pan.
        self.assertEqual(at(0x8d51af24, 4).hex(), '29e42600')
        self.assertEqual(at(0x8d51af28, 2).hex(), '40a1')
        self.assertEqual(decode_branch(0x8d51af2a, at(0x8d51af2a, 4)), (0x8d51a688, True))
        # The publish hook's flush: the tick's own publication of a track's changed words,
        # 8d5acb9c(project, track) (8d595242..8d59524e).
        self.assertEqual(decode_branch(0x8d59524e, at(0x8d59524e, 4)), (0x8d5acb9c, True))
        self.assertEqual(at(0x8d595242, 2).hex(), '0d30')       # R1 = R5, the track
        source = (PLATFORM / 'drum_locks.cpp').read_text(encoding='utf-8')
        self.assertIn('stock<void(u32, u32)>(0x8d5acb9c)(project, track);', source)


if __name__ == '__main__':
    unittest.main()
