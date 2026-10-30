"""The boot-screen version label: one display-only edit in stock OS startup."""
from pathlib import Path
import re
import struct

from .formats import require
from .pipeline import ROOT

PROFILE = 'opxy-1.2.1'
SITE = 0x8c0b85dc
CONTINUATION = 0x8c0b85e6
DRAW = 0x8c0b7078
BEFORE = bytes.fromhex('32e412000207ffe34bf5')
OBJECT = 'sdk/startup_label.o'
SYMBOL = 'kit_startup_label'
TEMPLATE = 'sdk/platform/opxy-1.2.1/startup_label.s.in'
# Packages built before releases were versioned showed this label's version.
UNVERSIONED = '0.0.1'


def release_version():
    """The release version in VERSION, which the builder shows too. Read when a build
    needs it, so a missing or malformed file fails only that build."""
    path = ROOT / 'VERSION'
    require(path.is_file(), 'Missing VERSION: the release version, <major>.<minor>.<patch>')
    return checked(path.read_text(encoding='utf-8').strip())


def checked(version):
    require(isinstance(version, str) and re.fullmatch(r'\d+\.\d+\.\d+', version) is not None,
            'A release version is <major>.<minor>.<patch>: ' + repr(version))
    return version


def label(version):
    """The boot label's terminated text: the release and the stock OS it extends."""
    return f'OPEN {checked(version)}-1.2.1'.encode('ascii') + b'\0'


def generate(directory, version):
    """The label unit's assembly, showing this release."""
    source = (ROOT / TEMPLATE).read_text(encoding='utf-8')
    require(source.count('@LABEL@') == 1, 'Startup label template placeholder missing')
    destination = Path(directory) / 'startup_label.s'
    destination.write_text(source.replace('@LABEL@', label(version)[:-1].decode('ascii')), encoding='utf-8', newline='\n')
    return destination


def jump(destination):
    require(0 <= destination <= 0xffffffff and destination % 2 == 0, 'Invalid absolute jump')
    return struct.pack('<4H', 0xdc00, destination >> 16, destination & 0xffff, 0) + b'\0\0'


def destination(instruction):
    require(len(instruction) == 10, 'Startup hook must occupy ten bytes')
    opcode, high, low, zero, nop = struct.unpack('<5H', instruction)
    require(opcode == 0xdc00 and zero == nop == 0 and low % 2 == 0, 'Unexpected startup hook encoding')
    return high << 16 | low


def allowed(profile, patch):
    return (profile['id'] == PROFILE and int(patch['address'], 16) == SITE and
            bytes.fromhex(patch['before']) == BEFORE and patch['length'] == len(BEFORE) and
            int(patch['file_offset'], 16) == 0xef818 and patch.get('symbol') == SYMBOL)


def thunk_ok(module, offset, size, text):
    """The linked thunk replays the stock R2 load, loads R0 = text, calls the stock draw and resumes."""
    code = module[offset:offset + size]
    return (size == 28 and code[:4] == bytes.fromhex('32e41200') and
            code[4:12] == struct.pack('<4H', 0xe140, text >> 16, 0xe100, text & 0xffff) and
            code[12:20] == struct.pack('<4H', 0xde00, DRAW >> 16, DRAW & 0xffff, 0) and
            code[20:28] == struct.pack('<4H', 0xdc00, CONTINUATION >> 16, CONTINUATION & 0xffff, 0))
