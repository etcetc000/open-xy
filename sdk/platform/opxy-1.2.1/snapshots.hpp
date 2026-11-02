// The snapshot store's SDK-internal side (snapshots.cpp), part of the control service.
#pragma once
#include <opxy/snapshots.hpp>

namespace opxy { namespace platform {
// The encoding's most bytes: a version byte and two slot bytes, then per slot
// eight engine words and 8 x 58 values of at most five bytes each.
const u32 kSnapshotBytes =
    3 + snapshots::slots * (control::tracks * 4 + control::tracks * control::track_parameters * 5);
// Controller path only: fills a slot from every instrument track's stored values.
bool snapshot_take(u32 slot);
// After a new project: no snapshots.
void snapshots_clear();
// Writes a slot's values back into every track that still holds the engine it
// held then, through stock's own parameter setter. False for an empty slot.
bool snapshot_load(u32 slot);
// How many instrument tracks still hold the engine they held when `slot` was taken.
u32 snapshot_fits(u32 slot);

// Whether the tracks hold `slot` now: every track still holding its engine
// holds its values, and at least one does.
bool snapshot_holds(u32 slot);
// Empties the slots in `mask` (bit n for slot n); how many held a snapshot.
u32 snapshots_clear_slots(u32 mask);

// The saved form (docs/projects.md): bytes written, or 0 for none.
u32 snapshots_encode(u8* out, u32 capacity);
// Replaces every slot with what a project kept, or with none.
void snapshots_decode(const u8* data, u32 bytes);
}}

extern "C" {
// The extension runtime's snapshot actions, through startup.cpp: the store's in a
// build with the control service, stand-ins that refuse in one without it.
bool sdk_snapshot_take(opxy::u32 slot) __asm__("sdk_snapshot_take");
// Loads a slot into the tracks that still hold its engines; false when none can.
bool sdk_snapshot_load(opxy::u32 slot) __asm__("sdk_snapshot_load");
opxy::u32 sdk_snapshot_clear(opxy::u32 mask) __asm__("sdk_snapshot_clear");
}
