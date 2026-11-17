# CPU capabilities

Build capabilities ([settings extensions](extensions.md#settings-extensions),
`"build": true` in [`profiles/extensions.json`](../profiles/extensions.json)) that
cut the stock firmware's audio work. All of them are
**experimental**. Each is a named group of SDK-owned edits, off unless a settings
extension in the collection names it. Off, every one of its sites holds stock's
bytes and its SDK units are not linked. Every edit carries
the stock bytes it replaces (`before`), and the build refuses an image whose site
differs (`tooling/firmware.py` `assemble`).

The build capabilities are chosen only by `sdk.py build`: `sdk.py settings apply` refuses them and
browser kits cannot carry them.
`sdk.py settings show --image os.ldr` reports
whether an image was built with each.

Firmware coverage: every capability below has addresses for **OP-XY OS 1.2.1 only**
(`profiles/opxy-1.2.1.json`). A build whose profile lacks a selected capability's
service fails with "the <name> capability has no firmware sites for OP-XY OS
<version>".

**What has run on a device: nothing from this repository.** No build made from this
repository with any of these capabilities on has run on a device. A capability
off leaves stock's bytes, which a build
without it has byte for byte ([Off means off](#off-means-off)).

## Off means off

A build that selects none of them links the same module, byte for byte, as the same
checkout with the CPU capabilities taken out of it. `tests/test_off_path_code.py`
builds every fixture collection, with profiling off and on, from this checkout and from a
copy of it without the capabilities (`tests/off_path_code.py` `without_capabilities`: their
registry entries, profile services and edits, units, template placeholders and the two
places `tooling/native.py` names them), and compares the two modules function by
function. `tests/test_off_unless_asked.py` compares the stock edits and units the same way
without the toolchain. Nothing is stored, so a change elsewhere in the SDK cannot fail them;
a change to a capability that the copy no longer matches fails, naming the step. By hand:
`python tests/off_path_code.py` (needs the pinned Blackfin tools). Each capability's own tests
below show its sites stock and its units absent with it off.
