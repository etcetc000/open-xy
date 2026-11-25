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
BUILD = dict(services=['players'], defines=[], headers=[], unitDefines=dict(player=[]))


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
        # The builder imports each kind a native build makes (builder/core.mjs importMachinePack).
        for kind in ('synth', 'drum', 'sample', 'effect', 'player', 'aux'):
            kit = self.kit()
            # An aux machine is a synth taking over an aux track, with that track's presets.
            kit['machines'][0].update(dict(kind=kind) if kind != 'aux' else dict(
                kind='synth', features=['auxTrack', 'trackControl'], catalog='',
                auxPresets={'content/aux_presets/cv/init.json': '{}'}))
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
        self.assertEqual(set(whole['defines']), {native.KIT_CONTROLS_DEFINE, native.PLAYER_INSTRUMENT_DEFINE,
                                                 native.MODEL_EDIT_DEFINE})
        self.assertEqual(whole['headers'], sorted({h for m in machines for h in native.machine_build(profile, m)['headers']}))
        self.assertEqual(whole['unitDefines'], {'player': [native.PLAYER_INSTRUMENT_DEFINE]})
        # Each machine names only what it adds: a 1.25 player its define, a plain synth nothing.
        self.assertEqual(native.machine_build(profile, fixture('probepads')),
                         dict(defines=[native.PLAYER_INSTRUMENT_DEFINE], headers=[]))
        self.assertEqual(native.machine_build(profile, fixture('probesynth')), dict(defines=[], headers=[]))
        # A build without such machines compiles every unit as before.
        self.assertEqual(native.shared_build(profile, [fixture('probesynth')])['unitDefines'], {'player': []})
