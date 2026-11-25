"""Custom machines for local catalog import, without stock content or firmware."""
import base64
import json
from pathlib import Path
import zipfile

from .formats import require, sha


MAX_PACK_BYTES = 64 * 1024 * 1024
FORMAT = 'opxy-machine-pack/2'


def write(kit, output, notices=None):
    """Called only after the kit's native/browser verification completes.

    A pack carries only its custom machines (the kit's personal ones, of any kind) and
    their own objects, records included: no shared object depends on which machines a
    build holds (tooling/records.py), so the pack extends any site kit of the same release
    and SDK revision whatever its catalog holds, as long as the site's SDK objects were
    built with every service, define and header-tied unit the pack's machines use
    (builder/core.mjs importMachinePack).
    `notices` are the custom machines' own (the site's kit already holds the rest);
    by default the kit's.
    """
    machines = [m for m in kit['machines'] if m['origin'] == 'personal']
    require(machines, 'A machine pack needs at least one custom machine')
    owners = {m['id'] for m in machines}
    # Every kind a native build makes. `build` is what shaped the objects (native.shared_build):
    # the site's must match it where the pack's machines depend on it (core.mjs importMachinePack).
    pack = dict(format=FORMAT, version=kit['version'], sdk=kit['sdk'],
                profileSha256=sha(base64.b64decode(kit['profileSource'], validate=True)),
                definitions=kit['definitions'], fixed=kit['fixed'], build=kit['build'], machines=machines,
                objects=[o for o in kit['objects'] if o['owner'] in owners],
                notices=kit['notices'] if notices is None else notices)
    data = json.dumps(pack, separators=(',', ':'), ensure_ascii=True).encode('utf-8')
    require(len(data) + 124 <= MAX_PACK_BYTES, 'Machine pack exceeds 64 MiB')
    path = Path(output) / 'machines.zip'
    # One stored entry: bounded parsing in the browser, no archive extraction.
    with zipfile.ZipFile(path, 'x', compression=zipfile.ZIP_STORED) as archive:
        info = zipfile.ZipInfo('machines.json', date_time=(1980, 1, 1, 0, 0, 0))
        archive.writestr(info, data)
    return path
