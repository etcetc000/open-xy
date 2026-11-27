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
    built with every service, define and header-tied unit the pack's machines use, or its
    kit carries them built so (builder/core.mjs importMachinePack, variant_build).
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


def variant_build(site, carried, machines):
    """What a site's SDK objects must have been built with for pack machines, as the builder decides
    it (builder/core.mjs variantBuild): a native build of the site's collection with those machines
    compiles them so (native.shared_build). `site` is its kit's build record, `carried` what the
    site's variants can add (native.carried_variants) and `machines` each pack machine's services,
    defines and header-tied units. A machine may add only what the site has or carries; its own
    objects are the same in any build (native.own_unit_defines). {'target': the site's build with
    all they add (the site's own when they add nothing)} or {'refused': [machine index, 'service' |
    'define' | 'header', what]}."""
    from .native import DEFINE_ORDER, SHARED_HEADERS
    services, defines, headers = set(site['services']), set(site['defines']), set(site['headers'])
    for index, m in enumerate(machines):
        for what, needed, key in (('service', m['services'], 'services'), ('define', m['defines'], 'defines'),
                                  ('header', m['headers'], 'headers')):
            missing = [x for x in needed if x not in site[key] and x not in carried[key]]
            if missing:
                return dict(refused=[index, what, missing[0]])
        services.update(m['services'])
        defines.update(m['defines'])
        headers.update(m['headers'])
    if len(services) == len(site['services']) and len(defines) == len(site['defines']) and len(headers) == len(site['headers']):
        return dict(target=site)
    return dict(target=dict(services=sorted(services), defines=[d for d in DEFINE_ORDER if d in defines],
                            headers=[h for h in SHARED_HEADERS if h in headers]))
