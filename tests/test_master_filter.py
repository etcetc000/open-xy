"""Shared master-filter adapter, using a passthrough probe extension."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tooling.formats import ldr, mapped_offset, sha
from tooling.pipeline import config
import test_sample_services as services

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'


class MasterFilter(unittest.TestCase):
    @unittest.skipUnless(services.HOST_CXX, 'No working host C++ compiler')
    def test_mix_hooks_selector_transport_and_project_reset(self):
        with tempfile.TemporaryDirectory(prefix='master-hooks-') as folder:
            folder = Path(folder)
            stock = (PLATFORM / 'stock.hpp').read_text()
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (folder / 'stock.hpp').write_text(stock)
            for name in ('stock_browser.hpp', 'stock_browser.cpp'):
                (folder / name).write_bytes((PLATFORM / name).read_bytes())
            (folder / 'master_filter_stock_fixture.cpp').write_bytes((PLATFORM / 'master_filter_stock.cpp').read_bytes())
            binary = folder / ('hooks.exe' if os.name == 'nt' else 'hooks')
            command = [services.HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror',
                       '-I' + str(folder), '-I' + str(ROOT / 'sdk/include'), '-I' + str(PLATFORM),
                       str(ROOT / 'tests/fixtures/master_filter_stock.cpp'),
                       str(PLATFORM / 'master_filter_state.cpp'),
                       str(folder / 'stock_browser.cpp'),
                       '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Private stock input not supplied')
    def test_exact_stock_hook_boundaries(self):
        data=Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        profile=config()
        self.assertEqual(sha(data),profile['stock_sha256'])
        for patch in profile['patches']:
            if patch.get('service')!='master_filter':continue
            start=int(patch['address'],16)
            offset=mapped_offset(ldr(data),start,patch['length'])
            self.assertEqual(data[offset:offset+patch['length']],bytes.fromhex(patch['before']))

    def test_capability_activates_only_its_service_and_assets(self):
        from tooling import extensions,native
        from tooling.firmware import active_services
        from test_extensions import Folder
        with Folder() as folder:
            (folder.root/'panel.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 222"><path id="x" d="M 0 0 L 1 1" fill="none" stroke="#000000"/></svg>')
            e=extensions.extension_at(folder.declare(master_filter={'artwork':'panel.svg'}))
            collection=native.Collection([], [e])
            self.assertIn('master_filter',active_services(config(),native.service_users(collection)))
            self.assertNotIn('master_filter',active_services(config(),['extension']))
            from dataclasses import replace
            with self.assertRaisesRegex(ValueError,'multiple owners'):
                extensions.check_collection([e,replace(e,id='other',claims=(),actions=())])
            with self.assertRaises(ValueError):
                extensions.extension_at(folder.declare(master_filter={'artwork':'../panel.svg'}))

    def test_state_identity_survives_an_extension_rename(self):
        from tooling import extensions
        from test_extensions import Folder
        with Folder() as folder:
            (folder.root/'panel.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 222"/>')
            e=extensions.extension_at(folder.declare(id='renamed_filter',
                master_filter={'artwork':'panel.svg','state_id':'original_filter'}))
            source=extensions.generate_master_collection(folder.root,[e]).read_text()
            self.assertIn('sdk_extension_renamed_filter_filter',source)
            self.assertIn('return "original_filter";',source)
            for invalid in ('eq','bad-id','',True):
                with self.subTest(state_id=invalid), self.assertRaises(ValueError):
                    extensions.extension_at(folder.declare(master_filter={'artwork':'panel.svg','state_id':invalid}))

    @unittest.skipUnless(services.HOST_CXX, 'No working host C++ compiler')
    def test_filter_state_survives_streamed_companion_with_high_machine_numbers(self):
        import json
        from test_projects import host_writer
        layout = {f'm{i:03d}': {'engine': 34 + i, 'page': 100} for i in range(219)}
        layout['karplus'] = {'engine': 253, 'page': 254}
        table = ''.join(f'    {{"{name}", number({n["engine"]}), number({n["page"]}), false}},\n'
                        for name, n in layout.items())
        source = (ROOT / 'tests/fixtures/project_companion.cpp').read_text()
        start = source.index('const opxy::platform::CompanionMachine sdk_companion_machines[] = {\n')
        end = source.index('    {nullptr, nullptr, nullptr, false}', start)
        with tempfile.TemporaryDirectory(prefix='master-stream-') as directory:
            folder = Path(directory)
            fixture = folder / 'project_companion_many.cpp'
            fixture.write_text(source[:start] +
                               'const opxy::platform::CompanionMachine sdk_companion_machines[] = {\n' +
                               table + source[end:])
            writer = host_writer(folder, fixture)
            reader = host_writer(folder, 'project_load.cpp')
            project = folder / 'song.xy'
            packet = (1 << 30) | 987
            saved = subprocess.run([str(writer), str(project), '1', str(packet)],
                                   capture_output=True, text=True, timeout=30)
            self.assertEqual(saved.returncode, 0, saved.stderr)
            raw = project.with_suffix('.oxy').read_bytes()
            self.assertGreater(len(raw), 8 * 1024)
            document = json.loads(raw)
            self.assertEqual(document['sections']['open-xy.machines']['machines'], layout)
            self.assertEqual(document['sections']['open-xy.master-filter'],
                             dict(version=1, processor='probe', sweep=987))
            loaded = subprocess.run([str(reader), str(project), '1', 'master', '0:253,34'],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(loaded.returncode, 0, loaded.stderr)
            self.assertEqual(loaded.stdout.strip().splitlines(), ['0:41,18', f'master:{packet},512'])

    @unittest.skipUnless(services.HOST_CXX, 'No working host C++ compiler')
    def test_companion_round_trip_remapping_and_safe_defaults(self):
        import json
        from test_projects import host_writer
        with tempfile.TemporaryDirectory(prefix='master-project-') as directory:
            folder=Path(directory)
            writer=host_writer(folder)
            reader=host_writer(folder,'project_load.cpp')
            project=folder/'song.xy'
            packet=(1<<30)|123
            result=subprocess.run([str(writer),str(project),'1',str(packet)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            path=project.with_suffix('.oxy')
            saved=path.read_bytes()
            document=json.loads(saved)
            section=document['sections']['open-xy.master-filter']
            self.assertEqual(section,dict(version=1,processor='probe',sweep=123))
            def load():
                result=subprocess.run([str(reader),str(project),'1','master','0:36,18'],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                return result.stdout.strip().splitlines()
            self.assertEqual(load(),['0:41,18',f'master:{packet},512'])
            # Missing/future/foreign processor and bad values restore EQ/neutral;
            # the upstream machine mapping still applies to a valid companion.
            for changes in (dict(version=2),dict(processor='missing'),dict(sweep=-1),dict(sweep=1024),dict(sweep=True)):
                section.update(version=1,processor='probe',sweep=123)
                section.update(changes)
                path.write_text(json.dumps(document))
                self.assertEqual(load(),['0:41,18','master:512,512'])
            # Older machine-only companions preserve remapping and default the filter.
            document['sections'].pop('open-xy.master-filter')
            path.write_text(json.dumps(document))
            self.assertEqual(load(),['0:41,18','master:512,512'])
            # No companion or a binding mismatch also preserves stock pattern numbers.
            path.unlink()
            self.assertEqual(load(),['0:36,18','master:512,512'])
            path.write_bytes(saved)
            project.write_bytes(project.read_bytes()+b'changed')
            self.assertEqual(load(),['0:36,18','master:512,512'])
