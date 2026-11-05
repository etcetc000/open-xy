# Modules

A module is one machine in a file of its own, `<id>.opx`, that Open-XY firmware
loads at power-on from `/open-xy/modules` on the user drive. Installing it means
copying the file; removing it means deleting the file. No firmware build or
flash is involved.

A module is built from the same machine folder as a built-in machine, unchanged.
So far only synths with their own display can be modules.

## Build

```sh
python sdk.py module build --machine <machine folder> --version 1.0.0 \
  --out <out>/open-xy/modules/<id>.opx
```

`--toolchain` and `--compiler` work as for [firmware builds](build.md). The
module is named after the machine's ID, and the file must be `<id>.opx`. Raise
`--version` (`MAJOR.MINOR.PATCH`) with every release.

The firmware that loads modules is an ordinary build with `--modules`:

```sh
python sdk.py build --modules --machine <folder> ... --out <firmware out>
```

## Install and remove

1. Put the OP-XY in file-transfer mode (COM → MTP).
2. `python sdk.py device push <out>`
3. Power cycle.

To remove a module, delete it from `/open-xy/modules` over MTP and power cycle.
The folders appear a few seconds after the first power-on with this firmware.

## What the file carries

Everything the machine needs, so nothing is installed beside it:

- its code;
- its screen artwork and catalog entry;
- its presets, listed as factory presets while the module is loaded;
- its controls' behaviour: fine turns, enum steps, value captions.

Deleting the file removes all of it, presets included.

## Numbers and projects

The firmware numbers a module by its machine ID when it loads, with the same rule
a build uses for a machine new to its layout, so a module gets the numbers a
built-in machine of that ID would. Projects record modules in their
[companion](projects.md) like built-in machines. A project that uses a module
the device no longer has opens that track with Prism.

## Safe mode

Hold Shift while powering on. The boot label shows `SAFE-1.2.1`, and neither
board loads any module. The folders still appear over MTP, so a module that
keeps the device from starting can be deleted.

## Limits

- Machine IDs must start with a lowercase letter, followed by lowercase letters,
  digits or `_`, at most 15 characters in all.
- At most 1 MiB per module file.
- No fixed number of modules. Memory decides, the GUI board's first (about 11 MB
  free in measured projects), and the 256-entry engine and page tables. A
  module that does not fit is skipped; the device starts without it.
- Built for stock OS 1.2.1.

## Reference

- File format and loader statuses: [`module_loader.hpp`](../sdk/platform/module_loader.hpp)
- What a module gets from the firmware: [`module.hpp`](../sdk/include/opxy/module.hpp)
