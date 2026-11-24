"""Build capabilities (docs/cpu-capabilities.md): the registry, coverage, and that each CPU
capability is exactly its own edits and units, with every site stock while it is off."""
import copy
import hashlib
import io
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

    def test_a_build_capabilitys_limit_edits_are_marked(self):
        """Every fixed-byte (limit) edit of a build capability carries the "build" mark, the
        profile's record that it is a build capability's. An unmarked one is refused."""
        self.assertTrue(all(p.get('build') is True for p in config()['patches']
                            if p['kind'] == 'limit' and REGISTRY.get(p.get('service'), {}).get('build')))
        for marked in (False, True):
            profile = copy.deepcopy(document(ROOT / 'profiles/opxy-1.2.1.json'))
            hook = next(p for p in profile['patches'] if p.get('service') == 'idle')
            extra = {k: v for k, v in hook.items() if k not in ('symbol', 'call')}
            extra.update(kind='limit', address='0x8d5116a8', before='0000', after='0100', length=2)
            if marked:
                extra['build'] = True
            profile['patches'].append(extra)
            with mock.patch.object(extensions, 'document',
                                   lambda path: profile if 'opxy-' in str(path) else document(path)):
                if marked:
                    extensions.capabilities()
                else:
                    with self.assertRaisesRegex(ValueError, 'Invalid build capability: idle'):
                        extensions.capabilities()

    def test_an_image_capability_has_no_hooks(self):
        for name, capability in REGISTRY.items():
            if not capability.get('build'):
                self.assertTrue(all(p['kind'] not in ('hook', 'startup') for p in config()['patches']
                                    if p.get('service') == name))


class UnitCapability(unittest.TestCase):
    """fast_helpers: a build capability with no edit whose setting (0 off, the default, or 1) chooses SDK units'
    sources (tooling/native.py UNIT_SOURCES)."""
    REAL = REGISTRY['fast_helpers']

    def test_the_registry_entry_and_the_tooling_agree(self):
        self.assertEqual(self.REAL, {'build': True, 'units': ['divide', 'memory'],
                                     'setting': {'label': 'Fast helpers', 'unit': '0 off, 1 on',
                                                 'minimum': 0, 'maximum': 1, 'default': 0}})
        self.assertEqual(extensions.unit_capabilities(), {'fast_helpers': ('divide', 'memory')})
        self.assertEqual(native.UNIT_SOURCES, {'fast_helpers': {'divide': 'sdk/platform/blackfin/divide_fast.s',
                                                                'memory': 'sdk/platform/blackfin/memory_fast.cpp'}})
        self.assertEqual((native.RUNTIME_ASM, native.RUNTIME_CXX),
                         ('sdk/platform/blackfin/divide.s', 'sdk/platform/blackfin/memory.cpp'))
        self.assertEqual(edits_of(config(), 'fast_helpers'), {})
        self.assertIn('fast_helpers', extensions.build_capabilities())
        native.check_unit_sources()  # configuration() runs it on every build

    def test_the_registry_refuses_other_shapes(self):
        setting = self.REAL['setting']
        for shape in ({'build': True, 'units': ['divide']},                                # no setting
                      {'build': True, 'units': [], 'setting': setting},                    # no unit
                      {'build': True, 'units': ['divide', 'divide'], 'setting': setting},
                      {'build': True, 'units': ['divide'], 'setting': dict(setting, default=1)},   # on by default
                      {'build': True, 'units': ['divide'], 'setting': dict(setting, maximum=2)},
                      {'build': True, 'units': ['divide'], 'setting': dict(setting, minimum=-1)},
                      {'build': True, 'units': ['divide'], 'setting': setting, 'extra': 1},
                      {'units': ['divide'], 'setting': setting}):                          # not a build capability
            with self.subTest(shape=shape), registry_with(fast_helpers=shape), self.assertRaises(ValueError):
                extensions.capabilities()
        # A unit capability has no edit: one in the profile is refused.
        profile = copy.deepcopy(document(ROOT / 'profiles/opxy-1.2.1.json'))
        edit = copy.deepcopy(next(p for p in profile['patches'] if p.get('service') == 'load_governor'
                                  and 'choices' not in p))
        edit.update(service='fast_helpers', address='0x8d51e4f2')
        profile['patches'].append(edit)
        with mock.patch.object(extensions, 'document', lambda path: profile if 'opxy-' in str(path) else document(path)):
            with self.assertRaisesRegex(ValueError, 'Invalid build capability: fast_helpers'):
                extensions.capabilities()

    def test_the_tooling_refuses_sources_the_registry_does_not_name(self):
        for sources in ({'fast_helpers': {'divide': native.UNIT_SOURCES['fast_helpers']['divide']}},
                        {**native.UNIT_SOURCES, 'other': {'memory': 'sdk/platform/blackfin/memory_fast.cpp'}},
                        {'fast_helpers': {**native.UNIT_SOURCES['fast_helpers'],
                                          'divide': 'sdk/platform/blackfin/nothing.s'}},
                        {'fast_helpers': {'memory': 'sdk/platform/blackfin/memory_fast.cpp',
                                          'nounit': 'sdk/platform/blackfin/divide_fast.s'}}):
            with self.subTest(sources=sources), mock.patch.object(native, 'UNIT_SOURCES', sources):
                with self.assertRaisesRegex(ValueError, 'The unit capabilities and their sources differ'):
                    native.check_unit_sources()

    def test_a_unit_no_build_step_matches_is_refused(self):
        # The names agree and the file exists: only the SDK-unit check refuses it. Without it a build that asks
        # for the capability would compile no step from the chosen source and silently link the plain one.
        source = native.UNIT_SOURCES['fast_helpers']['divide']
        with mock.patch.object(extensions, 'unit_capabilities', lambda: {'fast_helpers': ('divide', 'nounit')}), \
                mock.patch.object(native, 'UNIT_SOURCES', {'fast_helpers': {'divide': source, 'nounit': source}}):
            with self.assertRaisesRegex(ValueError, 'The unit capabilities and their sources differ'):
                native.check_unit_sources()

    def test_every_configuration_checks_the_unit_sources(self):
        # configuration(), which every compile and build runs first, refuses sources the registry does not name.
        entries = [{'path': str(ROOT / 'tests/fixtures/machines/probesynth')}]
        native.configuration(entries)
        divide = {'divide': native.UNIT_SOURCES['fast_helpers']['divide']}
        with mock.patch.object(native, 'UNIT_SOURCES', {'fast_helpers': divide}):
            with self.assertRaisesRegex(ValueError, 'The unit capabilities and their sources differ'):
                native.configuration(entries)

    def test_only_setting_1_chooses_the_fast_sources(self):
        self.assertEqual(native.unit_sources(Machines(), config()), {})
        for settings, expected in (({}, {}), ({'fast_helpers': 0}, {}),
                                   ({'fast_helpers': 1}, native.UNIT_SOURCES['fast_helpers'])):
            with self.subTest(settings=settings):
                e = extension_at(folder(self, ['fast_helpers'], **({'settings': settings} if settings else {})))
                self.assertEqual(native.unit_sources(Machines(e), config()), expected)
        # The SDK's own extension: off unless its setting is changed.
        cpu = extension_at(ROOT / 'extensions' / 'cpu')
        self.assertEqual(native.unit_sources(Machines(cpu), config()), {})
        # A setting for a capability the extension does not declare, or out of range, is refused.
        with self.assertRaises(ValueError):
            extension_at(folder(self, ['idle'], settings={'fast_helpers': 1}))
        for value in (2, -1, True, '1'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                extension_at(folder(self, ['fast_helpers'], settings={'fast_helpers': value}))

    def test_an_image_cannot_take_it_and_cannot_be_read_for_it(self):
        e = extension_at(folder(self, ['fast_helpers'], settings={'fast_helpers': 1}))
        with self.assertRaisesRegex(ValueError, 'Chosen when the firmware is built.*fast_helpers'):
            image_settings.apply(b'', [e])
        # A browser kit carries it: its units' objects for each value (tooling/browser_kit.py unit_sources).
        from tooling import browser_kit
        browser_kit.refuse_adapter_capabilities([e])
        # No site: settings.built leaves it out rather than report it off (the compile report says it).
        with mock.patch.object(image_settings, 'build_capabilities', lambda: ('fast_helpers',)):
            self.assertEqual(image_settings.built(loader(bytes(range(16))), dict(stock_bytes=16, patches=[])), {})


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

    def test_a_browser_kit_refuses_only_what_changes_machines_code(self):
        # A kit's objects serve every selection, so it cannot carry a capability that changes every
        # machine's generated code: the registry marks those (`"adapters": true`, idle alone).
        from tooling import browser_kit
        self.assertEqual(extensions.adapter_capabilities(), ('idle',))
        for shape in ({'adapters': True}, {'build': True, 'adapters': False}, {'build': True, 'units': ['divide'],
                      'setting': REGISTRY['fast_helpers']['setting'], 'adapters': True}):
            with self.subTest(shape=shape), registry_with(fast_helpers=shape, idle=shape), self.assertRaises(ValueError):
                extensions.capabilities()
        browser_kit.refuse_adapter_capabilities([extension_at(folder(self, ['long_samples', 'block_trims', 'fast_helpers']))])
        with self.assertRaisesRegex(ValueError, "change every machine's generated code.*sdk.py build.*: idle"):
            browser_kit.refuse_adapter_capabilities([extension_at(folder(self, ['idle', 'block_trims']))])
        # The SDK's cpu extension declares none of them, so a kit carries it whole.
        browser_kit.refuse_adapter_capabilities([extension_at(ROOT / 'extensions' / 'cpu')])

    def test_the_sdk_extensions_are_official_without_a_catalog_entry(self):
        # Independent of this checkout's git state: git's answers are given (tracked, then clean).
        import subprocess as process
        from tooling import browser_kit
        cpu = extension_at(ROOT / 'extensions' / 'cpu')
        other = extension_at(folder(self, ['long_samples']))

        def git(tracked, changed):
            def run(command, **_options):
                out = {'ls-files': 'extensions/cpu/extension.json\n' if tracked else '',
                       'status': ' M extensions/cpu/extension.json\n' if changed else ''}[command[1]]
                return process.CompletedProcess(command, 0, out, '')
            return mock.patch.object(browser_kit.subprocess, 'run', run)
        with git(True, False):
            self.assertTrue(browser_kit.sdk_extension(cpu))
            self.assertFalse(browser_kit.sdk_extension(other))
            self.assertEqual(browser_kit.extension_origins([cpu], personal=False), {'cpu': 'official'})
            self.assertEqual(browser_kit.extension_origins([cpu, other], personal=True), {'cpu': 'official', 'cpu_test': 'personal'})
            with self.assertRaisesRegex(ValueError, 'catalog machines and extensions at their pinned commits'):
                browser_kit.extension_origins([cpu, other], personal=False)
        # Untracked or changed, it is a folder like any other: no published kit takes it.
        for tracked, changed in ((False, False), (True, True)):
            with self.subTest(tracked=tracked, changed=changed), git(tracked, changed):
                self.assertFalse(browser_kit.sdk_extension(cpu))
                with self.assertRaisesRegex(ValueError, 'pinned commits'):
                    browser_kit.extension_origins([cpu], personal=False)

    def test_only_the_registry_mark_changes_generated_adapters(self):
        # For every fixture machine, the generated adapter sources with a build capability's service
        # differ from those without it exactly when the registry marks it `adapters` (idle): the mark
        # that keeps it out of browser kits is the code generation's own behavior.
        entries = [{'path': str(FIXTURES / name)} for name in
                   ('probesynth', 'probefx', 'probesample', 'probeshift', 'probesources', 'probekit21', 'probecanvas')]
        profile, machines, _definitions = native.configuration(entries)
        base = native.selected_services(profile, machines)

        def sources(services):
            found = {}
            with tempfile.TemporaryDirectory() as work:
                for n, m, step in native.plan(machines, profile):
                    if m is None or ':' in step:
                        continue
                    where = Path(work) / n.replace('/', '_')
                    where.mkdir(parents=True, exist_ok=True)
                    found[n] = Path(native.adapter_source(where, m, step, False, services)).read_text(encoding='utf-8')
            return found
        plain = sources(base)
        for name in extensions.build_capabilities():
            with self.subTest(capability=name):
                self.assertEqual(sources(base | {name}) != plain, bool(REGISTRY[name].get('adapters')))

    def test_sdk_py_kit_refuses_an_adapters_capability_before_building(self):
        # sdk.py kit refuses it from the collection alone, before the native build or any output.
        from tooling import browser_kit
        output = Path(tempfile.mkdtemp()) / 'kit'
        self.addCleanup(shutil.rmtree, output.parent)
        idle = folder(self, ['idle'])
        with mock.patch.object(browser_kit, 'resolve_toolchain', return_value=('tools', 'compiler')), \
                mock.patch.object(native, 'build', side_effect=AssertionError('built first')), \
                mock.patch('sys.stderr', new_callable=io.StringIO) as err, self.assertRaises(SystemExit) as exit_:
            browser_kit.main(['--personal', '--stock-os', 'os.ldr', '--out', str(output),
                              '--machine', str(FIXTURES / 'probesynth'), '--extension', str(idle)])
        self.assertEqual(exit_.exception.code, 1)
        self.assertRegex(err.getvalue(), "change every machine's generated code.*: idle")
        self.assertFalse(output.exists())

    def test_exporting_a_browser_kit_refuses_an_adapters_capability_before_writing(self):
        # browser_kit.export itself refuses, before reading the catalog or writing anything.
        from tooling import browser_kit
        output = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, output)
        machines = Machines(extension_at(folder(self, ['idle'])))
        with mock.patch.object(native, 'package_collection'),                 mock.patch.object(native, 'configuration', return_value=(config(), machines, {})),                 mock.patch.object(browser_kit, 'catalog_sources', side_effect=AssertionError('read the catalog first')):
            with self.assertRaisesRegex(ValueError, "change every machine's generated code.*: idle"):
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
    def test_one_cpu_extension_labelled_experimental(self):
        # One CPU extension, labelled experimental, always; it carries the
        # load governor (a switch: named, it is on) and the fast helpers at 0, off.
        self.assertEqual(sorted(p.name for p in (ROOT / 'extensions').iterdir() if p.is_dir()), ['cpu'])
        cpu = extension_at(ROOT / 'extensions' / 'cpu')
        # idle is deprecated: not shipped in cpu (docs/cpu-capabilities.md#idle), still in the registry.
        self.assertEqual((cpu.id, cpu.text['name'], cpu.capabilities, cpu.linked, cpu.settings),
                         ('cpu', 'CPU (experimental)',
                          ('boot_log_fix', 'block_trims', 'output_slack', 'load_governor', 'pitch_constants',
                           'fast_helpers'),
                          False, {'fast_helpers': 0}))
        for key in ('description', 'note'):
            self.assertIn('experimental', cpu.text[key].lower(), key)
            self.assertNotIn('idle', cpu.text[key].lower(), key)
        for words in ('Governor on', 'Fast helpers off, 1 on'):
            self.assertIn(words, cpu.text['note'])
        self.assertIn('fast helpers (off)', cpu.text['description'])
        self.assertNotIn('release_floor', cpu.capabilities)
        services = active_services(config(), native.service_users(Machines(cpu)))
        self.assertEqual(services, set(CPU) - {'idle'} | {'boot_log_fix', 'load_governor', 'pitch_constants', 'fast_helpers'})
        self.assertFalse({'send_fx', 'idle_cut'} & {step for _n, _m, step in native.plan(Machines(cpu))})

    def test_a_collection_may_select_a_subset_with_its_own_extension(self):
        # A collection's own settings extension in place of cpu: only what it names is on.
        subset = extension_at(folder(self, ('boot_log_fix', 'idle', 'block_trims')))
        services = active_services(config(), native.service_users(Machines(subset)))
        self.assertEqual(services & (set(CPU) | {'boot_log_fix', 'load_governor', 'fast_helpers', 'pitch_constants'}),
                         {'boot_log_fix', 'idle', 'block_trims'})

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


@unittest.skipUnless(off_path_code.toolchain(), 'No Blackfin toolchain')
class FastHelpersLink(unittest.TestCase):
    """What each build links as its divide and memory units: the plain helpers without fast_helpers or with its
    setting at 0, the fast files with 1, under the same object names and the same global symbols; the compile
    report names the fast sources only then. Nothing here pins the plain helpers' own bytes or sizes: a change to
    them that keeps their symbols passes."""
    def test_off_links_the_public_helpers_and_on_the_fast_ones(self):
        import re, subprocess, sys
        out = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, out)
        compiler = off_path_code.toolchain()
        objdump = next((compiler / 'bin').glob('bfin-elf-objdump*'))
        units = ('memory', 'divide')
        found = {}
        for case, setting in (('none', None), ('zero', 0), ('one', 1)):
            command = [sys.executable, str(ROOT / 'sdk.py'), 'compile', '--toolchain', str(compiler / 'bin'),
                       '--compiler', str(compiler), '--out', str(out / case), '--overwrite',
                       '--machine', str(ROOT / 'tests/fixtures/machines/probesynth')]
            if setting is not None:
                command += ['--extension', str(folder(self, ['fast_helpers'], settings={'fast_helpers': setting}))]
            run = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout[-2000:] + run.stderr[-2000:])

            def functions(path):
                table = subprocess.run([str(objdump), '-t', str(path)], capture_output=True, text=True).stdout
                return {name: int(size, 16) for size, name in
                        re.findall(r'\sg\s+F \.text\s+([0-9a-f]{8}) (\S+)$', table, re.M)}
            objects = {unit: functions(out / case / 'sdk' / (unit + '.o')) for unit in units}
            module = functions(out / case / 'module.elf')
            found[case] = (objects, module, document(out / case / 'compile-report.json').get('unit_sources'),
                           {unit: (out / case / 'sdk' / (unit + '.o')).read_bytes() for unit in units})
        # Off and setting 0: the very same objects, and no unit sources in the report; 1: the fast sources.
        self.assertEqual(found['none'][3], found['zero'][3], 'setting 0 links the very objects of a build without it')
        self.assertIsNone(found['none'][2])
        self.assertIsNone(found['zero'][2])
        self.assertEqual(found['one'][2], native.UNIT_SOURCES['fast_helpers'])
        for unit in units:
            with self.subTest(unit=unit):
                # On, each unit is another object with the same global functions; both define the helpers the
                # compiler calls.
                self.assertNotEqual(found['one'][3][unit], found['none'][3][unit])
                self.assertEqual(set(found['one'][0][unit]), set(found['none'][0][unit]))
        self.assertTrue({'_memset', '_memcpy'} <= set(found['none'][0]['memory']))
        self.assertIn('___udivsi3', found['none'][0]['divide'])
        # The module links the helpers from those objects: each one's size in the module is its object's.
        for case in found:
            objects, module = found[case][:2]
            for unit in units:
                for name, size in objects[unit].items():
                    self.assertEqual(module.get(name), size, f'{case}: {name}')


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
        names = set(CPU) | {'boot_log_fix', 'load_governor', 'pitch_constants'}
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
        owned = [s for chosen in native.UNIT_SOURCES.values() for s in chosen.values()]
        owned += [u for u, service in native.UNIT_SERVICES.items() if service in names]
        self.assertFalse([o for o in owned if Path(o).suffix == '.ld' or Path(o).name == Path(native.LINKER).name])


if __name__ == '__main__':
    unittest.main()
