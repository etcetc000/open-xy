"""Purpose-built machines for the SDK's tests (tests/fixtures/machines/). The SDK ships
no machines of its own; these are small, valid folders, not copies of published ones.

- probesynth: a synth with a drawing display (two paths) and its panel.svg.fragment.
- probesources: a synth with an extra translation unit and include folder, and a
  display without paths.
- probefx: an effect with a Dry control (dryParameter) and controls of each mapping.
- probesample: a drum-page (page 7) sample engine declaring every optional service.
- probekit16, probekit21: probesynth as a two-model drawn drum kit, alike but for the digits
  of their id and name and the SDK they declare (1.16, 1.21); image-size measurements of the
  played-pad snapshot build them.
- probekit23: probekit21 declaring SDK 1.23 and the drum-kit controls (padVolume, padRandom,
  ampEnvelope, with chokeSameKey); its screen reads the selected key and the Shift choices.
- probekit25: probekit21 as a three-model kit declaring SDK 1.25 and modelEdit: its first
  control runs through every model (edit_model_parameter).
- probepads: a note player declaring SDK 1.25 that reads Controls.instrument: one fixed pad
  on a drum track, its notes snapped to a scale elsewhere.
- voiceheap: a sine with a long echo whose voices are too large for module memory
  (SDK 1.19 heap voices); not in NAMES.
- probestage: a saw whose track stage (SDK 1.24) widens its track's voice sum with a short
  delay; not in NAMES.
"""
from pathlib import Path
import os
import tempfile

from tooling.machines import machine_at, read_collection

# machine_at rejects a path behind a symlink, and macOS's temp directory (/var) is one.
tempfile.tempdir = os.path.realpath(tempfile.gettempdir())

FOLDER = Path(__file__).resolve().parent / 'fixtures/machines'
# Firmware order for a collection holding every kind.
NAMES = ('probesynth', 'probesources', 'probefx', 'probesample')


def fixture(name):
    """A fixture machine, loaded as any machine folder is."""
    return machine_at(FOLDER / name)


def fixture_entries(*names):
    """Collection entries for fixture machines, as read_collection gives them."""
    return read_collection(None, [FOLDER / name for name in names or NAMES])
