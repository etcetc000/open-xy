"""The device identity (SDK 1.30, docs/api.md#device-identity): a machine declaring
resources.deviceIdentity reads 18 bytes of stock's boot information through
opxy/device_identity.hpp.

The GUI processor reads them from its main loop (8c0b73a8, device_identity_hooks.s) and
sends them to the audio processor in stock's global parameter message, whose receiver
call (8d5b145e) the master processor also needs: that site has one record, the
parameter_receive service, and a build with the identity enters its receiver, which
passes every other number on to the master processor's receiver or stock's setter. The
host fixture drives the real unit against stock doubles; the browser preview and host
builds give the documented preview identity."""
import copy
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

from tooling import native
from tooling.definitions import DEVICE_IDENTITY_SDK, validate_definition
from tooling.firmware import active_services, decode_branch, hook_symbol, patch_active
from tooling.machines import SDK_VERSION, machine_at
from tooling.pipeline import ROOT, check_patch_boundaries, config
from machine_fixtures import FOLDER, NAMES, fixture
from test_sample_services import HOST_CXX
from test_service_links import TOOLS, run, symbols

PLATFORM = ROOT / 'sdk/platform/opxy-1.2.1'
HEADER = ROOT / 'sdk/include/opxy/device_identity.hpp'
HIDDEN = dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}
LOOP_SITE, RECEIVE_SITE = '0x8c0b73a8', '0x8d5b145e'
UNITS = ('device_identity', 'device_identity_hooks')


def record(profile, address):
    found = [p for p in profile['patches'] if p['address'] == address]
    assert len(found) == 1
    return found[0]


class Declaration(unittest.TestCase):
    def test_the_fixture_declares_it_with_its_sdk(self):
        m = fixture('probeident')
        self.assertEqual(m.sdk, DEVICE_IDENTITY_SDK)
        self.assertEqual(m.definition['resources']['deviceIdentity'], 1)
        self.assertGreaterEqual(SDK_VERSION, DEVICE_IDENTITY_SDK)
        self.assertIn('SDK 1.30', HEADER.read_text(encoding='utf-8'))

    def test_every_kind_may_declare_it_as_0_or_1(self):
        for name, kind in (('probesynth', 'synth'), ('probekit21', 'drum'), ('probefx', 'effect'),
                           ('probesample', 'sample'), ('probepads', 'player')):
            for value in (0, 1):
                definition = copy.deepcopy(fixture(name).definition)
                definition['resources']['deviceIdentity'] = value
                with self.subTest(kind=kind, value=value):
                    validate_definition(definition, kind)
            for bad in (2, -1, True, '1', 1.0):
                definition = copy.deepcopy(fixture(name).definition)
                definition['resources']['deviceIdentity'] = bad
                with self.subTest(kind=kind, value=bad), self.assertRaises(ValueError):
                    validate_definition(definition, kind)

    def test_a_master_processor_may_not(self):
        definition = dict(schema=1, id='probe', name='Probe', parameters=[dict(
            id='sweep', label='filter', default=.5, mapping=dict(kind='linear', minimum=0, maximum=1), display='percent')],
            presets={}, resources=dict(display=dict(stateBytes=4, stateAlignment=4, paths=0, points=0, nodes=1, texts=0,
                                                    palette=1)))
        validate_definition(copy.deepcopy(definition), 'master')
        definition['resources']['deviceIdentity'] = 1
        with self.assertRaises(ValueError):
            validate_definition(definition, 'master')

    def copy_fixture(self, directory, name='probeident', sdk=None, declared=True):
        folder = Path(directory) / name
        shutil.copytree(FOLDER / name, folder)
        if sdk is not None:
            record = json.loads((folder / 'machine.json').read_text(encoding='utf-8'))
            record['sdk'] = sdk
            record.pop('identity', None)  # older fixtures carry one; from 1.5 the build gives the numbers
            (folder / 'machine.json').write_text(json.dumps(record), encoding='utf-8')
        definition = json.loads((folder / 'definition.json').read_text(encoding='utf-8'))
        if declared is False:
            definition['resources'].pop('deviceIdentity', None)
        else:
            definition['resources']['deviceIdentity'] = int(declared)
        (folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
        return folder

    def test_a_machine_declaring_an_older_sdk_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            older = '%d.%d' % (DEVICE_IDENTITY_SDK[0], DEVICE_IDENTITY_SDK[1] - 1)
            with self.assertRaisesRegex(ValueError, 'device identity'):
                machine_at(self.copy_fixture(directory, sdk=older))

    def test_a_machine_that_includes_the_header_declares_it(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'deviceIdentity'):
                machine_at(self.copy_fixture(directory, declared=False))
        with tempfile.TemporaryDirectory() as directory:
            # Declaring it as 0 is not declaring it.
            with self.assertRaisesRegex(ValueError, 'deviceIdentity'):
                machine_at(self.copy_fixture(directory, declared=0))
        with tempfile.TemporaryDirectory() as directory:
            # Nor may one declare the reader itself.
            folder = self.copy_fixture(directory, 'probestage', sdk='1.30', declared=False)
            engine = folder / 'engine.cpp'
            engine.write_text('extern "C" bool opxy_device_identity(unsigned char*, unsigned);\n' +
                              engine.read_text(encoding='utf-8'), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'deviceIdentity'):
                machine_at(folder)
        with tempfile.TemporaryDirectory() as directory:
            # Declaring it without the header is allowed: the build links the service for nothing.
            m = machine_at(self.copy_fixture(directory, 'probestage', sdk='1.30'))
            self.assertIn('deviceIdentity', native.service_users([m]))
        with tempfile.TemporaryDirectory() as directory:
            # Any kind: an effect too.
            m = machine_at(self.copy_fixture(directory, 'probefx', sdk='1.30'))
            self.assertEqual(m.kind, 'effect')
            self.assertIn('deviceIdentity', native.service_users([m]))


class Profile(unittest.TestCase):
    def test_the_main_loop_record_calls_stocks_function_after_it(self):
        patch = record(config(), LOOP_SITE)
        self.assertEqual((patch['kind'], patch['length'], patch['before'], patch['call'], patch['service'],
                          patch['symbol'], patch['file_offset']),
                         ('hook', 8, '00de578df8c00000', True, 'device_identity', 'sdk_identity_gui_tick', '0xee5e4'))
        self.assertNotIn('symbols', patch)
        # The 8-byte absolute call it replaces calls 8d57c0f8, which the handler calls in its place.
        before = bytes.fromhex(patch['before'])
        self.assertEqual(int.from_bytes(before[:2], 'little') & 0xff00, 0xde00)
        self.assertEqual(int.from_bytes(before[2:4], 'little') << 16 | int.from_bytes(before[4:6], 'little'), 0x8d57c0f8)
        handler = (PLATFORM / 'device_identity_hooks.s').read_text(encoding='utf-8')
        body = handler[handler.index('sdk_identity_gui_tick:\n'):handler.index('.size sdk_identity_gui_tick')]
        self.assertEqual(body, 'sdk_identity_gui_tick:\n    LINK 0;\n    SP += -12;\n    P0.H = 0x8d57; P0.L = 0xc0f8;\n'
                               '    CALL (P0);\n    SP += 12;\n    HOOK_SAVE\n    CALL sdk_identity_gui_service;\n'
                               '    HOOK_RESTORE\n    UNLINK;\n    RTS;\n')

    def test_one_record_holds_the_parameter_receivers_call(self):
        profile = config()
        patch = record(profile, RECEIVE_SITE)
        self.assertEqual((patch['kind'], patch['length'], patch['before'], patch['call'], patch['service'], patch['symbol']),
                         ('hook', 4, 'fae397eb', True, 'parameter_receive', 'sdk_master_receive'))
        self.assertEqual(list(patch['symbols'].items()), [('device_identity', 'sdk_identity_receive'),
                                                          ('master_filter', 'sdk_master_receive')])
        # The call it replaces is stock's setter, which the receiver calls without the master processor.
        self.assertEqual(decode_branch(int(RECEIVE_SITE, 16), bytes.fromhex(patch['before'])), (0x8d50eb8c, True))
        unit = (PLATFORM / 'device_identity.cpp').read_text(encoding='utf-8')
        self.assertIn('kStockSet = 0x8d50eb8c', unit)
        self.assertIn('stock<Receiver>(kStockSet)(base, id, value);', unit)
        services = profile['services']
        self.assertEqual(services['device_identity'], ['deviceIdentity'])
        self.assertEqual(set(services['parameter_receive']), set(services['master_filter']) | set(services['device_identity']))
        check_patch_boundaries(profile, len(profile['patches']))

    def test_the_handler_follows_which_services_the_build_has(self):
        profile = config()
        receive, loop = record(profile, RECEIVE_SITE), record(profile, LOOP_SITE)
        for users, symbol in (([], None), (['synth'], None), (['master_filter'], 'sdk_master_receive'),
                              (['deviceIdentity'], 'sdk_identity_receive'),
                              (['master_filter', 'deviceIdentity'], 'sdk_identity_receive')):
            services = active_services(profile, users)
            with self.subTest(users=users):
                self.assertEqual(patch_active(receive, services, False), symbol is not None)
                self.assertEqual(patch_active(loop, services, False), 'deviceIdentity' in users)
                if symbol:
                    self.assertEqual(hook_symbol(receive, services), symbol)

    def test_the_browser_enters_the_same_handler(self):
        profile = config()
        patch = record(profile, RECEIVE_SITE)
        cases = [sorted(active_services(profile, users)) for users in
                 (['master_filter'], ['deviceIdentity'], ['master_filter', 'deviceIdentity'])]
        script = ('import {hookSymbol, validSharedHook} from %s;\nconst [edit, services, cases] = JSON.parse(process.argv[1]);\n'
                  'process.stdout.write(JSON.stringify([validSharedHook(edit, services), '
                  '...cases.map(c => hookSymbol(edit, new Set(c)))]));'
                  % json.dumps((ROOT / 'builder/core.mjs').as_uri()))
        result = subprocess.run(['node', '--input-type=module', '-e', script,
                                 json.dumps([patch, profile['services'], cases])],
                                capture_output=True, text=True, cwd=ROOT, timeout=60, check=True)
        self.assertEqual(json.loads(result.stdout), [True, 'sdk_master_receive', 'sdk_identity_receive',
                                                     'sdk_identity_receive'])


@unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Stock OS image not supplied (OPXY_TEST_STOCK_OS)')
class StockImage(unittest.TestCase):
    def test_both_sites_hold_the_reviewed_bytes(self):
        from tooling.formats import ldr, mapped_offset, sha
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        profile = config()
        self.assertEqual(sha(data), profile['stock_sha256'])
        for address in (LOOP_SITE, RECEIVE_SITE):
            patch = record(profile, address)
            offset = mapped_offset(ldr(data), int(address, 16), patch['length'])
            with self.subTest(address=address):
                self.assertEqual(data[offset:offset + patch['length']], bytes.fromhex(patch['before']))
                self.assertEqual(offset, int(patch['file_offset'], 16))


class Plan(unittest.TestCase):
    def test_only_a_build_with_a_declaring_machine_links_its_units(self):
        self.assertEqual({u: native.UNIT_SERVICES[u] for u in UNITS}, dict.fromkeys(UNITS, 'device_identity'))
        self.assertTrue(set(UNITS) <= set(native.SDK_UNITS))
        units = lambda machines: [step for _name, m, step in native.plan(machines) if m is None]
        for name in NAMES:
            self.assertNotIn('deviceIdentity', native.service_users([fixture(name)]))
        plain = units([fixture(name) for name in NAMES])
        declared = units([fixture(name) for name in NAMES] + [fixture('probeident')])
        self.assertFalse(set(UNITS) & set(plain))
        self.assertEqual([u for u in declared if u not in plain], list(UNITS))
        self.assertIn('deviceIdentity', native.service_users([fixture('probeident')]))

    def test_without_the_master_processor_the_receiver_goes_on_to_stocks_setter(self):
        profile = config()
        # kit_absent, linked in every build (so a selection without the identity's
        # machine links too), tells the receiver to call stock's setter.
        self.assertEqual(native.service_fallbacks(active_services(profile, ['synth', 'deviceIdentity'])),
                         {'master_filter': {'sdk_master_receive': 'kit_absent'}})
        self.assertNotIn('master_filter', native.service_fallbacks(active_services(profile, ['synth', 'master_filter'])))
        # With both, the fallback is declared for selections that leave the master processor out.
        self.assertEqual(native.service_fallbacks(active_services(profile, ['synth', 'deviceIdentity', 'master_filter'])),
                         {'master_filter': {'sdk_master_receive': 'kit_absent'}})
        unit = (PLATFORM / 'device_identity.cpp').read_text(encoding='utf-8')
        self.assertIn('if (reinterpret_cast<Address>(master) == absent) stock<Receiver>(kStockSet)(base, id, value);', unit)
        self.assertIn('int kit_absent() __asm__("kit_absent");', (PLATFORM / 'startup.cpp.in').read_text(encoding='utf-8'))

    def test_the_header_calls_only_the_sdk_unit_on_the_device(self):
        header = HEADER.read_text(encoding='utf-8')
        # On the device the header only calls the SDK unit; elsewhere it gives the preview identity.
        self.assertIn('return opxy_device_identity(output, size);', header)
        self.assertEqual(re.findall(r'0x([0-9a-f]{2})', header[header.index('preview_device_identity['):])[:18],
                         ['%02x' % (0xa0 + i) for i in range(18)])


class Host(unittest.TestCase):
    def test_the_service_reads_sends_and_receives(self):
        """The unit's GUI and audio paths against stock doubles (tests/fixtures/device_identity.cpp)."""
        with tempfile.TemporaryDirectory(prefix='device-identity-') as folder:
            stock = (PLATFORM / 'stock.hpp').read_text(encoding='utf-8')
            stock = stock.replace('typedef __UINTPTR_TYPE__ Address;',
                                  'typedef __UINTPTR_TYPE__ Address;\nAddress host_stock_address(Address);')
            for pointer in ('T*', 'const volatile u32*', 'const volatile unsigned char*'):
                stock = stock.replace('reinterpret_cast<' + pointer + '>(address)',
                                      'reinterpret_cast<' + pointer + '>(host_stock_address(address))')
            (Path(folder) / 'stock.hpp').write_text(stock, encoding='utf-8')
            shutil.copy(PLATFORM / 'device_identity.cpp', Path(folder) / 'device_identity_unit.cpp')
            binary = Path(folder) / ('device_identity' + ('.exe' if os.name == 'nt' else ''))
            command = [HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-D__builtin_bfin_ssync()=host_barrier()',
                       '-I' + folder, '-I' + str(ROOT / 'sdk/include'), '-I' + str(PLATFORM),
                       str(ROOT / 'tests/fixtures/device_identity.cpp'), '-o', str(binary)]
            # With the master processor's receiver, and with kit_absent in its place.
            for setter in (0, 1):
                result = subprocess.run([*command[:-2], '-DSTOCK_SETTER=%d' % setter, *command[-2:]],
                                        capture_output=True, text=True, timeout=120, **HIDDEN)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                for mode in ('gui', 'torn', 'giveup', 'lastlook', 'audio'):
                    result = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=120, **HIDDEN)
                    with self.subTest(setter=setter, mode=mode):
                        self.assertEqual((result.returncode, result.stdout.strip()), (0, 'ok'), result.stdout + result.stderr)

    def test_a_machine_reads_the_preview_identity_on_the_host(self):
        """The header's host path, as a machine's host test or the browser preview compiles it."""
        source = '''#include <cstdio>
#include <opxy/device_identity.hpp>
int main() {
    opxy::u8 out[20] = {};
    if (!opxy::device_identity(out, 20)) return 1;
    for (unsigned i = 0; i < 18; ++i) if (out[i] != 0xa0 + i) return 2;
    if (out[18] || out[19]) return 3;
    const opxy::u8 mine[18] = {1, 2, 3};
    opxy::u8 two[2] = {};
    if (!opxy::configure_preview_device_identity(mine) || !opxy::device_identity(two, 2) || two[0] != 1 || two[1] != 2) return 4;
    if (!opxy::configure_preview_device_identity(nullptr) || opxy::device_identity(two, 2) || two[0] != 1) return 5;
    std::printf("ok\\n");
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='device-identity-header-') as folder:
            (Path(folder) / 'main.cpp').write_text(source, encoding='utf-8')
            binary = Path(folder) / ('main' + ('.exe' if os.name == 'nt' else ''))
            result = subprocess.run([HOST_CXX, '-std=c++11', '-O2', '-Wall', '-Werror', '-I' + str(ROOT / 'sdk/include'),
                                     str(Path(folder) / 'main.cpp'), '-o', str(binary)],
                                    capture_output=True, text=True, timeout=120, **HIDDEN)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=120, **HIDDEN)
            self.assertEqual((result.returncode, result.stdout.strip()), (0, 'ok'), result.stdout + result.stderr)


@unittest.skipUnless((TOOLS / ('bfin-elf-g++' + ('.exe' if os.name == 'nt' else ''))).is_file(),
                     'No Blackfin toolchain in local/toolchain (python sdk.py toolchain fetch, or set OPXY_TEST_COMPILER)')
class Links(unittest.TestCase):
    def test_every_selection_links_and_only_one_with_the_machine_hooks_for_it(self):
        """A real build of probesynth beside probeident, and the browser linker (builder/core.mjs
        linkKit) over its objects: each selection links, and only one with probeident links the
        service's units and turns on both of its hooks' services."""
        import base64
        from tooling import browser_kit
        from tooling.machines import FIXED_NUMBERS, read_collection
        folders = [FOLDER / 'probesynth', FOLDER / 'probeident']
        with tempfile.TemporaryDirectory(prefix='links-identity-') as folder:
            out = Path(folder) / 'module'
            result = run([sys.executable, ROOT / 'sdk.py', 'compile', *(a for m in folders for a in ('--machine', m)),
                          '--toolchain', TOOLS, '--compiler', TOOLS.parent, '--out', out])
            self.assertEqual(result.returncode, 0, result.stdout[-4000:] + result.stderr[-4000:])
            report = json.loads((out / 'compile-report.json').read_text(encoding='utf-8'))
            self.assertTrue({'device_identity', 'parameter_receive'} <= set(report['services']))
            self.assertIn('_opxy_device_identity', symbols(out / 'sdk/device_identity.o')[0])
            profile, machines, definitions = native.configuration(read_collection(None, folders))
            owners, services = native.object_owners(machines, profile), native.object_services(machines, profile)
            objects = [dict(name=name, owner=owner, service=services.get(name),
                            bytes=base64.b64encode((out / name).read_bytes()).decode('ascii'))
                       for name, owner in owners.items()]
            entries = [browser_kit.machine_entry(m, profile, {}, Path(folder), 'clang++') for m in machines]
            fallbacks = native.service_fallbacks(active_services(profile, native.service_users(machines)))
            kit = dict(version='0.0.0', profile=profile, fixed=FIXED_NUMBERS, machines=entries, serviceFallbacks=fallbacks,
                       definitions={key: value for key, value in definitions.items() if key in native.DEFINITIONS})
            script = """
import {linkKit, readObject} from %s;
let input = '';
for await (const chunk of process.stdin) input += chunk;
const {kit, objects, selections} = JSON.parse(input);
kit.objects = objects.map(({bytes, service, ...o}) =>
  ({...readObject(Buffer.from(bytes, 'base64')), ...o, ...(service ? {service} : {})}));
process.stdout.write(JSON.stringify(selections.map(selection => {
  const linked = linkKit(kit, selection);
  return {objects: linked.objects.map(o => o.name), services: [...linked.services],
          masterIsAbsent: linked.aliases.has('sdk_master_receive') &&
            linked.aliases.get('sdk_master_receive') === linked.globals.get('kit_absent')};
})));
""" % json.dumps((ROOT / 'builder/core.mjs').as_uri())
            selections = [['probesynth'], ['probeident'], ['probesynth', 'probeident']]
            result = subprocess.run(['node', '--input-type=module', '-e', script], capture_output=True, text=True,
                                    encoding='utf-8', cwd=ROOT, timeout=120,
                                    input=json.dumps(dict(kit=kit, objects=objects, selections=selections)))
            self.assertEqual(result.returncode, 0, result.stderr[-4000:])
            for selection, linked in zip(selections, json.loads(result.stdout)):
                with self.subTest(selection=selection):
                    expected = 'probeident' in selection
                    self.assertEqual('sdk/device_identity.o' in linked['objects'], expected)
                    self.assertEqual('sdk/device_identity_hooks.o' in linked['objects'], expected)
                    self.assertEqual({'device_identity', 'parameter_receive'} & set(linked['services']),
                                     {'device_identity', 'parameter_receive'} if expected else set())
                    # No selection has the master processor: its receiver is exactly kit_absent,
                    # which the identity's receiver compares against to call stock's setter instead.
                    self.assertTrue(linked['masterIsAbsent'])


if __name__ == '__main__':
    unittest.main()
