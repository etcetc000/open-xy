"""Machine distribution archives never include the kit's stock inputs."""
import base64
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

from tooling.machine_pack import write
from tooling.formats import sha


class MachinePackTests(unittest.TestCase):
    def kit(self):
        return dict(version='0.0.2', definitions={}, fixed={}, machines=[dict(id='custom', origin='personal')],
                    objects=[], notices='Author license', profileSource=base64.b64encode(b'profile').decode(),
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
                self.assertEqual(manifest['notices'], 'Author license')
            other = root / 'other'
            other.mkdir()
            self.assertEqual(pack.read_bytes(), write(self.kit(), other).read_bytes())
            script = "import {readFileSync} from 'node:fs'; import {readMachinePack} from './builder/core.mjs'; " \
                     "console.log(readMachinePack(readFileSync(process.argv[1])).format);"
            result = subprocess.run(['node', '--input-type=module', '-e', script, str(pack)],
                                    cwd=Path(__file__).resolve().parents[1], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout.strip(), 'opxy-machine-pack/1')
            with self.assertRaises(FileExistsError):
                write(self.kit(), root)

    def test_export_requires_a_custom_machine(self):
        kit = self.kit()
        kit['machines'][0]['origin'] = 'catalog'
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'custom machine'):
                write(kit, directory)
            self.assertFalse((Path(directory) / 'machines.zip').exists())
