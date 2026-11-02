"""MTP over USB (macOS and Linux): the protocol against an in-memory responder, and descriptor parsing."""
import contextlib
import io
from pathlib import Path
import shutil
import struct
import tempfile
import unittest

from tooling import device as transfer
from tooling import usbmtp
from tooling.usbmtp import (COMMAND, DATA, RESPONSE, Device, HEADER, container, file_transfer_interface, object_info,
                            ptp_string, read_string, read_words, words)

STORAGE = 0x10001


class Responder:
    """A transport that answers MTP containers from a tree of folders and files (handle: (parent, name, bytes or None))."""
    packet = 512

    def __init__(self, files=()):
        self.objects, self.next, self.pending, self.reply = {}, 0, None, []
        self.closed = self.cancelled = False
        self.fail = {}  # operation code: response code
        self.stall = None  # an operation code whose data phase raises
        for path, data in dict(files).items():
            parent = usbmtp.ROOT
            *folders, name = path.split('/')
            for folder in folders:
                parent = self.find(parent, folder) or self.add(parent, folder, None)
            self.add(parent, name, data)

    def add(self, parent, name, data):
        self.next += 1
        self.objects[self.next] = (parent, name, data)
        return self.next

    def find(self, parent, name):
        return next((h for h, (p, n, _) in self.objects.items() if p == parent and n == name), None)

    def write(self, data):
        if self.pending is None:
            if not data:
                return
            self.pending = [data, 0]
            self.pending[1] = HEADER.unpack_from(data)[0]
        else:
            self.pending[0] += data
        if len(self.pending[0]) >= self.pending[1]:
            data, self.pending = self.pending[0], None
            self.receive(data)

    def receive(self, data):
        length, kind, code, transaction = HEADER.unpack_from(data)
        payload = data[12:length]
        if kind == COMMAND:
            self.command = (code, transaction, list(struct.unpack(f'<{len(payload) // 4}I', payload)))
            if code in (usbmtp.SEND_OBJECT,):
                return  # The data phase follows.
            if code == usbmtp.SEND_OBJECT_INFO:
                return
            self.answer(*self.command, None)
        elif kind == DATA:
            code, transaction, params = self.command
            self.answer(code, transaction, params, payload)

    def answer(self, code, transaction, params, payload):
        reply, data, out = usbmtp.OK, None, []
        if code in self.fail:
            reply = self.fail[code]
        elif code == usbmtp.OPEN_SESSION:
            pass
        elif code == usbmtp.STORAGE_IDS:
            data = words([1, STORAGE])
        elif code == usbmtp.OBJECT_HANDLES:
            handles = [h for h, (p, _, _) in self.objects.items() if p == params[2]]
            data = words([len(handles), *handles])
        elif code == usbmtp.OBJECT_INFO:
            parent, name, body = self.objects[params[0]]
            data = object_info(STORAGE, name, len(body or b''), parent, body is None)
        elif code == usbmtp.GET_OBJECT:
            data = self.objects[params[0]][2]
        elif code == usbmtp.DELETE_OBJECT:
            del self.objects[params[0]]
        elif code == usbmtp.SEND_OBJECT_INFO:
            info = self.info
            name = read_string(info, 52)
            folder = struct.unpack_from('<H', info, 4)[0] == usbmtp.FOLDER_FORMAT
            self.created = self.add(params[1], name, None if folder else b'')
            out = [STORAGE, params[1], self.created]
        elif code == usbmtp.SEND_OBJECT:
            parent, name, _ = self.objects[self.created]
            self.objects[self.created] = (parent, name, bytes(payload))
        if data is not None:
            self.reply.append(container(DATA, code, transaction, data))
        self.reply.append(container(RESPONSE, reply, transaction, words(out)))

    def read(self):
        return self.reply.pop(0)

    def cancel(self, transaction):
        self.cancelled = True

    def close(self):
        self.closed = True


class InfoCapture(Responder):
    """SendObjectInfo's data phase is the ObjectInfo itself; its response comes after it."""

    def receive(self, data):
        length, kind, code, transaction = HEADER.unpack_from(data)
        if kind == DATA and self.command[0] == usbmtp.SEND_OBJECT_INFO:
            self.info = data[12:length]
            self.answer(self.command[0], transaction, self.command[2], None)
        else:
            super().receive(data)


class UsbMtpTests(unittest.TestCase):
    def device(self, files=()):
        self.transport = InfoCapture(files)
        device = Device(self.transport)
        self.addCleanup(device.close)
        return device

    def test_strings_round_trip_and_objects_are_laid_out_as_ptp_requires(self):
        self.assertEqual(ptp_string('a'), b'\x02a\x00\x00\x00')
        self.assertEqual(ptp_string(''), b'\x00')
        self.assertEqual(read_string(ptp_string('os.ldr'), 0), 'os.ldr')
        info = object_info(7, 'x', 5, 0xffffffff)
        self.assertEqual(struct.unpack_from('<IHxxI', info), (7, usbmtp.UNDEFINED_FORMAT, 5))
        self.assertEqual(struct.unpack_from('<I', info, 38), (0xffffffff,))
        self.assertEqual(struct.unpack_from('<HH', object_info(7, 'x', 0, 3, folder=True), 4 + 0)[0], usbmtp.FOLDER_FORMAT)
        self.assertEqual(read_words(words([2, 7, 8])), [7, 8])
        with self.assertRaisesRegex(ValueError, 'too long'):
            ptp_string('x' * 255)

    def test_the_file_transfer_interface_is_found_in_a_configuration_descriptor(self):
        config = bytes([9, 2, 0, 0, 1, 1, 0, 0x80, 50])
        interface = lambda number, klass: bytes([9, 4, number, 0, 3, klass, 1, 1, 0])
        endpoint = lambda address, kind, packet: bytes([7, 5, address, kind, packet & 255, packet >> 8, 0])
        mtp = interface(1, 0xff) + endpoint(0x81, 2, 512) + endpoint(0x01, 2, 512) + endpoint(0x82, 3, 28)
        self.assertEqual(file_transfer_interface(config + interface(0, 8) + endpoint(0x83, 2, 64) + mtp), (1, 0x81, 0x01, 512))
        self.assertIsNone(file_transfer_interface(config + interface(0, 8) + endpoint(0x83, 2, 64)))
        self.assertIsNone(file_transfer_interface(config + interface(1, 0xff) + endpoint(0x81, 3, 512)))
        self.assertIsNone(file_transfer_interface(b''))

    def test_listing_reading_and_the_root(self):
        device = self.device({'os.ldr': b'abc', 'content/a.json': b'{}'})
        self.assertEqual(device.root(), usbmtp.ROOT)
        top = {x.name: x for x in device.list(device.root())}
        self.assertEqual({k: (v.folder, v.size) for k, v in top.items()}, {'os.ldr': (False, 3), 'content': (True, 0)})
        self.assertEqual(device.read(top['os.ldr']), b'abc')
        self.assertEqual([x.name for x in device.list(top['content'].id)], ['a.json'])

    def test_creating_replacing_and_deleting(self):
        device = self.device({'os.ldr': b'old'})
        root = device.root()
        device.create_folder(root, 'content')
        folder = next(x for x in device.list(root) if x.name == 'content')
        self.assertTrue(folder.folder)
        data = bytes(range(256)) * 4  # A whole number of packets: needs the zero-length packet.
        device.create_file(folder.id, 'big.bin', data)
        written = next(x for x in device.list(folder.id) if x.name == 'big.bin')
        self.assertEqual(device.read(written), data)
        device.delete(next(x for x in device.list(root) if x.name == 'os.ldr'))
        self.assertEqual([x.name for x in device.list(root)], ['content'])
        with self.assertRaisesRegex(ValueError, 'Refusing to delete the folder'):
            device.delete(folder)

    def test_a_refusal_names_the_problem_and_leaves_the_session_usable(self):
        device = self.device()
        self.transport.fail[usbmtp.SEND_OBJECT_INFO] = 0x200c
        with self.assertRaisesRegex(ValueError, 'is full'):
            device.create_file(device.root(), 'x.bin', b'1')
        self.transport.fail[usbmtp.SEND_OBJECT_INFO] = 0x2002
        with self.assertRaisesRegex(ValueError, r"couldn't accept x.bin \(MTP error 0x2002\)"):
            device.create_file(device.root(), 'x.bin', b'1')
        self.transport.fail.clear()
        device.create_file(device.root(), 'x.bin', b'1')
        self.assertEqual([x.name for x in device.list(device.root())], ['x.bin'])

    def test_a_failed_transfer_cancels_and_abandons_the_session(self):
        device = self.device({'a': b'1'})
        self.transport.read = lambda: (_ for _ in ()).throw(ValueError('stalled'))
        with self.assertRaisesRegex(ValueError, 'stalled'):
            device.list(device.root())
        self.assertTrue(self.transport.cancelled)
        device.close()
        self.assertTrue(self.transport.closed)

    def test_a_short_or_oversized_reply_is_refused(self):
        device = self.device({'a': b'1234'})
        entry = device.list(device.root())[0]
        entry.size = 2
        with self.assertRaisesRegex(ValueError, 'transfer size'):
            device.read(entry)

    def test_push_runs_over_the_protocol_end_to_end(self):
        temp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, temp, ignore_errors=True)
        self.enterContext(contextlib.redirect_stdout(io.StringIO()))
        source = temp / 'candidate'
        (source / 'content/display').mkdir(parents=True)
        (source / 'os.ldr').write_bytes(b'new os')
        (source / 'content/display/mine.svg').write_bytes(b'<svg/>')
        device = self.device({'os.ldr': b'stock', 'content/other.json': b'{}'})
        report = transfer.push(device, {('os.ldr',): b'new os', ('content', 'display', 'mine.svg'): b'<svg/>'}, temp / 'record')
        self.assertTrue(report['complete'])
        names = {(self.transport.objects[h][1], self.transport.objects[h][2]) for h in self.transport.objects}
        self.assertTrue({('os.ldr', b'new os'), ('mine.svg', b'<svg/>'), ('other.json', b'{}')} <= names)
        self.assertEqual((temp / 'record/before/os.ldr').read_bytes(), b'stock')

    def test_device_selection_depends_on_the_platform(self):
        from unittest import mock
        with mock.patch.object(transfer.sys, 'platform', 'darwin'), mock.patch.object(usbmtp, 'Usb') as usb:
            usb.return_value = InfoCapture()
            self.assertIsInstance(transfer.open_device(), Device)


if __name__ == '__main__':
    unittest.main()
