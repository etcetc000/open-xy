"""Settings extensions on an existing OS image: show or apply their capabilities.

A capability is fixed reviewed bytes at stock sites (tooling.extensions.capabilities),
so it can be read from, or written into, stock OS 1.2.1 or any open-xy OS made from
it without rebuilding its machines: open-xy builds add their module record before
FINAL and leave every stock record, and so every capability site, where stock has it.
Each site must hold stock bytes or one of the capability's reviewed encodings; the
output is a copy with only those bytes changed. No device operations.

An image is authenticated as the builder does: with its module record removed and
this profile's reviewed edits undone it must be stock by SHA-256. An open-xy OS
from another SDK (other hooks, another module base) cannot be undone that way;
given the stock OS (--stock-os), it is accepted when every stock loader record is
unchanged and in place and none of its other edits touches a capability site.
"""
import argparse
import json
from pathlib import Path

from .extensions import capabilities, extension_at
from .firmware import hook_target
from .formats import header, ldr, mapped_offset, require, sha
from . import release_floor, startup_label, stock_limits
from .pipeline import add_overwrite_argument, config, fresh_output, read
from .release import outside_checkouts

# Stock ranges each capability's edits rely on beyond their sites: (address, bytes).
DEPENDS = {release_floor.SERVICE: release_floor.DEPENDS, stock_limits.SERVICE: stock_limits.DEPENDS}

UNSUPPORTED = 'Not OP-XY OS 1.2.1 or an open-xy OS made from it'


def capability_edits(profile, name):
    return [p for p in profile['patches'] if p.get('service') == name]


def label(patch):
    return patch.get('name', patch['address'])


def site(image, blocks, patch):
    offset = int(patch['file_offset'], 16)
    require(mapped_offset(blocks, int(patch['address'], 16), patch['length']) == offset,
            'Capability site is not where OS 1.2.1 holds it: ' + label(patch))
    return offset, image[offset:offset + patch['length']].hex()


def encodings(patch):
    return {patch['before'], *([patch['after']] if 'after' in patch else [])}


def authenticate(image, blocks, profile):
    """The image is stock OS 1.2.1, or an open-xy OS made from it: with its module record
    (before FINAL) removed and every reviewed edit undone, it is stock by SHA-256, as
    builder/core.mjs recoverFirmware requires. Since apply writes the image back out, every
    other edit site must also hold stock, or its reviewed bytes, or for a hook or startup
    label a branch into the image's own module record; without one, only capability edits."""
    stock, low, high = bytearray(image), None, None
    first, final = blocks[0], blocks[-1]
    # The loader frame builder/core.mjs recoverFirmware checks: FIRST holds the size, FINAL ends.
    require(first['count'] == 0 and first['code'] & 0xffff == 0x5004 and first['argument'] == len(image) - 16 and
            final['count'] == 0 and final['code'] & 0xffff == 0x8004, UNSUPPORTED)
    if len(blocks) == profile['stock_records'] + 1:
        module = blocks[-2]
        require(module['offset'] == profile['stock_bytes'] - 16 and module['code'] & 0xffff == 4 and
                module['argument'] == 0 and module['count'] > 0 and
                len(image) == profile['stock_bytes'] + 16 + module['count'], UNSUPPORTED)
        stock = bytearray(image[:module['offset']] + image[final['offset']:])
        stock[:16] = header(0x5004, first['target'], 0, len(stock) - 16)
        low, high = module['target'], module['target'] + module['count']
    else:
        require(len(blocks) == profile['stock_records'], UNSUPPORTED)
    for patch in profile['patches']:
        offset = int(patch['file_offset'], 16)
        held = image[offset:offset + patch['length']]
        # Without a module record only capability (limit) edits may be applied: hooks, the
        # startup label and relocated tables all lead into the module.
        if held.hex() == patch['before'] or (held.hex() == patch.get('after') and
                                              (low is not None or patch['kind'] == 'limit')):
            continue
        require(low is not None and patch['kind'] in ('hook', 'startup'),
                UNSUPPORTED + ': unexpected bytes at a reviewed edit site ' + label(patch))
        target = (startup_label.destination(held) if patch['kind'] == 'startup'
                  else hook_target(patch, held)[0])
        require(low <= target < high, UNSUPPORTED + ': a hook leaves its module: ' + label(patch))
    require(len(stock) == profile['stock_bytes'], UNSUPPORTED)
    for patch in profile['patches']:
        offset = int(patch['file_offset'], 16)
        require(offset + patch['length'] <= len(stock), UNSUPPORTED)
        stock[offset:offset + patch['length']] = bytes.fromhex(patch['before'])
    require(sha(bytes(stock)) == profile['stock_sha256'], UNSUPPORTED)


def compare_with_stock(image, blocks, profile, stock):
    """An open-xy OS from another SDK: the verified stock OS's loader record headers unchanged
    and in place, records of its own only before FINAL, FIRST holding the image size, and no
    edit of its own near a capability site or in the stock code a capability relies on. The sites themselves may hold only stock or reviewed bytes, which state() checks;
    the rest of that build is its own SDK's to verify."""
    require(len(stock) == profile['stock_bytes'] and sha(stock) == profile['stock_sha256'],
            '--stock-os is not the exact stock OS 1.2.1')
    reference = ldr(stock)
    key = lambda b: (b['code'], b['target'], b['count'], b['argument'], b['offset'])
    require(len(blocks) > len(reference) and blocks[0]['code'] & 0xffff == 0x5004 and
            blocks[0]['argument'] == len(image) - 16 and
            [key(b) for b in blocks[1:len(reference) - 1]] == [key(b) for b in reference[1:-1]] and
            image[-16:] == stock[-16:], UNSUPPORTED + ': its stock loader records moved or changed')
    names, window = set(capabilities()), 32
    # Its own records must not load over what the capabilities rely on in memory either.
    guarded = [(int(p['address'], 16) - window, int(p['address'], 16) + p['length'] + window)
               for p in profile['patches'] if p.get('service') in names]
    guarded += [(address, address + size) for name in names for address, size in DEPENDS.get(name, ())]
    for b in blocks[len(reference) - 1:-1]:
        require(not any(b['target'] < hi and lo < b['target'] + b['count'] for lo, hi in guarded),
                'Another record loads over stock code a capability relies on (0x%x)' % b['target'])
    # Around each capability site (the instructions its edit relies on, such as the compare
    # between the floor and stage edits) the image must be stock, apart from the sites.
    spans = [(int(p['file_offset'], 16), int(p['file_offset'], 16) + p['length'])
             for p in profile['patches'] if p.get('service') in names]
    for lo, hi in spans:
        for i in range(max(16, lo - window), hi + window):  # FIRST's header is checked above
            require(image[i] == stock[i] or any(a <= i < b for a, b in spans),
                    'Another edit is within %d bytes of a capability site' % window)
    for name in names:
        for address, size in DEPENDS.get(name, ()):
            at = mapped_offset(reference, address, size)
            require(all(image[i] == stock[i] or any(a <= i < b for a, b in spans) for i in range(at, at + size)),
                    'Another edit changes stock code %s relies on (0x%x)' % (name, address))


def check_image(image, blocks, profile, stock=None):
    """Authenticate as this SDK reconstructs an image, else, given stock, as another SDK's."""
    try:
        authenticate(image, blocks, profile)
    except ValueError as error:
        # Only another SDK's build (its module loads elsewhere) may be checked against stock;
        # one of this SDK's that fails reconstruction is corrupt.
        other = len(blocks) > profile['stock_records'] and blocks[-2]['target'] != profile['module_base']
        require(other, str(error))
        require(stock is not None, f'{error}. For an open-xy OS from another SDK, pass the stock OS with --stock-os')
        compare_with_stock(image, blocks, profile, stock)


def state(image, profile=None, stock=None):
    """Capability -> whether the image has it on, read from the bytes at its sites."""
    profile = profile or config()
    blocks = ldr(image)
    check_image(image, blocks, profile, stock)
    result = {}
    for name in capabilities():
        edits, held = capability_edits(profile, name), {}
        for patch in edits:
            _, data = site(image, blocks, patch)
            require(data in encodings(patch), 'Unexpected bytes at a capability site: ' + label(patch))
            held[patch['address']] = data
        on = [held[p['address']] == p['after'] for p in edits]
        require(all(on) or not any(on), 'Capability only partly applied: ' + name)
        result[name] = bool(on) and all(on)  # a capability without edits here is never on
    return result


def apply(image, extensions, profile=None, stock=None):
    """A copy of `image` with every capability the extensions declare turned on, and the
    capabilities the copy has on."""
    profile = profile or config()
    before = state(image, profile, stock)  # authenticated; every site holds stock or reviewed bytes
    require(all(not e.linked for e in extensions),
            'Only settings extensions apply to an image: an extension with code needs a build')
    declared = list(dict.fromkeys(c for e in extensions for c in e.capabilities))
    require(declared, 'The extensions declare no capabilities')
    blocks, patched = ldr(image), bytearray(image)
    for name in declared:
        for patch in capability_edits(profile, name):
            offset, _ = site(image, blocks, patch)
            patched[offset:offset + patch['length']] = bytes.fromhex(patch['after'])
    result = bytes(patched)
    require(len(ldr(result)) == len(blocks), 'Loader records changed')
    return result, [name for name, on in before.items() if on or name in declared]


def main(argv=None):
    parser = argparse.ArgumentParser(prog='sdk.py settings', description=__doc__.split('\n')[0])
    commands = parser.add_subparsers(dest='command', required=True)
    show = commands.add_parser('show', help='List the capabilities an OS image has on')
    show.add_argument('--image', type=Path, required=True, help='os.ldr: stock OS 1.2.1 or an open-xy OS made from it')
    show.add_argument('--stock-os', type=Path, help='stock OS 1.2.1, to check an open-xy OS from another SDK')
    put = commands.add_parser('apply', help='Write a copy of an OS image with settings extensions applied')
    put.add_argument('--image', type=Path, required=True, help='os.ldr: stock OS 1.2.1 or an open-xy OS made from it')
    put.add_argument('--stock-os', type=Path, help='stock OS 1.2.1, to check an open-xy OS from another SDK')
    put.add_argument('--extension', type=Path, action='append', required=True, help='settings extension folder; repeatable')
    put.add_argument('--out', type=Path, required=True, help='new folder outside the checkout for os.ldr and settings.json')
    add_overwrite_argument(put)
    args = parser.parse_args(argv)
    try:
        image = read(args.image)
        stock = read(args.stock_os) if args.stock_os else None
        if args.command == 'show':
            print(json.dumps(state(image, stock=stock), indent=2))
            return 0
        require(outside_checkouts(args.out), f'Firmware stays outside every checkout: choose --out elsewhere ({args.out})')
        extensions = [extension_at(folder) for folder in args.extension]
        result, on = apply(image, extensions, stock=stock)
        inputs = [args.image, *args.extension, *([args.stock_os] if args.stock_os else [])]
        out = fresh_output(args.out, args.overwrite, keep=inputs)  # --overwrite never deletes an input
        (out / 'os.ldr').write_bytes(result)
        report = dict(format='opxy-settings/1', source=str(Path(args.image).resolve()),
                      source_sha256=sha(image), sha256=sha(result),
                      extensions=[e.id for e in extensions], capabilities=on,
                      changed_bytes=sum(a != b for a, b in zip(image, result)))
        (out / 'settings.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(report, indent=2))
        return 0
    except (ValueError, OSError) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')
