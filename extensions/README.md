# Settings extensions of this SDK

`cpu` ("CPU (experimental)") names the seven CPU capabilities
([docs/cpu-capabilities.md](../docs/cpu-capabilities.md)): `boot_log_fix`, `idle`,
`block_trims`, `output_slack`, `load_governor`, `pitch_constants` and `fast_helpers`,
the last one off (setting 0). It is experimental, always.
Like every settings extension it holds only `extension.json`: the SDK owns every
patch site, and the extension only names capabilities. Add it to a collection's
`extensions` (`{"path": ".../extensions/cpu"}`) and build with `sdk.py build`.
A build without it has none of them.

`idle` cuts voices only with a threshold, which is the `release_floor` setting's, and
`cpu` does not select `release_floor`: with `cpu` alone, idle sends act and no voice
is cut (docs/cpu-capabilities.md#idle). A collection that wants the idle cut selects
`release_floor` too, with its own settings extension; that also shortens every
release that crosses the floor, which is a change in sound.

`load_governor` is a plain switch, on when named: stock's governor then acts on a
short mean of the block load, cuts voices from 99 % of its scale (93.8 % of a real
block) instead of 94.5 % (89.6 %), and gives them back under 93 % (88.1 %) instead
of 84.8 % (80.3 %). It never sheds overload that comes in bursts between lighter
blocks: such a project lags or clicks until its load drops
(docs/cpu-capabilities.md lists what it misses). Like every build
capability it is chosen only by `sdk.py build`: `sdk.py settings apply` refuses it
and browser kits cannot carry it.

`pitch_constants` is a plain switch like `load_governor`: stock's note-to-pitch routine
loads its two constants ready-made (about 600 cycles less a call in the simulator, one
call a synth voice start, with every result equal to stock's).

`fast_helpers` has a setting: 0, off (the extension's value and the default),
or 1, which links hand-written fill, copy and divide routines in place of the
plain ones. Every native machine calls them; their results are checked against C
in an instruction model, not on silicon (docs/cpu-capabilities.md lists the risks).
A collection that wants them sets `"fast_helpers": 1` in its own copy of this
extension.

A collection that wants only some of the capabilities selects them with a settings
extension of its own that names just those, in place of `cpu`.

This is the SDK's only extension: others live in repositories of their own and
reach the builder through `catalog/extensions.json`. `cpu` stays with the SDK
because everything it names is SDK code chosen when the firmware is built, which the
builder cannot offer.
