"""Shared local build utilities and stock firmware compatibility boundary."""
import argparse
import json
from pathlib import Path
import platform
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

from .formats import elf32, header, ldr, mapped_offset, require, sha

# Child processes never open a console window on Windows.
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)

ROOT = Path(__file__).resolve().parents[1]
MAX_FILE = 16 * 1024 * 1024
PROFILE = 'profiles/opxy-1.2.1.json'
TOOLCHAINS = 'profiles/toolchains.json'
TOOLCHAIN_HOME = 'local/toolchain'  # where `sdk.py toolchain fetch` installs
TOOLCHAINS_SHA256 = '318e955c019f31e7ed1c5611ceaf6c476389bea88626bf535dd1cfd705baa468'
# Additional exclusions, not a complete map of critical code. The exact source,
# module and whole-output pins remain the stronger reference boundary.
PROTECTED = (
    (0x11a00000, 0x11a0c000),  # Startup/entry payload
    (0x8c03d000, 0x8c0d0380),  # OS block containing storage/partition/reboot code
    (0x8d517b94, 0x8d517d64),  # Simple factory/callbacks
    (0x8d5051c4, 0x8d505220),  # Shared default voice callbacks
    (0x8d506896, 0x8d5068bc),  # Simple registration
    (0x84050780, 0x840507ac),  # Simple method table
    (0x840479f4, 0x840479f8),  # Stock amplitude constant
    (0x84047f8c, 0x84047f94),  # Simple name
    (0x840481e8, 0x84048208),  # Simple label pointers
    (0x84047b6c, 0x84047b74),  # Stock shape label
)


PATCH_KINDS = ('heap', 'table', 'text', 'hook', 'startup', 'step')


def check_patch_boundaries(profile, count):
    """Well-formed, non-overlapping edits. The build computes the bytes of hooks, the
    startup label and step edits (tooling/steps.py) only; the others carry them."""
    from .startup_label import allowed as startup_label_allowed
    from .steps import check_patch as check_step
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
        require('profiling' not in patch or (patch['profiling'] is False and kind == 'hook'),
                'Invalid profiling hook')
        if 'after' in patch:
            after = bytes.fromhex(patch['after'])
            # Optional hooks, and step slots no machine replaces, stay stock in builds that leave them out.
            require(len(after) == len(before) and (after != before or kind == 'step' or
                    (kind == 'hook' and ('service' in patch or 'profiling' in patch))),
                    'Invalid instruction edit')
        else:
            require(kind in ('hook', 'startup', 'step'), 'Heap, table and text edits carry their reviewed bytes')
        if kind == 'step':
            check_step(patch)
        if kind == 'text':
            # A stock string rewritten in place: printable text, then at least one NUL.
            for data in (before, bytes.fromhex(patch['after'])):
                text = data.rstrip(b'\0')
                require(len(text) < len(data) and all(0x20 <= c < 0x7f for c in text), 'Invalid text edit')
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
    """The firmware profile: stock 1.2.1 hashes, the reservation, every edit and the identity rules."""
    data = read(ROOT / PROFILE)
    require(sha(data) == reviewed_profile_sha256(), 'Reviewed firmware profile changed')
    profile = json.loads(data)
    require(profile['schema'] == 1 and profile['id'] == 'opxy-1.2.1', 'Incompatible profile')
    check_patch_boundaries(profile, len(profile['patches']))
    require(all('after' not in p for p in profile['patches'] if p['kind'] == 'step'), 'Step edits are computed')
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
    return subprocess.check_output([str(a) for a in args], stderr=subprocess.STDOUT, text=True, timeout=120)


def fresh_output(path):
    path = Path(path).resolve()
    require(not path.exists(), f'Output already exists; choose a new directory: {path}')
    path.mkdir(parents=True)
    return path
