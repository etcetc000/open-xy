# Your first machine

Create a small C++ instrument, play it in the browser, then change its pitch by
one octave. This path needs no firmware, Blackfin compiler or device. Run the
commands from the SDK checkout. Your machine can live in `local/` while learning
or in a separate source repository.

## Check your tools

Install [Python 3.11 or newer](https://www.python.org/downloads/),
[Node 22.12 or newer](https://nodejs.org/en/download), and
[LLVM](https://releases.llvm.org/download.html) with Clang's `wasm32` target and
`wasm-ld`. Use a recent Chrome or Edge for the workbench.

Create an isolated Python environment. On Windows PowerShell:

```powershell
python -m venv .venv
.venv/Scripts/python.exe -m pip install numpy==2.3.5
.venv/Scripts/python.exe sdk.py doctor --preview --cxx "C:/Program Files/LLVM/bin/clang++.exe"
```

On macOS or Linux, create it with `python3 -m venv .venv`, use `.venv/bin/python`
instead of `.venv/Scripts/python.exe`, and pass `--cxx /path/to/clang++` for your
LLVM installation. Apple's Xcode Clang lacks `wasm-ld`: on macOS install
Homebrew's `llvm` and `lld` and pass `--cxx "$(brew --prefix llvm)/bin/clang++"`.
The commands below use
`python` for that environment's interpreter; substitute its full path if you
have not activated it. Omit `--cxx` when the correct Clang is already on PATH.

The doctor must print **PASS: WebAssembly compilation, linking and execution**.
It actually compiles and executes a probe. A C++ compiler that works for desktop
programs may still lack WebAssembly support. If the probe fails, choose a Clang
installation containing `wasm-ld` and repeat it before creating the machine.

## Create and play

```sh
python sdk.py new --id mymachine --name "My Machine" --out local/mymachine
python sdk.py preview --source local/mymachine --out local/my-preview-001
python tools/serve_harness.py --preview local/my-preview-001
```

Add the same `--cxx` option from the doctor to the `preview` command if needed.
The ID follows the [machine name rule](api.md#registration-boundary): a
lowercase letter, then up to 14 lowercase letters, digits or underscores. It
must differ from the bundled machines.

Open http://127.0.0.1:8917/. You should see **My Machine**, **Local authoring**,
four controls and the **Triangle** and **Pulse** presets. Click **Enable audio**,
then use the on-screen keys or `A W S E D F T G Y H U J K`. Start with a low
monitor gain. **Stop all** or Escape releases the notes.

**Blend** moves from triangle to pulse, **Width** changes the pulse's duty cycle,
and **Level** changes its output level. **Motion** changes the display animation.
The starter is a learning oscillator; its pulse needs band-limiting before use
as a production instrument.

## Make an audible change

Open `local/mymachine/engine.cpp`. Inside `render()`, change:

```cpp
voice.phase += pitch[i];
```

to:

```cpp
voice.phase += pitch[i] / 2;
```

The supplied pitch is a phase increment for each sample. Halving it makes the
oscillator repeat half as often, lowering every note by one octave.

Stop the server with Ctrl+C, build into a new directory, then serve that snapshot:

```sh
python sdk.py preview --source local/mymachine --out local/my-preview-002
python tools/serve_harness.py --preview local/my-preview-002
```

Use the same `--cxx` option on this rebuild if you selected LLVM explicitly.
Reload the browser, enable audio and play the same key. It should sound one
octave lower. Restore the original line and build another fresh snapshot to
undo the change. `tools/check_first_machine.py`, which CI also runs, makes this exact edit and
checks that A4 changes from 440 Hz to 220 Hz:

```sh
python tools/check_first_machine.py
```

That exercise also accepts `--cxx`. It uses temporary source directories and
does not change your machine.

## What you own

| File | Purpose |
| --- | --- |
| `definition.json` | Name, four controls, presets and declared state sizes |
| `core.h`, `engine.cpp` | `Voice` and `Shared` state, lifecycle callbacks and the 64-sample render |
| `display.h`, `display.cpp` | Display state and drawing callback |
| `panel.svg.fragment` | Artwork; IDs carry the machine's ID (`<id>_panel`, `<id>_node_N`, `<id>_palette_N`) |
| `machine.json` | Kind, SDK version and a free engine ID and page, used by a [firmware build](build.md#3-add-your-machine) of the folder |

Change labels and presets directly in `definition.json`; custom previews read it
when building, with no separate JavaScript definition generator. Parameter
order determines the C++ control indices. See the [engine API](api.md) for
buffer ownership and the [display API](display.md) for drawing.

Keep callbacks bounded: no allocation, blocking, logging, I/O, OS headers or
platform conditionals. The SDK supplies the adapters and memory. A custom
preview compiles `engine.cpp` and `display.cpp` through those adapters; additional
implementation code must be included by them. Includes must name local machine
headers or SDK `opxy/*.hpp` headers. Keep compiled output outside the source
directory, and use the SDK server rather than serving the checkout root.

## When a command fails

| Message or symptom | Next action |
| --- | --- |
| Compiler cannot build WebAssembly | Run `doctor --preview` with the exact `--cxx` path used for the build |
| Output already exists | Choose a fresh output suffix; existing source and builds are never overwritten |
| Preview is stale | Rebuild after source edits, restart the server with the new output and reload |
| Declared size differs from C++ type | Update the corresponding size/alignment in `definition.json` after changing state |
| Peak or drawing check fails | Correct the callback or its declared contract; the build prints the failing check |
| No sound | Enable audio, check monitor gain and Level, then play a key; the workbench status reports audio errors |
| Address already in use | Stop the previous server, or pass `--port 8937` and open that port |

Local authoring permits different sounds and drawings while running the shared
callback checks described in the [preview guide](preview.md). It does not
allocate a native engine ID or produce an installable machine. To put it on
the device, [build your own firmware](build.md#3-add-your-machine).
