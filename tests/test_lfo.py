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
        self.run_fixture('lfo', ('sdk/platform/opxy-1.2.0/lfo.cpp',))

    def test_metadata_includes_instrument_records_and_stable_slot_names(self):
        machines = [fixture(name) for name in ('probesynth', 'probesample', 'probefx')]
        definition = copy.deepcopy(machines[0].definition)
        for i in range(4):
            parameter = copy.deepcopy(definition['parameters'][i])
            parameter.update(id='extra' + str(i), label='Extra ' + str(i), control=(3, 3, 0, 2)[i])
            if i == 1:
                parameter.update(mapping={'kind': 'enum', 'labels': ['A', 'B', 'C']}, display='enum')
            definition['parameters'].append(parameter)
        machines[0] = replace(machines[0], definition=definition)
        with tempfile.TemporaryDirectory() as directory:
            table = generate_synth_controls(directory, machines).read_text(encoding='utf-8')
            voice = generate_voice(directory, machines[0]).read_text(encoding='utf-8')
        self.assertIn('{probesynth_engine_id, probesynth_record_ready, 8, {0, 1, 2, 3, 3, 3, 0, 2}}', table)
        # Each enum's choices, for stepping one a detent (synth_controls.cpp).
        self.assertRegex(table, r'\{probesynth_engine_id, probesynth_ready, .*, \{0, 0, 0, 0, 0, 3, 0, 0\}, 0\},')
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
            0x8d53ec60: ('sdk_lfo_range', 0x8d4d78e0),
            0x8d53ef50: ('sdk_lfo_range', 0x8d4d78e0),
            0x8d53ff32: ('sdk_lfo_range', 0x8d4d78e0),
            0x8d5426b8: ('sdk_lfo_range', 0x8d4d78e0),
            0x8d5ab7ea: ('sdk_lfo_step', 0x8d5ab618),
            0x8d5ab8ce: ('sdk_lfo_step', 0x8d5ab618),
            0x11a03f20: ('sdk_lfo_audio', 0x8d5182d4),
            0x8d545b2e: ('sdk_synth_title', 0x8d5065fc),
            0x8d53f676: ('sdk_lfo_element_icon', 0x8d5fffa4),
            0x8d540b6e: ('sdk_lfo_random_icon', 0x8d5fffa4),
            0x8d5431ca: ('sdk_lfo_value_icon', 0x8d5fffa4),
            0x8d53f3e8: ('sdk_lfo_element_name', 0x8d4d8940),
            0x8d53ecc0: ('sdk_lfo_element_initial_icon', None),
            0x8c0b38b4: ('sdk_lfo_cycle', 0x8d5ab258),
            0x8d5404b6: ('sdk_lfo_label', 0x8d4d8940),
            0x8d542da8: ('sdk_lfo_label', 0x8d4d8940),
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
            (0x11a03f10, '00d80484584a0202'),
            (0x11a042d8, '00d80484584a0100'),
            (0x11a042e8, '00de4d8d60720000'),
        ):
            offset = mapped_offset(ldr(data), address, 8)
            self.assertEqual(data[offset:offset + 8].hex(), before)
