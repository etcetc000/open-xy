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


class MachinePackTests(unittest.TestCase):
    def kit(self):
        return dict(version='0.0.2', sdk='c' * 64, definitions={}, fixed={}, machines=[dict(id='custom', kind='synth', origin='personal')],
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
        self.assertEqual(set(manifest), {'format', 'version', 'sdk', 'profileSha256', 'definitions', 'fixed', 'machines',
                                         'objects', 'notices'})
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

    def test_export_refuses_kinds_the_builder_cannot_import(self):
        for kind in ('drum', 'player'):
            kit = self.kit()
            kit['machines'][0]['kind'] = kind
            with tempfile.TemporaryDirectory() as directory:
                with self.assertRaisesRegex(ValueError, 'synths, effects and sample engines only'):
                    write(kit, directory)

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
