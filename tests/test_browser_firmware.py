"""The independent release checker honors selected extension services and settings."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tooling.pipeline import config
from tools import check_browser_firmware as release


class BrowserFirmware(unittest.TestCase):
    def test_drumline_services_apply_only_when_its_extension_is_selected(self):
        kit = {'profile': config(), 'machines': [{'id': 'synth', 'kind': 'synth'}],
               'extensions': [{'id': 'drumline_page', 'drumline_page': {'label': 'Drumline'}}]}
        result = {'selected': ['synth'], 'extensions': []}
        self.assertEqual(release.selection_services(kit, result), set())
        result['extensions'] = ['drumline_page']
        self.assertEqual(release.selection_services(kit, result),
                         {'extensions', 'samples', 'drums', 'octaves', 'drum_sends', 'drum_locks', 'drumline'})
        self.assertEqual(release.selection_settings(kit, result), {})

    def test_expanded_firmware_uses_selected_extension_settings_in_both_modes(self):
        kit = {'profile': config(), 'machines': [], 'extensions': [
            {'id': 'chosen', 'linked': False, 'settings': {'release_floor': {'value': 12}}},
            {'id': 'unused', 'linked': False, 'settings': {'release_floor': {'value': 24}}}]}
        result = {'name': 'selection', 'selected': [], 'extensions': ['chosen']}
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            reference = directory / 'reference'
            reference.mkdir()
            (reference / 'selections-expanded.json').write_text(json.dumps([result]))
            for suffix in ('.ldr', '.profiling-off.ldr'):
                (reference / ('selection' + suffix)).write_bytes(b'fixture')
            with patch.object(release, 'check_module', return_value=(b'module', {})), \
                    patch.object(release, 'check_numbers'), \
                    patch.object(release, 'source_selection_services', return_value=set()), \
                    patch.object(release, 'assembled', return_value=b'fixture') as assembled:
                self.assertEqual(release.check_expanded(directory, kit, b'stock', b'label', {}, {}), 1)
            self.assertEqual([(call.args[5], call.args[6]) for call in assembled.call_args_list],
                             [(True, {'release_floor': 12}), (False, {'release_floor': 12})])


if __name__ == '__main__':
    unittest.main()
