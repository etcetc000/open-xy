"""Shared local build utilities and stock firmware compatibility boundary."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

from .formats import elf32, header, ldr, mapped_offset, require, sha

ROOT = Path(__file__).resolve().parents[1]
MAX_FILE = 16 * 1024 * 1024
PROFILE = 'profiles/opxy-1.2.0.json'
TOOLCHAINS = 'profiles/toolchains.json'
TOOLCHAIN_HOME = 'local/toolchain'  # where `sdk.py toolchain fetch` installs
TOOLCHAINS_SHA256 = 'ab575720dc8685fa3e5edac1891c9e917cced62a723bfa61e7faf83b1da64d72'
# Additional exclusions, not a complete map of critical code. The exact source,
# module and whole-output pins remain the stronger reference boundary.
PROTECTED = (
    (0x11a00000, 0x11a0c000),  # Startup/entry payload
    (0x8c040ad4, 0x8c0d0d5c),  # OS block containing storage/partition/reboot code
    (0x8d516fa4, 0x8d517174),  # Simple factory/callbacks
    (0x8d504b74, 0x8d504bd0),  # Shared default voice callbacks
    (0x8d506246, 0x8d50626c),  # Simple registration
    (0x8404d2dc, 0x8404d308),  # Simple method table
    (0x84044afc, 0x84044b00),  # Stock amplitude constant
    (0x84045094, 0x8404509c),  # Simple name
    (0x840452f0, 0x84045310),  # Simple label pointers
    (0x84044c74, 0x84044c7c),  # Stock shape label
)


PATCH_KINDS = ('heap', 'table', 'hook', 'startup')


def check_patch_boundaries(profile, count):
    """Well-formed, non-overlapping edits; hooks are the only kind whose bytes the build computes."""
    from .startup_label import allowed as startup_label_allowed
    occupied = set()
    require(len(profile['patches']) == count, f'Expected {count} reviewed edit intervals')
    for patch in profile['patches']:
        kind = patch.get('kind')
        require(kind in PATCH_KINDS, 'Unknown edit kind')
        start = int(patch['address'], 16)
        before = bytes.fromhex(patch['before'])
        end = start + len(before)
        require(before and len(before) == patch['length'] and 0 <= start < end <= 0x100000000, 'Invalid instruction edit')
        require('service' not in patch or patch['service'] in profile.get('services', {}), 'Undeclared service')
        if 'after' in patch:
            after = bytes.fromhex(patch['after'])
            # A service's hook stays stock in a build that leaves the service out.
            require(len(after) == len(before) and (after != before or (kind == 'hook' and 'service' in patch)),
                    'Invalid instruction edit')
        else:
            require(kind in ('hook', 'startup'), 'Heap and table edits carry their reviewed bytes')
        if kind == 'hook':
            require(isinstance(patch.get('symbol'), str) and patch['symbol'] and type(patch.get('call')) is bool,
                    'A hook names its module symbol and call/jump form')
        # Protected code is only ever redirected into the module by a hook whose
        # destination the build verifies, or by the separately reviewed startup label.
        require(kind != 'startup' or startup_label_allowed(profile, patch), 'Unexpected startup label edit')
        require(kind in ('hook', 'startup') or not any(start < hi and lo < end for lo, hi in PROTECTED),
                'Protected code/data edit')
        positions = set(range(start, end))
        require(not occupied.intersection(positions), 'Overlapping instruction edits')
        occupied.update(positions)


def read(path):
    path = Path(path)
    require(path.is_file() and path.stat().st_size <= MAX_FILE, f'Missing/oversized input: {path}')
    return path.read_bytes()


def document(path):
    return json.loads(read(path))


def save_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def child(root, relative):
    require(isinstance(relative, str) and relative and '\\' not in relative and ':' not in relative,
            'Expected relative POSIX path')
    require(not relative.startswith('/') and all(p not in ('', '.', '..') for p in relative.split('/')),
            'Unsafe relative path')
    path = (root / relative).resolve()
    require(path.is_relative_to(root.resolve()), 'Path escapes its root')
    return path


def reviewed_profile_sha256():
    """The reviewed profile digest. One constant, shared with the browser builder that enforces it too."""
    match = re.search(r"^export const PROFILE_SHA256 = '([0-9a-f]{64})';$", read(ROOT / 'builder/core.mjs').decode('utf-8'),
                      re.MULTILINE)
    require(match, 'Missing reviewed profile digest')
    return match.group(1)


def config():
    """The firmware profile: stock 1.2.0 hashes, the reservation, every edit and the identity rules."""
    data = read(ROOT / PROFILE)
    require(sha(data) == reviewed_profile_sha256(), 'Reviewed firmware profile changed')
    profile = json.loads(data)
    require(profile['schema'] == 1 and profile['id'] == 'opxy-1.2.0', 'Incompatible profile')
    check_patch_boundaries(profile, len(profile['patches']))
    return profile


def host_platform():
    """Pin key of the running host, such as windows-x86_64 or darwin-arm64."""
    system, machine = platform.system().lower(), platform.machine().lower()
    if system.startswith(('msys_nt', 'cygwin', 'mingw')):
        system = 'windows'  # MSYS2/Cygwin Python runs the same Windows executables
    return system + '-' + {'amd64': 'x86_64', 'aarch64': 'arm64'}.get(machine, machine)


def check_toolchains(pins):
    require(pins['schema'] == 1, 'Incompatible toolchain pins')
    require(all('executable_suffix' in host for host in pins['hosts'].values()), 'Missing executable suffix')
    release = pins['hosts'].get(pins['release'], {})
    require('tools' in release and 'compiler' in release, 'The release host needs pinned tools and compiler')
    return pins


def toolchains():
    """Reviewed tool pins of every host; the release host builds published packages and kits."""
    data = read(ROOT / TOOLCHAINS)
    require(sha(data) == TOOLCHAINS_SHA256, 'Reviewed toolchain pins changed')
    return check_toolchains(json.loads(data))


def require_release_host():
    """Only the release host reproduces the reference module bytes (docs/toolchain.md)."""
    release = toolchains()['release']
    require(host_platform() == release,
            f'Release packages and browser kits are built and verified on {release}, not {host_platform()}')


def host_pins(kind):
    """Reviewed file hashes of this host's 'tools' or 'compiler', and its executable suffix."""
    host = toolchains()['hosts'].get(host_platform(), {})
    require(kind in host, f'No reviewed Blackfin {kind} for {host_platform()} (docs/toolchain.md)')
    return host[kind], host['executable_suffix']


def resolve_toolchain(toolchain=None, compiler=None):
    """--toolchain and --compiler, defaulting to the fetched toolchain folder."""
    home = ROOT / TOOLCHAIN_HOME
    if toolchain is None or compiler is None:
        require(home.is_dir(), 'No --toolchain/--compiler given and nothing in local/toolchain; '
                'run: python sdk.py toolchain fetch')
    return Path(toolchain or home / 'bin'), Path(compiler or home)


def tool_paths(directory):
    directory = Path(directory).resolve()
    pins, suffix = host_pins('tools')
    result = {}
    for name, expected in pins.items():
        path = directory / ('bfin-elf-' + name + suffix)
        require(sha(read(path)) == expected, f'Tool hash mismatch: {path}')
        result[name] = path
    return result


def execute(args):
    return subprocess.check_output([str(a) for a in args], stderr=subprocess.STDOUT, text=True, timeout=120,
                                   **hidden_process())


def hidden_process():
    return dict(creationflags=subprocess.CREATE_NO_WINDOW) if os.name == 'nt' else {}


def fresh_output(path):
    path = Path(path).resolve()
    require(not path.exists(), f'Output already exists; choose a new directory: {path}')
    path.mkdir(parents=True)
    return path
