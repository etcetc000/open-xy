# CPU capabilities

Build capabilities ([settings extensions](extensions.md#settings-extensions),
`"build": true` in [`profiles/extensions.json`](../profiles/extensions.json)) that
cut the stock firmware's audio work, and
one image capability, `boot_log_fix`, kept with them because the `cpu` extension selects it. All of them are
**experimental**. Each is a named group of SDK-owned edits, off unless a settings
extension in the collection names it. Off, every one of its sites holds stock's
bytes and its SDK units are not linked. Every edit carries
the stock bytes it replaces (`before`), and the build refuses an image whose site
differs (`tooling/firmware.py` `assemble`).

The build capabilities are chosen only by `sdk.py build`: `sdk.py settings apply` refuses them and
browser kits cannot carry them. `boot_log_fix`, one fixed data byte, is an image
capability: `sdk.py settings apply` can write it into an existing image and a browser kit can carry it.
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

| Capability | Edits | Units linked when on |
| --- | --- | --- |
| `boot_log_fix` (an image capability) | limit `8400ea65` (one data byte) | none |

## Budgets

A block is 64 frames, 580,352 cycles at the OP-XY's clock (1.45 ms). No figure here was
measured on a device. Each cycle figure comes from the BF707 Functional-Sim (no cache, no
DDR model) run by a test in this repository, with the command that prints it; an
instruction count is read from the built code (the loop and its count are named, so a
disassembly of the build shows it); anything else is said not to be measured here.

| Capability | Runs in the audio callback | Cycles of a 580,352-cycle block |
| --- | --- | --- |
| `boot_log_fix` | nothing | 0 |

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

## boot_log_fix

Not a CPU change: a fix for a boot crash, kept with the CPU work because the `cpu`
extension selects it. It is an image capability (one fixed data byte), so `sdk.py settings
apply` can also write it into an existing image, and a browser kit can carry it.

What it changes in stock: the GUI board's log-sink switch (`8400ea65`, a data
byte) goes from 1 to 0, so the GUI board's log lines are not forwarded over the
SPI bridge to the audio board. The reason is the author's observation on one OP-XY,
which this repository cannot reproduce: a boot crash in the SPI bridge's code while the
log was forwarded.

Off: the byte stays 1.

What it risks: GUI board log lines are no longer seen on the audio board's side
(a diagnostic loss, no sound change).

Not verified: that it prevents the crash. Host tests show it is its own edit, apart
from `idle`.
