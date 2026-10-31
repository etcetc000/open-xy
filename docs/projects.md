# Project companions

Projects save each track's machine by engine, effect and page number, and those
numbers come from the [layout](api.md#identity) of the firmware that saved them.
A **companion** is a small JSON file beside the project, recording what those
numbers meant by machine ID, so firmware with another layout, or another
user's firmware, can open the project with the right machines. Sections let
other firmware features keep their own data in the same file.

Open-XY firmware writes a companion after every project save, listing its
machines, and reads it whenever it opens a project, the workspace at startup
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

The host writer rewrites its own sections and keeps other owners' sections
unchanged while the previous companion still matches the project, dropping
them otherwise. Firmware retains sections from the committed source project and rewrites
`open-xy.machines` plus sections explicitly published by their owners.

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

## Commands

| Command | Does |
| --- | --- |
| `python sdk.py project write --layout layout.json <song.xy>...` | Writes each project's companion from the layout of the firmware that saved it, for projects saved before companions |
| `python sdk.py project check <song.xy>...` | Reports whether each project's companion applies to it, and which machines it names |

## Interrupted firmware saves

Firmware writes `song.oxy.tmp` after stock successfully saves `song.xy`, checks
all writes and close, then renames the completed temporary over `song.oxy`.
A failed write or close leaves the published companion untouched. Stock's
replacement removes the old destination before rename; if rename then fails,
the complete temporary remains available for recovery.
Loading prefers the published companion when it validates and matches the
project bytes and CRC-32, even if its version or profile is unsupported or it
contains no machine mappings. Only a missing, malformed or mismatched published
companion permits checking the temporary with the same validation. Partial files, read errors and companions for other project bytes
never supply machine mappings. Project measurement is bounded to 16 MiB.

This is recovery for a completed companion awaiting publication, not an atomic
transaction across the project and its companion. There is no filesystem sync
or power-loss durability guarantee. Preflight protection or a size limit can refuse a save before stock changes
the project. Once stock has saved successfully, a later companion publication
error cannot roll that project write back; the native success result is retained.
Keep both project and companion in backups.

## Extension state and project ownership

Firmware retains up to 32 valid sections, including sections whose extensions
are absent from the current collection. Retained values are copied verbatim;
`open-xy.machines` is regenerated for the current collection. A registered
extension's opaque payload uses `{"version": 1, "data": "lowercasehex"}` under
its owned namespace. The version is its declared schema version; the SDK does
not decode the binary payload. Changing that version preserves the old section
and reports it as unsupported. Explicit schema migration is not exposed.

A companion is staged against the loaded candidate and becomes active only for
its matching committed project identity and generation. New invalidates an
abandoned candidate. Save As copies the active source's sections, regardless of
what previously occupied the destination. Native named Save moves both `.oxy`
and `.oxy.tmp` alongside the renamed `.xy`. The complete output must fit the
64 KiB envelope before stock serialization starts; otherwise the save is refused.

A valid companion bound to different project bytes is stale, as after editing
in stock firmware: it supplies no extension state and does not block saving.
An opened but corrupt companion, or a bound unsupported envelope, instead
protects the loaded project's original files from subsequent saves. The SDK
shows **project data: reload or New** on the controller and delivers an
`unavailable` state notification. Restore a valid project/companion pair and
reload, or choose New to start an independent project. The SDK does not silently
discard that recovery data. A valid load or New clears protection.

Native `fopen` failure has no verified missing-versus-I/O-error distinction;
it is treated as an absent companion. Read and close errors after opening are
detected. File publication and two-file rename are not atomic transactions, and
host tests do not establish power-loss durability or device scheduling behavior.
