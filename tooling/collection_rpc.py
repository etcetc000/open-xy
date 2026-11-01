"""Opt-in startup IPC identity probe. No activation or executable-memory writes."""
import copy
import hashlib
import json
import struct
from pathlib import Path

from .collection_probe import platform_id
from .formats import require, sha
from .firmware import branch_instruction
from .pipeline import ROOT, check_patch_boundaries, read

OVERLAY = 'profiles/collection-rpc-1.2.1.json'
OVERLAY_SHA256 = 'df88d036aba6464b8e4b89bb47e631a4d3cca76a013b199bc3f30aa314135735'


def profile(base):
    raw = read(ROOT / OVERLAY)
    require(sha(raw) == OVERLAY_SHA256, 'Unreviewed collection RPC profile')
    overlay = json.loads(raw)
    require(overlay['schema'] == 2 and overlay['base'] == base['id'], 'Wrong collection RPC profile')
    hook = overlay['hook']
    require(hook['address'] == '0x8c0c236c' and hook['file_offset'] == '0xf95a8' and
            hook['length'] == 8 and hook['before'] == '00e80000f5053830' and hook['call'] is False,
            'Changed collection RPC dispatcher boundary')
    result = copy.deepcopy(base)
    result['patches'].append(hook)
    entry = overlay['entry_hook']
    require(entry['address'] == '0x8d5067d4' and entry['file_offset'] == '0x13da30' and
            entry['length'] == 8 and entry['before'] == '00e80000fb05466f' and entry['call'] is False,
            'Changed collection initializer entry boundary')
    result['patches'].append(entry)
    check_patch_boundaries(result, len(base['patches'])+2)
    return result


def generate(directory, sources, services, profiling, compiler):
    digest = platform_id(sources, services, profiling, compiler)
    identity = digest.hex()
    text = read(ROOT / 'sdk/platform/opxy-1.2.1/collection_rpc.cpp.in').decode()
    text = text.replace('@PLATFORM@', identity)
    fields = probe_identities(digest)
    text = text.replace('@IDENTITY@', ','.join('{'+','.join(str(b) for b in field)+'}' for field in fields)+',1,0,0')
    require('@' not in text, 'Unexpanded collection RPC probe')
    path = Path(directory) / 'collection_rpc.cpp'
    path.write_text(text, encoding='utf-8', newline='\n')
    return path


def probe_identities(platform):
    require(len(platform)==32, 'Invalid diagnostic identity')
    return (platform, hashlib.sha256(b'openxy-hold-abi\0'+platform).digest(),
            hashlib.sha256(b'openxy-hold-image\0'+platform).digest())


def thunk_ok(module, offset, size):
    return size == 16 and module[offset:offset+size] == (
        bytes.fromhex('00e80000f5053830') + struct.pack('<4H',0xdc00,0x8c0c,0x2374,0))


def entry_ok(module, low, symbols):
    symbol = symbols['sdk_collection_rpc_entry_hook']
    at = symbol['value']
    expected = (bytes.fromhex('00e80000') +
                branch_instruction(at+4,symbols['sdk_hook_save']['value'],True) +
                branch_instruction(at+8,symbols['sdk_collection_rpc_entry']['value'],True) +
                branch_instruction(at+12,symbols['sdk_hook_restore']['value'],True) +
                bytes.fromhex('01e8000000e80000fb05466f00dc508ddc670000'))
    return symbol['size']==36 and module[at-low:at-low+36]==expected
