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
import re

from .extensions import capabilities, capability_settings, chosen_settings, extension_at
from .firmware import hook_target
from .formats import header, ldr, mapped_offset, require, sha
from . import startup_label
from .pipeline import add_overwrite_argument, config, fresh_output, read
from .release import outside_checkouts

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
    return {patch['before'], *([patch['after']] if 'after' in patch else []), *patch.get('choices', {}).values()}


def authenticate(image, blocks, profile):
    """The image is stock OS 1.2.1, or an open-xy OS made from it: with its module record
    (before FINAL) removed and every reviewed edit undone, it is stock by SHA-256, as
    builder/core.mjs recoverFirmware requires. Since apply writes the image back out, every
    other edit site must also hold stock, or its reviewed bytes, or for a hook or startup
    label a branch into the image's own module record (none without one)."""
    stock, low, high = bytearray(image), None, None
    if len(blocks) == profile['stock_records'] + 1:
        first, module, final = blocks[0], blocks[-2], blocks[-1]
        require(module['offset'] == profile['stock_bytes'] - 16 and module['code'] & 0xffff == 4 and
                first['code'] & 0xffff == 0x5004, UNSUPPORTED)
        stock = bytearray(image[:module['offset']] + image[final['offset']:])
        stock[:16] = header(0x5004, first['target'], 0, len(stock) - 16)
        low, high = module['target'], module['target'] + module['count']
    else:
        require(len(blocks) == profile['stock_records'], UNSUPPORTED)
    for patch in profile['patches']:
        offset = int(patch['file_offset'], 16)
        held = image[offset:offset + patch['length']]
        if held.hex() in encodings(patch):
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
    """An open-xy OS from another SDK: the verified stock OS's loader records unchanged and in
    place, records of its own only before FINAL, and no edit of its own beside a capability
    site. The sites themselves may hold only stock or reviewed bytes, which state() checks;
    the rest of that build is its own SDK's to verify."""
    require(len(stock) == profile['stock_bytes'] and sha(stock) == profile['stock_sha256'],
            '--stock-os is not the exact stock OS 1.2.1')
    reference = ldr(stock)
    key = lambda b: (b['code'], b['target'], b['count'], b['argument'], b['offset'])
    require(len(blocks) > len(reference) and blocks[0]['code'] & 0xffff == 0x5004 and
            [key(b) for b in blocks[1:len(reference) - 1]] == [key(b) for b in reference[1:-1]] and
            image[-16:] == stock[-16:], UNSUPPORTED + ': its stock loader records moved or changed')
    # Around each capability site (the instructions its edit relies on, such as the compare
    # between the floor and stage edits) the image must be stock, apart from the sites.
    names, window = set(capabilities()), 32
    spans = [(int(p['file_offset'], 16), int(p['file_offset'], 16) + p['length'])
             for p in profile['patches'] if p.get('service') in names]
    for lo, hi in spans:
        for i in range(max(16, lo - window), hi + window):  # FIRST's header is checked above
            require(image[i] == stock[i] or any(a <= i < b for a, b in spans),
                    'Another edit is within %d bytes of a capability site' % window)


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
    """Capability -> None (off), True (on) or its setting value, read from the image's bytes."""
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
        if all(held[p['address']] == p['before'] for p in edits):
            result[name] = None
            continue
        require(all(held[p['address']] == p['after'] for p in edits if 'choices' not in p),
                'Capability only partly applied: ' + name)
        chosen = [int(v) for p in edits if 'choices' in p for v, b in p['choices'].items() if b == held[p['address']]]
        require(len(set(chosen)) <= 1, 'Inconsistent capability setting: ' + name)
        result[name] = chosen[0] if chosen else True
    return result


def apply(image, extensions, overrides=None, profile=None, stock=None):
    """A copy of `image` with every capability the extensions declare turned on, each
    setting at its override, else the first declaring extension's value, else its default."""
    profile, overrides = profile or config(), dict(overrides or {})
    specs = capability_settings()
    state(image, profile, stock)  # authenticated; every site holds stock or reviewed bytes
    require(all(not e.linked for e in extensions),
            'Only settings extensions apply to an image: an extension with code needs a build')
    declared = [c for e in extensions for c in e.capabilities]
    require(declared, 'The extensions declare no capabilities')
    require(set(overrides) <= set(declared) & set(specs), 'A setting names a capability the extensions do not declare')
    values = chosen_settings(extensions, overrides)
    for name, value in values.items():
        require(type(value) is int and specs[name]['minimum'] <= value <= specs[name]['maximum'],
                f'{name} is {specs[name]["minimum"]}..{specs[name]["maximum"]} {specs[name]["unit"]}')
    blocks, patched = ldr(image), bytearray(image)
    for name in dict.fromkeys(declared):
        for patch in capability_edits(profile, name):
            offset, _ = site(image, blocks, patch)
            after = (patch['choices'][str(values[name])] if 'choices' in patch
                     else patch['after'])
            patched[offset:offset + patch['length']] = bytes.fromhex(after)
    result = bytes(patched)
    require(len(ldr(result)) == len(blocks), 'Loader records changed')
    return result, {n: values[n] if n in specs else True for n in dict.fromkeys(declared)}


def parse_setting(text):
    name, separator, value = text.partition('=')
    require(separator and name and re.fullmatch(r'-?[0-9]+', value), 'A setting is <capability>=<whole number>')
    return name, int(value)


def main(argv=None):
    parser = argparse.ArgumentParser(prog='sdk.py settings', description=__doc__.split('\n')[0])
    commands = parser.add_subparsers(dest='command', required=True)
    show = commands.add_parser('show', help='List the capabilities an OS image has on, with their settings')
    show.add_argument('--image', type=Path, required=True, help='os.ldr: stock OS 1.2.1 or an open-xy OS made from it')
    show.add_argument('--stock-os', type=Path, help='stock OS 1.2.1, to check an open-xy OS from another SDK')
    put = commands.add_parser('apply', help='Write a copy of an OS image with settings extensions applied')
    put.add_argument('--image', type=Path, required=True, help='os.ldr: stock OS 1.2.1 or an open-xy OS made from it')
    put.add_argument('--stock-os', type=Path, help='stock OS 1.2.1, to check an open-xy OS from another SDK')
    put.add_argument('--extension', type=Path, action='append', required=True, help='settings extension folder; repeatable')
    put.add_argument('--set', action='append', default=[], metavar='CAPABILITY=VALUE',
                     help='override a setting, for example --set release_floor=-40')
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
        result, values = apply(image, extensions, dict(parse_setting(s) for s in args.set), stock=stock)
        inputs = [args.image, *args.extension, *([args.stock_os] if args.stock_os else [])]
        out = fresh_output(args.out, args.overwrite, keep=inputs)  # --overwrite never deletes an input
        (out / 'os.ldr').write_bytes(result)
        report = dict(format='opxy-settings/1', source=str(Path(args.image).resolve()),
                      source_sha256=sha(image), sha256=sha(result),
                      extensions=[e.id for e in extensions], capabilities=values,
                      changed_bytes=sum(a != b for a, b in zip(image, result)))
        (out / 'settings.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(report, indent=2))
        return 0
    except (ValueError, OSError) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')
