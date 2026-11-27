"""Preview a synth screen drawn with the screen kit (docs/screen-kit.md).

Renders the screen with every control at 0 and 100, at its chosen setting (or every control at
50), and with each control alone at 100 (the others at 50), and puts them on one contact sheet
with its node count, reshaping outlines and redraw cost. The sheet also shows the LIKE guide
examples' range images (every control at 0, then 100), or with --stock, the stock screens in
their place; --like adds examples either way. Every frame, 50 included, is saved in frames/.

    python tools/screen_kit/preview.py <screen.py | example> --out <new folder> [--like <example> ...]
    python tools/screen_kit/preview.py <screen.py> --out <private folder> --stock <stock.svg> ...
    python tools/screen_kit/preview.py <a.py> <b.py> [<c.py>] --out <new folder> [--control <name>]
    python tools/screen_kit/preview.py --list

Given two or three screens, it makes one comparison sheet instead, lettered A, B, C, so the
person can pick a version: each at its chosen setting and with every control at 100, or with
--control, that control at 0 and at 100 while the others stay at the chosen setting.

Writes sheet.svg, sheet.png (macOS only, through qlmanage and sips), report.txt and each
frame as frames/*.svg. Offline; no device operations.
"""
import argparse
import base64
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tooling.formats import require  # noqa: E402
from tooling.release import outside_checkouts  # noqa: E402
from tools.screen_kit import kit  # noqa: E402

EXAMPLES = Path(__file__).resolve().parent / 'examples'
IMAGES = ROOT / 'docs/images/synth-screens'
GAP, PAD, CAPTION, LINE = 10, 20, 22, 18
PAGE, INK, DIM = '#262626', '#E6E6E6', '#9A9A9A'


def slug(name):
    return re.sub(r'[^a-z0-9]+', '-', name.lower()).strip('-')


def load(target):
    """A screen module from a .py file, or a kit example by name ('blinds', 'cd-player')."""
    path = Path(target)
    if path.suffix != '.py':
        path = EXAMPLES / (slug(target).replace('-', '_') + '.py')
        require(path.is_file(), f'no screen file or kit example called {target!r} (try --list)')
    spec = importlib.util.spec_from_file_location('screen_' + path.stem, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    require(callable(getattr(module, 'draw', None)), f'{path} needs draw(a, b, c, d)')
    labels = getattr(module, 'LABELS', None)
    require(isinstance(labels, (list, tuple)) and len(labels) == 4, f'{path} needs LABELS, four control names')
    module.NAME = getattr(module, 'NAME', path.stem)
    module.SDK = getattr(module, 'SDK', '1.0')
    module.HERO = tuple(getattr(module, 'HERO', ()))
    like = getattr(module, 'LIKE', slug(module.NAME) if path.parent == EXAMPLES else None)
    module.LIKE = [like] if isinstance(like, str) else list(like or [])
    return module


def examples():
    return [load(p) for p in sorted(EXAMPLES.glob('*.py'))]


def frames(screen):
    """(caption, file stem, values) for every frame on the sheet, in reading order."""
    out = [('every control at 0', 'all-0', (0, 0, 0, 0)), ('every control at 50', 'all-50', (.5,) * 4),
           ('every control at 100', 'all-100', (1, 1, 1, 1))]
    if screen.HERO:
        out.append(('chosen setting ' + ' '.join(str(round(v * 100)) for v in screen.HERO), 'chosen', screen.HERO))
    for c, name in enumerate(screen.LABELS):
        out.append((f'{name} 100, others 50', f'{c + 1}-{slug(name) or "control"}-100',
                    tuple(1 if i == c else .5 for i in range(4))))
    return out


def _escape(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def _place(svg, x, y, size=None):
    """An SVG document placed at (x, y) inside the sheet, scaled to size (w, h) when given."""
    svg = re.sub(r'<\?xml[^>]*>|<!--.*?-->|<!DOCTYPE[^>]*>', '', svg, flags=re.S).strip()
    head = re.match(r'<svg\b[^>]*>', svg).group(0)                  # drop the file's own placement (Illustrator sets x and y)
    drop = r'\s(?:x|y|width|height)="[^"]*"' if size else r'\s(?:x|y)="[^"]*"'
    at = f'<svg x="{x}" y="{y}"' + (f' width="{size[0]}" height="{size[1]}"' if size else '')
    if size and 'viewBox' not in head:                          # Illustrator files give width and height only
        w, h = (re.search(f'\\s{k}="([\\d.]+)', head) for k in ('width', 'height'))
        at += f' viewBox="0 0 {w.group(1) if w else kit.W} {h.group(1) if h else kit.H}"'
    placed = re.sub(drop, '', head).replace('<svg', at, 1)
    return placed + svg[len(head):]


def _stock(source, x, y):
    """A stock screen at 480 x 220: an SVG file, or a picture (PNG, JPEG) such as a capture of the official guide."""
    if source.suffix.lower() in ('.png', '.jpg', '.jpeg'):
        kind = 'png' if source.suffix.lower() == '.png' else 'jpeg'
        data = base64.b64encode(source.read_bytes()).decode()
        return (f'<image x="{x}" y="{y}" width="{kit.W}" height="{kit.H}" preserveAspectRatio="none" '
                f'href="data:image/{kind};base64,{data}"/>')
    return _place(source.read_text(encoding='utf-8'), x, y, (kit.W, kit.H))


def sheet(screen, rendered, report, like=None, stock=()):
    """The contact sheet as one SVG: report lines, the guide example's range image, then the frames two across."""
    width = 2 * kit.W + GAP
    parts, y = [], PAD
    parts.append(f'<text x="{PAD}" y="{y + 16}" font-size="18" fill="{INK}">{_escape(screen.NAME)}</text>')
    y += 26
    for line in report:
        parts.append(f'<text x="{PAD}" y="{y + 13}" font-size="13" fill="{DIM}">{_escape(line)}</text>')
        y += LINE
    y += 8
    for i, source in enumerate(stock):                          # the person's own stock screens, to look at only
        x = PAD + (i % 2) * (kit.W + GAP)
        parts.append(f'<text x="{x}" y="{y + 15}" font-size="14" fill="{INK}">stock {_escape(Path(source).stem)}</text>')
        parts.append(_stock(Path(source), x, y + CAPTION))
        if i % 2 or i == len(stock) - 1:
            y += CAPTION + kit.H + GAP
    if stock:
        y += GAP
    for name in like or []:
        reference = IMAGES / f'{name}-range.svg'
        require(reference.is_file(), f'no guide example image {reference.name}')
        parts.append(f'<text x="{PAD}" y="{y + 15}" font-size="14" fill="{INK}">guide example {name}: every control at 0, then 100</text>')
        parts.append(_place(reference.read_text(encoding='utf-8'), PAD, y + CAPTION))
        y += CAPTION + kit.H + 2 * GAP
    if screen.HERO:                                             # the chosen setting stands in for 50 on the sheet
        rendered = [r for r in rendered if r[1] != 'all-50']
    for i, (caption, _, values, svg) in enumerate(rendered):
        x = PAD + (i % 2) * (kit.W + GAP)
        parts.append(f'<text x="{x}" y="{y + 15}" font-size="14" fill="{INK}">{_escape(caption)}</text>')
        parts.append(_place(svg, x, y + CAPTION))
        if i % 2 or i == len(rendered) - 1:
            y += CAPTION + kit.H + GAP
    w, h = width + 2 * PAD, y + PAD - GAP
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" '
            f'font-family="Helvetica, Arial, sans-serif"><style>text {{ font-family: Helvetica, Arial, sans-serif }}</style>'
            f'<rect width="{w}" height="{h}" fill="{PAGE}"/>{"".join(parts)}</svg>\n'), w, h


def png(svg_path, w, h, out, scale=1):
    """Render an SVG to PNG with macOS Quick Look: it makes square thumbnails, so centre the sheet
    in a square and crop it back out. Returns False where the tools are missing."""
    if not (shutil.which('qlmanage') and shutil.which('sips')):
        return False
    side = max(w, h)
    body = svg_path.read_text(encoding='utf-8')
    with tempfile.TemporaryDirectory() as tmp:
        square = Path(tmp) / 'square.svg'
        square.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="{side}" height="{side}" viewBox="0 0 {side} {side}">'
                          f'<rect width="{side}" height="{side}" fill="{PAGE}"/>{_place(body, (side - w) / 2, (side - h) / 2)}</svg>',
                          encoding='utf-8')
        subprocess.run(['qlmanage', '-t', '-s', str(side * scale), '-o', tmp, str(square)], check=True, capture_output=True)
        thumb = Path(tmp) / 'square.svg.png'
        require(thumb.is_file(), 'qlmanage made no image')
        subprocess.run(['sips', '-c', str(h * scale), str(w * scale), str(thumb), '--out', str(out)], check=True, capture_output=True)
    return True


def preview(screen, out, like=None, scale=1, stock=()):
    out = Path(out)
    if stock:
        require(all(outside_checkouts(p) for p in stock) and outside_checkouts(out),
                'stock screens stay outside every checkout: keep them, and --out, in a private folder')
    require(not out.exists() or not any(out.iterdir()), f'{out} already has files; pick a new folder')
    (out / 'frames').mkdir(parents=True, exist_ok=True)
    problems = kit.lint(screen.draw, screen.SDK)
    report = kit.report(screen.draw, screen.LABELS, screen.SDK)
    rendered = []
    for caption, stem, values in frames(screen):
        svg = kit.render(screen.draw, values, screen.LABELS)
        (out / 'frames' / f'{stem}.svg').write_text(svg + '\n', encoding='utf-8')
        rendered.append((caption, stem, values, svg))
    text, w, h = sheet(screen, rendered, report, like, stock)
    (out / 'sheet.svg').write_text(text, encoding='utf-8')
    (out / 'report.txt').write_text('\n'.join([screen.NAME] + report) + '\n', encoding='utf-8')
    made = png(out / 'sheet.svg', w, h, out / 'sheet.png', scale)
    return report, problems, made


def compare(screens, out, control=None, scale=1):
    """One sheet of two or three versions side by side, lettered A, B, C."""
    out = Path(out)
    require(2 <= len(screens) <= 3, 'compare two or three versions')
    require(not out.exists() or not any(out.iterdir()), f'{out} already has files; pick a new folder')
    out.mkdir(parents=True, exist_ok=True)
    labels = screens[0].LABELS
    require(control is None or control in labels, f'--control must be one of {", ".join(labels)}')
    width = 2 * kit.W + GAP
    parts, y, lines = [], PAD, []
    for letter, screen in zip('ABC', screens):
        base = screen.HERO or (.5,) * 4
        if control:
            c = list(screen.LABELS).index(control)
            shots = [(f'{control} 0', tuple(0 if i == c else v for i, v in enumerate(base))),
                     (f'{control} 100', tuple(1 if i == c else v for i, v in enumerate(base)))]
        else:
            shots = [('chosen setting' if screen.HERO else 'every control at 50', base), ('every control at 100', (1,) * 4)]
        head = f'{letter} · {screen.NAME}'
        notes = kit.report(screen.draw, screen.LABELS, screen.SDK)
        lines.append('\n'.join([head] + notes))
        parts.append(f'<text x="{PAD}" y="{y + 16}" font-size="18" fill="{INK}">{_escape(head)}</text>')
        y += 26
        for note in notes[1:]:
            parts.append(f'<text x="{PAD}" y="{y + 13}" font-size="13" fill="{DIM}">{_escape(note)}</text>')
            y += LINE
        for i, (caption, values) in enumerate(shots):
            x = PAD + i * (kit.W + GAP)
            parts.append(f'<text x="{x}" y="{y + 15}" font-size="14" fill="{INK}">{_escape(caption)}</text>')
            parts.append(_place(kit.render(screen.draw, values, screen.LABELS), x, y + CAPTION))
        y += CAPTION + kit.H + 2 * GAP
    w, h = width + 2 * PAD, y + PAD - 2 * GAP
    text = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" '
            f'font-family="Helvetica, Arial, sans-serif"><style>text {{ font-family: Helvetica, Arial, sans-serif }}</style>'
            f'<rect width="{w}" height="{h}" fill="{PAGE}"/>{"".join(parts)}</svg>\n')
    (out / 'sheet.svg').write_text(text, encoding='utf-8')
    (out / 'report.txt').write_text('\n\n'.join(lines) + '\n', encoding='utf-8')
    return lines, png(out / 'sheet.svg', w, h, out / 'sheet.png', scale)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('screen', nargs='*', help='a screen .py file or a kit example name; two or three to compare versions')
    parser.add_argument('--out', type=Path, help='a new folder for the sheet and frames')
    parser.add_argument('--like', nargs='+', help='guide examples to show beside it, e.g. blinds spokes (kit examples show their own)')
    parser.add_argument('--scale', type=int, default=1, choices=(1, 2), help='PNG pixels per screen pixel')
    parser.add_argument('--list', action='store_true', help='list the kit examples')
    parser.add_argument('--control', help='when comparing, show this control at 0 and 100')
    parser.add_argument('--stock', nargs='+', default=(), help="stock screens (SVG files from the person's own OP-XY, or pictures of the official guide's) to show beside it, look only; they and --out must be outside every checkout")
    args = parser.parse_args(argv)
    try:
        if args.list:
            for screen in examples():
                print(f'{slug(screen.NAME):14} {", ".join(screen.LABELS)}')
            return 0
        if not args.screen or not args.out:
            parser.error('give a screen and --out')
        if len(args.screen) > 1:
            lines, made = compare([load(t) for t in args.screen], args.out, args.control, args.scale)
            print('\n\n'.join(lines))
            print(f'wrote {args.out / "sheet.svg"}' + (f' and {args.out / "sheet.png"}' if made else ' (no PNG: needs macOS qlmanage and sips)'))
            return 0
        screen = load(args.screen[0])
        like = [slug(n) for n in args.like] if args.like else ([] if args.stock else screen.LIKE)
        report, problems, made = preview(screen, args.out, like, args.scale, args.stock)
    except ValueError as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        return 1
    print('\n'.join([screen.NAME] + report))
    print(f'wrote {args.out / "sheet.svg"}' + (f' and {args.out / "sheet.png"}' if made else ' (no PNG: needs macOS qlmanage and sips)'))
    return 1 if problems else 0


if __name__ == '__main__':
    raise SystemExit(main())
