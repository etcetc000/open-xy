"""Web manager refuses stale updates and shares the checked device transaction."""
import base64
import contextlib
import http.client
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch
from unittest.mock import Mock
from types import SimpleNamespace

from tooling import collection_manifest as inventory, manager, device
from tooling.collection_kernel import checked_json
from tooling.formats import sha
import test_collection_install as install_fixture


class Inventory(unittest.TestCase):
    def sample(self):
        return dict(format=inventory.FORMAT, kernel='ab' * 32, digest='cd' * 32,
                    machines=[dict(id='keep', name='Keep', kind='synth', identity=dict(engine=34, page=82),
                                   source=dict(type='local'))], extensions=['capture'],
                    layout=dict(keep=dict(engine=34, page=82), removed=dict(effect=46, page=21)))

    def test_inventory_checks_checksum_collection_binding_and_tombstones(self):
        body = self.sample()
        raw = checked_json(body)
        self.assertEqual(inventory.decode(raw, digest=body['digest']), body)
        with self.assertRaises(ValueError):
            inventory.decode(raw, digest='ef' * 32)
        with self.assertRaises(ValueError):
            inventory.decode(raw[:-2] + b'X\n')
        body['machines'][0]['identity']['engine'] = 35
        with self.assertRaises(ValueError):
            inventory.decode(checked_json(body))


class InventoryTransfer(unittest.TestCase):
    def setUp(self):
        self.fixture = install_fixture.CollectionInstall()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.f = self.fixture

    def metadata(self):
        description = json.loads((self.f.bundle / 'collection.json').read_text())
        identity = dict(engine=34, page=82)
        return inventory.encode(description, dict(keep=identity),
                                [dict(id='keep', name='Keep', kind='synth', identity=identity, source={})], [])

    def test_inventory_is_immutable_staged_verified_and_selected_last(self):
        raw = self.metadata()
        result = device.install_collection(self.f.device, self.f.bundle, self.f.root / 'with-inventory', inventory=raw)
        self.assertEqual(self.f.device.writes[-1], 'activate.oxc')
        self.assertEqual(self.f.device.get('/projects/openxy/' + inventory.filename(result['digest'])), raw)
        self.assertEqual(self.f.device.get('/os.ldr'), b'OS')
        self.assertEqual(self.f.device.get('/projects/song.xy'), b'music')

    def test_invalid_inventory_never_writes_and_inventory_failure_never_selects(self):
        with self.assertRaises(ValueError):
            device.install_collection(self.f.device, self.f.bundle, self.f.root / 'bad', inventory=b'{}')
        self.assertFalse(self.f.device.writes)
        self.f.device.fail_on = inventory.filename(self.f.image[112:144].hex())
        with self.assertRaises(ValueError):
            device.install_collection(self.f.device, self.f.bundle, self.f.root / 'fail', inventory=self.metadata())
        self.assertIsNone(self.f.device.get('/projects/openxy/activate.oxc'))

    def manager(self):
        m = manager.Manager.__new__(manager.Manager)
        m.connect = lambda: self.f.device
        m.known = {self.f.image[112:144].hex(): str(self.f.bundle)}
        m.describe_bundle = lambda _: inventory.decode(self.metadata())
        m.current = None
        m.home = self.f.root
        m.state_path = self.f.root / 'state.json'
        m.prepared = {}
        m.offline = False
        m.releases = SimpleNamespace(selected=Mock(return_value={}))
        self.f.install()
        from tooling import collection_activation as activation
        self.f.replace_record('attempt', activation.control(phase=3, generation=2, slot=0,
            digest=self.f.image[112:144], platform=self.f.platform, abi=self.f.abi))
        self.f.device.writes.clear()
        return m

    def test_detection_reads_actual_slot_and_never_writes_even_without_legacy_inventory(self):
        m = self.manager()
        state = m.read_status()
        self.assertTrue(state['can_manage'])
        self.assertEqual(state['inventory']['machines'][0]['id'], 'keep')
        self.assertFalse(self.f.device.writes)
        self.f.replace_record('slot0', self.f.image[:-1] + bytes([self.f.image[-1] ^ 1]))
        with self.assertRaisesRegex(ValueError, 'differs from its saved build'):
            m.read_status()
        self.assertFalse(self.f.device.writes)

    def test_apply_uses_checked_installer_consumes_plan_and_leaves_boot_and_projects_untouched(self):
        m = self.manager()
        before = m.read_status()
        description = json.loads((self.f.bundle / 'collection.json').read_text())
        m.prepared['reviewed'] = dict(before=before, bundle=str(self.f.bundle),
                                     description=description, inventory=self.metadata())
        result = m.apply(dict(plan='reviewed'))
        self.assertEqual(result['generation'], 3)
        self.assertEqual(self.f.device.writes[-1], 'activate.oxc')
        self.assertFalse(m.prepared)
        self.assertEqual(self.f.device.get('/os.ldr'), b'OS')
        self.assertEqual(self.f.device.get('/projects/song.xy'), b'music')
        self.assertEqual(self.f.device.get('/content/display/keep.svg'), b'unchanged')


class ManagerTests(unittest.TestCase):
    def bare(self, folder):
        m = manager.Manager.__new__(manager.Manager)
        m.home = folder
        m.local = {}
        m.prepared = {}
        m.current = None
        m.jobs = {}
        m.lock = threading.Lock()
        m.offline = False
        m.releases = SimpleNamespace(active=None, version=(1, 9), services={'samples', 'drums', 'players'},
                                    selected=Mock(return_value={}))
        return m

    def test_source_import_accepts_multiple_machines_and_refuses_traversal_duplicates(self):
        with tempfile.TemporaryDirectory() as temporary:
            m = self.bare(Path(temporary))
            files = []
            for name in ('one', 'two'):
                for filename, value in [('machine.json', dict(kind='effect')), ('definition.json', dict(id=name, name=name))]:
                    files.append(dict(path=f'collection/{name}/{filename}', data=base64.b64encode(json.dumps(value).encode()).decode()))
            result = m.import_folder(dict(files=files))
            self.assertEqual([x['id'] for x in result['machines']], ['one', 'two'])
            for path in ('../escape', '/absolute', 'x/../bad', 'a\\b', 'C:/file', './file', 'a//b'):
                with self.assertRaises(ValueError, msg=path):
                    m.import_folder(dict(files=[dict(path=path, data='YQ==')]))
            with self.assertRaises(ValueError):
                m.import_folder(dict(files=[dict(path='x', data='YQ=='), dict(path='X', data='Yg==')]))
            self.assertEqual(set(m.local), {'one', 'two'})

    def test_snapshot_refuses_links_instead_of_reading_outside_machine(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'source'; source.mkdir()
            (source / 'link').symlink_to(root / 'secret')
            m = self.bare(root)
            with self.assertRaises(ValueError):
                m._snapshot(source, root / 'snapshot')

    def test_prepare_rejects_absent_unknown_duplicate_and_replacement_ids_before_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            m = self.bare(Path(temporary))
            m.current = dict(can_manage=True, inventory=dict(machines=[dict(id='keep')]))
            m.entries = dict(keep={}, add={})
            for payload in [dict(remove=['absent']), dict(add=['unknown']), dict(add=['add', 'add']),
                            dict(add=['keep']), dict(remove=['keep']), dict()]:
                with self.assertRaises(ValueError):
                    m.prepare(payload)

    def test_stale_device_refuses_apply_before_any_push(self):
        with tempfile.TemporaryDirectory() as temporary:
            m = self.bare(Path(temporary))
            m.state_path = m.home / 'state.json'
            m.known = {}
            before = dict(kernel='kernel', selection=dict(generation=7), attempt=dict(state='confirmed'))
            m.prepared['plan'] = dict(before=before, bundle=str(m.home / 'bundle'), description=dict(digest='abc'))
            m.connect = lambda: contextlib.nullcontext(object())
            with patch.object(device, 'collection_status', return_value={**before, 'selection':dict(generation=8)}), \
                    patch.object(device, 'install_collection') as install:
                with self.assertRaisesRegex(ValueError, 'changed after detection'):
                    m.apply(dict(plan='plan'))
                install.assert_not_called()

    def test_connector_requires_pairing_and_origin_and_exposes_no_flash_or_path_api(self):
        with tempfile.TemporaryDirectory() as temporary:
            m = self.bare(Path(temporary)); m.entries = {}
            server = manager.ThreadingHTTPServer(('127.0.0.1', 0), manager.handler(m, 'secret', None))
            thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
            try:
                connection = http.client.HTTPConnection('127.0.0.1', server.server_port)
                def request(method, path, headers=None, body=None):
                    connection.request(method, path, body=body, headers=headers or {})
                    response = connection.getresponse(); raw = response.read()
                    return response.status, json.loads(raw)
                self.assertEqual(request('GET', '/api/manager/catalog')[0], 400)
                headers = dict(Authorization='Bearer secret', Origin='https://evil.example')
                self.assertEqual(request('GET', '/api/manager/catalog', headers)[0], 400)
                headers['Origin'] = 'https://open-xy.com'
                self.assertEqual(request('GET', '/api/manager/catalog', headers), (200, dict(machines=[], channel=None, offline=False)))
                headers['Content-Type'] = 'application/json'
                for action in ('flash', 'push', 'shell', 'read-file'):
                    self.assertEqual(request('POST', '/api/manager/' + action, headers, '{}')[0], 400)
                self.assertEqual(request('GET', '/../../sdk.py', headers)[0], 400)
                connection.close()
            finally:
                server.shutdown(); server.server_close(); thread.join()
