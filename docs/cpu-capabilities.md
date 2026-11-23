# CPU capabilities

Build capabilities ([settings extensions](extensions.md#settings-extensions),
`"build": true` in [`profiles/extensions.json`](../profiles/extensions.json)) that
cut the stock firmware's audio work, or change how it sheds load, and
one image capability, `boot_log_fix`, kept with them because the `cpu` extension selects it. All of them are
**experimental**. Each is a named group of SDK-owned edits, off unless a settings
extension in the collection names it. Off, every one of its sites holds stock's
bytes and its SDK units are not linked. Every edit carries
the stock bytes it replaces (`before`), and the build refuses an image whose site
differs (`tooling/firmware.py` `assemble`). `fast_helpers` is the exception: it
has no site and no unit of its own, and its two units are linked either way, from
the plain sources when it is off ([below](#fast_helpers)).

The build capabilities are chosen only by `sdk.py build`: `sdk.py settings apply` refuses them and
browser kits cannot carry them. `boot_log_fix`, one fixed data byte, is an image
capability: `sdk.py settings apply` can write it into an existing image and a browser kit can carry it.
`sdk.py settings show --image os.ldr` reports
whether an image was built with each, except `fast_helpers`, which has no site in
the image: a build's `analysis/compile-report.json` names the sources it chose
under `unit_sources` when the setting is 1 (the key is absent at 0).

Firmware coverage: every capability below has addresses for **OP-XY OS 1.2.1 only**
(`profiles/opxy-1.2.1.json`). A build whose profile lacks a selected capability's
service fails with "the <name> capability has no firmware sites for OP-XY OS
<version>".

The SDK's one CPU extension, `extensions/cpu` ("CPU (experimental)"), selects all seven:
`boot_log_fix`, `idle`, `block_trims`, `output_slack`, `load_governor`,
`pitch_constants` and `fast_helpers`, the last at its setting 0, off. It needs `sdk.py build`. It
does not select `release_floor`, whose threshold the idle cut uses: with `cpu` alone, `idle` skips
silent sends and cuts no voice ([idle](#idle)). A
collection that wants only some of the capabilities selects them with a settings
extension of its own that names just those, in place of `cpu` (`sdk.py build`
takes a settings extension from any folder the collection names).

**What has run on a device: nothing from this repository.** No build made from this
repository with any of these capabilities on has run on a device. A capability
off leaves stock's bytes (and, for `fast_helpers`, the plain helpers), which a build
without it has byte for byte ([Off means off](#off-means-off)).

| Capability | Edits | Units linked when on |
| --- | --- | --- |
| `boot_log_fix` (an image capability) | limit `8400ea65` (one data byte) | none |
| `idle` | hooks `sdk_send_fx` at `8d5116a0` and `8d511908`, `sdk_idle_render` at `8d51c7a6` | `send_fx`, `idle_cut` |
| `block_trims` | hooks `sdk_track_mix` at `8d511014` and (without profiling) `8d510f04`, `sdk_lfo_pass` at `11a044e6`, `sdk_tape_play` at `8d503878`, `sdk_tape_tail` at `8d50fac4` | `track_hooks`, `floor_hooks` |
| `output_slack` | hook `sdk_dsp_block_audio` at `8c0c6ffe` (and, once armed, a run-time swap of the audio task's block pointer `11b008cc`) | `dsp_block`, `dsp_block_hooks`, `output_slack`, `output_slack_hooks` |
| `load_governor` (fixed bytes, no code) | limits `8d51c430`, `8d51c48c`, `8d51c492`, `8d51c49a`, `8d51e4f0`, `8d51e4fe` | none |
| `fast_helpers` (a setting, 0 off or 1 on; default 0) | none: it chooses the source of two units every build links | at 1, `divide` from `divide_fast.s` and `memory` from `memory_fast.cpp`; at 0, the plain `divide.s` and `memory.cpp` |
| `pitch_constants` (fixed bytes, no code) | limits `8d5c0a1e`, `8d5c0a2c` (12 bytes each) | none |

## Budgets

A block is 64 frames, 580,352 cycles at the OP-XY's clock (1.45 ms). No figure here was
measured on a device. Each cycle figure comes from the BF707 Functional-Sim (no cache, no
DDR model) run by a test in this repository, with the command that prints it; an
instruction count is read from the built code (the loop and its count are named, so a
disassembly of the build shows it); a figure from a run outside this repository says
so; anything else is said not to be measured here. Code
and data are the bytes the capability adds to the linked module, measured on a build of Open-XY's own catalog
machines (fm2op, macroosc, karplus, timestretch, ringmod, pingpong, chords) with
only that capability on, against the same build with none; RAM is the zeroed memory
(`.bss`) and initialised data (`.data`, already among the module bytes) it adds.

| Capability | Runs in the audio callback | Cycles of a 580,352-cycle block | Module bytes |
| --- | --- | --- | --- |
| `boot_log_fix` | nothing | 0 | 0 |
| `idle` | per send per block: `sdk_send_fx` (a scan of 384 words while the send is quiet, then the effect's call is skipped once idle); per sounding voice per block: `sdk_idle_render` (stock's chooser plus a check) | a quiet send: its scans, one hardware loop of two instructions a word over 384 words (about 770 instructions; counted from the built code, not timed); a sounding send: a scan of its 128-word bus before the effect's call, one hardware loop of two instructions a word (about 260 instructions), and about 90 more of checks (counted from the built code, not timed); with `release_floor`, the drum sampler's tables scan up to 1,024 samples a block in all (not counted); with `cpu` alone, an SDK voice whose engine defines `settled()` still takes its block's peak and asks `settled()` every block, though nothing is cut (no catalog engine defines `settled()`, so nothing for the catalog; not counted). Saves an idle send's whole effect call and a cut voice's remaining blocks; not measured in this repository | 3,476; RAM 36,704 zeroed and 8 initialised (33,472 of sample tables, 504 of send state) |
| `block_trims` | per sounding track per block: `sdk_track_mix`; per LFO pass: `sdk_lfo_pass`; per block: `sdk_tape_play`, `sdk_tape_tail` | Functional-Sim, from this repository's tests ([block_trims](#block_trims)): a silent sounding track's mixes 402 against stock's 1,704; an LFO pass check 45 against 58 (up to 72 calls a block); an idle tape about 2,610 a block | 1,268; RAM 4 zeroed |
| `output_slack` | per block: `sdk_dsp_block_audio`; until armed, a quiet check of the newest block (256 words, more with USB multichannel); once armed, each period a copy of one block (1,024 words) out of a 4 KB queue | the copy about 2,050 instructions a period (counted from the built code: a hardware loop of two instructions a word over 1,024 words; not timed) | 436; RAM 4,144 zeroed (the 4 KB queue) |
| `load_governor` | stock's governor, as before, on its edited bytes | 0 (it changes when voices are cut, not the work) | 0 |
| `fast_helpers` | whenever machine code calls `memset`, `memcpy` or an integer divide | less per call; never measured as a share of a block | 248 |
| `pitch_constants` | per synth voice start: stock's note pitch, with two conversions fewer | Functional-Sim, this repository's test ([pitch_constants](#pitch_constants)): stock's note pitch 834 against 233 a call; in whole-image runs outside this repository, 600 less every voice start, on every synth run (four SDK synths and stock's), so 14,400 (2.5 % of a block) in a block that starts 24 voices | 0 |

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
  effect declares it or as its memory can hold it, plus 64 blocks; for an effect
  that declares no tail, never under 2,112 blocks, 3.07 s) with nothing it reads
  changing.
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

## output_slack

What it changes in stock: the DSP callback's call at `8c0c6ffe` goes to
`sdk_dsp_block_audio`, the per-block hook, which after about 4 s installs a wrapper
through the audio task's block-function pointer (`11b008cc`, a run-time write, not a
profile edit; `output_slack.hpp`). The wrapper runs stock unchanged until a block whose
main and USB-record planes (and, in USB multichannel mode, the USB monitor taps) are
quiet, below 2^17 in magnitude; arming waits for that block, however long. Then the
DMA plays that block once more (the one inserted block) and from then on each period
the wrapper delivers the previous block to the transmit half and has stock render the
new block into a 4 KB queue: every output is one block (64 samples, 1.45 ms) later, and
a block that finishes up to about two periods late, or two of 1.3 in a row, no longer
tears the output.

Off: `8c0c6ffe` keeps stock's call, none of its four units is linked, and the
pointer is never written.

What it saves: no CPU; it hides single late blocks.

What it costs: the copy, about 2,050 instructions a period (counted from the built code,
not timed), and while it waits to arm, the quiet check of each block.

What it risks: 1.45 ms more output latency on every output, the USB planes included;
real DMA timing and the copy's cost are not modelled by the simulator, and the queue
sits in cached memory, so stock now renders into cacheable memory that the copy reads
back each block (its cache cost, and whether anything but the core reads the queue,
were never checked). The copy writes the transmit halves with core stores just before
the DMA reads them; stock's own block function writes the same halves the same way, but
their cache attributes were not read from stock's memory protection setup: if they were
write-back cached, the DMA could read a stale half. Nothing disarms it once armed: stock rewrites
the task's pointer only in its audio init, which runs only during boot, and in its
fault handler, which silences the audio as before. That is a reading of the call
sites, not a measurement: if the audio init ever re-ran, the slack would be silently
off and the audio stock again. It hides one late block, not a run of them: sustained
overload still clicks.

A block later than the slack: if a block ends after the second boundary, the DMA
plays the last delivered half again (a clean repeat). If the task's semaphore then
runs the missed iteration at once, that iteration copies into the half the DMA is
playing (a tear), and the block delivered before it is not played. Stock tears in
the same case, one block sooner.

Tests: host test `test_output_slack.py` (quiet check, half index, arming and its
about-4 s wait, delivery); a host test pins the hook's call to the stock address the
replaced instruction called (`test_cpu_capabilities.py`). That its output equals the
build without it one block later, and that it hides late blocks on a device, are not
measured in this repository.

## load_governor

A plain switch: six fixed-byte edits and no code, each marked `"build": true` in
the firmware profile. Off, all six sites hold stock's bytes. On, stock's governor
acts on a short mean of the block load instead of its held peaks, cuts voices only
near continuous saturation and gives them back sooner.

What stock does (OS 1.2.1, read from the code): every audio block ends in
`8d50ec6c`, which makes a load byte from the block's own cycle count: cycles x 256 /
550,000, at most 255, so 255 from 547,852 cycles (94.4 % of the 580,352-cycle period;
100 % of the byte's scale would be 550,000 cycles, 94.8 %). It then calls the
voice manager's governor `8d51e4c4`. With L = the byte x 256 (of 65,536):
- the smoothed load (`pool+0xe0`) rises a quarter of the gap in any block whose L is
  higher, then holds for 256 blocks (0.37 s), then falls 8 a block (`8d51c480`);
- a block whose own L reaches 89.8 % kills one released voice at or below -26 dB that
  is at least 8 blocks old, two from 93 % (`8d51e3fc`, stock's one-block fade);
- a smoothed load of 94.5 % or more drops the voice budget to max(12, active - 2) and
  kills every voice over it (`8d51cf3c`); under 84.8 % the budget comes back one voice
  every 32 blocks, up to 24 + 3;
- at 84.8 % or more the track voice loop starts one stolen note per track per block
  (`8d51c708`: the others wait two blocks).
While USB audio is configured every threshold is lower by 0x26c + 0xa3 per routed USB
stream (`0803ba84`). A few heavy blocks (a chord's note starts) can lift the smoothed
load past 94.5 %, and the 256-block hold keeps it there: the budget stays down through
the hold and the fixed fall after it, and every new note steals.

What it changes, on:
- The load figure becomes a short mean of the block load. `8d51c480` is rewritten
  inside its own bytes: every block the figure moves 1/8 of the way to the block's
  load, whether the block is above or below it (`8d51c492`: `LSHIFT R1 >>= 2` becomes
  `ASHIFT R1 >>>= 3` on the signed gap), with no hold and no fixed fall (`8d51c48c`:
  the rise test and its branch to the hold, decay and floor paths become
  `R2 = R1 >> 1`; those paths stay in the image, unreachable; the hold word
  `pool+0xe4` is no longer read or written). Time constant 8 blocks (about 12 ms).
  The pool is the voice manager, a member of the audio engine object: stock passes the
  engine at `0x84c696fc` to its audio loop (`11a02b32`..`11a02b3a`, a call of
  `8d510760`), whose load step (`8d50ec6c`, called at `8d51231c`) passes the engine plus
  `0x13c` to the governor (`8d50ed1c`..`8d50ed26`); so `pool+0xe0` is `0x84c69918` and
  `pool+0xe4` is `0x84c6991c`. The stock voice routine at `8d51b274` also reads `+0xe0` and
  `+0xe4` of a record (`8d51b330`, `8d51b31c`), but of another object: the per-track
  table at `0x84eab39c`, 0xe8 bytes a track (`8d51b2a2`..`8d51b2ae` through
  `8d518d94`, base + track × 0xe8), 2.3 MB from the pool. It does not read the
  governor's words.
- The routine returns `0x6680 + L / 2` (`8d51c49a`, where it used to reload the hold
  with 256), and the single-block test compares 89.8 % with that return instead of
  the block's own load (`8d51e4f0`: R4 = R0 before the test; this edit marks the
  capability on). So the single-block kill fires only on a block at the load byte's
  ceiling (547,852 cycles or more, 94.4 % of the period): one released voice at or
  below -26 dB, never two (the 93 % test needs `0xee00`, above the routine's largest
  return) unless USB audio's overhead term reaches 0x800: 9 or more routed USB
  entries, and then only on a saturated block.
- The budget cut's line moves from 94.5 % to 99.0 % of the load byte's scale
  (`8d51c430`: `R1 = 0xf200 (Z)` becomes `R1 = 0xfd70 (Z)` in `8d51c420`, whose only
  caller is that test). The byte saturates at 255 (any block of 547,852 cycles or
  more: the figure can never read more than 0xff00, 99.6 %), and the mean settles at
  least 0xfef9 under saturation on every block, so the line is reached: after 27
  saturated blocks from 80 %, and by a constant byte of 254 (99.2 %), never by 253.
- The recovery (one voice per 32 blocks) starts under 93.0 % instead of 84.8 %: its
  threshold routine (`8d51c468`) is shared with the pacing of stolen notes
  (`8d51c778`), so its constant stays and the pacing stays at 84.8 %; instead the
  recovery's own call (`8d51e4fe`) goes to `8d51c438`, which returns 0xee00 (93.0 %)
  less the same USB overhead term. Between 93 % and 99 % neither the cut nor the
  recovery moves the budget: after a cut, a steady 92 % gets every voice back, a
  steady 95 % keeps the budget down.
Everything else is stock's and reads the new figure: the kill routine, the floor of
12, the 84.8 % pacing of stolen notes, the single-block and two-tail thresholds and
the load byte's scale.

Every percentage above is of the governor's scale, on which 100 % is 550,000 cycles,
94.8 % of the real 580,352-cycle block. In both units (scale / real block):

| line | stock | on |
| --- | --- | --- |
| budget cut | 94.5 % / 89.6 % | 99.0 % / 93.8 % |
| recovery | 84.8 % / 80.3 % | 93.0 % / 88.1 % |
| pacing of stolen notes | 84.8 % / 80.3 % | unchanged |
| one quiet tail killed | the block's own load from 89.8 % / 85.1 % | only at the byte's ceiling, 99.6 % / 94.4 % |
| two tails | from 93.0 % / 88.1 % | never (without USB audio) |
| the byte's ceiling | 99.6 % / 94.4 % | unchanged |

So on, a steady load of about 94 % of a real block is cut (stock: 89.6 %), and the
governor is blind to anything above 94.4 % of a block: a block at 95 % and one at
200 % read the same.

Off: the six sites hold stock's bytes.

What it saves: no CPU. It changes when stock takes voices away under load.

What it risks, bluntly: **it never sheds overload that comes in bursts.** It cuts
only near-continuous saturation. It does NOT cut: a constant load up to 98.8 % of the
byte's scale; saturated blocks alternating with 95 % blocks (the figure reads about
97 %, while the saturated blocks may each be far over the period: the byte cannot
show by how much); three saturated blocks in four with the fourth at 90 %; a
saturated block every 2nd, 4th or 8th block on a lighter base; any pattern of very
late blocks between light ones (say every 2nd block at twice the period, the others
at 40 %: the mean stays near 70 %). Each of those can be a real overload that runs
late block after block. Until the load drops, such a project lags or clicks; the
output slack hides one late block, not a run of them, and long runs of late blocks
may also starve the screen board's link (the author saw an assertion in the SPI
bridge's code on one OP-XY when the audio board stopped reading, which this repository
cannot reproduce).
Stock's held peaks cut such patterns.
- The pacing of stolen notes (`8d51c708`: while the figure is over 84.8 % each track
  starts one waiting stolen note per block and the others wait two blocks) follows
  the mean: it is on only while the short mean is over 84.8 %, where stock keeps it
  on for seconds after any peak. So stolen notes are staggered less often than in
  stock (in a host model over the same load sequences, never in a block where
  stock's would not be, without USB audio and with up to 102 routed USB entries; at
  103, 114 and 125 entries a constant load reached from above can keep it on where
  stock's is off; whether that many entries can be routed is not known).
- The single-block kill now needs a saturated block (stock: 89.8 %), so quiet
  released tails (at or below -26 dB) are trimmed less early. With USB audio this
  holds up to 36 routed entries: from 37 the one-tail kill fires on blocks where
  stock's does not, from 49 also the two-tail kill (released voices at or below
  -26 dB only); whether the routing table can hold 37 or more entries is not known.
- The time constant was chosen by the author on synthetic loads in a simulator outside
  this repository, not on a real project's load; what it then does is what the host
  tests check.
- The edits are fixed bytes at stock sites. The SDK writes them only in `sdk.py
  build`, but any tool that writes bytes into an image could write them.

Tests: host only. `test_load_governor.py`: the edits and their encodings, the
smoother's bytes run instruction by instruction off and on against models of stock's
and the mean's arithmetic and return value, the cut and recovery lines decoded from
the image's bytes, what it cuts and what it does not (saturation on every block from
80 % cuts on the 27th block), that it has no setting and its refusals outside `sdk.py
build`. Nothing runs stock's governor routine itself on the edited bytes in this
repository.

## fast_helpers

What it changes: no stock byte. Every native build links two SDK units that
compiled machine code calls: `memory` (`memset`, `memcpy`: struct initialization
and copies) and `divide` (`___udivsi3`, `___umodsi3`, `___divsi3`, `___modsi3`:
GCC's names for integer division and modulo). With its setting at 1 a build
compiles them from `sdk/platform/blackfin/memory_fast.cpp` (hand-written: whole
words when source and destination agree mod 4, half-words when they agree mod 2,
the byte loop under 16 bytes or when the ranges overlap) and `divide_fast.s`
(a divisor below 32768 uses the chip's divide steps, DIVS then 16 DIVQ: once when
the quotient fits 16 bits, twice when it does not; larger divisors keep the plain
shift/subtract loop). The objects keep their names and their place in the link
order. `divide_fast.s` differs from `divide.s` only in `___udivsi3`; its three
wrappers are `divide.s`'s text, and a test fails if they ever differ.

Off (setting 0: the default, the `cpu` extension's value, and every build without
the capability): the plain `divide.s` and `memory.cpp`, byte for byte, as in a
build that selects no capability. Host and browser-preview builds always compile
`memory.cpp`'s C and never link either fast file.

What it costs: 248 bytes of every image that turns it on (fill and copy 216 bytes
against 140, divide 336 against 164).

What it saves: time per call (word copies instead of byte loops; 17 divide steps
instead of up to 32 shift/subtract steps for a small divisor). Never measured as
a share of an audio block.

What it risks, bluntly: every native machine calls these routines, so a fault in
them is a fault in every machine, at any time. Their results are C's according to
an instruction model (`tests/blackfin_model.py`, `tests/test_runtime_helpers.py`)
written from the instruction-set reference, not according to silicon: a model and
a routine that read DIVS/DIVQ the same wrong way would both pass. No build from this
repository with them has run on a device. The BF70x silicon anomaly list was not
checked for DIVS/DIVQ or for the hardware loops these use. The two-pass divide (a
divisor below 32768 with a quotient wider than 16 bits, such as 0x7fffffff / 10) is
checked only in the model. `memset` and `memcpy` count in LC0 where the plain compiled
C uses LC1, so hand assembly that calls them inside an LC0 hardware loop must save it:
no SDK assembly does, and no catalog machine's hand assembly calls anything (the
catalog's pinned commits when this was written; a machine added later is not
checked). `sdk.py settings show` cannot tell whether an image has them; the build's
compile report can.

Tests: host only. The instruction model runs both fast files' own text:
quotients, remainders and the signed wrappers against C over edge and random
operands, `memset` and `memcpy` against byte loops at every alignment and overlap
in a window, and a register check that refuses a helper which changes a
callee-saved register. A link test shows a build at 0 links the plain objects and
one at 1 the fast ones, with the sizes above.

## pitch_constants

What it changes in stock: stock's note-to-pitch routine (`8d5c09d6`), which stock's
note pitch `8d5c0a70` calls (itself called at `8d507596` and `8d51acbe`; a synth voice
start makes one such call, as the whole-image runs below show), loads the float constants
1200.0 and 100.0 and converts each to fixed point by calling `8d5bf5ec` (a floor, two
float-to-integer, an integer-to-float, a subtract, an add of 0.005 and a multiply by 100,
all software floating point). The answers never change. The two load-and-call pairs
(`8d5c0a1e`, `8d5c0a2c`, 12 bytes each) become loads of the words those calls return,
`0x04b00000` and `0x00640000`, and two 16-bit NOPs where each call was. The rest of the
routine is stock's. Two fixed-byte edits, marked `"build"`; no code.

Off: both sites hold stock's bytes. The `cpu` extension turns it on.

The words are stock's own: the simulator test below runs `8d5bf5ec` on both constants
and gets them, twice each, and a host test computes them from the routine's arithmetic
in single precision.

What the replaced calls did besides returning the word: the conversion's work leaves
values in scratch registers (the calling convention's R0-R3, P0-P2, the accumulators
A0 and A1 and the arithmetic flags) that differ with the edits.
Its stores are to its own stack frame. Both are a reading of its code, not checked
instruction by instruction here; that nothing after the routine depends on them is the
calling convention, and the equal results and state below.

What it saves (BF707 Functional-Sim; no cache, no DDR model; not a device measurement):
- the note pitch call: 834 cycles against 233 with the edits (mean of 24,576 calls,
  tracks 0-7, notes 0-127, 24 values of its third argument), 835 against 234 with
  random pitch tables;
- in the whole audio loop of a built image, booted on the Functional-Sim by a harness
  outside this repository (a probe synth and the catalog's fm2op, macroosc and karplus,
  stock's synths on the other tracks): exactly 600 cycles less for every voice start, on each of the
  four and on stock's synths: 4,800 in a block that starts 8 voices of one machine (a
  probe synth block of 88,811 becomes 84,011), 14,400 in one that starts 24 voices
  across the four (2.5 % of a block), nothing in a block without a voice start. The
  same runs show the audio buffers, stock's audio data regions, the engine block and the
  first 320 bytes of each of stock's 27 voice slots equal, hash for hash, to the build
  without it in every block (7 scenarios, 482 blocks, among them a note sweep over every
  MIDI note on random track words, once with the four SDK synths on half the tracks and
  once with stock's synths alone).
A device's saving may differ: the conversions run from DDR and ROM, whose fetches the
simulator does not model.

What it costs: nothing in the module; 24 bytes of stock code.

What it risks: code in `8d5c09d6` after either site that read a scratch register or
flag the conversion left would see another value (none is known: the compiler's
convention, and equal results on all 24,576 inputs). Stock synths' voice starts change too, cheaper
only.

Tests: `test_pitch_constants.py`: the edits, their stock bytes and offsets, the calls'
target, the words against the routine's arithmetic, that it is off unless an extension names it and on with `cpu` (host); on
the Functional-Sim (needs a stock OS image and CCES, so CI skips it), `8d5bf5ec`'s own
answers and stock's note pitch on 24,576 inputs with booted and with random tables,
stock against the edits, equal on every input. The whole-image runs above are not in
this repository. To print the call's figures: `python tests/sim_cycles.py`.
