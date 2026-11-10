#pragma once
#include <opxy/extension.hpp>

namespace opxy { namespace platform {
using extensions::RecordingContext;
using RecordedNote = extensions::Note;

// Actions are SDK code, one bit each in profiles/extensions.json's order; a
// test keeps the two in step. Claims are data: the generated registry gives
// each claim's keys, and each action the claims that grant it (0: any claim).
const u32 action_edit_notes = 1u << 0;
const u32 action_notify = 1u << 1;
const u32 action_loop_bars = 1u << 2;
const u32 action_play_from = 1u << 3;

struct ExtensionEntry {
    const char* id;
    u32 claims, actions;  // Bits in registry order.
    const extensions::Hooks* hooks;
};
// One key a claim governs (a claim may govern several), the stock screen
// where its holder takes the key from stock (0: none; the key only fires the
// claim's actions), and a key that must be held for it to (keys::none: none).
struct ClaimKey { u32 claim, key, screen, held; };
struct ExtensionRegistry {
    const ClaimKey* claim_keys;
    u32 claim_key_count, claim_count;
    const u32* action_claims;
    u32 action_count;
};
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
// Whether, on this screen, a claim's holder takes the key, with or without
// Shift, its claim's held key (if any) being held: the adapter then keeps stock
// from it. Only claims with a screen take keys.
bool extensions_takes(u32 key, u32 screen);
// Whether a claim its holder owns takes a key on this screen only while `held`
// is held.
bool extensions_needs_held(u32 held, u32 screen);

// An instrument track's pattern, as notes are written to it.
struct TrackPattern { u32 pattern, length, scale; bool editable; };
TrackPattern extension_pattern(u32 track);
RecordingContext extension_context(bool& playing);
i32 extension_position(u32 track);
u32 extension_scaled_delta(u32 track, u32 delta);
i32 extension_collision_radius(u32 track);
bool extension_record_held();
bool extension_key_held(u32 key);
u32 extension_write(u32 track, const RecordedNote* notes, u32 count, bool replace);
void extension_notify(const char* text);
void extension_notify_for(const char* text, u32 milliseconds);
// The sequencer's tracks, and the bars they are held in (no_bar: none).
void extension_sequencer(extensions::Sequencer& sequencer);
bool extension_loop(const extensions::BarLoop& loop);
// Start playback with the song clock at a point of the scene.
bool extension_play_from(u32 song_tick);
// Playing: at the end of `track`'s playing bar, the loop and the start from
// `song_tick`; a later loop change, start or stop drops it.
bool extension_loop_at_bar_end(const extensions::BarLoop& loop, u32 song_tick, u32 track);
// The bar menu was built again (its texts are stock's again).
void extension_bar_menu_built();
}}
