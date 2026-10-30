"""Locate a firmware profile's reviewed edits and stock addresses in another OS release.

Porting aid for a new stock OS (for example OP-XY 1.2.1), run against two
private stock images. It never writes firmware. For every profile edit it finds
the same code in the target image by its surrounding bytes, tolerant of the
relative branches and addresses that change when code moves, and for a hook
that replaces a call it checks the call reaches the same stock function. For
every stock address the platform layer and tooling name, it finds the same
bytes in the target. The report says which map cleanly and which need review, and
it writes the address map and a candidate profile for the new release. The output
holds excerpts of stock code, so it belongs in the private folder, never a checkout.
See docs/porting.md.

    python tools/os_port.py --source <private>/stock-1.2.0/os.ldr --target <private>/stock-1.2.1/os.ldr --out <private>/ports/1.2.1-001
"""
import argparse
import json
from pathlib import Path
import re
import struct
import sys

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tooling.firmware import absolute_target, decode_branch  # noqa: E402
from tooling.formats import ldr, mapped_offset, require, sha  # noqa: E402
from tooling.release import outside_checkouts  # noqa: E402
from tooling.stock_tables import decode as decode_immediate  # noqa: E402

# Bytes compared either side of an edit, and how far around its expected place to look.
CONTEXT, WINDOW = 48, 40000
# Agreement of the context and its lead over the next best place, for a confident match;
# identical edit bytes are also accepted with weaker context and a clear lead.
AGREE, MARGIN = 0.8, 0.2
IDENTICAL_AGREE, IDENTICAL_MARGIN = 0.7, 0.25
# Where the SDK names stock addresses: platform layer, tooling, linker script, tools, tests and docs.
SOURCES = ('sdk/platform/opxy-1.2.1', 'tooling', 'linker', 'tools', 'tests', 'docs')
SUFFIXES = ('.py', '.mjs', '.cpp', '.hpp', '.s', '.in', '.inc', '.ld', '.md')
# A stock address in code, data or RAM, with or without 0x (comments and docs name them
# bare); round region bases (low half-word 0) are constants.
ADDRESS = re.compile(r'(?<![0-9a-fA-F\w])(?:0x)?((?:8[4-9a-f]|08|11[89ab])[0-9a-fA-F]{5,6})(?![0-9a-fA-F])')
# An address assembly builds from half-words: `P0.H = 0x11a0; P0.L = 0x969c;`, a `.short`
# absolute call or jump (0xde00/0xdc00, high, low, 0) or a high, low macro argument pair.
HALVES = re.compile(r'0x((?:8[4-9a-f]|08|11[89ab])[0-9a-fA-F]{2})(?:;\s*\w+\.L =|,) 0x([0-9a-fA-F]{4})\b')
# A table edit's immediate form by its length (tooling.stock_tables).
IMMEDIATE = {8: 'imm32', 4: 'imm16', 2: 'imm7'}


class Image:
    def __init__(self, path=None, data=None):
        self.data = Path(path).read_bytes() if data is None else bytes(data)
        self.bytes = np.frombuffer(self.data, np.uint8)
        self.records = ldr(self.data)
        self.blocks = [b for b in self.records if not b['fill'] and b['count']]

    def offset(self, address):
        """The file offset of a stored byte at `address`, or None (RAM, a fill or ambiguous)."""
        try:
            return mapped_offset(self.records, address, 1)
        except ValueError:
            return None

    def address(self, offset):
        for b in self.blocks:
            start = b['offset'] + 16
            if start <= offset < start + b['count']:
                return b['target'] + offset - start
        return None


def locate(source, target, offset, length, context=CONTEXT, window=WINDOW, expected=None):
    """(target offset, agreement, margin) of the source bytes [offset, offset+length) with context,
    searched within `window` of `expected` (default: the same offset). Among equally good places
    (repeated code), the one nearest `expected` wins, its margin then measured against places
    that agree less."""
    low = max(0, offset - context)
    pattern = source.bytes[low: offset + length + context]
    size = len(pattern)
    centre = low + ((expected - offset) if expected is not None else 0)
    start = max(0, centre - window)
    stop = min(len(target.bytes) - size, centre + window)
    if stop <= start:
        return None, 0.0, 0.0
    views = np.lib.stride_tricks.sliding_window_view(target.bytes[start: stop + size], size)
    score = (views == pattern).sum(axis=1)
    best = int(score.argmax())
    ties = np.flatnonzero(score == score[best])
    if expected is not None and len(ties) > 1:
        best = int(ties[np.abs(start + ties + (offset - low) - expected).argmin()])
        others = np.where(score == score[best], 0, score)
    else:
        others = score.copy()
    others[max(0, best - 4): best + 5] = 0
    return start + best + (offset - low), float(score[best]) / size, float(score[best] - others.max()) / size


def call_target(site, data):
    """The stock function a replaced instruction calls or jumps to, or None."""
    try:
        if len(data) == 8:
            return absolute_target(data)
        return decode_branch(site, data[:4])[0]
    except ValueError:
        return None


def same_function(source, target, old, new, length=64):
    """Agreement of the first bytes of a stock function and its candidate in the target."""
    a, b = source.offset(old), target.offset(new)
    if a is None or b is None:
        return 0.0
    return float((source.bytes[a: a + length] == target.bytes[b: b + length]).mean())


def port_edits(source, target, profile, loads):
    """loads: constant_loads of the source and the target, to find hooks through their calls.
    A first pass searches around each edit's old offset; edits it misses are searched again
    around the shift of the nearest edit the first pass verified, so a region that moved
    further than the window is still found."""
    rows = [place_edit(source, target, edit, loads) for edit in profile['patches']]
    anchors = [(int(e['file_offset'], 16), r['delta']) for e, r in zip(profile['patches'], rows)
               if r['status'] == 'verified']
    for i, (edit, row) in enumerate(zip(profile['patches'], rows)):
        if row['status'] == 'review' and anchors:
            offset = int(edit['file_offset'], 16)
            shift = min(anchors, key=lambda a: abs(a[0] - offset))[1]
            again = place_edit(source, target, edit, loads, expected=offset + shift)
            if again['status'] != 'review':
                rows[i] = again
    return rows


def place_edit(source, target, edit, loads, expected=None):
    """One edit's row: where it is in the target and how sure that is."""
    offset, length = int(edit['file_offset'], 16), edit['length']
    before = bytes.fromhex(edit['before'])
    require(source.data[offset: offset + length] == before, "Source image is not this profile's stock OS")
    found, agree, margin = locate(source, target, offset, length, expected=expected)
    row = dict(name=edit.get('name') or edit.get('symbol'), kind=edit['kind'], address=edit['address'],
               agree=round(agree, 3), margin=round(margin, 3))
    identical = found is not None and target.data[found: found + length] == before
    weak = found is None or agree < AGREE or margin < MARGIN
    if weak and not (identical and agree >= IDENTICAL_AGREE and margin >= IDENTICAL_MARGIN):
        # A hook replacing a call: find the called function in the target through
        # its callers, then the call to it whose surroundings match best.
        old = call_target(int(edit['address'], 16), before) if edit['kind'] == 'hook' else None
        found = by_call_site(source, target, offset, old, loads) if old is not None else None
        if found is None:
            return dict(row, status='review', reason='no confident match')
        row['how'] = 'call site'
    new_before = target.data[found: found + length]
    row.update(target_address=hex(target.address(found)), target_offset=hex(found), target_before=new_before.hex(),
               delta=found - offset)
    if new_before == before:
        return dict(row, status='verified', reason='identical bytes')
    old = call_target(int(edit['address'], 16), before) if edit['kind'] == 'hook' else None
    new = call_target(target.address(found), new_before) if old is not None else None
    if old is not None and new is not None:
        similar = same_function(source, target, old, new)
        return dict(row, status='verified' if similar >= 0.7 else 'review',
                    reason=f'calls {hex(old)} -> {hex(new)} ({similar:.0%} alike)')
    # Found in place, but the replaced instructions themselves changed (an address
    # load, a moved table or reworked code): read them.
    return dict(row, status='located', reason=f'{edit["kind"]} bytes differ at the matching place')


def by_call_site(source, target, offset, function, loads, context=48):
    """The target offset of the call, to `function`'s counterpart, whose surroundings best match
    the source call at `offset`, when it clearly beats the other calls; else None."""
    (source_values, _), (target_values, target_offsets) = loads
    votes = {}
    for at in source_values.get(function, [])[:20]:
        new, agree, _ = locate(source, target, at, 8, context=24)
        if new is not None and agree >= 0.75 and new in target_offsets and \
                target.data[new + 1] == source.data[at + 1]:
            votes[target_offsets[new]] = votes.get(target_offsets[new], 0) + 1
    if not votes:
        return None
    counterpart = max(votes, key=votes.get)
    if same_function(source, target, function, counterpart) < 0.7:
        return None
    pattern = source.bytes[offset - context: offset + context]
    scored = sorted(((float((pattern == target.bytes[b - context: b + context]).mean()), b)
                     for b in target_values.get(counterpart, [])
                     if len(target.bytes[b - context: b + context]) == len(pattern)), reverse=True)
    if not scored or scored[0][0] < 0.85 or (len(scored) > 1 and scored[0][0] - scored[1][0] < 0.1):
        return None
    return scored[0][1]


def constant_loads(image):
    """{constant: [file offset]} and {file offset: constant} of 32-bit constants in Blackfin+
    64-bit instructions (0xD8xx-0xDBxx: a register load or an absolute memory access, the
    high then low half-word of the address following the opcode), which is how stock code
    addresses its data."""
    by_value, by_offset = {}, {}
    for block in image.blocks:
        start, count = block['offset'] + 16, block['count'] & ~1
        if block['target'] & 1 or count < 8:
            continue
        words = np.frombuffer(image.data[start: start + count], '<u2')
        for i in np.flatnonzero((words[:-3] & 0xfc00) == 0xd800).tolist():
            constant, at = int(words[i + 1]) << 16 | int(words[i + 2]), start + 2 * i
            by_value.setdefault(constant, []).append(at)
            by_offset[at] = constant
        # Calls reach stock functions: an absolute call (0xDE00, high, low, 0) or a
        # PC-relative CALL (0xE3xx); their targets are constants of the same kind.
        for i in np.flatnonzero(words[:-3] == 0xde00).tolist():
            if words[i + 3] == 0:
                constant, at = int(words[i + 1]) << 16 | int(words[i + 2]), start + 2 * i
                by_value.setdefault(constant, []).append(at)
                by_offset[at] = constant
        for i in np.flatnonzero((words[:-1] & 0xff00) == 0xe300).tolist():
            at = start + 2 * i
            constant = call_target(block['target'] + 2 * i, image.data[at: at + 4])
            if constant is not None:
                by_value.setdefault(constant, []).append(at)
                by_offset[at] = constant
    return by_value, by_offset


def port_addresses(source, target, loads, anchors, edits=(), own=()):
    """anchors: (source file offset, file offset shift) of verified matches, to place repeated code;
    edits: the edit rows, whose sites tooling also names; own: (start, end, shift) of SDK-owned
    memory (the module reservation), which moves with the stock heap rather than by content."""
    found = {}
    for name in SOURCES:
        path = ROOT / name
        for file in ([path] if path.is_file() else sorted(path.rglob('*'))):
            if file.is_file() and file.suffix in SUFFIXES and '__pycache__' not in file.parts:
                text = file.read_text(encoding='utf-8', errors='replace')
                values = [int(m.group(1), 16) for m in ADDRESS.finditer(text)]
                values += [int(m.group(1), 16) << 16 | int(m.group(2), 16) for m in HALVES.finditer(text)]
                for value in values:
                    if value & 0xffff:
                        found.setdefault(value, set()).add(str(file.relative_to(ROOT).as_posix()))
    shift = lambda offset: min(anchors, key=lambda a: abs(a[0] - offset))[1] if anchors else 0
    (source_loads, _), (_, target_loads) = loads
    bases = sorted(source_loads)

    def by_references(address):
        # The code that loads the address, or the struct it lies in (within 1 KiB), found in
        # the target; the constant it loads there, plus the same field offset, by majority.
        import bisect
        i = bisect.bisect_right(bases, address) - 1
        if i < 0 or address - bases[i] > 1024:
            return None, 0
        base = bases[i]
        votes = {}
        for at in source_loads[base][:12]:
            new, agree, margin = locate(source, target, at, 8, context=24, expected=at + shift(at))
            if new is None or agree < AGREE:
                continue
            # The same instruction at the matching place: same opcode, nearest first.
            opcode = source.data[at: at + 2]
            for probe in (new, new - 2, new + 2, new - 4, new + 4):
                if probe in target_loads and target.data[probe: probe + 2] == opcode:
                    value = target_loads[probe] + address - base
                    votes[value] = votes.get(value, 0) + 1
                    break
        if not votes:
            return None, 0
        value = max(votes, key=votes.get)
        return value, votes[value] / sum(votes.values())

    sites = {int(r['address'], 16): r for r in edits if r['status'] in ('verified', 'located')}
    rows = []
    for address in sorted(found):
        row = dict(address=hex(address), files=sorted(found[address]))
        if address in sites:
            rows.append(dict(row, status='mapped', target_address=sites[address]['target_address'], how='edit site'))
            continue
        moved = next((shift for low, high, shift in own if low <= address < high), None)
        if moved is not None:
            rows.append(dict(row, status='mapped', target_address=hex(address + moved), how='SDK reservation'))
            continue
        offset = source.offset(address)
        if offset is not None:
            new, agree, margin = locate(source, target, offset, 16, context=16, expected=offset + shift(offset))
            if new is not None and agree >= AGREE and margin >= MARGIN:
                rows.append(dict(row, status='mapped', target_address=hex(target.address(new)), how='bytes',
                                 agree=round(agree, 3)))
                continue
        value, share = by_references(address)
        if value is not None and share >= 0.75:
            rows.append(dict(row, status='mapped', target_address=hex(value), how='references', agree=round(share, 3)))
        else:
            rows.append(dict(row, status='review',
                             reason='no confident match by bytes or by the code that loads it' +
                                    (f' (references disagree: {share:.0%})' if value is not None else '')))
    return rows


def heap_fill(image, base=None, near=None):
    """The stock heap's zero-fill record: the one at `base` (the profile's heap_fill_base), else
    the fill nearest `near` bytes long (the source heap's size)."""
    fills = [b for b in image.records if b['fill']]
    if base is not None:
        exact = [b for b in fills if b['target'] == base]
        require(len(exact) == 1, "No heap fill at the profile's heap_fill_base: is this its stock OS?")
        return exact[0]
    require(fills and near is not None, 'No zero-fill record (the stock heap) in this image')
    return min(fills, key=lambda b: abs(b['count'] - near))


def candidate_table_after(edit, before, reservation, shift):
    """A relocated-table edit's replacement at its new place: the new stock instruction with the
    relocated value, moved with the reservation when that value is an address inside it."""
    kind = IMMEDIATE[edit['length']]
    _, value = decode_immediate(kind, bytes.fromhex(edit['after']))
    if reservation[0] <= value < reservation[1]:
        value += shift
    opcode, _ = decode_immediate(kind, before)
    if kind == 'imm32':
        return struct.pack('<4H', opcode, value >> 16, value & 0xffff, 0).hex()
    if kind == 'imm16':
        return struct.pack('<2H', opcode, value & 0xffff).hex()
    return struct.pack('<H', opcode | (value & 0x7f) << 3).hex()


def regions(image):
    """Loaded bytes per memory area, by the address's top byte."""
    totals = {}
    for b in image.blocks:
        key = hex(b['target'] >> 24)
        totals[key] = totals.get(key, 0) + b['count']
    return dict(sorted(totals.items()))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--source', required=True, type=Path, help='stock os.ldr the profile was made for')
    parser.add_argument('--target', required=True, type=Path, help='stock os.ldr of the new release')
    parser.add_argument('--profile', default=ROOT / 'profiles/opxy-1.2.1.json', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args(argv)
    require(not args.out.exists(), 'Use a fresh output directory')
    require(outside_checkouts(args.out), 'The report holds stock code: write it outside every checkout '
            '(the private folder, docs/porting.md)')
    profile = json.loads(args.profile.read_text(encoding='utf-8'))
    source, target = Image(args.source), Image(args.target)
    loads = constant_loads(source), constant_loads(target)
    edits = port_edits(source, target, profile, loads)
    # The module reservation sits below stock code at the top of the shrunk heap; stock code
    # starts where the heap fill ends, so the reservation moves with it.
    old_fill = heap_fill(source, base=profile['heap_fill_base'])
    new_fill = heap_fill(target, near=old_fill['count'])
    old_end, new_end = old_fill['target'] + old_fill['count'], new_fill['target'] + new_fill['count']
    reservation, shift = (old_end - profile['reserved_bytes'], old_end), new_end - old_end
    own = [(*reservation, shift)]
    addresses = port_addresses(source, target, loads, [(int(r['target_offset'], 16) - r['delta'], r['delta'])
                                                       for r in edits if r['status'] == 'verified'], edits, own)
    report_layout = dict(heap_fill_base=hex(new_fill['target']), heap_fill_bytes=new_fill['count'],
                         module_base=hex(new_end - profile['reserved_bytes']), code_start=hex(new_end))
    report = dict(source_bytes=len(source.data), target_bytes=len(target.data), target_layout=report_layout,
                  source_regions=regions(source), target_regions=regions(target), edits=edits, addresses=addresses)
    args.out.mkdir(parents=True)
    (args.out / 'report.json').write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')
    # The address map (source -> target) and the profile's edits at their target places, as
    # the starting point for the new release's profile and platform address tables. Edits
    # still to review keep their source values and a port_status of 'review'.
    mapped = {r['address']: r['target_address'] for r in addresses if r['status'] == 'mapped'}
    (args.out / 'address-map.json').write_text(json.dumps(mapped, indent=1, sort_keys=True) + '\n', encoding='utf-8')
    candidate = []
    for edit, row in zip(profile['patches'], edits):
        if 'target_address' in row and row['status'] != 'review':
            moved = dict(edit, address=row['target_address'], file_offset=row['target_offset'],
                         before=row['target_before'], port_status=row['status'])
            if edit['kind'] == 'table':
                moved['after'] = candidate_table_after(edit, bytes.fromhex(row['target_before']), reservation, shift)
            candidate.append(moved)
        else:
            candidate.append(dict(edit, port_status='review'))
    # The target image's own facts. What the port also changes (the linker script, stock
    # content) and the release naming stay null until they are reviewed and set.
    candidate_profile = dict(profile, id=None, firmware_version=None, stock_sha256=sha(target.data),
                             stock_bytes=len(target.data), stock_records=len(target.records),
                             module_base=new_end - profile['reserved_bytes'], heap_fill_base=new_fill['target'],
                             heap_fill_bytes=new_fill['count'], linker_sha256=None, stock_catalog_sha256=None,
                             stock_svg_sha256=None, patches=candidate)
    (args.out / 'profile-candidate.json').write_text(json.dumps(candidate_profile, indent=2) + '\n', encoding='utf-8')
    count = lambda rows, status: sum(r['status'] == status for r in rows)
    lines = [f'Edits: {count(edits, "verified")} verified, {count(edits, "located")} located with changed bytes, '
             f'{count(edits, "review")} to review, of {len(edits)}.',
             f'Stock addresses: {count(addresses, "mapped")} mapped, {count(addresses, "review")} to review, '
             f'of {len(addresses)}.']
    for status in ('review', 'located'):
        lines += ['', f'Edits, {status}:'] + [f'  {r["address"]} {r["kind"]:7} {r["name"]}: {r["reason"]}'
                                               for r in edits if r['status'] == status]
    lines += ['', 'Addresses to review:'] + [f'  {r["address"]} {r["reason"]} ({", ".join(r["files"])})'
                                             for r in addresses if r['status'] == 'review']
    (args.out / 'report.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('\n'.join(lines[:2]))
    print('Report: ' + str(args.out))


if __name__ == '__main__':
    main()
