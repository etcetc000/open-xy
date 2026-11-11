"""The screen kit (docs/screen-kit.md): its worked examples still draw the guide's approved
images, its checks catch what the display API refuses, and its preview writes a sheet."""
from contextlib import redirect_stderr, redirect_stdout
import io
from pathlib import Path
import re
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

from tools.screen_kit import kit, preview

ROOT = Path(__file__).resolve().parents[1]
IMAGES = ROOT / 'docs/images/synth-screens'
ELEMENT = re.compile(r'<(path|circle)\b[^>]*/>')
NUMBER = re.compile(r'-?\d+(?:\.\d+)?')


def drawing(svg):
    """The drawing's paths and circles in a guide image: everything after the backdrop and the
    eight header bar blocks."""
    return [m.group(0) for m in ELEMENT.finditer(svg)][9:]


def close(a, b):
    """Same element, paint and shape, each number within the images' two decimals."""
    if NUMBER.sub('#', a) != NUMBER.sub('#', b):
        return False
    return all(abs(float(x) - float(y)) <= .011 for x, y in zip(NUMBER.findall(a), NUMBER.findall(b)))


class Examples(unittest.TestCase):
    def test_every_guide_example_has_a_kit_source(self):
        names = {preview.slug(s.NAME) for s in preview.examples()}
        images = {p.stem for p in IMAGES.glob('*.svg') if not p.stem.endswith('-range') and not p.stem.startswith('grey-')}
        self.assertEqual(names, images)

    def test_kit_examples_draw_the_approved_images(self):
        # The guide's examples are approved drawings; a change to the kit must not alter them.
        for screen in preview.examples():
            name = preview.slug(screen.NAME)
            with self.subTest(name):
                frames = [(IMAGES / f'{name}.svg').read_text(encoding='utf-8')]
                frames += re.split(r'(?=<svg x=)', (IMAGES / f'{name}-range.svg').read_text(encoding='utf-8'))[1:]
                for svg, values in zip(frames, (screen.HERO or (.5,) * 4, (0,) * 4, (1,) * 4)):
                    want, got = drawing(svg), screen.draw(*values)
                    self.assertEqual(len(want), len(got))
                    bad = [(w, g) for w, g in zip(want, got) if not close(w, g)]
                    self.assertEqual(bad, [], f'{name} at {values}')

    def test_kit_examples_fit_the_display_api(self):
        for screen in preview.examples():
            with self.subTest(screen.NAME):
                self.assertEqual(kit.lint(screen.draw, screen.SDK), [])
                self.assertLessEqual(kit.measure(screen.draw)[2], 4, 'more than four outlines change shape')


class Checks(unittest.TestCase):
    def test_lint_names_what_the_display_api_refuses(self):
        self.assertEqual(kit.lint(lambda a, b, c, d: [kit.circle(240, 120, 10, kit.L)]), [])
        self.assertIn('#FF0000 is not one of the guide\'s greys', kit.lint(lambda a, b, c, d: [kit.circle(1, 1, 1, '#FF0000')]))
        many = lambda a, b, c, d: [kit.circle(i, 100, 1, kit.L) for i in range(300)]
        self.assertTrue(kit.lint(many)[0].startswith('300 nodes'))
        self.assertEqual(kit.lint(many, '1.8'), [])
        repeated = '<path d="M0 0 L10 0 L0 0 L5 5 Z" fill="#CDCDCD"/>'
        self.assertTrue(kit.lint(lambda a, b, c, d: [repeated])[0].startswith('an outline repeats a point'))
        self.assertTrue(kit.lint(lambda a, b, c, d: ['<rect width="1"/>'])[0].startswith('only paths'))
        with self.assertRaisesRegex(ValueError, '65 points'):
            kit.path([(i, i % 2) for i in range(65)])

    def test_measure_counts_reshaping_and_redraw(self):
        def screen(a, b, c, d):
            return [kit.rect(20, 40, lerp_w(a), 10, kit.L),                       # grows: reshapes
                    kit.circle(100 + 100 * b, 100, 5 + 5 * c, kit.D6),            # moves and scales: free
                    kit.path(kit.turned([(0, 0), (20, 0), (10, 30)], 10, 10, 90 * d), kit.D3)]   # turns: free
        lerp_w = lambda v: kit.lerp(10, 100, v)
        nodes, texts, reshaping, peak = kit.measure(screen)
        self.assertEqual((nodes, texts, reshaping, peak), (3, 0, 1, [1, 1, 1, 1]))
        five = lambda a, b, c, d: [kit.rect(20 * i, 40, 5 + 10 * a, 10, kit.L) for i in range(5)]
        self.assertIn('5 outlines change shape', ' '.join(kit.report(five, ['a', 'b', 'c', 'd'])[1:]))
        with self.assertRaisesRegex(ValueError, 'same number of elements'):
            kit.measure(lambda a, b, c, d: [kit.PARKED] * (1 + round(a)))

    def test_report_flags_a_change_that_is_easy_to_miss(self):
        def screen(a, b, c, d):
            return [kit.circle(100, 100, 3, kit.RAMP[round(8 * a)]),           # a tiny dot that brightens
                    kit.circle(200 + 4 * b, 100, 20, kit.L),                    # a big disc that barely moves
                    kit.circle(300, 60 + 100 * c, 20, kit.L),                   # a disc that travels
                    kit.circle(400, 100, 10 + 30 * d, kit.D4)]                  # a disc that grows
        lines = ' '.join(kit.report(screen, ['a', 'b', 'c', 'd']))
        self.assertIn('a changes only a 6 x 6 px area', lines)
        self.assertIn('b moves its parts at most 4 px', lines)
        self.assertNotIn('c changes', lines)
        self.assertNotIn('d changes', lines)

    def test_report_notes_departures_from_stock(self):
        heavy = lambda a, b, c, d: [kit.rect(100, 100, 40, 40, kit.D4, kit.K, 2)]
        crowded = lambda a, b, c, d: [kit.circle(240, 30, 5, kit.L)]
        hanging = lambda a, b, c, d: [kit.rect(160, 20, 160, 60, kit.L)]
        self.assertIn('a black outline is 2 px', ' '.join(kit.stock_notes(heavy)))
        self.assertIn('a part reaches y 25, 5 px under the header', ' '.join(kit.stock_notes(crowded)))
        self.assertEqual(kit.stock_notes(hanging), [])
        self.assertEqual(kit.stock_notes(lambda a, b, c, d: kit.box(240, 105, 0, 0, 0, 160, 60, 30, kit.D6)), [])
        small = lambda a, b, c, d: kit.box(240, 120, 0, 0, 0, 30, 30, 30, kit.D6)
        self.assertIn('the drawing spans only', ' '.join(kit.stock_notes(small)))
        wave = lambda a, b, c, d: [kit.line(300, 120, 500 + 40 * a, 120, 1, kit.L)]
        self.assertIn('a part runs 60 px past the right edge', ' '.join(kit.stock_notes(wave)))
        self.assertNotIn('edge', ' '.join(kit.stock_notes(lambda a, b, c, d: [kit.PARKED])))

    def test_report_flags_a_changing_outline_too_wide_to_write(self):
        wide = lambda a, b, c, d: [kit.path([(100, 100), (380, 100), (380, 110 + 20 * a), (100, 110)], kit.L)]
        self.assertIn('an outline that changes shape spans 280 px', ' '.join(kit.report(wide, ['a', 'b', 'c', 'd'])))
        fixed = lambda a, b, c, d: [kit.rect(0, 200, 480, 1, kit.D2)]
        self.assertNotIn('changes shape spans', ' '.join(kit.report(fixed, ['a', 'b', 'c', 'd'])))

    def test_parts_keep_the_stock_drawing_rules(self):
        faces = kit.box(240, 100, 0, 0, 0, 20, 20, 20, kit.D6)
        self.assertEqual([re.search(r'fill="([^"]+)"', f).group(1) for f in faces], [kit.D6, kit.D5, kit.D6])
        self.assertTrue(all('stroke="#000000" stroke-width="0.5"' in f for f in faces + kit.slab(240, 100, 0, 0, 0, 60, 40, 8, kit.D2)))
        self.assertEqual(len(kit.plate(240, 100, 50, 50, 4, kit.D2, kit.D1)), 2)
        self.assertEqual(len(kit.cylinder(240, 100, 20, 4, kit.D6)), 2)
        lines = kit.hatch([(0, 0), (40, 0), (40, 40), (0, 40)], 45, 8, 1, kit.D4)
        self.assertGreater(len(lines), 4)
        for e in lines:
            self.assertTrue(all(-1 <= float(v) <= 41 for v in NUMBER.findall(e.split('fill')[0])))
        self.assertEqual(kit.cell_random(3, 4, 5), kit.cell_random(3, 4, 5))
        self.assertEqual(kit.euclid_hits(4), [0, 4, 8, 12])

    def test_render_uses_the_sdk_header(self):
        root = ET.fromstring(kit.render(lambda a, b, c, d: [kit.circle(240, 120, 10, kit.L)], (0, .5, .25, 1), ['one', 'two', 'three', 'four']))
        groups = {g.get('id'): g for g in root}
        self.assertEqual([g.get('id') for g in root], ['bg', 'bar', 'values', 'titles', 'drawing'])
        self.assertEqual([t[0].text for t in groups['titles']], ['one', 'two', 'three', 'four'])
        self.assertEqual([t[0].text for t in groups['values']], ['0', '50', '25', '100'])
        self.assertEqual(len(groups['drawing']), 1)


class Preview(unittest.TestCase):
    def test_preview_writes_one_sheet_of_every_setting(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(preview, 'png', return_value=False), \
                redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            out = Path(tmp) / 'blinds'
            self.assertEqual(preview.main(['blinds', '--out', str(out)]), 0)
            frames = sorted(p.name for p in (out / 'frames').iterdir())
            self.assertEqual(frames, ['1-low-100.svg', '2-mid-100.svg', '3-high-100.svg', '4-air-100.svg',
                                      'all-0.svg', 'all-100.svg', 'all-50.svg', 'chosen.svg'])
            sheet = (out / 'sheet.svg').read_text(encoding='utf-8')
            ET.fromstring(sheet)
            self.assertIn('guide example blinds', sheet)
            self.assertIn('12 nodes', (out / 'report.txt').read_text(encoding='utf-8'))
            self.assertEqual(preview.main(['blinds', '--out', str(out)]), 1)     # never into a used folder

    def test_compare_puts_versions_on_one_sheet(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(preview, 'png', return_value=False), redirect_stdout(io.StringIO()):
            out = Path(tmp) / 'pick'
            self.assertEqual(preview.main(['blinds', 'blinds', '--out', str(out), '--control', 'mid']), 0)
            sheet = (out / 'sheet.svg').read_text(encoding='utf-8')
            ET.fromstring(sheet)
            self.assertIn('A · Blinds', sheet)
            self.assertIn('B · Blinds', sheet)
            self.assertIn('mid 100', sheet)

    def test_preview_keeps_stock_screens_out_of_checkouts(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(preview, 'png', return_value=False), \
                redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            mine = Path(tmp) / 'stock.svg'
            mine.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="480" height="220"/>', encoding='utf-8')
            self.assertEqual(preview.main(['blinds', '--out', str(Path(tmp) / 'out'), '--stock', str(mine)]), 0)
            sheet = (Path(tmp) / 'out/sheet.svg').read_text(encoding='utf-8')
            self.assertIn('stock stock', sheet)
            self.assertNotIn('guide example', sheet)                     # the stock screens take the examples' place
            self.assertNotIn('every control at 50', sheet)               # blinds has a chosen setting
            self.assertEqual(preview.main(['blinds', '--out', str(ROOT / 'local/never'), '--stock', str(mine)]), 1)
            self.assertFalse((ROOT / 'local/never').exists())

    def test_preview_loads_an_authors_screen_file(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(preview, 'png', return_value=False), redirect_stdout(io.StringIO()):
            source = Path(tmp) / 'mine.py'
            source.write_text('from tools.screen_kit.kit import *\nLABELS = ["a", "b", "c", "d"]\n'
                              'def draw(a, b, c, d):\n    return [circle(240, 120, 10 + 20 * a, L)]\n', encoding='utf-8')
            self.assertEqual(preview.main([str(source), '--out', str(Path(tmp) / 'out'), '--like', 'Cube ring']), 0)
            self.assertIn('guide example cube-ring', (Path(tmp) / 'out/sheet.svg').read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
