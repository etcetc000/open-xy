# Measure CPU cost on your OP-XY

New or changed `draft` and `published` catalog entries must include a hardware
`cpu` measurement for their pinned machine commit. This applies to synths,
sample engines and effects. Unchanged older entries can remain without one
until their next update. A maintainer can run a batch for contributors who do
not have hardware; contributors can also follow this procedure themselves.
The [host estimate](api.md#cpu) is useful during development but cannot fill
this field. CI validates the record and revision; it cannot independently
attest that a contributor ran the hardware test.

## Build and install

Use the SDK's `staging` branch and commit your machine changes before building.
Follow [building firmware](build.md) to install the pinned tools and obtain your
own stock OS. Keep firmware and packages outside every checkout. In the commands
below, `<private>` is that external folder and `<machine>` is your machine folder.

```sh
python -m pip install python-rtmidi
python sdk.py build --machine <machine> --profiling on --stock-os <private>/stock-1.2.1/os.ldr --out <private>/cpu-build-001
```

Profiling builds sweep synth controls by default: after each synth's standard
run, the firmware repeats the stock workload once per setting, each control in
turn through nine positions (or each option of a list) with the others at their
defaults, then once with every control at the position that ran heaviest. A
synth's catalog record is that sweep. The standard run is stock's own: controls
0-3 at midpoint, any others as the track holds them; swept runs set all eight.
A sweep build's voices also check, once per block, whether a sweep run is
theirs, a few instructions beside a voice's thousands. `--sweep off` gives a quick
midpoint-only build for development; it cannot publish a synth record.

Repeat `--machine` or use `--collection` to build a batch. Keep the package:
its manifest and compile report identify the firmware and expected test order.
Install its `candidate/` files using [the installation steps](build.md#5-install),
write the OS to the boot partition, and power-cycle. The bootloader is unchanged.
Website builds disable the required per-voice counters; use the SDK build above.

Boot to the normal instrument screen, with USB connected. MTP mode does not
provide the MIDI endpoint. Save your project and stop playback: this is an
active diagnostic that plays its own notes and changes temporary instrument
settings. Close applications holding the XY's MIDI ports and leave USB audio off.
The collector needs exactly one OP-XY MIDI input/output pair. It uses USB MIDI,
not a serial/CDC port. No additional dev-menu toggle is needed after booting
the installed development image normally.

## Collect a run

One capture is enough: each run averages 1,500 blocks of a fixed workload, and
repeated captures of the same build agree to within a few hundredths of a
percent. A machine with randomness (noise, random modulation) may vary more:
collect several, one after another, each to a new directory, and pass every
capture to the report, which publishes their median (two give their average,
so take three or more to outvote an outlier). Each stock-workload
run takes about 2.2 seconds, so a sweep run takes longer: about 80 seconds for a
synth with four controls. The collector waits for every expected report (by
default up to 3 seconds per run plus 15) and stops early once they have arrived.

```sh
python sdk.py profile benchmark --package <private>/cpu-build-001 --installed --greet --out <private>/cpu-run-001
```

Never run two collectors at once: they share the OP-XY's one MIDI port pair.
A swept setting that times nothing (a level at 0) is left out only when two
or more captures agree it is silent, since a failed run reads silent too:
`profile report` names it and asks for a second capture.

`--installed` confirms that this exact package is on the device and your work is
saved. Metadata identifies normal OS 1.2.1 but does not attest the installed image
hash. The collector checks CRCs, endpoint, packet size, and the complete ordered
report count before assigning results to machines. A missing or failed
registration cannot silently shift the reported names. Do not operate the
instrument while a run is active. Each command waits up to 60 seconds for data.

Each run saves `capture.json`, raw `traffic.jsonl`, and a readout. Keep these
private: device metadata can contain a serial number. The command installs no
firmware and never reboots the device. Restart the instrument after testing
before resuming normal work; do not save the diagnostic's temporary settings.

For connection diagnosis, `python sdk.py profile probe --greet --out <fresh-folder>`
requests metadata without running the benchmark. `profile selftest` exercises
the protocol codec without opening a device. An inactive/failed workload, missing
reports, or disabled profiling is a failed measurement, never a zero-cost machine.

## Add the catalog field

The following is offline; `--machine` here takes the machine ID, not its
folder. It verifies that the clean, committed machine source still matches the
measured package.

```sh
python sdk.py profile report --package <private>/cpu-build-001 --machine mymachine --capture <private>/cpu-run-001/capture.json --out <private>/cpu-report-001
```

Copy the `cpu` member from `catalog-cpu.json` into your machine's entry in
`catalog/machines.json`. To link evidence as well, publish the generated
`report.md` (an issue, PR comment or file in your repository) and pass its URL
with `--report-url`; if you only learn a comment's permalink after posting,
rerun the offline report command into a new directory with it. No new device
run is needed. The report omits serial numbers and local paths. Share the raw
captures privately with a maintainer if needed; never publish firmware or
firmware-containing packages.

| `cpu` field | Meaning |
| --- | --- |
| `benchmark` | Versioned workload from the table below; must match the machine kind |
| `commit` | Full machine source SHA, equal to the catalog entry's `commit` (or an earlier release's, when a maintainer approves carrying it over) |
| `firmware_sha256` | Exact tested candidate OS image |
| `runs` | 1 to 100 independent, complete captures of the same build and collector (one is enough) |
| `block_percent` | The run's mean cost (the median across captures when there are several), as a percentage of the stock 580,352-cycle block budget. For a synth, its heaviest run: the median of each swept setting and of the midpoint run, the largest of them |
| `midpoint_percent` | Synths: the midpoint run's median, stock's own workload |
| `combined_percent` | Synths: the median of the run with every control at its heaviest position |
| `worst_setting` | Synths: the heaviest swept setting: `control` (0-7), its `label`, and its Q31 `value` |
| `report` | Optional: public HTTPS evidence URL containing the generated readout |

Keep `block_percent` at or below the **recommended 3.5%**. A higher result is
still accepted, but `profile report`, the collector's readout and `catalog check`
print a warning naming the heaviest setting: make that path lighter where you can.

Synth records from before the sweep (`opxy-synth-c8-v1`) stay valid while their
release stays at the commit they measured; a new synth, or one whose release
moves, submits a sweep (unless a maintainer approves carrying the record over).

Updating a machine's commit requires a new measurement, unless a catalog
maintainer approves keeping the previous one ([catalog updates](catalog.md#submit-a-machine)).
CI rejects stale results, host estimates, non-finite/zero/saturated values and
missing records on changed submissions. Retiring an entry does not require a new run. Validate your submission:

```sh
python sdk.py catalog check --base origin/staging
```

## What is measured

All workloads run on the Blackfin handling the diagnostic USB-MIDI command.
They are synthetic measurements on real hardware, not passive measurements of
the audio core during normal project playback. Every block is 64 samples.

| Machine kind / benchmark | Scope and fixed workload |
| --- | --- |
| Synth / `opxy-synth-c8-sweep-v1` | Stock whole-voice timer, including oscillator and filter. Stock diagnostic plays note 60 at velocity 127 for 1,000 blocks and releases for 500, first with controls 0-3 at midpoint (stock's own run, `opxy-synth-c8-v1`), then once per swept setting: one control at a time through nine positions from 0 to full scale (each option of a list), the others at their defaults, as the SDK's host estimate sweeps them, then once with every control at the position that ran heaviest in that capture (the firmware picks them from stock's timing totals; the readout names them from the same capture's reports). The voice adapter renders with the swept controls while a sweep run is active. Other combinations of controls are not run. |
| Effect / `opxy-effect-callback-v1` | One stereo `process` callback at declared defaults for 1,500 blocks. Deterministic triangle/impulse input, separate initialized state, zeroed private scratch buffer when declared, 120 BPM transport. Includes the wet algorithm, not the stock send/mix path. |
| Sample / `opxy-sample-callback-v1` | One `render_sample` callback at declared defaults for 1,500 blocks. A generated two-second, mono 44.1 kHz PCM triangle/impulse sample, full region, unity level, forward playback, C4 pitch, 120 BPM, no transpose; note restarts every 500 blocks. Declared sample analysis and assignment run before timing. Output uses the machine's mono/stereo contract. |

Sample and effect tests allocate their own buffers during setup, outside audio
callbacks, and free them afterward. They do not borrow a project's assigned sample,
live voice or send scratch buffer. Failed allocation/preparation or an entirely
silent fixture yields an invalid measurement. Their stopwatch includes counter-call
overhead and may include interrupt time; the stock synth runner masks interrupts
while rendering. Do not compare different scopes as though they include the same work.

100% is the stock diagnostic's normalization budget, not total device utilization.
The number is neither a worst-case bound nor a maximum-polyphony prediction.
Graphics, project scheduling, disk/USB I/O and sample preparation are excluded.
Check demanding settings, control motion, many simultaneous voices, feedback/tails
and normal playback separately. A successful benchmark is not full hardware acceptance.

## Agents and maintainers

`sdk.py test`, `check`, `build` and catalog CI never contact a device. Hardware
profiling is an explicit task: confirm the user wants the device test, prepare
and verify a local build, install only when instructed, then wait for the user to
flash/reboot before collecting. Do not infer installed firmware from a package
path or turn host timings into hardware claims. The same batch captures can
produce one offline report per included machine.
