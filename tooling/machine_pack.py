"""A compiled collection for local catalog import, without stock content or firmware."""
import base64
import json
from pathlib import Path
import zipfile

from .formats import require, sha


MAX_PACK_BYTES = 64 * 1024 * 1024


def write(kit, output):
    """Called only after the kit's native/browser verification completes.

    The receiver compares the existing catalog and runtime with its own kit.
    Collection-specific objects travel together so registration, controls and
    project companions describe exactly the exported collection.
    """
    require(any(m['origin'] == 'personal' for m in kit['machines']),
            'A machine pack needs at least one custom machine')
    pack = {key: kit[key] for key in ('version', 'definitions', 'fixed', 'machines', 'objects', 'notices')}
    pack.update(format='opxy-machine-pack/1', extensions=kit.get('extensions', []),
                profileSha256=sha(base64.b64decode(kit['profileSource'], validate=True)))
    data = json.dumps(pack, separators=(',', ':'), ensure_ascii=True).encode('utf-8')
    require(len(data) + 124 <= MAX_PACK_BYTES, 'Machine pack exceeds 64 MiB')
    path = Path(output) / 'machines.zip'
    # One stored entry: bounded parsing in the browser, no archive extraction.
    with zipfile.ZipFile(path, 'x', compression=zipfile.ZIP_STORED) as archive:
        info = zipfile.ZipInfo('machines.json', date_time=(1980, 1, 1, 0, 0, 0))
        archive.writestr(info, data)
    return path
