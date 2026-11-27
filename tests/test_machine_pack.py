"""Machine distribution archives never include the kit's stock inputs."""
import base64
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile
from types import SimpleNamespace

from tooling.browser_kit import notices
from tooling import machine_pack
from tooling.machine_pack import write
from tooling.formats import sha


# What shaped a kit's objects (tooling.native.shared_build), which its pack records.
BUILD = dict(services=['players'], defines=[], headers=[])


class MachinePackTests(unittest.TestCase):
    def kit(self):
        return dict(version='0.0.2', sdk='c' * 64, definitions={}, fixed={}, build=BUILD, machines=[dict(id='custom', kind='synth', origin='personal')],
                    objects=[dict(name='custom/voice.o', owner='custom'), dict(name='sdk/startup.o', owner=None)],
                    notices='Author license', profileSource=base64.b64encode(b'profile').decode(),
                    stockCatalog='must not ship', screenTemplate='must not ship', profile={'stock':'must not ship'})

    def test_archive_is_bounded_deterministic_and_readable_by_browser(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pack = write(self.kit(), root)
            with zipfile.ZipFile(pack) as archive:
                self.assertEqual(archive.namelist(), ['machines.json'])
                self.assertEqual(archive.getinfo('machines.json').compress_type, zipfile.ZIP_STORED)
                data = archive.read('machines.json')
                self.assertNotIn(b'must not ship', data)
                manifest = json.loads(data)
                self.assertEqual(manifest['profileSha256'], sha(b'profile'))
                self.assertEqual(manifest['sdk'], 'c' * 64)
                self.assertEqual(manifest['build'], BUILD)
                self.assertEqual(manifest['objects'], [dict(name='custom/voice.o', owner='custom')])
                self.assertEqual(manifest['notices'], 'Author license')
            other = root / 'other'
            other.mkdir()
            self.assertEqual(pack.read_bytes(), write(self.kit(), other).read_bytes())
            script = "import {readFileSync} from 'node:fs'; import {readMachinePack} from './builder/core.mjs'; " \
                     "console.log(readMachinePack(readFileSync(process.argv[1])).format);"
            result = subprocess.run(['node', '--input-type=module', '-e', script, str(pack)],
                                    cwd=Path(__file__).resolve().parents[1], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout.strip(), 'opxy-machine-pack/2')
            with self.assertRaises(FileExistsError):
                write(self.kit(), root)

    def test_site_machines_and_shared_objects_stay_with_the_site(self):
        # A pack holds only its own machines and their objects, records included: the site's
        # kit has the rest, whatever its catalog holds (builder/core.mjs importMachinePack).
        kit = self.kit()
        kit['machines'].insert(0, dict(id='site', origin='catalog', catalog='', controls=['A']))
        kit['objects'].insert(0, dict(name='site/voice.o', owner='site'))
        with tempfile.TemporaryDirectory() as directory:
            with zipfile.ZipFile(write(kit, directory, notices='Custom only')) as archive:
                manifest = json.loads(archive.read('machines.json'))
        self.assertEqual(manifest['machines'], [dict(id='custom', kind='synth', origin='personal')])
        self.assertEqual(manifest['objects'], [dict(name='custom/voice.o', owner='custom')])
        self.assertEqual(set(manifest), {'format', 'version', 'sdk', 'profileSha256', 'definitions', 'fixed', 'build',
                                         'machines', 'objects', 'notices'})
        self.assertEqual(manifest['notices'], 'Custom only')

    def test_archive_size_is_bounded(self):
        kit = self.kit()
        kit['notices'] = 'x' * 64
        with tempfile.TemporaryDirectory() as directory:
            limit = machine_pack.MAX_PACK_BYTES
            try:
                machine_pack.MAX_PACK_BYTES = 256
                with self.assertRaisesRegex(ValueError, '64 MiB'):
                    write(kit, directory)
            finally:
                machine_pack.MAX_PACK_BYTES = limit
            self.assertFalse((Path(directory) / 'machines.zip').exists())

    def test_export_takes_every_kind(self):
        # The exporter writes every kind's metadata as the kit lists it; the builder's import of
        # each kind is tested in tests/test_browser_kit.mjs.
        for kind in ('synth', 'drum', 'sample', 'effect', 'player', 'aux'):
            kit = self.kit()
            # An aux machine is a synth taking over an aux track, with that track's presets.
            kit['machines'][0].update(dict(kind=kind) if kind != 'aux' else dict(
                kind='synth', features=['auxTrack', 'trackControl'], catalog='',
                auxPresets={'content/aux_presets/cv/init.json': json.dumps(dict(platform='OP-XY', type='cv', version=1))}))
            with tempfile.TemporaryDirectory() as directory:
                with zipfile.ZipFile(write(kit, directory)) as archive:
                    machine = json.loads(archive.read('machines.json'))['machines'][0]
                    self.assertEqual(machine, kit['machines'][0])

    def test_export_requires_a_custom_machine(self):
        kit = self.kit()
        kit['machines'][0]['origin'] = 'catalog'
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'custom machine'):
                write(kit, directory)
            self.assertFalse((Path(directory) / 'machines.zip').exists())

    def test_pack_preserves_machine_and_vendor_license_notices(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'vendor').mkdir()
            (root / 'LICENSE').write_text('Machine author terms', encoding='utf-8')
            (root / 'vendor/LICENSE').write_text('Vendor attribution', encoding='utf-8')
            kit = self.kit()
            kit['notices'] = notices([SimpleNamespace(root=root, name='Custom')])
            with zipfile.ZipFile(write(kit, root)) as archive:
                text = json.loads(archive.read('machines.json'))['notices']
            self.assertIn('Machine author terms', text)
            self.assertIn('Vendor attribution', text)
            self.assertIn('OP-XY SDK modules', text)


class SharedBuildTests(unittest.TestCase):
    """A kit's build record (tooling.native.shared_build) is its machines' contributions together,
    each as tooling.native.machine_build and its services give it, which is what the builder
    compares a pack machine's with (builder/core.mjs importMachinePack)."""

    def test_a_build_is_the_union_of_its_machines(self):
        from machine_fixtures import fixture
        from tooling import native
        from tooling.firmware import active_services
        from tooling.pipeline import config
        profile = config()
        machines = [fixture(name) for name in ('probesynth', 'probefx', 'probesample', 'probepads', 'probekit23',
                                               'probekit25')]
        whole = native.shared_build(profile, machines)
        services = set().union(*(active_services(profile, native.service_users([m])) for m in machines))
        self.assertEqual(whole['services'], sorted(services))
        self.assertEqual(set(whole['defines']), {d for m in machines for d in native.machine_build(profile, m)['defines']})
        self.assertEqual(set(whole['defines']), {native.KIT_CONTROLS_DEFINE, native.MODEL_EDIT_DEFINE})
        self.assertEqual(whole['headers'], sorted({h for m in machines for h in native.machine_build(profile, m)['headers']}))
        self.assertIn('track_instrument', whole['services'])
        self.assertEqual(sorted(whole), ['defines', 'headers', 'services'])
        # Each machine names only what it adds: a player reading its track instrument a service
        # (its feature), no define; a plain synth nothing.
        self.assertEqual(native.machine_build(profile, fixture('probepads')), dict(defines=[], headers=[]))
        self.assertIn('trackInstrument', native.service_users([fixture('probepads')]))
        self.assertEqual(native.machine_build(profile, fixture('probesynth')), dict(defines=[], headers=[]))


def ci_synth_folder(folder):
    """The probesynth fixture as a synth reading the last control touched (opxy/control_interaction.hpp)."""
    import shutil
    from machine_fixtures import FOLDER
    shutil.copytree(FOLDER / 'probesynth', folder)
    for f in folder.iterdir():
        f.write_text(f.read_text(encoding='utf-8').replace('probesynth', 'probeci'), encoding='utf-8', newline='\n')
    definition = json.loads((folder / 'definition.json').read_text(encoding='utf-8'))
    definition.update(id='probeci', name='Probe CI')
    (folder / 'definition.json').write_text(json.dumps(definition), encoding='utf-8')
    machine = json.loads((folder / 'machine.json').read_text(encoding='utf-8'))
    machine['sdk'] = '1.20'  # the last control touched
    machine.pop('identity', None)
    (folder / 'machine.json').write_text(json.dumps(machine), encoding='utf-8')
    display = folder / 'display.cpp'
    display.write_text('#include <opxy/control_interaction.hpp>\n' + display.read_text(encoding='utf-8'),
                       encoding='utf-8', newline='\n')


class KitVariants(unittest.TestCase):
    """The rule the builder swaps a site kit's variants in by (builder/core.mjs variantBuild), as
    tooling.machine_pack.variant_build states it: the same on the shared cases, and a native build of
    the site's collection with the pack's machines (tooling.native.shared_build)."""

    def test_shared_cases(self):
        cases = json.loads((Path(__file__).parent / 'fixtures/kit_variants.json').read_text(encoding='utf-8'))['cases']
        self.assertGreaterEqual(len(cases), 10)
        for case in cases:
            with self.subTest(case['name']):
                self.assertEqual(machine_pack.variant_build(case['site'], case['carried'], case['machines']),
                                 case['expected'])

    def test_the_rule_gives_a_native_build_of_the_site_with_the_pack(self):
        from itertools import combinations
        from machine_fixtures import FOLDER, fixture
        from off_path_code import player_folder
        from tooling import native
        from tooling.firmware import active_services
        from tooling.machines import machine_at
        from tooling.pipeline import config
        profile = config()
        # The folders stay while the build records read their sources (includes_header).
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        directory = Path(temp.name)
        player_folder(directory / 'probeplayer')
        ci_synth_folder(directory / 'probeci')
        # A site with a synth, an older player and an older kit; pack machines adding each variant.
        site = [fixture('probesynth'), machine_at(directory / 'probeplayer'), fixture('probekit16')]
        pack = [fixture('probepads'), fixture('probekit23'), fixture('probekit21'), fixture('probekit25'),
                machine_at(directory / 'probeci')]
        self.assertEqual(native.machine_build(profile, pack[-1]), dict(defines=[], headers=['control_interaction']))
        carried = native.carried_variants(profile, site)
        self.assertEqual(carried, dict(services=['amp_envelope', 'kit_controls', 'played_pad', 'track_instrument'],
                                       defines=list(native.DEFINE_ORDER), headers=['control_interaction']))
        site_build = native.shared_build(profile, site)
        checked = 0
        for count in range(len(pack) + 1):
            for chosen in combinations(pack, count):
                union = site + list(chosen)
                needs = [dict(kind=m.kind, services=sorted(active_services(profile, native.service_users([m]))),
                              **native.machine_build(profile, m)) for m in chosen]
                result = machine_pack.variant_build(site_build, carried, needs)
                self.assertEqual(result, dict(target=native.shared_build(profile, union)), [m.id for m in chosen])
                checked += 1
        self.assertEqual(checked, 32)
        # Without them a site carries nothing for them: no kit, no player, a header its machines include.
        self.assertEqual(native.carried_variants(profile, [fixture('probesynth')]),
                         dict(services=[], defines=[], headers=['control_interaction']))
        self.assertEqual(native.carried_variants(profile, [pack[-1], fixture('probekit23')]),
                         dict(services=[], defines=[native.MODEL_EDIT_DEFINE], headers=[]))
        # A site whose kit has the drum-kit controls without an amp envelope carries the envelope alone.
        import shutil
        shutil.copytree(FOLDER / 'probekit23', directory / 'probekit23')
        definition = json.loads((directory / 'probekit23/definition.json').read_text(encoding='utf-8'))
        del definition['resources']['ampEnvelope']
        (directory / 'probekit23/definition.json').write_text(json.dumps(definition), encoding='utf-8')
        controls = machine_at(directory / 'probekit23')
        self.assertEqual(native.carried_variants(profile, [controls]),
                         dict(services=['amp_envelope'], defines=[native.MODEL_EDIT_DEFINE], headers=['control_interaction']))
        amp = dict(kind='drum', services=sorted(active_services(profile, native.service_users([fixture('probekit23')]))),
                   **native.machine_build(profile, fixture('probekit23')))
        site_build = native.shared_build(profile, [controls])
        self.assertEqual(machine_pack.variant_build(site_build, native.carried_variants(profile, [controls]), [amp]),
                         dict(target=native.shared_build(profile, [controls, fixture('probekit23')])))
