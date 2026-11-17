"""Build capabilities (docs/cpu-capabilities.md): the registry, coverage, and that each CPU
capability is exactly its own edits and units, with every site stock while it is off."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

from tooling import extensions, native, settings as image_settings
from tooling.extensions import extension_at
from tooling.firmware import active_services, decode_branch, patch_active
from tooling.formats import header
from tooling.pipeline import ROOT, check_patch_boundaries, config, document
import off_path_code

REGISTRY = document(ROOT / 'profiles/extensions.json')['capabilities']


# Each CPU capability's edits (address -> kind) and the SDK units it links.
CPU = {
    'idle': ({'0x8d5116a0': 'hook', '0x8d511908': 'hook', '0x8d51c7a6': 'hook'},
             ('send_fx', 'idle_cut')),
    'block_trims': ({'0x8d511014': 'hook', '0x8d510f04': 'hook', '0x11a044e6': 'hook', '0x8d503878': 'hook',
                     '0x8d50fac4': 'hook'}, ('track_hooks', 'floor_hooks')),
    'output_slack': ({'0x8c0c6ffe': 'hook'}, ('dsp_block', 'dsp_block_hooks', 'output_slack', 'output_slack_hooks')),
}
FIXTURES = ROOT / 'tests/fixtures/machines'


def edits_of(profile, name):
    return {p['address']: p['kind'] for p in profile['patches'] if p.get('service') == name}


def folder(test, capabilities, **fields):
    root = Path(tempfile.mkdtemp())
    test.addCleanup(shutil.rmtree, root)
    data = {'id': 'cpu_test', 'name': 'CPU test', 'description': 'Turns on CPU capabilities.',
            'claims': [], 'actions': [], 'capabilities': list(capabilities), **fields}
    (root / 'extension.json').write_text(json.dumps(data), encoding='utf-8')
    return root


class Machines(list):
    def __init__(self, *extensions_):
        super().__init__()
        self.extensions = extensions_


def registry_with(**capabilities):
    """extensions.document, answering the extension registry with these capabilities added."""
    real = document

    def fake(path):
        data = real(path)
        if Path(path).name == 'extensions.json':
            data = copy.deepcopy(data)
            data['capabilities'].update(capabilities)
        return data
    return mock.patch.object(extensions, 'document', fake)


class Registry(unittest.TestCase):
    def test_registry_validates(self):
        self.assertEqual(set(extensions.capabilities()), set(REGISTRY))
        self.assertEqual(set(extensions.build_capabilities()), {n for n, c in REGISTRY.items() if c.get('build')})

    def test_a_build_capability_needs_its_service(self):
        with registry_with(missing={'build': True}), self.assertRaises(ValueError):
            extensions.capabilities()

    def test_an_image_capability_has_no_hooks(self):
        for name, capability in REGISTRY.items():
            if not capability.get('build'):
                self.assertTrue(all(p['kind'] not in ('hook', 'startup') for p in config()['patches']
                                    if p.get('service') == name))


class Coverage(unittest.TestCase):
    def test_a_capability_without_sites_in_the_profile_fails(self):
        for name in REGISTRY:
            e = extension_at(folder(self, [name]))
            extensions.check_coverage(config(), [e])
            profile = copy.deepcopy(config())
            del profile['services'][name]
            with self.assertRaisesRegex(ValueError, f'the {name} capability has no firmware sites for OP-XY OS 1.2.1'):
                extensions.check_coverage(profile, [e])


class Switches(unittest.TestCase):
    def test_each_capability_is_exactly_its_edits(self):
        for name, (sites, _units) in CPU.items():
            self.assertTrue(REGISTRY[name].get('build'), name)
            self.assertEqual(edits_of(config(), name), sites, name)

    def test_off_every_site_is_stock_and_no_unit_is_linked(self):
        services = active_services(config(), native.service_users(Machines()))
        for name, (_sites, units) in CPU.items():
            self.assertNotIn(name, services)
            for patch in config()['patches']:
                if patch.get('service') == name:
                    for profiling in (True, False):
                        self.assertFalse(patch_active(patch, services, profiling), patch['address'])
            for unit in units:
                self.assertEqual(native.UNIT_SERVICES[unit], name)
        planned = {step for _n, _m, step in native.plan(Machines())}
        self.assertFalse(planned & {u for _s, units in CPU.values() for u in units})

    def test_on_exactly_its_own_sites_and_units(self):
        for name, (sites, units) in CPU.items():
            machines = Machines(extension_at(folder(self, [name])))
            services = active_services(config(), native.service_users(machines))
            self.assertEqual(services & set(CPU), {name})
            for patch in config()['patches']:
                if patch.get('service') in CPU:
                    on = patch['service'] == name
                    self.assertEqual(patch_active(patch, services, False), on, patch['address'])
                    self.assertEqual(patch_active(patch, services, True), on and 'profiling' not in patch)
            planned = {step for _n, _m, step in native.plan(machines)}
            self.assertEqual(planned & {u for _s, us in CPU.values() for u in us}, set(units))

    def test_an_image_cannot_take_a_build_capability(self):
        e = extension_at(folder(self, ['idle']))
        with self.assertRaisesRegex(ValueError, 'Chosen when the firmware is built'):
            image_settings.apply(b'', [e])

    def test_a_browser_kit_cannot_carry_a_build_capability(self):
        from tooling import browser_kit
        browser_kit.refuse_build_capabilities([extension_at(folder(self, ['long_samples']))])
        for name in ('idle',):
            with self.assertRaisesRegex(ValueError, 'a browser kit cannot carry them: ' + name):
                browser_kit.refuse_build_capabilities([extension_at(folder(self, [name]))])

    def test_exporting_a_browser_kit_refuses_a_build_capability_before_writing(self):
        # browser_kit.export itself refuses, before reading the catalog or writing anything.
        from tooling import browser_kit
        output = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, output)
        machines = Machines(extension_at(folder(self, ['idle'])))
        with mock.patch.object(native, 'package_collection'),                 mock.patch.object(native, 'configuration', return_value=(config(), machines, {})),                 mock.patch.object(browser_kit, 'catalog_sources', side_effect=AssertionError('read the catalog first')):
            with self.assertRaisesRegex(ValueError, 'a browser kit cannot carry them: idle'):
                browser_kit.export(output, output, None, stock_catalog=b'')
        self.assertEqual(list(output.iterdir()), [])

    def test_an_image_capability_is_fixed_limit_edits(self):
        # A hook in an image capability would let an image setting reach SDK code the build did
        # not choose; the registry refuses it.
        profile = copy.deepcopy(document(ROOT / 'profiles/opxy-1.2.1.json'))
        for p in profile['patches']:
            if p.get('service') == 'long_samples':
                p['kind'] = 'hook'
                break
        with mock.patch.object(extensions, 'document', lambda path: profile if 'opxy-' in str(path) else document(path)):
            with self.assertRaisesRegex(ValueError, 'Invalid capability: long_samples'):
                extensions.capabilities()


@unittest.skipUnless(off_path_code.toolchain(), 'No Blackfin toolchain')
class IdleSwitch(unittest.TestCase):
    """compile_collection generates the voice and effect adapters with their idle pieces exactly when the build
    has the idle capability (a dropped `idle=` argument would silently lose the voice idle cut)."""
    def test_the_adapters_take_their_idle_pieces_only_with_idle(self):
        import subprocess, sys
        out = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, out)
        compiler = off_path_code.toolchain()
        for idle in (False, True):
            with self.subTest(idle=idle):
                command = [sys.executable, str(ROOT / 'sdk.py'), 'compile', '--toolchain', str(compiler / 'bin'),
                           '--compiler', str(compiler), '--out', str(out / str(idle)), '--overwrite',
                           '--machine', str(ROOT / 'tests/fixtures/machines/probesynth'),
                           '--machine', str(ROOT / 'tests/fixtures/machines/probefx')]
                if idle:
                    command += ['--extension', str(folder(self, ['idle']))]
                run = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(run.returncode, 0, run.stdout[-2000:] + run.stderr[-2000:])
                voice = next((out / str(idle)).rglob('voice.cpp')).read_text(encoding='utf-8')
                self.assertEqual('sdk_idle_floor' in voice, idle)
        effects = [next((out / str(idle)).rglob('fx_object_*.cpp')).read_text(encoding='utf-8') for idle in (False, True)]
        self.assertNotEqual(effects[0], effects[1])


def loader(payload):
    """A loader image: FIRST, one data record of `payload` at 0x1000, FINAL."""
    body = header(4, 0x1000, len(payload), 0) + payload + header(0x8004, 0, 0, 0)
    return header(0x5004, 0x1000, 0, len(body)) + body


class Built(unittest.TestCase):
    """settings.built: a build capability is on in an image when every site holds its bytes."""
    def profile(self):
        edit = lambda at, before, after: dict(kind='limit', service='fast', address=hex(0x1000 + at),
                                              file_offset=hex(32 + at), length=2, before=before, after=after)
        return dict(stock_bytes=16, patches=[edit(0, '0102', 'aabb'), edit(4, '0506', 'ccdd')])

    def built(self, payload):
        with mock.patch.object(image_settings, 'build_capabilities', lambda: ('fast',)):
            return image_settings.built(loader(bytes(payload)), self.profile())

    def test_all_stock_all_built_and_partly_built(self):
        stock = [1, 2, 3, 4, 5, 6, 7, 8]
        self.assertEqual(self.built(stock), {'fast': False})
        self.assertEqual(self.built([0xaa, 0xbb, 3, 4, 0xcc, 0xdd, 7, 8]), {'fast': True})
        with self.assertRaisesRegex(ValueError, 'Capability only partly built: fast'):
            self.built([0xaa, 0xbb, 3, 4, 5, 6, 7, 8])
        with self.assertRaisesRegex(ValueError, 'Unexpected bytes at a capability site'):
            self.built([0xaa, 0xbc, 3, 4, 0xcc, 0xdd, 7, 8])


class Extensions(unittest.TestCase):
    def test_the_boot_log_fix_is_its_own_image_capability(self):
        # One data byte, fixed bytes: an image setting, separate from idle.
        profile = config()
        self.assertEqual(edits_of(profile, 'boot_log_fix'), {'0x8400ea65': 'limit'})
        self.assertNotIn('boot_log_fix', extensions.build_capabilities())
        self.assertIn('boot_log_fix', extensions.capabilities())
        self.assertNotIn('0x8400ea65', edits_of(profile, 'idle'))
        idle_only = active_services(profile, native.service_users(Machines(extension_at(folder(self, ['idle'])))))
        self.assertFalse(any(patch_active(p, idle_only, True) for p in profile['patches']
                             if p.get('service') == 'boot_log_fix'))


class StockAddresses(unittest.TestCase):
    """Each hook reaches stock where stock's own replaced instruction did: the address that instruction calls or
    loads, decoded from its bytes (the profile's `before`), is the one the hook's unit names, in the form it uses;
    a hook that resumes stock right after the replaced bytes names that address. With a stock OS image, every CPU
    edit's `before` is checked against the image's bytes, not only the profile's own copy."""
    # address -> (unit source, the text that must name the decoded stock address there)
    TARGETS = {
        '0x8d5116a0': ('send_fx.cpp', 'const u32 kProcessSend = {:#x};'),
        '0x8d511908': ('send_fx.cpp', 'const u32 kProcessSend = {:#x};'),
        '0x8d51c7a6': ('idle_cut.cpp', 'stock<int(unsigned char*, i32*)>({:#x})(voice, output);'),
        '0x8d503878': ('floor_hooks.s', 'P2.L = 0x{0:04x}; P2.H = 0x{1:04x};\n    CALL (P2);'),
        '0x8d50fac4': ('floor_hooks.s', 'P1.L = 0x{0:04x}; P1.H = 0x{1:04x};'),
        '0x8c0c6ffe': ('dsp_block_hooks.s', 'P1 = {:#x};\n    CALL (P1);'),
    }
    # address -> (unit source, the text that resumes stock at the address right after the replaced bytes)
    RESUMES = {
        '0x8d503878': ('floor_hooks.s', 'P1.L = 0x{0:04x}; P1.H = 0x{1:04x};\n    JUMP (P1);'),
        '0x8d50fac4': ('floor_hooks.s', 'P2.L = 0x{0:04x}; P2.H = 0x{1:04x};'),
    }

    @staticmethod
    def stock_target(patch):
        """What the replaced instruction calls (a 32-bit CALL) or loads (a 64-bit immediate load)."""
        data = bytes.fromhex(patch['before'])
        first = int.from_bytes(data[:2], 'little')
        if first & 0xfe00 == 0xe200:
            return decode_branch(int(patch['address'], 16), data[:4])[0]
        if first & 0xff00 in (0xde00, 0xda00) and len(data) == 8:
            return int.from_bytes(data[2:4], 'little') << 16 | int.from_bytes(data[4:6], 'little')
        raise AssertionError('not a call or an immediate load: ' + patch['address'])

    @staticmethod
    def text(form, address):
        return form.format(address) if '{:' in form else form.format(address & 0xffff, address >> 16)

    def test_each_hook_names_the_stock_address_it_replaces(self):
        hooks = {p['address']: p for p in config()['patches'] if p.get('service') in CPU and p['kind'] == 'hook'}
        checked = set()
        for address, (unit, form) in self.TARGETS.items():
            source = (ROOT / 'sdk/platform/opxy-1.2.1' / unit).read_text(encoding='utf-8')
            want = self.text(form, self.stock_target(hooks[address]))
            with self.subTest(address=address):
                self.assertIn(want, source)
            checked.add(address)
        for address, (unit, form) in self.RESUMES.items():
            source = (ROOT / 'sdk/platform/opxy-1.2.1' / unit).read_text(encoding='utf-8')
            after = int(address, 16) + hooks[address]['length']
            with self.subTest(resume=address):
                self.assertIn(self.text(form, after), source)
        # The others: the two jump hooks of the track mixes (tests/test_track_hooks.py: back to 8d511182) and the
        # LFO pass check, which replaces its helper call with the helper's own work (tests/test_floor_hooks.py).
        self.assertEqual(set(hooks) - checked, {'0x8d511014', '0x8d510f04', '0x11a044e6'})

    @unittest.skipUnless(os.environ.get('OPXY_TEST_STOCK_OS'), 'Stock OS image not supplied')
    def test_every_edit_replaces_the_stock_image_bytes(self):
        data = Path(os.environ['OPXY_TEST_STOCK_OS']).read_bytes()
        names = set(CPU) | {'boot_log_fix', 'load_governor'}
        edits = [p for p in config()['patches'] if p.get('service') in names]
        self.assertGreaterEqual(len(edits), 10)
        for p in edits:
            offset = int(p['file_offset'], 16)
            with self.subTest(address=p['address']):
                self.assertEqual(data[offset:offset + p['length']].hex(), p['before'])


class LinkOrder(unittest.TestCase):
    def test_no_capability_changes_the_pinned_layout(self):
        # The module links in the base's order and layout: the linker script is the one the firmware profile
        # pins (its linker_sha256, which the build checks), and no capability names it among its edits,
        # units or unit sources.
        profile = document(ROOT / 'profiles/opxy-1.2.1.json')
        self.assertEqual(hashlib.sha256((ROOT / native.LINKER).read_bytes()).hexdigest(), profile['linker_sha256'])
        names = set(off_path_code.cpu_capabilities(ROOT))
        self.assertFalse([p for p in profile['patches'] if p.get('service') in names and 'linker' in json.dumps(p)])
        owned = [u for u, service in native.UNIT_SERVICES.items() if service in names]
        self.assertFalse([o for o in owned if Path(o).suffix == '.ld' or Path(o).name == Path(native.LINKER).name])


if __name__ == '__main__':
    unittest.main()
