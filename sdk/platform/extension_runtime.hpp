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
const u32 action_lfo_copies = 1u << 2;

struct ExtensionEntry {
    const char* id;
    u32 claims, actions;  // Bits in registry order.
    const extensions::Hooks* hooks;
    // Its layer's nodes in the SDK's overlay (extension_overlay.cpp.in): first and count.
    u32 layer_first, layer_count;
};
struct ExtensionRegistry {
    const u32* claim_keys;
    u32 claim_count;
    const u32* action_claims;
    u32 action_count;
    // Each claim's page (tooling.extensions.PAGES): 0 none, page_lfo. A page
    // claim opens its owner's layer while that page shows.
    const u32* claim_pages;
};
const u32 page_lfo = 1;
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
// The stock page an instrument track shows now (page_lfo, or 0 for any other
// screen), checked on every controller update; a page claim's layer opens and
// closes with it.
void extensions_page(u32 page, u32 track);
// Whether an extension in the collection holds this action.
bool extensions_hold(u32 action);
// A key or encoder turn on the instrument controller, before stock: true when
// the open page layer takes it.
bool extensions_page_key(u32 key, bool pressed, bool shift);
bool extensions_page_turn(u32 encoder, i32 delta, bool shift);
// The SDK's overlay screen (extension_overlay.cpp.in): whether a layer is open,
// its nodes there and which opening it is, its frame time, and its painting.
bool extensions_layer_open(u32& first, u32& count, u32& opening);
void extensions_layer_frame(u32 elapsed_ms);
void extensions_layer_draw(const extensions::Canvas& canvas);

// An instrument track's pattern, as notes are written to it.
struct TrackPattern { u32 pattern, length, scale; bool editable; };
TrackPattern extension_pattern(u32 track);
RecordingContext extension_context(bool& playing);
i32 extension_position(u32 track);
u32 extension_scaled_delta(u32 track, u32 delta);
i32 extension_collision_radius(u32 track);
bool extension_record_held();
u32 extension_write(u32 track, const RecordedNote* notes, u32 count, bool replace);
void extension_notify(const char* text);
// Stock's LFO and its copies (the multi-LFO state, multi_lfo.hpp).
bool extension_set_lfo(u32 track, u32 copy, const extensions::Lfo& lfo);
bool extension_get_lfo(u32 track, u32 index, extensions::Lfo& lfo);
}}
