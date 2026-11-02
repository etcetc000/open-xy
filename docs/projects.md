# Project companions

Projects save each track's machine by engine, effect and page number, and those
numbers come from the [layout](api.md#identity) of the firmware that saved them.
A **companion** is a small JSON file beside the project, recording what those
numbers meant by machine ID, so firmware with another layout, or another
user's firmware, can open the project with the right machines. Sections let
other firmware features keep their own data in the same file.

Open-XY firmware writes a companion after every project save, listing its
machines and what its [track control](api.md#track-control) machines keep with
the project, and reads it whenever it opens a project, the workspace at startup
included ([firmware](abi-1.2.1.md#project-companions)).
[`tooling/projects.py`](../tooling/projects.py) is the reference reader and
writer.

## Files

`song.oxy` sits beside `song.xy`, including `workspace.oxy` beside stock's
`/projects/workspace.xy`. Its own extension keeps stock from taking it for a
project. Stock's MTP view shows only the paths it allows, `.xy` but not `.oxy`
under `/projects`, so Open-XY firmware adds `.oxy` beside `.xy` to that view.
Copy both files when you share a project.

Firmware back on stock leaves companions in place, unread and hidden from MTP.
A project saved on stock no longer matches its companion's
[binding](#binding), so returning to Open-XY reads that project's numbers as
saved, as it would without a companion.

## Format

```json
{
  "format": "open-xy-project",
  "project": {
    "bytes": 183204,
    "crc32": "9f1c0a42"
  },
  "sections": {
    "open-xy.machines": {
      "machines": {
        "karplus": {
          "engine": 36,
          "page": 84
        },
        "pingpong": {
          "effect": 40,
          "page": 21
        }
      },
      "profile": "opxy-1.2.1",
      "version": 1
    }
  },
  "version": 1
}
```

| Field | Contract |
| --- | --- |
| `format` | `open-xy-project` |
| `version` | The envelope's version, 1. Added fields do not change it; a reader ignores a file with a version it does not know |
| `project` | The [binding](#binding) |
| `sections` | Objects by name, each with a positive integer `version` |

A companion is UTF-8 JSON of at most 64 KiB. Readers ignore fields they do not
know. A file that is not valid JSON, has another `format` or a newer `version`,
or has an invalid binding or section, is ignored whole.

### Binding

`bytes` and `crc32` (zlib CRC-32, eight lowercase hex digits) of the `.xy`
file the companion describes. Readers use a companion only when both match the
project's bytes.

### Sections

A section name is `owner.name`, lowercase letters, digits and hyphens, where
the owner is the GitHub user or organisation that defines it; `open-xy` is the
SDK's. A section changes `version` only when an older reader would misread it,
and a reader ignores a section with a version it does not know, using the
rest.

A writer replacing a project rewrites its own sections. It keeps other owners'
sections unchanged while the previous companion still matched the project it
replaces, and drops them otherwise. The firmware writes `open-xy.machines` and,
when a track control machine keeps anything, `open-xy.control`.

### Canonical form

Writers emit one form: keys sorted, two-space indentation, LF line endings and
a final newline, with no timestamps. The same contents are the same bytes, and
a change diffs as the lines it touched. Readers accept any JSON within the
[reader limits](#reader-limits), but never `NaN` or `Infinity`, which are not
JSON. A key given twice in one object counts once, its last value deciding.

### Reader limits

The firmware reader streams the file and keeps no document, so a companion it
reads stays within these limits; one beyond them is ignored whole:

- at most 64 KiB;
- a value it skips (an unknown field, or another owner's section) nested at
  most 8 objects or arrays deep;
- section names at most 64 bytes;
- at most 256 distinct machines in `open-xy.machines` (the reference reader
  enforces this one too).

## `open-xy.machines`

Version 1. `profile` is the firmware profile that saved the project, and
`machines` its machines' numbers by ID, in the layout's form: an `engine` and
`page`, or an `effect` and `page`, each an integer. A machine ID is a
lowercase letter, then lowercase letters, digits or `_`, at most 15
characters; a section with any other is invalid. The firmware lists every
machine it holds, including ones the project does not use; the host writer
lists its whole layout.

A reader with the same profile maps each listed engine or effect number,
0 to 62, to that machine's number in its own layout. A machine missing from
its layout is never replaced by whatever holds its old number: the reader
opens that track with stock's fallback for a machine it lacks, Prism (engine
18) on an instrument track or no effect (0) on a send, and does not yet say
which machine was missing. A number two listed machines claim falls back the
same way, since neither can be trusted. Numbers the section does not list are
read as saved. A section for another profile is ignored.

## `open-xy.control`

Version 1. `machines` holds what each track control machine kept with the
project, by machine ID, as a base64 string of at most 8192 bytes. The machine
alone knows what the bytes mean. After loading a project, the firmware gives
each machine its entry when the companion matches the project, and nothing
otherwise.

`aux` holds, by track index (`"11"` for track 12) and then by pattern index
(`"0"` to `"15"`), the stock role's eight controls kept aside for each pattern
a machine [took over](api.md#aux-tracks), as base64 of eight little-endian
32-bit words. The project itself holds which machine each pattern runs, in its
engine number. After a load, a pattern whose machine the firmware lacks is the
stock role again and gets those controls back. Early test builds of aux tracks wrote one entry per
track instead (`"machine"` and `"controls"`); the reader skips it, so such a
project opens with CV on track 12, playing the machine's values as its controls.

## Commands

| Command | Does |
| --- | --- |
| `python sdk.py project write --layout layout.json <song.xy>...` | Writes each project's companion from the layout of the firmware that saved it, for projects saved before companions |
| `python sdk.py project check <song.xy>...` | Reports whether each project's companion applies to it, and which machines it names |
