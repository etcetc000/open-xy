"""Windows Portable Devices access to an OP-XY in MTP file-transfer mode.

Windows binds its own MTP driver, so the browser's WebUSB transfer
(builder/mtp.mjs) cannot open the device there; this goes through the same
driver File Explorer uses. Needs Windows and `pip install comtypes==1.4.17`.
Only standard WPD content operations: list, read, create and delete one file,
create a folder. No vendor commands, no eject and no firmware write.
"""
import contextlib
import ctypes as ct
import gc
import os

from .formats import require

OPXY = 'vid_2367&pid_0021'
CLIENT = '{204D9F0C-2292-4080-9F42-40664E70F859}'
OBJ = '{EF6B490D-5CD8-437A-AFFC-DA8B60EE4A3C}'
RESOURCE = '{E81E79BE-34F0-41BF-B53F-F1A06AE87842}'
ID, PARENT, NAME, FORMAT, CONTENT_TYPE, SIZE, FILENAME = 2, 3, 4, 6, 7, 11, 12
FOLDER = '{27E2E392-A111-48E0-AB0C-E17705A05F85}'  # WPD_CONTENT_TYPE_FOLDER
FOLDER_TYPES = {FOLDER, '{99ED0160-17FF-4C44-9D98-1D7A6F941921}'}  # and WPD_CONTENT_TYPE_FUNCTIONAL_OBJECT
GENERIC_FILE = '{0085E0A6-8D34-45D7-BC5C-447E59C73D48}'
UNSPECIFIED_FORMAT = '{30000000-AE6C-4804-98BA-C57B46965FE7}'
PROPERTIES_ONLY = '{30010000-AE6C-4804-98BA-C57B46965FE7}'
CHUNK = 32768
GENERIC_READ_WRITE = 0xc0000000


@contextlib.contextmanager
def failure(action):
    """Name the action when a driver call fails (OSError from raw calls, COMError from wrapped ones)."""
    try:
        yield
    except Exception as error:
        if not isinstance(error, OSError) and type(error).__name__ != 'COMError':
            raise
        raise ValueError(f'{action} failed: {error}') from error


class Entry:
    def __init__(self, id, name, size, folder, format=None, content_type=None):
        self.id, self.name, self.size, self.folder = id, name, size, folder
        self.format, self.content_type = format, content_type


class Device:
    """The one connected OP-XY; use as a context manager."""

    def __init__(self, purpose='Open-XY device transfer'):
        self.device = None
        try:
            import comtypes
            import comtypes.client
        except ImportError:
            raise ValueError('Device transfer needs Windows and comtypes: python -m pip install comtypes==1.4.17')
        self.comtypes = comtypes
        system = os.path.join(os.environ.get('SystemRoot', r'C:\Windows'), 'System32')
        self.api = comtypes.client.GetModule(os.path.join(system, 'PortableDeviceApi.dll'))
        self.types = comtypes.client.GetModule(os.path.join(system, 'PortableDeviceTypes.dll'))
        self.free = ct.OleDLL('ole32').CoTaskMemFree
        self.free.argtypes, self.free.restype = [ct.c_void_p], None
        manager = self._create(self.api.PortableDeviceManager, self.api.IPortableDeviceManager)
        manager.RefreshDeviceList()
        # Raw calls: comtypes' in/out array conversion is wrong on this typelib.
        get = manager._IPortableDeviceManager__com_GetDevices
        count = ct.c_ulong()
        get(None, ct.byref(count))
        ids = (ct.c_void_p * count.value)()
        if count.value:
            get(ids, ct.byref(count))
        names = []
        for pointer in ids[:count.value]:
            names.append(ct.wstring_at(pointer))
            self.free(pointer)
        matches = [x for x in names if OPXY in x.lower()]
        require(len(matches) == 1, f'Expected one OP-XY in file-transfer mode, found {len(matches)}: '
                'choose "mtp file transfer" on the device and close other apps using it')
        client = self._values()
        client.SetStringValue(self._key(CLIENT, 2), purpose)
        client.SetUnsignedIntegerValue(self._key(CLIENT, 9), GENERIC_READ_WRITE)
        self.device = self._create(self.api.PortableDevice, self.api.IPortableDevice)
        self.device.Open(matches[0], client)
        try:
            self.content = self.device.Content()
            self.properties = self.content.Properties()
            self.resources = self.content.Transfer()
            # Listings fetch only the properties they use, not every one.
            self.keys = self._create(self.types.PortableDeviceKeyCollection, self.api.IPortableDeviceKeyCollection)
            for pid in (FILENAME, NAME, CONTENT_TYPE, FORMAT, SIZE):
                self.keys.Add(self._key(OBJ, pid))
            self.id_key = self._create(self.types.PortableDeviceKeyCollection, self.api.IPortableDeviceKeyCollection)
            self.id_key.Add(self._key(OBJ, ID))
        except BaseException:
            self.close()
            raise

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def close(self):
        device, self.device = self.device, None
        if device is not None:
            try:
                device.Close()
            except self.comtypes.COMError:
                pass  # Every write was already read back; a device that vanished on close changes nothing.

    def _create(self, cls, interface):
        return self.comtypes.client.CreateObject(cls, interface=interface)

    def _values(self):
        return self._create(self.types.PortableDeviceValues, self.api.IPortableDeviceValues)

    def _key(self, guid, pid):
        return self.api._tagpropertykey(self.comtypes.GUID(guid), pid)

    def _optional(self, read):
        try:
            return read()
        except self.comtypes.COMError:
            return None

    def root(self):
        stores = self.list('DEVICE')
        require(len(stores) == 1 and stores[0].name == 'OP-XY', 'Unexpected OP-XY storage layout')
        return stores[0].id

    def list(self, parent):
        enumeration = self.content.EnumObjects(0, parent, None)
        result = []
        for _ in range(100000):
            ids, count = (ct.c_wchar_p * 1)(), ct.c_ulong()
            with failure('Listing a device folder'):
                enumeration._IEnumPortableDeviceObjectIDs__com_Next(1, ids, ct.byref(count))
            if count.value == 0:
                return result
            object_id = ids[0]
            self.free(ct.cast(ids, ct.POINTER(ct.c_void_p))[0])
            props = self.properties.GetValues(object_id, self.keys)
            name = (self._optional(lambda: props.GetStringValue(self._key(OBJ, FILENAME)))
                    or self._optional(lambda: props.GetStringValue(self._key(OBJ, NAME))))
            kind = self._optional(lambda: props.GetGuidValue(self._key(OBJ, CONTENT_TYPE)))
            format = self._optional(lambda: props.GetGuidValue(self._key(OBJ, FORMAT)))
            size = self._optional(lambda: props.GetUnsignedLargeIntegerValue(self._key(OBJ, SIZE)))
            folder = str(kind).upper() in FOLDER_TYPES or str(format).upper() == PROPERTIES_ONLY
            result.append(Entry(object_id, name, size, folder, format, kind))
        raise ValueError('Folder listing did not end')

    def read(self, entry):
        require(entry.size is not None, f'{entry.name} reports no size')
        stream, preferred = ct.POINTER(self.api.IStream)(), ct.c_ulong()
        resource = self._key(RESOURCE, 0)
        with failure(f'Opening {entry.name}'):
            self.resources._IPortableDeviceResources__com_GetStream(
                entry.id, ct.byref(resource), 0, ct.byref(preferred), ct.byref(stream))
        data = bytearray()
        try:
            while len(data) < entry.size:
                wanted = min(CHUNK, entry.size - len(data))
                buf, count = (ct.c_ubyte * wanted)(), ct.c_ulong()
                stream._ISequentialStream__com_RemoteRead(buf, wanted, ct.byref(count))
                require(0 < count.value <= wanted, f'Reading {entry.name} stopped early')
                data.extend(bytes(buf[:count.value]))
        finally:
            del stream
        return bytes(data)

    def delete(self, entry):
        """Delete exactly one file, never recursively."""
        require(not entry.folder, f'Refusing to delete the folder {entry.name}')
        ids = self._create(self.types.PortableDevicePropVariantCollection, self.api.IPortableDevicePropVariantCollection)
        ids.Add(self.properties.GetValues(entry.id, self.id_key).GetValue(self._key(OBJ, ID)))
        results = ct.POINTER(self.api.IPortableDevicePropVariantCollection)()
        with failure(f'Deleting {entry.name}'):
            status = self.content._IPortableDeviceContent__com_Delete(0, ids, ct.byref(results))
        require(status == 0, f'Deleting {entry.name} failed: {status:#x}')  # S_FALSE (1): not deleted.

    def create_file(self, parent, name, data, like=None):
        """Write a new file; format and content type follow a sibling file when given."""
        props = self._values()
        props.SetStringValue(self._key(OBJ, PARENT), parent)
        props.SetStringValue(self._key(OBJ, NAME), name)
        props.SetStringValue(self._key(OBJ, FILENAME), name)
        props.SetUnsignedLargeIntegerValue(self._key(OBJ, SIZE), len(data))
        guid = self.comtypes.GUID
        props.SetGuidValue(self._key(OBJ, FORMAT), like.format if like and like.format else guid(UNSPECIFIED_FORMAT))
        props.SetGuidValue(self._key(OBJ, CONTENT_TYPE),
                           like.content_type if like and like.content_type else guid(GENERIC_FILE))
        stream, preferred, cookie = ct.POINTER(self.api.IStream)(), ct.c_ulong(), ct.c_wchar_p()
        with failure(f'Creating {name}'):
            self.content._IPortableDeviceContent__com_CreateObjectWithPropertiesAndData(
                props, ct.byref(stream), ct.byref(preferred), ct.byref(cookie))
        if ct.cast(cookie, ct.c_void_p).value:
            self.free(ct.cast(cookie, ct.c_void_p))
        try:
            for offset in range(0, len(data), CHUNK):
                chunk = data[offset:offset + CHUNK]
                buf, count = (ct.c_ubyte * len(chunk)).from_buffer_copy(chunk), ct.c_ulong()
                stream._ISequentialStream__com_RemoteWrite(buf, len(chunk), ct.byref(count))
                require(count.value == len(chunk), f'Short write to {name}')
            stream.Commit(0)
        except BaseException:  # Ctrl+C too: never leave a half-written object.
            try:
                stream.Revert()
            except Exception:
                pass
            raise
        finally:
            del stream
            gc.collect()

    def create_folder(self, parent, name):
        props = self._values()
        props.SetStringValue(self._key(OBJ, PARENT), parent)
        props.SetStringValue(self._key(OBJ, NAME), name)
        props.SetStringValue(self._key(OBJ, FILENAME), name)
        props.SetGuidValue(self._key(OBJ, FORMAT), self.comtypes.GUID(PROPERTIES_ONLY))
        props.SetGuidValue(self._key(OBJ, CONTENT_TYPE), self.comtypes.GUID(FOLDER))
        new_id = ct.c_wchar_p()
        with failure(f'Creating the folder {name}'):
            self.content._IPortableDeviceContent__com_CreateObjectWithPropertiesOnly(props, ct.byref(new_id))
        if ct.cast(new_id, ct.c_void_p).value:
            self.free(ct.cast(new_id, ct.c_void_p))
