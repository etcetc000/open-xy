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
| `idle` | hooks `sdk_send_fx` at `8d5116a0` and `8d511908`, `sdk_idle_render` at `8d51c7a6` | `send_fx`, `idle_cut` |
| `block_trims` | hooks `sdk_track_mix` at `8d511014` and (without profiling) `8d510f04`, `sdk_lfo_pass` at `11a044e6`, `sdk_tape_play` at `8d503878`, `sdk_tape_tail` at `8d50fac4` | `track_hooks`, `floor_hooks` |

## Budgets

A block is 64 frames, 580,352 cycles at the OP-XY's clock (1.45 ms). No figure here was
measured on a device. Each cycle figure comes from the BF707 Functional-Sim (no cache, no
DDR model) run by a test in this repository, with the command that prints it; an
instruction count is read from the built code (the loop and its count are named, so a
disassembly of the build shows it); anything else is said not to be measured here.

| Capability | Runs in the audio callback | Cycles of a 580,352-cycle block |
| --- | --- | --- |
| `boot_log_fix` | nothing | 0 |
| `idle` | per send per block: `sdk_send_fx` (a scan of 384 words while the send is quiet, then the effect's call is skipped once idle); per sounding voice per block: `sdk_idle_render` (stock's chooser plus a check) | a quiet send: its scans, one hardware loop of two instructions a word over 384 words (about 770 instructions; counted from the built code, not timed); a sounding send: a scan of its 128-word bus before the effect's call, one hardware loop of two instructions a word (about 260 instructions), and about 90 more of checks (counted from the built code, not timed); with `release_floor`, the drum sampler's tables scan up to 1,024 samples a block in all (not counted); with `cpu` alone, an SDK voice whose engine defines `settled()` still takes its block's peak and asks `settled()` every block, though nothing is cut (no catalog engine defines `settled()`, so nothing for the catalog; not counted). Saves an idle send's whole effect call and a cut voice's remaining blocks; not measured in this repository |
| `block_trims` | per sounding track per block: `sdk_track_mix`; per LFO pass: `sdk_lfo_pass`; per block: `sdk_tape_play`, `sdk_tape_tail` | Functional-Sim, from this repository's tests ([block_trims](#block_trims)): a silent sounding track's mixes 402 against stock's 1,704; an LFO pass check 45 against 58 (up to 72 calls a block); an idle tape about 2,610 a block |

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

## idle

What it changes in stock:
- [Idle sends](abi-1.2.1.md#send-idling): both send effects' calls
  (`8d5116a0`, `8d511908`) go to `sdk_send_fx`, which skips a send whose input,
  output and bus stayed below its floor for its effect's hold (its tail, as the
  effect declares it or as its memory can hold it, plus 64 blocks) with nothing
  it reads changing.
- [Idle cut](abi-1.2.1.md#idle-cut): the track voice loop's renderer call
  (`8d51c7a6`) goes to `sdk_idle_render`, which kills (stock's one-block fade) a
  voice that can no longer get louder and has fallen below the project's idle
  threshold relative to its own peak. The SDK voice adapters' own idle cut
  (engines with `settled`, kits' choke fade) reads the same threshold through
  `sdk_idle_floor`; the adapters are generated with it only in builds with idle.

Off: the three calls are stock's, `idle_cut` and `send_fx` are not linked, and the
effect and voice adapters are generated without their idle pieces, so nothing
records for the send hook and no voice is ever judged idle.

**The idle cut needs `release_floor`.** Its threshold is the `release_floor` setting's
(`idle_cut.cpp` asks stock's envelope routine once where a release ends; stock alone
gives no threshold). With `idle` on and `release_floor` off, which is what the `cpu`
extension alone gives, the threshold is 0 and no voice is ever cut: `sdk_idle_render`
calls stock's chooser and returns, the SDK adapters' cut never fires, and only the idle
sends act. The 33,472 bytes of sample tables are still reserved: `release_floor` is an
image setting that `sdk.py settings apply` can write into the image later, and the cut
reads its threshold from the image at run time, so it is linked whenever `idle` is.

What it saves: an idle send's whole effect call; with `release_floor` set, a voice's
remaining blocks after it falls below the threshold (stock keeps rendering a sampler
voice to the end of its sample, and a released SDK voice until its envelope reaches
-66 dB).

What it costs: about 33 KB of RAM for the idle cut's sample tables (16 samples at
once, two 512-entry tables of 16-bit maxima each), and the send hook's state
(about 500 bytes).

What it risks: a send effect with an internal state that changes while its input
is silent (an LFO, a self-oscillating feedback path) idles anyway once its input,
output and result stay within the floor (|x| < 64 raw, about -150 dBFS of Q31) for
its hold, unless it declares an unbounded tail. The reverse: an effect whose output
or result rests at 64 raw or more with no input (a DC offset, a limit cycle in a
truncating feedback path) never idles, and costs its whole call every block while it
is loaded. The floor is chosen, not derived from what the bus can carry: it is well under
the send levels the author read on one OP-XY, and above the small residue a stock effect
was seen to leave at rest in a simulator outside this repository (both the author's
observations, which this repository cannot reproduce). A voice cut early sounds shorter than stock
(that is the setting).

Catalog effects: none of the fifteen (fourteen published, one draft) declares a tail
(`tailFrames` is new). Each was run
on the host from the catalog's pinned commit by `python tools/idle_send_study.py
<effect folder>...` (its own `process`, compiled for the host, not the Blackfin build;
the script prints the table these figures come from) with its defaults, all controls at 0, at 0.5 and at 1, and each
control at 1 alone; a 2 s noise burst at 2^18 or 2^28 raw then silence, or silence
throughout; transport stopped and playing: 1,008 runs. In 1,004 the send either idles and its
output, had stock kept calling it, stays below the floor to the end of the run (at least
three holds later), or never idles. In four it rises again after idling, to 67 raw
(Chroma, Time at 1) and 88 raw (Diffusion, defaults), about -150 dBFS: what `idle`
cuts there is residue, not sound. Not idling within the run (the output stays at 64 raw or
more after the input stops, so the send keeps its full cost and nothing changes) was seen in
some setting of every effect but Ring Mod (in half the runs or more for Lustre, Vibe,
Halo, Earthshine, Rebound and Oread), and for Gen Tape with its noise up even with no input at all. Not covered: other control values, control movements
(which wake a send at once), the Blackfin build's own arithmetic, and community effects
outside the catalog.

Not judged: voices of SDK sample engines (`sample_voice.cpp.in`). They have their own method
table, so `sdk_idle_render` passes them by and they end as stock's sampler voices do.

Tests: `test_send_fx.py`, `test_idle_cut.py`, `test_voices.py`,
`test_drum_voices.py` (host); this file's tests show the sites stock and the unit
absent with the capability off.

## block_trims

What it changes in stock: per-block work replaced by exact equivalents that skip
what cannot change the result.
- [Track mixing](abi-1.2.1.md#track-mixing) (`8d511014`): a sounding track's five
  bus mixes skip a zero gain or an all-zero track buffer; without profiling,
  `8d510f04` also skips the monitor tap's stage timer.
- [LFO pass check](abi-1.2.1.md#lfo-pass-check) (`11a044e6`) without its two DDR
  helper calls.
- [Idle tape](abi-1.2.1.md#idle-tape): tape playback (`8d503878`) and tape track
  mixing (`8d50fac4`) skipped while the tape is silent.

Off: every site keeps stock's bytes and `track_hooks` and `floor_hooks` are not linked.

What it saves (BF707 Functional-Sim, from this repository's tests; no cache, no DDR
model, not a device measurement): a sounding track's mixes 1,704 cycles (stock)
against 402 on a silent buffer; an LFO pass check 58 against 45 (up to 72 calls a block,
so about 940 cycles, 0.16 % of a block, in the simulator; what the check avoids is two
calls into DDR code, whose cache misses the simulator does not model, so the device figure
is unknown and may be larger or no better); a block with the tape idle about 2,610 cycles. To print them (needs CCES's simulator and a stock OS image,
neither in this repository): `python tests/sim_cycles.py`, with `OPXY_TEST_STOCK_OS` and
`OPXY_TEST_CCES` set as for the tests.

What it risks: each hook re-implements a stock loop; a mistake changes audio.

Tests: each hook against stock's own relocated bytes on the Functional-Sim
(`test_track_hooks.py`, `test_floor_hooks.py`; they need a stock OS image, which is
not in this repository, and the simulator, so CI skips them); host tests pin each hook's
stock addresses and its scan's length against the stock bytes it replaces
(`test_cpu_capabilities.py`, `test_track_hooks.py`). Never measured in a whole loop
with voices sounding on a device.
