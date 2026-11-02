"""MTP over USB to an OP-XY in "mtp file transfer (mac)" mode, for macOS and Linux.

Windows binds its own MTP driver, so there tooling/wpd.py goes through that
driver instead. Here the standard Media Transfer Protocol runs directly over
libusb, loaded with ctypes: no Python package, only the system library
(`brew install libusb` on macOS, `apt install libusb-1.0-0` on Debian and Ubuntu).
It offers the same operations as wpd.Device and no others: list, read, create
and delete one file, create a folder. No vendor commands, no eject and no
firmware write. The protocol follows builder/mtp.mjs, the browser's WebUSB transfer.
"""
import contextlib
import ctypes as ct
import ctypes.util
import struct
import sys
import time

from .formats import require
from .wpd import Entry

VENDOR = 0x2367
# Vendor-specific, the class "mac" mode uses so that macOS leaves the device alone;
# Still Image is the standard MTP class.
CLASSES = (0xff, 6)
ROOT = 0xffffffff
COMMAND, DATA, RESPONSE = 1, 2, 3
OK, SESSION_ALREADY_OPEN = 0x2001, 0x201e
OPEN_SESSION, CLOSE_SESSION, STORAGE_IDS, OBJECT_HANDLES = 0x1002, 0x1003, 0x1004, 0x1007
OBJECT_INFO, GET_OBJECT, DELETE_OBJECT, SEND_OBJECT_INFO, SEND_OBJECT = 0x1008, 0x1009, 0x100b, 0x100c, 0x100d
UNDEFINED_FORMAT, FOLDER_FORMAT = 0x3000, 0x3001
CHUNK = 1 << 20  # A multiple of every bulk packet size, so only the last transfer of a container can be short.
TIMEOUT_MS = 20000
HEADER = struct.Struct('<IHHI')
LIBUSB_TIMEOUT, LIBUSB_PIPE = -7, -9
LIBUSB_ERRORS = {-1: 'input/output error', -3: 'access denied', -4: 'no such device', -5: 'not found', -6: 'busy',
                 -7: 'timed out', -8: 'overflow', -9: 'stalled', -10: 'interrupted', -12: 'not supported'}
REFUSALS = {0x200c: 'Your OP-XY is full. Free some space, then try again.',
            0x200d: "Your OP-XY's storage is read-only.",
            0x200f: 'Your OP-XY refused access to that file.'}
BUSY = ('Another app is using your OP-XY. Quit Field Kit, OpenMTP, Android File Transfer, a browser tab '
        'using the builder or any other MTP tool, then try again')
STOPPED = 'Your OP-XY stopped responding. Reconnect it, then try again'
NOT_IN_MODE = ('Expected one OP-XY in file-transfer mode, found none: choose "mtp file transfer (mac)" '
               'on the device and close other apps using it')
LIBRARY_NAMES = ('libusb-1.0.so.0', 'libusb-1.0.dylib', '/opt/homebrew/lib/libusb-1.0.dylib',
                 '/usr/local/lib/libusb-1.0.dylib', '/opt/local/lib/libusb-1.0.dylib')


def load_libusb():
    for name in (ctypes.util.find_library('usb-1.0'), *LIBRARY_NAMES):
        if name:
            with contextlib.suppress(OSError):
                return ct.CDLL(name)
    hint = 'brew install libusb' if sys.platform == 'darwin' else 'install libusb-1.0 (Debian/Ubuntu: apt install libusb-1.0-0)'
    raise ValueError('Device transfer needs the libusb library: ' + hint)


def words(values):
    return struct.pack(f'<{len(values)}I', *values)


def container(kind, code, transaction, payload=b''):
    return HEADER.pack(12 + len(payload), kind, code, transaction) + payload


def ptp_string(value):
    """A PTP string: a UTF-16 code-unit count including the terminator, then UTF-16LE."""
    text = (value + '\0').encode('utf-16-le') if value else b''
    require(len(text) // 2 <= 255, f'{value} is too long a name for your OP-XY')
    return bytes([len(text) // 2]) + text


def read_string(data, at):
    count = data[at]
    require(at + 1 + 2 * count <= len(data), 'Unexpected file information from your OP-XY')
    return data[at + 1:at + 1 + 2 * count].decode('utf-16-le').rstrip('\0')


def object_info(storage, name, size, parent, folder=False):
    """An ObjectInfo dataset; a folder is a generic association (type 1) with no data."""
    fixed = bytearray(52)
    struct.pack_into('<IHxxI', fixed, 0, storage, FOLDER_FORMAT if folder else UNDEFINED_FORMAT, size)
    struct.pack_into('<I', fixed, 38, parent)
    if folder:
        struct.pack_into('<H', fixed, 42, 1)
    return bytes(fixed) + ptp_string(name) + ptp_string('') * 3


def read_words(data):
    require(data is not None and len(data) >= 4, 'Unexpected reply from your OP-XY')
    count = struct.unpack_from('<I', data)[0]
    require(len(data) >= 4 + 4 * count, 'Unexpected reply from your OP-XY')
    return list(struct.unpack_from(f'<{count}I', data, 4))


def file_transfer_interface(descriptor):
    """(interface number, bulk in endpoint, bulk out endpoint, packet size) of the first
    MTP-class interface in a raw configuration descriptor, or None."""
    interfaces, at = [], 0
    while at + 2 <= len(descriptor) and descriptor[at] >= 2:
        length, kind = descriptor[at], descriptor[at + 1]
        if kind == 4 and length >= 9:
            interfaces.append([descriptor[at + 2], descriptor[at + 3], descriptor[at + 5], []])
        elif kind == 5 and length >= 7 and interfaces:
            interfaces[-1][3].append((descriptor[at + 2], descriptor[at + 3] & 3, struct.unpack_from('<H', descriptor, at + 4)[0]))
        at += length
    for number, alternate, klass, endpoints in interfaces:
        bulk = [(address, packet) for address, kind, packet in endpoints if kind == 2]
        inbound = [x for x in bulk if x[0] & 0x80]
        outbound = [x for x in bulk if not x[0] & 0x80]
        if alternate == 0 and klass in CLASSES and inbound and outbound:
            return number, inbound[0][0], outbound[0][0], outbound[0][1]
    return None


class UsbError(ValueError):
    def __init__(self, action, code):
        super().__init__(f'{action} failed: {LIBUSB_ERRORS.get(code, f"libusb error {code}")}')
        self.code = code


class Usb:
    """The one connected OP-XY's file-transfer interface, claimed through libusb."""

    def __init__(self):
        self.handle = self.interface = self.context = None
        self.lib = load_libusb()
        lib, void, pointer = self.lib, ct.c_void_p, ct.POINTER
        lib.libusb_init.argtypes = [pointer(void)]
        lib.libusb_get_device_list.argtypes = [void, pointer(pointer(void))]
        lib.libusb_get_device_list.restype = ct.c_ssize_t
        lib.libusb_free_device_list.argtypes = [pointer(void), ct.c_int]
        lib.libusb_free_device_list.restype = None
        lib.libusb_get_device_descriptor.argtypes = [void, void]
        lib.libusb_open.argtypes = [void, pointer(void)]
        lib.libusb_close.argtypes = [void]
        lib.libusb_close.restype = None
        lib.libusb_exit.argtypes = [void]
        lib.libusb_exit.restype = None
        lib.libusb_get_configuration.argtypes = [void, pointer(ct.c_int)]
        lib.libusb_set_configuration.argtypes = [void, ct.c_int]
        lib.libusb_set_auto_detach_kernel_driver.argtypes = [void, ct.c_int]
        lib.libusb_claim_interface.argtypes = lib.libusb_release_interface.argtypes = [void, ct.c_int]
        lib.libusb_clear_halt.argtypes = [void, ct.c_ubyte]
        lib.libusb_bulk_transfer.argtypes = [void, ct.c_ubyte, void, ct.c_int, pointer(ct.c_int), ct.c_uint]
        lib.libusb_control_transfer.argtypes = [void, ct.c_ubyte, ct.c_ubyte, ct.c_ushort, ct.c_ushort,
                                                void, ct.c_ushort, ct.c_uint]
        self.buffer = (ct.c_ubyte * CHUNK)()
        context = ct.c_void_p()
        self._check(lib.libusb_init(ct.byref(context)), 'Starting libusb')
        self.context = context
        try:
            self._open()
        except BaseException:
            self.close()
            raise

    @staticmethod
    def _check(code, action):
        if code < 0:
            raise UsbError(action, code)
        return code

    def _control(self, handle, request_type, request, value, index, length=0, data=b'', timeout=5000):
        buffer = ct.create_string_buffer(data, max(length, len(data)))
        count = self.lib.libusb_control_transfer(handle, request_type, request, value, index, buffer,
                                                 max(length, len(data)), timeout)
        self._check(count, 'A USB control request')
        return buffer.raw[:count]

    def _layout(self, handle):
        """The file-transfer interface of an open device, selecting its configuration if none is."""
        head = self._control(handle, 0x80, 6, 0x0200, 0, 9)
        descriptor = self._control(handle, 0x80, 6, 0x0200, 0, struct.unpack_from('<H', head, 2)[0])
        active = ct.c_int()
        self._check(self.lib.libusb_get_configuration(handle, ct.byref(active)), 'Reading the USB configuration')
        if active.value == 0:
            self._check(self.lib.libusb_set_configuration(handle, descriptor[5]), 'Selecting the USB configuration')
        return file_transfer_interface(descriptor)

    def _open(self):
        lib = self.lib
        listing = ct.POINTER(ct.c_void_p)()
        count = self._check(lib.libusb_get_device_list(self.context, ct.byref(listing)), 'Listing USB devices')
        found, opened = [], []
        try:
            for index in range(count):
                descriptor = ct.create_string_buffer(18)
                if lib.libusb_get_device_descriptor(listing[index], descriptor) != 0:
                    continue
                if struct.unpack_from('<H', descriptor.raw, 8)[0] != VENDOR:
                    continue
                handle = ct.c_void_p()
                if lib.libusb_open(listing[index], ct.byref(handle)) != 0:
                    continue  # Another app, or the system, holds it.
                opened.append(handle)
                if (layout := self._layout(handle)) is not None:
                    found.append((handle, layout))
            require(found, NOT_IN_MODE)
            require(len(found) == 1, f'Expected one OP-XY in file-transfer mode, found {len(found)}: disconnect the others')
        finally:
            lib.libusb_free_device_list(listing, 1)
            for handle in opened:
                if len(found) != 1 or handle is not found[0][0]:
                    lib.libusb_close(handle)
        self.handle, (self.number, self.inbound, self.outbound, self.packet) = found[0]
        lib.libusb_set_auto_detach_kernel_driver(self.handle, 1)  # Linux only; elsewhere unsupported and harmless.
        code = lib.libusb_claim_interface(self.handle, self.number)
        if code in (-3, -6):
            raise ValueError(BUSY)
        self._check(code, 'Claiming the file-transfer interface')
        self.interface = self.number

    def _bulk(self, endpoint, length, action):
        """One bulk transfer; a stalled endpoint is cleared and the transfer retried once."""
        count = ct.c_int()
        for attempt in range(2):
            code = self.lib.libusb_bulk_transfer(self.handle, endpoint, self.buffer, length, ct.byref(count), TIMEOUT_MS)
            if code == LIBUSB_PIPE and not attempt:
                self._check(self.lib.libusb_clear_halt(self.handle, endpoint), 'Clearing a stalled endpoint')
                continue
            if code == LIBUSB_TIMEOUT:
                raise ValueError(STOPPED)
            self._check(code, action)
            return count.value

    def write(self, data):
        ct.memmove(self.buffer, data, len(data))
        require(self._bulk(self.outbound, len(data), 'Sending to your OP-XY') == len(data), STOPPED)

    def read(self):
        return ct.string_at(self.buffer, self._bulk(self.inbound, CHUNK, 'Receiving from your OP-XY'))

    def cancel(self, transaction):
        """Cancel Request (0x64) for the transaction, then Get Device Status (0x67) until the device
        reports OK, as the still-image class requires before the next command."""
        number = self.number
        with contextlib.suppress(ValueError):
            self._control(self.handle, 0x21, 0x64, 0, number, data=struct.pack('<HI', 0x4001, transaction), timeout=2000)
        for _ in range(20):
            try:
                status = self._control(self.handle, 0xa1, 0x67, 0, number, 64, timeout=2000)
            except ValueError:
                return
            if len(status) < 4 or struct.unpack_from('<H', status, 2)[0] == OK:
                return
            time.sleep(0.1)

    def close(self):
        handle, self.handle = self.handle, None
        if handle is not None:
            if self.interface is not None:
                self.lib.libusb_release_interface(handle, self.interface)
                self.interface = None
            self.lib.libusb_close(handle)
        context, self.context = self.context, None
        if context is not None:
            self.lib.libusb_exit(context)


class Device:
    """The one connected OP-XY; use as a context manager. `transport` is for tests: write(bytes),
    read() -> bytes, cancel(transaction), close() and a `packet` size."""

    def __init__(self, transport=None):
        self.usb = transport or Usb()
        self.transaction = 0
        self.current = self.storage = None
        self.broken = False
        try:
            opened = self._run(OPEN_SESSION, [1])
            if opened[0] != SESSION_ALREADY_OPEN:
                self._expect(opened, 'open file transfer')
            storages = read_words(self._expect(self._run(STORAGE_IDS), 'find its storage')[1])
            require(storages, 'Your OP-XY has no storage available')
            self.storage = storages[0]
        except BaseException:
            self.close()
            raise

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        usb = self.usb
        if usb is None:
            return
        try:
            if not self.broken and self.storage is not None:  # After a failure the session is abandoned, not closed.
                with contextlib.suppress(Exception):
                    self._run(CLOSE_SESSION)
        finally:
            self.usb = None
            usb.close()

    @staticmethod
    def _expect(result, action):
        code = result[0]
        require(code == OK, REFUSALS.get(code, f"Your OP-XY couldn't {action} (MTP error {code:#06x})"))
        return result

    def _send(self, data):
        for at in range(0, len(data), CHUNK):
            self.usb.write(data[at:at + CHUNK])
        if len(data) % self.usb.packet == 0:  # A transfer ending on a packet boundary needs a zero-length packet.
            self.usb.write(b'')

    def _receive(self, limit):
        first = b''
        while not first:
            first = self.usb.read()  # Skips a trailing zero-length packet.
        require(len(first) >= 12, 'Unexpected reply from your OP-XY')
        length, kind, code, transaction = HEADER.unpack_from(first)
        require(12 <= length <= limit + 12 and len(first) <= length, 'Unexpected transfer size from your OP-XY')
        parts, have = [first], len(first)
        while have < length:
            more = self.usb.read()
            require(more and have + len(more) <= length, 'Unexpected reply from your OP-XY')
            parts.append(more)
            have += len(more)
        return kind, code, transaction, b''.join(parts)[12:]

    def _run(self, code, params=(), data=None, limit=16 << 20):
        """(response code, data phase or None, response parameters) of one MTP transaction."""
        transaction = self.current = self.transaction
        self.transaction += 1
        try:
            self._send(container(COMMAND, code, transaction, words(params)))
            if data is not None:
                require(len(data) <= 0xffffffff - 12, 'That file is too large for your OP-XY')
                self._send(container(DATA, code, transaction, data))
            kind, reply, tid, payload = self._receive(limit)
            phase = None
            if kind == DATA:
                require(tid == transaction and reply == code, 'Unexpected data from your OP-XY')
                phase = payload
                kind, reply, tid, payload = self._receive(limit)
            require(kind == RESPONSE and tid == transaction, 'Unexpected reply from your OP-XY')
            return reply, phase, list(struct.unpack_from(f'<{len(payload) // 4}I', payload))
        except BaseException:
            self.broken = True
            # Cancel the pending transaction so the device is idle again for a rollback, even after Ctrl+C.
            with contextlib.suppress(Exception):
                self.usb.cancel(transaction)
            raise

    def root(self):
        return ROOT

    def list(self, parent):
        handles = read_words(self._expect(self._run(OBJECT_HANDLES, [self.storage, 0, parent]), 'list a folder')[1])
        result = []
        for handle in handles:
            info = self._expect(self._run(OBJECT_INFO, [handle]), 'list a folder')[1]
            require(info is not None and len(info) >= 53, 'Unexpected file information from your OP-XY')
            size, = struct.unpack_from('<I', info, 8)
            format, = struct.unpack_from('<H', info, 4)
            result.append(Entry(handle, read_string(info, 52), size, format == FOLDER_FORMAT, format))
        return result

    def read(self, entry):
        require(entry.size is not None, f'{entry.name} reports no size')
        data = self._expect(self._run(GET_OBJECT, [entry.id], limit=entry.size), 'read ' + entry.name)[1]
        require(data is not None and len(data) == entry.size, f'Reading {entry.name} was incomplete')
        return data

    def delete(self, entry):
        """Delete exactly one file, never recursively."""
        require(not entry.folder, f'Refusing to delete the folder {entry.name}')
        self._expect(self._run(DELETE_OBJECT, [entry.id, 0]), 'delete ' + entry.name)

    def create_file(self, parent, name, data, like=None):
        info = object_info(self.storage, name, len(data), parent)
        self._expect(self._run(SEND_OBJECT_INFO, [self.storage, parent], info), 'accept ' + name)
        self._expect(self._run(SEND_OBJECT, [], data), 'save ' + name)

    def create_folder(self, parent, name):
        info = object_info(self.storage, name, 0, parent, folder=True)
        self._expect(self._run(SEND_OBJECT_INFO, [self.storage, parent], info), 'create the folder ' + name)
