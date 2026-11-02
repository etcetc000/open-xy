// Snapshots: ten project-wide pictures of every instrument track's stored
// parameters, kept by the SDK with the project.
//
// Taking and reading are separate on purpose. An extension takes a snapshot
// through its `snapshot` action (Actions::take_snapshot), in answer to a
// gesture it claims, and the user takes one with Save in the SDK's snapshot
// menu. Track control machines only read snapshots, on the GUI side in
// control(), and act on them through their ordinary overlay. Taking a snapshot
// never changes a track, starts no editing mode and is not a parameter, so no
// LFO, lock or preset can take one.
//
// A snapshot holds each track's base values (what its page stores for the
// current pattern, as a saved project keeps them), never an overlay, a lock or
// an LFO's movement, and each track's engine, so a reader can tell when a track
// has since changed engine and its values no longer mean the same thing.
#pragma once
#include <opxy/track_control.hpp>

extern "C" const void* sdk_snapshot(opxy::u32 slot);
extern "C" opxy::u32 sdk_snapshot_engine(opxy::u32 track);
extern "C" opxy::u32 sdk_snapshot_generation();
extern "C" bool sdk_snapshot_holds(opxy::u32 slot);

namespace opxy { namespace snapshots {
// One per black key, as stock numbers them (extensions::keys::black_index):
// slot n is labelled extensions::keys::black_label(n), 1..9 then 0.
const u32 slots = 10;

struct Snapshot {
    u32 taken;  // always 1 for a snapshot get() returns
    // Each instrument track's engine when the snapshot was taken (opaque).
    u32 engines[control::tracks];
    // Each instrument track's stored parameter words, as Input::values numbers them.
    i32 values[control::tracks][control::track_parameters];
};

// GUI side: slot 0..9, or null when it holds no snapshot. Valid until the end
// of the current control() call. Browser previews answer null.
inline const Snapshot* get(u32 slot) {
    return slot < slots ? static_cast<const Snapshot*>(sdk_snapshot(slot)) : nullptr;
}
// Whether `track` still holds the engine it held when `snapshot` was taken.
inline bool matches(const Snapshot& snapshot, u32 track) {
    return track < control::tracks && snapshot.engines[track] == sdk_snapshot_engine(track);
}
// Changes whenever any slot is taken, loaded or cleared: a reader may keep
// work derived from snapshots until it changes.
inline u32 generation() { return sdk_snapshot_generation(); }
// Whether the tracks hold `slot` now: every track still holding the engine it
// held then holds its stored values, and at least one does. Browser previews
// answer false.
inline bool holds(u32 slot) { return slot < slots && sdk_snapshot_holds(slot); }
}}
