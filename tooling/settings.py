"""Settings extensions on an existing OS image: show or apply their capabilities.

A capability is fixed reviewed bytes at stock sites (tooling.extensions.capabilities),
so it can be read from, or written into, stock OS 1.2.1 or any open-xy OS made from
it without rebuilding its machines: open-xy builds add their module record before
FINAL and leave every stock record, and so every capability site, where stock has it.
Each site must hold stock bytes or one of the capability's reviewed encodings; the
output is a copy with only those bytes changed. No device operations.
"""
import argparse
import hashlib
import json
from pathlib import Path

from .extensions import capabilities, capability_settings, chosen_settings, extension_at
from .formats import ldr, mapped_offset, require
from .pipeline import ROOT, add_overwrite_argument, config, fresh_output, read


def capability_edits(profile, name):
    return [p for p in profile['patches'] if p.get('service') == name]


def site(image, blocks, patch):
    offset = int(patch['file_offset'], 16)
    require(mapped_offset(blocks, int(patch['address'], 16), patch['length']) == offset,
            'Capability site is not where OS 1.2.1 holds it: ' + patch['name'])
    return offset, image[offset:offset + patch['length']].hex()


def encodings(patch):
    return {patch['before'], patch['after'], *patch.get('choices', {}).values()}


def state(image, profile=None):
    """Capability -> None (off), True (on) or its setting value, read from the image's bytes."""
    profile = profile or config()
    require(len(image) >= profile['stock_bytes'], 'Not OP-XY OS 1.2.1 or an open-xy OS made from it')
    blocks = ldr(image)
    require(len(blocks) >= profile['stock_records'], 'Not OP-XY OS 1.2.1 or an open-xy OS made from it')
    result = {}
    for name in capabilities():
        held = {}
        for patch in capability_edits(profile, name):
            _, data = site(image, blocks, patch)
            require(data in encodings(patch), 'Unexpected bytes at a capability site: ' + patch['name'])
            held[patch['address']] = data
        edits = capability_edits(profile, name)
        if all(held[p['address']] == p['before'] for p in edits):
            result[name] = None
            continue
        require(all(held[p['address']] == p['after'] for p in edits if 'choices' not in p),
                'Capability only partly applied: ' + name)
        chosen = [int(v) for p in edits if 'choices' in p for v, b in p['choices'].items() if b == held[p['address']]]
        require(len(set(chosen)) <= 1, 'Inconsistent capability setting: ' + name)
        result[name] = chosen[0] if chosen else True
    return result


def apply(image, extensions, overrides=None, profile=None):
    """A copy of `image` with every capability the extensions declare turned on, each
    setting at its override, else the first declaring extension's value, else its default."""
    profile, overrides = profile or config(), dict(overrides or {})
    specs = capability_settings()
    state(image, profile)  # every site holds stock or reviewed bytes
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
            after = (patch['choices'][str(values.get(name, specs[name]['default']))] if 'choices' in patch
                     else patch['after'])
            patched[offset:offset + patch['length']] = bytes.fromhex(after)
    result = bytes(patched)
    require(len(ldr(result)) == len(blocks), 'Loader records changed')
    return result, {n: values.get(n, specs[n]['default']) if n in specs else True for n in dict.fromkeys(declared)}


def parse_setting(text):
    name, separator, value = text.partition('=')
    require(separator and name and value.lstrip('-').isdigit(), 'A setting is <capability>=<whole number>')
    return name, int(value)


def main(argv=None):
    parser = argparse.ArgumentParser(prog='sdk.py settings', description=__doc__.split('\n')[0])
    commands = parser.add_subparsers(dest='command', required=True)
    show = commands.add_parser('show', help='List the capabilities an OS image has on, with their settings')
    show.add_argument('--image', type=Path, required=True, help='os.ldr: stock OS 1.2.1 or an open-xy OS made from it')
    put = commands.add_parser('apply', help='Write a copy of an OS image with settings extensions applied')
    put.add_argument('--image', type=Path, required=True, help='os.ldr: stock OS 1.2.1 or an open-xy OS made from it')
    put.add_argument('--extension', type=Path, action='append', required=True, help='settings extension folder; repeatable')
    put.add_argument('--set', action='append', default=[], metavar='CAPABILITY=VALUE',
                     help='override a setting, for example --set release_floor=-40')
    put.add_argument('--out', type=Path, required=True, help='new folder outside the checkout for os.ldr and settings.json')
    add_overwrite_argument(put)
    args = parser.parse_args(argv)
    try:
        image = read(args.image)
        if args.command == 'show':
            print(json.dumps(state(image), indent=2))
            return 0
        out = Path(args.out).resolve()
        require(not out.is_relative_to(ROOT), 'Firmware stays outside the checkout: choose --out elsewhere')
        extensions = [extension_at(folder) for folder in args.extension]
        result, values = apply(image, extensions, dict(parse_setting(s) for s in args.set))
        out = fresh_output(args.out, args.overwrite, keep=[args.image, *args.extension])
        (out / 'os.ldr').write_bytes(result)
        report = dict(format='opxy-settings/1', source=str(Path(args.image).resolve()),
                      source_sha256=hashlib.sha256(image).hexdigest(), sha256=hashlib.sha256(result).hexdigest(),
                      extensions=[e.id for e in extensions], capabilities=values,
                      changed_bytes=sum(a != b for a, b in zip(image, result)))
        (out / 'settings.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(report, indent=2))
        return 0
    except (ValueError, OSError) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')
