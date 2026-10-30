"""SDK display note events and authenticated native note-off routing."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tooling import native
from tooling.formats import ldr, mapped_offset, sha
from tooling.pipeline import ROOT, config
from test_sample_services import HOST_CXX


class NoteActivity(unittest.TestCase):
    @unittest.skipUnless(HOST_CXX, 'No working host C++ compiler (set OPXY_TEST_HOST_CXX)')
    def test_real_platform_adapter_and_public_event_tracker(self):
        platform = ROOT / 'sdk/platform/opxy-1.2.1'
        with tempfile.TemporaryDirectory(prefix='note-activity-') as directory:
            folder = Path(directory)
            # Substitute only the stock-address boundary, as other SDK fixtures do.
            stock = (platform / 'stock.hpp').read_text(encoding='utf-8')
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock, encoding='utf-8')
            (folder / 'note_activity.cpp').write_bytes((platform / 'note_activity.cpp').read_bytes())
            binary = folder / ('test.exe' if os.name == 'nt' else 'test')
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                '-I' + str(folder), '-I' + str(ROOT / 'sdk/include'), '-I' + str(platform),
                str(ROOT / 'tests/fixtures/note_activity.cpp'), '-o', str(binary)],
                capture_output=True, text=True, timeout=120, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_both_release_routes_are_reviewed_calls_in_a_shared_unit(self):
        hooks = [p for p in config()['patches'] if p.get('symbol') == 'sdk_note_off']
        self.assertEqual({int(p['address'],16) for p in hooks}, {0x8d5954b2,0x8d595514})
        self.assertTrue(all(p['call'] and p['length'] == 4 and 'service' not in p for p in hooks))
        self.assertIn('note_activity', native.SDK_UNITS)
        self.assertNotIn('note_activity', native.UNIT_SERVICES)

    def test_offline_builder_exposes_the_same_note_activity_helper(self):
        from tooling.browser_kit import write_builder
        from tooling.screen import template
        with tempfile.TemporaryDirectory(prefix='note-activity-builder-') as directory:
            folder = Path(directory)
            write_builder(dict(screenTemplate=template().decode('utf-8'), notices=''), folder)
            html = (folder / 'builder.pending.html').read_text(encoding='utf-8')
            script = re.search(r'<script type="module">(.*?)</script>', html, re.S).group(1)
            # Run the exported SDK namespaces from the actual offline bundle,
            # before the page's DOM/application layer. No firmware is embedded.
            script = script.split('const Ferret =', 1)[0] + '''
const notes = new Retained.NoteActivity();
notes.noteOn(60); notes.noteOn(64); notes.noteOff(60);
if (notes.held_notes !== 1 || notes.note_off_serial !== 1) throw Error('Offline note activity failed');
notes.panic();
if (notes.held_notes !== 0 || notes.note_off_serial !== 2) throw Error('Offline panic failed');
'''
            hidden = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
            result = subprocess.run(['node', '--input-type=module'], input=script, capture_output=True,
                                    text=True, timeout=30, **hidden)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_stock_release_calls_delegate_to_the_original_map_clear(self):
        from tooling.firmware import decode_branch
        stock = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        profile = config()
        self.assertEqual(sha(stock), profile['stock_sha256'])
        for patch in profile['patches']:
            if patch.get('symbol') != 'sdk_note_off':
                continue
            address = int(patch['address'],16)
            offset = mapped_offset(ldr(stock), address, patch['length'])
            before = stock[offset:offset+patch['length']]
            self.assertEqual(before.hex(), patch['before'])
            self.assertEqual(offset, int(patch['file_offset'],16))
            self.assertEqual(decode_branch(address,before), (0x8d5b7d2c,True))


if __name__ == '__main__':
    unittest.main()
