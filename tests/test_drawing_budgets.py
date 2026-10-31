"""The same declared shapes must fit metadata, C++ and the browser wire layout."""
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from tooling.definitions import ROOT, validate_display, display_frame_bytes
from test_sample_services import HOST_CXX


class DrawingBudgets(unittest.TestCase):
    def test_shared_budget_cases(self):
        rows = json.loads((ROOT / 'tests/fixtures/drawing-budgets.json').read_text())
        checks = []
        for row in rows:
            display = dict(zip(('paths', 'points', 'nodes', 'texts', 'palette'), row['shape']))
            display.update(stateBytes=4, stateAlignment=4)
            args = ','.join(map(str, row['shape']))
            if row['bytes'] is None:
                with self.assertRaises(ValueError):
                    validate_display(display)
                checks.append('static_assert(!frame_budget(%s), "reject budget");' % args)
            else:
                validate_display(display)
                self.assertEqual(display_frame_bytes(display), row['bytes'])
                checks.append('static_assert(frame_budget(%s), "accept budget");' % args)
                checks.append('static_assert(sizeof(Frame<%s>) == %d, "frame layout");' % (args, row['bytes']))
        if not HOST_CXX:
            self.skipTest('No working host C++ compiler')
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'budget.cpp'
            source.write_text('#include "opxy/draw.hpp"\nusing namespace opxy::draw;\n' + '\n'.join(checks))
            subprocess.run([HOST_CXX, '-std=gnu++11', '-fsyntax-only', '-I' + str(ROOT / 'sdk/include'), str(source)],
                           check=True, capture_output=True, timeout=120)

    def test_many_independent_rectangles_keep_geometry_bounds(self):
        if not HOST_CXX:
            self.skipTest('No working host C++ compiler')
        with tempfile.TemporaryDirectory() as folder:
            binary = str(Path(folder) / 'drawing.exe')
            subprocess.run([HOST_CXX, '-std=gnu++11', '-I' + str(ROOT / 'sdk/include'),
                            str(ROOT / 'tests/fixtures/drawing_budget.cpp'), '-o', binary],
                           check=True, capture_output=True, timeout=120)
            subprocess.run([binary], check=True, timeout=30)
