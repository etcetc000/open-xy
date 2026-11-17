# Experimental Morph bridge

This proposal ports SDK support for the external Morph AUX machine to current
`staging`. It is a compatibility bridge for review, not a catalog entry or a
conversion to the generic extension API. The machine, Manager, custom builder
UI, MultiFX implementation, firmware and generated releases are excluded.

Morph captures stored parameter banks across eight tracks and offers an
instrument-page HUD. Alongside existing control overlays it needs guarded
stored-value transactions, physical input ownership, verified screen
substitution and explicit GUI preset operations.

## Interfaces

| Header | Contract |
| --- | --- |
| `morph_aux_events.hpp` | Bounded read-and-clear encoder queue for the unique Morph owner on AUX track 11 |
| `morph_store_bridge.hpp` | Stored-value sessions bound to project, scene, track blocks and SHIFT schema; recheck pending writes |
| `morph_hud_overlay.hpp` | Optional screen/input callbacks; requested substitution does not authorize input |
| `morph_hud_raw_turn.hpp` | A-bank pulse sampler; reset on ownership, track, module or modifier changes |
| `morph_hud_slot_map.hpp` | M1 slots derived from installed SDK metadata, with basic stock-slot fallback |
| `morph_hud_snapshot.hpp` | MS04 read-only snapshot for one instrument track, including stored filter model |
| `morph_hud_contract.hpp` | Machine-side command types and authorization witness declarations |
| `morph_midi_input.hpp` | Latest CC15 on track/channel 11; delegate unmatched messages to stock |
| `morph_pad_input.hpp` | Change physical pad ownership on release; finish an existing gesture with its original owner |
| `morph_preset_files.hpp` | Explicit GUI reads/writes of sixteen two-copy A/B preset slots; no background scan |

State is plain zero-initialized C++11 storage. Stored writes are available only
inside the unique owner's GUI control callback. Project/scene/block/schema
changes invalidate sessions. The visual filter type uses stored block `+0x28`,
as stock's GUI does, rather than the independently updated audio-side table.

A read-only menu request suppresses paint slot 74 only when the compositor
actually selected Morph's registered replacement. It changes no menu actions.
A substituted source slot is guarded against accidental paint suppression.
The standalone display preview reports snapshot-unavailable instead of
fabricating captured values or input authorization.

## Profile changes

Two existing dispatch calls redirect under the control service:

- `0x8d58e818`: MIDI CC parameter dispatch; unmatched traffic delegates unchanged.
- `0x8d5b27f6`: physical pitch-pad receiver; MIDI/reset callers remain stock.

Original patches, stock authentication, memory reservations and compiler pins
are retained. `builder/core.mjs` changes only its profile digest to authenticate
the two added entries; no builder functionality changes.

## Checks and use

Use the external Morph machine in a separate repository against this SDK branch.
The PR includes no machine registration. Run:

```sh
python sdk.py check
python -m unittest discover -s tests -p test_morph_bridge.py -v
python -m unittest discover -s tests -p test_track_control.py -v
python -m unittest discover -s tests -p test_sample_services.py -v
```

The hook-preimage test uses `OPXY_TEST_STOCK_OS` only when supplied privately,
outside the checkout. Full native/release verification needs the complete
fixtures listed in `build.md`; these host tests do not replace it.

## Review decisions

This compatibility port explicitly retains machine identity `morphcapture`,
AUX host 11, page 219, menu slot 74, the MS04 snapshot shape and fixed preset
format. These need review before a general public API is promised. A reusable
interface should make ownership, registered-page identity, snapshot schema and
storage policy generic; renaming the functions alone does not achieve that.

Preset operations are explicit GUI actions, while generic extension callbacks
normally forbid I/O. Stored-write policy and physical MIDI/pad events likewise
need review against the SDK extension model. The contract remains experimental
and source-level.

The prior custom SDK/Morph combination was exercised on hardware. That does
not qualify this port to newer upstream code. Native compilation, allocation,
release verification, device regression and CPU profiling for this branch remain
pending. This proposal does not claim catalog readiness.
