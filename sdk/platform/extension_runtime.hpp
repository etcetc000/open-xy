#pragma once
#include <opxy/extension.hpp>

namespace opxy { namespace platform {
using extensions::RecordingContext;
using RecordedNote = extensions::Note;

struct ExtensionEntry {
    const char* id;
    bool record_double_tap, edit_notes;
    const extensions::Hooks* hooks;
};
const ExtensionEntry* extension_collection(u32& count);

// These entry points belong only to the version adapter.
void extensions_sync();
void extensions_activity();
void extensions_note_on(u32 track, u32 pitch, u32 velocity);
void extensions_note_off(u32 track, u32 pitch);
void extensions_tick(u32 delta);
void extensions_control(u32 key, bool pressed, bool accepted, bool unmodified,
                        extensions::Time time);

RecordingContext extension_context(bool& playing);
i32 extension_position(u32 track);
u32 extension_scaled_delta(u32 track, u32 delta);
i32 extension_collision_radius(u32 track);
bool extension_record_held();
u32 extension_write(u32 track, const RecordedNote* notes, u32 count);
void extension_notify(const char* text);
}}
