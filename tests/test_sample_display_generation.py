"""Custom sample screens preserve payload identity and real-voice lifecycle."""
import copy
from dataclasses import replace
from pathlib import Path
import tempfile
import unittest
from machine_fixtures import fixture
from tooling import adapters, startup, native
from tooling.stock_tables import SCREEN_COUNT


class SampleDisplayGeneration(unittest.TestCase):
    def test_runtime_display_range_matches_relocated_table(self):
        self.assertEqual(SCREEN_COUNT, 256)
        source = (Path(native.ROOT) / 'sdk/platform/opxy-1.2.1/sample_display_controls.hpp').read_text()
        self.assertIn(f'page >= 82 && page < {SCREEN_COUNT}', source)

    def machine(self):
        sample = fixture('probesample')
        definition = copy.deepcopy(sample.definition)
        definition['resources'] = dict(voiceCapacity=8, sampleDisplay=1,
            display=copy.deepcopy(fixture('probesynth').definition['resources']['display']))
        return replace(sample, identity=dict(engine=36, page=2), display='drawing', definition=definition)

    def test_custom_screen_never_replaces_payload_record_page(self):
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_registration(directory, self.machine()).read_text()
            table = adapters.generate_sample_callbacks(directory, [self.machine()]).read_text()
        self.assertIn('probesample_engine_id, probesample_page_id, 1}', text)
        self.assertIn('sdk_register_sample_screen(&screen, &display_page)', text)
        self.assertIn('sdk_engine_ready(&record, nullptr)', text)
        self.assertIn('sdk_sample_screen_page(&screen, display_page)', text)
        self.assertIn('{probesample_engine_id, 2, probesample_ready', table)
        self.assertIn('probesample_display_page}', table)
        self.assertNotIn('sdk_register_screen(&record, &screen)', text)

    def test_dynamic_screens_follow_all_fixed_screens_and_precede_arranger(self):
        text = startup.render(['sampled', 'fixed'], stock_page=['sampled'], sample_display=['sampled'])
        self.assertLess(text.index('if (fixed_screen_register() == 1) fixed_register();'),
                        text.index('    sampled_screen_register();'))
        self.assertLess(text.index('    sampled_screen_register();'), text.index('    sdk_arranger_screen_startup();'))
        self.assertIn('if (page && page == sampled_display_page())', text)
        self.assertNotIn('value(sampled_page_id)', text)
        with self.assertRaises(ValueError):
            startup.render(['fixed'], sample_display=['fixed'])

    def test_voice_snapshot_is_after_render_and_stale_serial_is_cleared(self):
        with tempfile.TemporaryDirectory() as directory:
            text = adapters.generate_sample_voice(directory, self.machine()).read_text()
        self.assertNotIn('@', text)
        self.assertIn('if (!++visual_serial) ++visual_serial;', text)
        self.assertIn('slot->visual_serial = visual_serial;', text)
        render = text[text.index('static int render('):]
        self.assertLess(render.index('::render_sample('), render.index('::sample_visual('))
        self.assertLess(render.index('::sample_visual('), render.index('sdk_sample_visual_render('))
        factory = text[text.index('void* probesample_factory(opxy::u32 index, opxy::u32 track) {'):text.index('static int render(')]
        self.assertLess(factory.index('sdk_sample_visual_clear('), factory.index('slot->track ='))
        self.assertIn('slot->visual_serial = 0;', factory)
        destroy = text[text.index('static int destroy(void* self) {'):text.index('void* probesample_factory(opxy::u32 index, opxy::u32 track) {')]
        self.assertIn('sdk_sample_visual_clear(', destroy)
        self.assertIn('sdk_sample_visual_clear(', render[:render.index('::render_sample(')])

    def test_synth_only_retains_dispatch_dependencies_not_sample_services(self):
        steps = {step for _, owner, step in native.plan([fixture('probesynth')]) if owner is None}
        self.assertTrue({'module_events', 'sample_display_controls', 'sample_display_telemetry', 'sample_display_preview',
                         'sample_machines', 'sample_callbacks'} <= steps)
        self.assertFalse({'drum_controls_stock', 'sample_hooks', 'sample_cache'} & steps)
        with tempfile.TemporaryDirectory() as directory:
            table = adapters.generate_sample_callbacks(directory, []).read_text()
        self.assertIn('{nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr}', table)
        self.assertIn('sizeof(sdk_sample_machines[0]) - 1', table)
