"""Purpose-built machines for the SDK's tests (tests/fixtures/machines/). The SDK ships
no machines of its own; these are small, valid folders, not copies of published ones.

- probesynth: a synth with a drawing display (two paths) and its panel.svg.fragment.
- probesources: a synth with an extra translation unit and include folder, and a
  display without paths.
- probefx: an effect with a Dry control (dryParameter) and controls of each mapping.
- probesample: a drum-page (page 7) sample engine declaring every optional service.
- probestep: a step component replacing the stock velocity component.
"""
from pathlib import Path

from tooling.machines import machine_at, read_collection

FOLDER = Path(__file__).resolve().parent / 'fixtures/machines'
# Firmware order for a collection holding every kind.
NAMES = ('probesynth', 'probesources', 'probefx', 'probesample', 'probestep')


def fixture(name):
    """A fixture machine, loaded as any machine folder is."""
    return machine_at(FOLDER / name)


def fixture_entries(*names):
    """Collection entries for fixture machines, as read_collection gives them."""
    return read_collection(None, [FOLDER / name for name in names or NAMES])
