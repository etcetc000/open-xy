"""Shared native LFO selection, slot names and exact stock hook contracts."""
import copy
from dataclasses import replace
import os
from pathlib import Path
import tempfile
import unittest

from tooling.adapters import generate_synth_controls, generate_voice
from tooling.firmware import absolute_target, decode_branch
from tooling.formats import ldr, mapped_offset, sha
from tooling.pipeline import config
from machine_fixtures import fixture
import test_sample_services as services


class Lfo(unittest.TestCase):
    run_fixture = services.SampleServices.run_fixture

    @unittest.skipUnless(services.HOST_CXX, 'No working host C++ compiler')
    def test_destinations_cycle_declared_slots_and_use_assigned_encoder_icons(self):
        self.run_fixture('lfo', ('sdk/platform/opxy-1.2.1/lfo.cpp',))

    def test_metadata_includes_instrument_records_and_stable_slot_names(self):
        machines = [fixture(name) for name in ('probesynth', 'probesample', 'probefx')]
        definition = copy.deepcopy(machines[0].definition)
        for i in range(4):
            parameter = copy.deepcopy(definition['parameters'][i])
            parameter.update(id='extra' + str(i), label='Extra ' + str(i), control=(3, 3, 0, 2)[i])
            definition['parameters'].append(parameter)
        machines[0] = replace(machines[0], definition=definition)
        with tempfile.TemporaryDirectory() as directory:
            table = generate_synth_controls(directory, machines).read_text(encoding='utf-8')
            voice = generate_voice(directory, machines[0]).read_text(encoding='utf-8')
        self.assertIn('{probesynth_engine_id, probesynth_record_ready, 8, {0, 1, 2, 3, 3, 3, 0, 2}}', table)
        self.assertIn('{probesample_engine_id, probesample_record_ready, 3, {0, 1, 2}}', table)
        self.assertNotIn('probefx_record_ready', table)
        self.assertNotIn('sdk_synth_title_slot(', voice)
        self.assertIn('? labels[index] : ""', voice)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_hooks_match_the_reviewed_stock_instructions(self):
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        profile = config()
        self.assertEqual(sha(data), profile['stock_sha256'])
        expected = {
            0x8d53f828: ('sdk_lfo_range', 0x8d4d6f28),
            0x8d53fb18: ('sdk_lfo_range', 0x8d4d6f28),
            0x8d540afa: ('sdk_lfo_range', 0x8d4d6f28),
            0x8d543280: ('sdk_lfo_range', 0x8d4d6f28),
            0x8d5ad4ba: ('sdk_lfo_step', 0x8d5ad2e8),
            0x8d5ad59e: ('sdk_lfo_step', 0x8d5ad2e8),
            0x11a04260: ('sdk_lfo_audio', 0x8d518ec4),
            0x8d5466f6: ('sdk_synth_title', 0x8d506c4c),
            0x8d54023e: ('sdk_lfo_element_icon', 0x8d602114),
            0x8d541736: ('sdk_lfo_random_icon', 0x8d602114),
            0x8d543d92: ('sdk_lfo_value_icon', 0x8d602114),
            0x8d53ffb0: ('sdk_lfo_element_name', 0x8d4d7f88),
            0x8d53f888: ('sdk_lfo_element_initial_icon', None),
            0x8c0b2d54: ('sdk_lfo_cycle', 0x8d5acf28),
            0x8d54107e: ('sdk_lfo_label', 0x8d4d7f88),
            0x8d543970: ('sdk_lfo_label', 0x8d4d7f88),
        }
        seen = set()
        for patch in profile['patches']:
            address = int(patch['address'], 16)
            if address not in expected:
                continue
            offset = mapped_offset(ldr(data), address, patch['length'])
            before = data[offset:offset + patch['length']]
            self.assertEqual(before.hex(), patch['before'])
            self.assertEqual(offset, int(patch['file_offset'], 16))
            self.assertEqual(patch['symbol'], expected[address][0])
            self.assertTrue(patch['call'])
            if expected[address][1] is None:
                # Element stores the raw destination before calculating the
                # icon offset. The hook replays only that offset calculation.
                self.assertEqual(before.hex(), '504f79c20820b14f')
                self.assertEqual(data[offset - 8:offset].hex(), '07cc08c2a8b10000')
            else:
                target = absolute_target(before) if len(before) == 8 else decode_branch(address, before)[0]
                self.assertEqual(target, expected[address][1])
            seen.add(address)
        self.assertEqual(seen, set(expected))
        # The engine-group audio hook follows a store of the folded index.
        # The same scratch word is read later as publication argument R1.
        for address, before in (
            (0x11a04250, '00d8048450790202'),
            (0x11a04618, '00d8048450790100'),
            (0x11a04628, '00de4d8db8680000'),
        ):
            offset = mapped_offset(ldr(data), address, 8)
            self.assertEqual(data[offset:offset + 8].hex(), before)
