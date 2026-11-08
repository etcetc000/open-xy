"""Four of stock's LFOs (lfo_copies) and page layers: host doubles of both
roles, the companion round trip, and the reviewed stock call sites."""
import base64
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tooling import extensions, native
from tooling.firmware import absolute_target, active_services, decode_branch
from tooling.pipeline import config
from test_extension_runtime import build_and_run
from test_sample_services import HOST_CXX

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
PROFILE = config()

# Each reviewed site: what stock does there and where the SDK takes it. The
# stock calls kept by the SDK's stubs are named by their stock target.
AUDIO_SITES = {
    # Voice renderer in L1 (11a09308): its LFO-on gates before passes 0 and 1.
    '0x11a0953e': ('sdk_multi_lfo_a_pass0', None),
    '0x11a095f6': ('sdk_multi_lfo_a_pass1', None),
    '0x11a09bfc': ('sdk_multi_lfo_voice_restore', 0x8d518d36),
    '0x11a09f32': ('sdk_multi_lfo_reset', 0x8d5047fe),
    # Voice renderer 8d51b274.
    '0x8d51b3d8': ('sdk_multi_lfo_b_pass0', None),
    '0x8d51b520': ('sdk_multi_lfo_b_pass1', None),
    '0x8d51b8dc': ('sdk_multi_lfo_voice_restore', 0x8d518d36),
    '0x8d51bb9e': ('sdk_multi_lfo_reset', 0x8d5047fe),
    # The audio loop's track passes and restores.
    '0x8d510aac': ('sdk_multi_lfo_loop_pass2', None),
    '0x8d510c02': ('sdk_multi_lfo_loop_pass4', None),
    '0x8d512404': ('sdk_multi_lfo_loop_pass3', None),
    '0x8d512360': ('sdk_multi_lfo_restore_before', 0x8d518d36),
    '0x8d512372': ('sdk_multi_lfo_restore_after', 0x8d518d36),
    '0x8d50e8b6': ('sdk_multi_lfo_reset', 0x8d5047fe),
    # Stock's telemetry call inside the evaluator.
    '0x11a04628': ('sdk_multi_lfo_telemetry', 0x8d4d68b8),
}
PAGE_SITES = {'0x8c0b12e4': 'sdk_page_turn', '0x8c0b2e4c': 'sdk_page_buttons', '0x8c0b28a8': 'sdk_page_shift_buttons'}


def stock_call(address, before):
    """The stock target of a 4-byte relative or 8-byte absolute call, else None."""
    if len(before) == 4 and before[1] & 0xfe == 0xe2:
        target, call = decode_branch(address, before)
        return target if call else None
    return absolute_target(before) if len(before) == 8 and before[:2] == b'\x00\xde' else None


class MultiLfo(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_lfos_run_through_stock_code_restore_last_in_first_out_and_swap_at_a_block(self):
        with tempfile.TemporaryDirectory(prefix='multi-lfo-') as folder:
            build_and_run(self, ROOT / 'tests/fixtures/multi_lfo.cpp', folder)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_page_layer_takes_keys_and_turns_and_grants_showing_lfos(self):
        with tempfile.TemporaryDirectory(prefix='page-layer-') as folder:
            build_and_run(self, ROOT / 'tests/fixtures/page_layer.cpp', folder)

    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_copies_round_trip_through_the_project_companion(self):
        import os
        from test_projects import host_writer
        with tempfile.TemporaryDirectory(prefix='lfo-companion-') as directory:
            folder = Path(directory)
            writer, reader = host_writer(folder), host_writer(folder, 'project_load.cpp')
            project = folder / 'song.xy'
            saved = subprocess.run([str(writer), str(project), '1'], capture_output=True, text=True, timeout=30,
                                   env=dict(os.environ, LFO='1'))
            self.assertEqual(saved.returncode, 0, saved.stderr)
            companion = project.with_suffix('.oxy')
            document = json.loads(companion.read_bytes())
            section = document['sections']['open-xy.lfo-copies']
            self.assertEqual(section['version'], 2)
            data = base64.b64decode(section['copies'])
            self.assertEqual(len(data), 8 * 106)
            # Track 3 shows LFO 2: slot 0 holds LFO 1 (stock's), slot 1 LFO 3 (the duck).
            at = 2 * 106
            self.assertEqual(data[at], 1)
            self.assertEqual(data[at + 1:at + 5], bytes([0, 1, 1, 11]))
            self.assertEqual(data[at + 36:at + 40], bytes([2, 1, 6, 100]))
            self.assertEqual(int.from_bytes(data[at + 43:at + 47], 'little', signed=True), -200)
            self.assertEqual(data[at + 71], 3)
            loaded = subprocess.run([str(reader), str(project), '1', 'lfo'], capture_output=True, text=True, timeout=30)
            self.assertEqual(loaded.returncode, 0, loaded.stderr)
            self.assertEqual(loaded.stdout.split(), ['show:2,1', 'lfo:2,0,0,1,1,11,12,13,14,15,16,17,18',
                                                     'lfo:2,1,2,1,6,100,-200,300,0,5,6,7,-8'])
            # Version 1 (the first test build's): LFO 1 shown, slot c holding LFO c + 2.
            v1 = bytearray(8 * 3 * 34)
            v1[(2 * 3 + 1) * 34:(2 * 3 + 1) * 34 + 3] = bytes([1, 6, 9])
            document['sections']['open-xy.lfo-copies'] = {'copies': base64.b64encode(bytes(v1)).decode(), 'version': 1}
            companion.write_text(json.dumps(document, indent=2), encoding='utf-8')
            loaded = subprocess.run([str(reader), str(project), '1', 'lfo'], capture_output=True, text=True, timeout=30)
            self.assertEqual(loaded.returncode, 0, loaded.stderr)
            self.assertEqual(loaded.stdout.split(), ['lfo:2,1,2,1,6,9,0,0,0,0,0,0,0'])
            # A version-2 section of version 1's length is not whole: nothing loads.
            document['sections']['open-xy.lfo-copies']['version'] = 2
            companion.write_text(json.dumps(document, indent=2), encoding='utf-8')
            loaded = subprocess.run([str(reader), str(project), '1', 'lfo'], capture_output=True, text=True, timeout=30)
            self.assertEqual((loaded.returncode, loaded.stdout.strip()), (0, ''))
            # Without copies the section is left out, and a load clears what was set.
            plain = folder / 'plain.xy'
            self.assertEqual(subprocess.run([str(writer), str(plain), '1'], timeout=30).returncode, 0)
            self.assertNotIn('open-xy.lfo-copies', json.loads(plain.with_suffix('.oxy').read_bytes())['sections'])
            loaded = subprocess.run([str(reader), str(plain), '1', 'lfo'], capture_output=True, text=True, timeout=30)
            self.assertEqual((loaded.returncode, loaded.stdout.strip()), (0, ''))
            # A failed load keeps the copies the GUI had.
            loaded = subprocess.run([str(reader), str(project), '0', 'lfo'], capture_output=True, text=True, timeout=30)
            self.assertEqual(loaded.returncode, 0, loaded.stderr)
            self.assertEqual(loaded.stdout.split(), ['lfo:0,0,1,1,0,1,1,1,1,1,1,1,1'])

    def test_every_site_holds_the_reviewed_stock_bytes(self):
        sites = {p['address']: p for p in PROFILE['patches'] if p.get('service') in ('multi_lfo', 'page_layers')}
        self.assertEqual(set(sites), set(AUDIO_SITES) | set(PAGE_SITES))
        for address, (symbol, target) in AUDIO_SITES.items():
            patch = sites[address]
            with self.subTest(address=address):
                self.assertEqual((patch['kind'], patch['symbol'], patch['call'], patch['service']),
                                 ('hook', symbol, True, 'multi_lfo'))
                before = bytes.fromhex(patch['before'])
                self.assertEqual(stock_call(int(address, 16), before), target)
        for address, symbol in PAGE_SITES.items():
            patch = sites[address]
            with self.subTest(address=address):
                self.assertEqual((patch['symbol'], patch['call'], patch['length'], patch['service'], patch['events']),
                                 (symbol, False, 8, 'page_layers', ['layer']))
                # LINK, then the register push the trampoline replays.
                self.assertEqual(patch['before'][:4], '00e8')
        # The build checks each site's bytes against the stock image (tooling.firmware).

    def test_trampolines_replay_the_bytes_they_replace(self):
        source = (PLATFORM / 'page_layer_hooks.s').read_text(encoding='utf-8')
        for address, symbol in PAGE_SITES.items():
            resume = int(address, 16) + 8
            stub = symbol.replace('sdk_page_', 'sdk_page_stock_')
            body = re.search(stub + r':\n(.*?)\.size', source, re.S)
            self.assertIsNotNone(body, stub)
            self.assertIn('P0.H = 0x%04x; P0.L = 0x%04x;' % (resume >> 16, resume & 0xffff), body.group(1))
        loop = (PLATFORM / 'multi_lfo_hooks.s').read_text(encoding='utf-8')
        # Pass 3's fallback rejoins the loop where stock's taken branch went.
        self.assertIn('0x8d51', loop)
        self.assertIn('0x0ca0', loop)

    def test_services_link_only_with_an_extension_that_uses_them(self):
        services = PROFILE['services']
        self.assertEqual(services['b3'], ['master_filter', 'lfo_copies'])
        self.assertEqual(services['multi_lfo'], ['lfo_copies'])
        self.assertEqual(services['page_layers'], ['page_layer'])
        for unit in ('multi_lfo_audio', 'multi_lfo_audio_stock', 'multi_lfo_hooks'):
            self.assertEqual(native.UNIT_SERVICES[unit], 'multi_lfo')
        for unit in ('page_layer_stock', 'page_layer_hooks'):
            self.assertEqual(native.UNIT_SERVICES[unit], 'page_layers')
        self.assertEqual(native.UNIT_SERVICES['b3_receive'], 'b3')
        # The GUI state and its transport link always: the companion reads and writes the copies.
        self.assertIsNone(native.UNIT_SERVICES.get('multi_lfo_state'))
        self.assertIsNone(native.UNIT_SERVICES.get('multi_lfo_send'))
        self.assertEqual(active_services(PROFILE, []), set())
        receiver = next(p for p in PROFILE['patches'] if p.get('symbol') == 'sdk_b3_receive')
        self.assertEqual(receiver['service'], 'b3')

    def test_the_lfo_page_claim_needs_a_layer_and_grants_lfo_copies(self):
        claims, actions = extensions.registry()
        self.assertEqual(claims['lfo_page']['page'], 'lfo')
        self.assertEqual(actions['lfo_copies']['requires'], 'lfo_page')
        self.assertEqual(extensions.PAGES['lfo'], 1)
        header = (ROOT / 'sdk/platform/extension_runtime.hpp').read_text(encoding='utf-8')
        self.assertIn('const u32 page_lfo = 1;', header)


if __name__ == '__main__':
    unittest.main()
