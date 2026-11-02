#pragma once
#include <opxy/extension.hpp>

namespace opxy { namespace platform {
using extensions::RecordingContext;
using RecordedNote = extensions::Note;

// Actions are SDK code, one bit each in profiles/extensions.json's order; a
// test keeps the two in step. Claims are data: the generated registry gives
// each claim's key, and each action the claims that grant it (0: any claim).
const u32 action_edit_notes = 1u << 0;
const u32 action_notify = 1u << 1;
const u32 action_snapshot = 1u << 2;

struct ExtensionEntry {
    const char* id;
    u32 claims, actions;  // Bits in registry order.
    const extensions::Hooks* hooks;
};
struct ExtensionRegistry {
    const u32* claim_keys;
    u32 claim_count;
    const u32* action_claims;
    u32 action_count;
    // Each claim's chord (tooling.extensions.CHORDS): 0 none, chord_black_keys.
    const u32* claim_chords;
    // Each claim's partner key, pressed together with its key (0: none).
    const u32* claim_partners;
};
const u32 chord_black_keys = 1;
// Generated with the collection (tooling.extensions.generate_collection).
const ExtensionEntry* extension_collection(u32& count);
const ExtensionRegistry& extension_registry();

// These entry points belong only to the version adapter.
void extensions_sync();
void extensions_input_activity();
void extensions_recordable_note_on(u32 track, u32 pitch, u32 velocity);
void extensions_selected_note_off(u32 track, u32 pitch);
void extensions_recording_tick(u32 delta);
void extensions_queued_control(const extensions::Control& control);
// A key arriving at stock's input entry, before stock. The SDK's snapshot menu
// opens on the chord claim of an extension holding the snapshot action, and
// takes that key and, while it is held, the black keys and M1-M4; any other
// chord claim delivers its black keys to its owner. Stock never sees a key taken.
const u32 key_stock = 0, key_taken = 1;
// Taken, but the key was only tapped: stock gets its own press, then this release.
const u32 key_tap = 2;
u32 extensions_key_input(u32 key, bool pressed, extensions::Time time);
// An encoder turn arriving at stock's input entry: true when the open snapshot
// menu takes it (encoder 1 steps the selection).
bool extensions_knob_input(u32 knob, i32 delta);

// An instrument track's pattern, as notes are written to it.
struct TrackPattern { u32 pattern, length, scale; bool editable; };
TrackPattern extension_pattern(u32 track);
RecordingContext extension_context(bool& playing);
i32 extension_position(u32 track);
u32 extension_scaled_delta(u32 track, u32 delta);
i32 extension_collision_radius(u32 track);
bool extension_record_held();
// Whether stock holds a key down now (its held-key list).
bool extension_key_held(u32 key);
u32 extension_write(u32 track, const RecordedNote* notes, u32 count, bool replace);
void extension_notify(const char* text);
}}

// The control service's snapshot store (snapshots.cpp), or startup's stand-in
// that refuses in a build without it.
extern "C" bool sdk_snapshot_take(opxy::u32 slot) __asm__("sdk_snapshot_take");
// Its menu (snapshots.hpp: kMenu* operations), or a stand-in that answers 0.
extern "C" opxy::u32 sdk_snapshot_menu(opxy::u32 op, opxy::i32 arg) __asm__("sdk_snapshot_menu");
