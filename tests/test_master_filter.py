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
    def test_shared_mutable_paths_can_start_with_an_opaque_fill(self):
        import xml.etree.ElementTree as ET
        from tooling.native import check_outlines
        display = dict(paths=1, points=4, nodes=1, texts=0, palette=1)
        node = ET.fromstring('<path id="probe_node_0" d="M 0 0 L 20 0 L 20 20 L 0 20 Z" '
                             'fill="#2F2F2F" stroke="#CCCCCC" stroke-width="1"/>')
        check_outlines({'probe_node_0': node}, 'probe', display)
        node.set('d', 'M 0 0 L 10 0 L 20 0 L 30 0 Z')
        with self.assertRaisesRegex(ValueError, 'enclose area'):
            check_outlines({'probe_node_0': node}, 'probe', display)
        node.set('fill', 'none')
        check_outlines({'probe_node_0': node}, 'probe', display)

    @unittest.skipUnless(services.HOST_CXX, 'No working host C++ compiler')
    def test_multi_parameter_companion_and_defaults(self):
        import json
        from test_projects import host_writer
        with tempfile.TemporaryDirectory(prefix='master-parameters-') as directory:
            folder = Path(directory)
            writer, reader = host_writer(folder), host_writer(folder, 'project_load.cpp')
            project = folder / 'song.xy'
            saved = subprocess.run([str(writer), str(project), '1', str((1 << 30) | 123), 'multi'],
                                   capture_output=True, text=True, timeout=30)
            self.assertEqual(saved.returncode, 0, saved.stderr)
            path = project.with_suffix('.oxy')
            document = json.loads(path.read_bytes())
            parameters = document['sections']['open-xy.master-filter']['parameters']
            self.assertEqual(parameters, dict(sweep=123 << 21, gain=1234567, mix=2469134,
                                             mode=3703701, alt=4938268))
            def load():
                result = subprocess.run([str(reader), str(project), '1', 'master-multi', '0:36'],
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                return result.stdout.strip().splitlines()
            self.assertEqual(load(), ['0:41', f'parameters:1,{123 << 21},1234567,2469134,3703701,4938268'])
            parameters.pop('mix')
            parameters['future'] = 999
            path.write_text(json.dumps(document))
            self.assertEqual(load(), ['0:41', f'parameters:1,{123 << 21},1234567,200,3703701,4938268'])
            parameters['gain'] = True
            path.write_text(json.dumps(document))
            self.assertEqual(load(), ['0:41', 'parameters:0,1073741824,100,200,0,300'])

    def test_svg_safety_is_shared_with_instrument_artwork(self):
        from tooling import extensions, native
        from machine_fixtures import fixture
        machine = fixture('probesynth')
        original = (machine.root / 'panel.svg.fragment').read_bytes()
        with tempfile.TemporaryDirectory(prefix='master-svg-') as directory:
            path = Path(directory) / 'panel.svg'
            for number, safe in (('0.1234', True), ('0.12345', False), ('1e-3', False)):
                with self.subTest(number=number):
                    path.write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 222">'
                                    f'<path d="M {number} 0 L 10 0 L 10 10 Z" fill="#000000"/></svg>')
                    fragment = original.rsplit(b'</g>',1)[0] + f'<path d="M {number} 0 L 10 0 L 10 10 Z" fill="#000000"/></g>'.encode()
                    if safe:
                        extensions.validate_master_artwork(path)
                        native.validate_panel(machine, fragment)
                    else:
                        with self.assertRaises(ValueError):
                            extensions.validate_master_artwork(path)
                        with self.assertRaises(ValueError):
                            native.validate_panel(machine, fragment)
            path.write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 222" width="480.12345"/>')
            with self.assertRaisesRegex(ValueError, 'four decimal'):
                extensions.validate_master_artwork(path)

    def test_stock_loader_rejects_xml_prefixed_element_names(self):
        from tooling import extensions, native
        with tempfile.TemporaryDirectory(prefix='master-svg-prefix-') as directory:
            path = Path(directory) / 'panel.svg'
            for xml in (
                '<ns0:svg xmlns:ns0="http://www.w3.org/2000/svg" viewBox="0 0 480 222"/>',
                '<svg xmlns="http://www.w3.org/2000/svg" xmlns:p="http://www.w3.org/2000/svg" '
                'viewBox="0 0 480 222"><p:path d="M 0 0 L 1 1" fill="none" stroke="#000000" stroke-width="1"/></svg>',
            ):
                path.write_text(xml)
                with self.subTest(xml=xml), self.assertRaisesRegex(ValueError, 'unprefixed'):
                    extensions.validate_master_artwork(path)
                with self.assertRaisesRegex(ValueError, 'unprefixed'):
                    native.panel_root(xml.encode(), 'probe')
            path.write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 222">'
                            '<path d="M 0 0 L 1 1" fill="none" stroke="#000000" stroke-width="1"/></svg>')
            self.assertEqual(extensions.validate_master_artwork(path), path.read_bytes())

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
            (folder.root/'panel.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 222"><path id="probe_node_0" d="M 0 0 L 1 1" fill="none" stroke="#000000" stroke-width="1"/><path id="probe_palette_0" d="M 0 0 L 1 0 L 1 1 Z" fill="#000000" stroke="#000000" stroke-width="1"/></svg>')
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
            (folder.root/'panel.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 480 222"><path id="renamed_filter_node_0" d="M 0 0 L 1 1" fill="none" stroke="#000000" stroke-width="1"/><path id="renamed_filter_palette_0" d="M 0 0 L 1 0 L 1 1 Z" fill="#000000" stroke="#000000" stroke-width="1"/></svg>')
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
                             dict(version=2, processor='probe', parameters={'sweep':987<<21}))
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
            self.assertEqual(section,dict(version=2,processor='probe',parameters={'sweep':123<<21}))
            def load():
                result=subprocess.run([str(reader),str(project),'1','master','0:36,18'],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                return result.stdout.strip().splitlines()
            self.assertEqual(load(),['0:41,18',f'master:{packet},512'])
            # Missing/future/foreign processor and bad values restore EQ/neutral;
            # the upstream machine mapping still applies to a valid companion.
            for changes in (dict(version=3),dict(processor='missing'),dict(sweep=-1),dict(sweep=1024),dict(sweep=True)):
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
