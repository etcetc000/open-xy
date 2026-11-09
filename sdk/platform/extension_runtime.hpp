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

// An extension's settings page (extension.json "settings"): a title and rows,
// each a label and the text of its values, the value it starts at, and the key
// stock saves it under (ext_<id>_<row>). A row with one value is an info row:
// its text is shown and never edited, and it raises no event.
const u32 kSettingPages = 4, kSettingRows = 12, kSettingValues = 8;
struct SettingRow {
    const char* label;
    const char* const* values;
    u32 value_count, initial;
    const char* key;
};
struct ExtensionSettings {
    const char* title;
    const SettingRow* rows;
    u32 row_count;
};

struct ExtensionEntry {
    const char* id;
    u32 claims, actions;  // Bits in registry order.
    const extensions::Hooks* hooks;
    const ExtensionSettings* settings;  // Null: no settings page.
};
struct ExtensionRegistry {
    const u32* claim_keys;
    u32 claim_count;
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
// The collection's settings pages, in order, once the collection checks out;
// null past the last. `entry` receives the page's collection index.
const ExtensionSettings* extensions_settings_page(u32 page, u32& entry);
// A settings row's value is known (restored) or was edited: delivered as the
// setting event at the next update, once per change.
void extensions_setting(u32 entry, u32 row, u32 value);

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
// Read the settings rows' saved values into extensions_setting; once, on the
// controller path, before the first event.
void extension_settings_load();
// A queued key, before stock's: knob clicks open and leave an extension's
// settings page from the open-xy list, and M1 leaves it; true for a press the
// adapter only records as held (stock's action never runs).
bool extension_settings_key(const u32* event);
}}
