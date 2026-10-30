# Platform support

Engine authors use the [audio](../docs/api.md) and [display](../docs/display.md)
interfaces. This directory implements those contracts across platforms.

| Directory | Responsibility |
| --- | --- |
| `include/opxy/` | C++ types, arithmetic, audio and effect buffers, drawing frames and resource pools |
| `host/` | WebAssembly preview bindings |
| `browser/` | AudioWorklet, controls, patch handling and retained drawing backends |
| `platform/` | Blackfin primitives and stock 1.2.1 calling-convention adapters |
| `templates/` | The starter machine `sdk.py new` copies |

Platform conditionals belong here, never in a machine. The generated adapters
provide C linkage and typed storage; machines implement ordinary C++ callbacks.
