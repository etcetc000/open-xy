#pragma once
#include <opxy/extension.hpp>

namespace opxy { namespace platform {
using extensions::RecordingContext;
using RecordedNote = extensions::Note;

// Actions are one bit each in profiles/extensions.json registry order.
const u32 action_edit_notes = 1u << 0;
const u32 action_notify = 1u << 1;
#ifndef OPXY_PLAIN_EXTENSIONS
const u32 action_monitor_input = 1u << 2;
enum class ActionScope : u32 { claimed_control, motion };
#endif

struct ExtensionEntry {
    const char* id;
    u32 claims, actions;  // Bits in registry order.
    const extensions::Hooks* hooks;
#ifndef OPXY_PLAIN_EXTENSIONS
    const extensions::MotionHooks* motion_hooks;
    constexpr ExtensionEntry(const char* id = nullptr, u32 claims = 0, u32 actions = 0,
                             const extensions::Hooks* hooks = nullptr,
                             const extensions::MotionHooks* motion_hooks = nullptr)
        : id(id), claims(claims), actions(actions), hooks(hooks), motion_hooks(motion_hooks) {}
#endif
};
struct ExtensionRegistry {
    const u32* claim_keys;
    u32 claim_count;
    const u32* action_claims;
    u32 action_count;
#ifndef OPXY_PLAIN_EXTENSIONS
    const ActionScope* action_scopes;
    constexpr ExtensionRegistry(const u32* keys, u32 claims, const u32* required, u32 actions,
                                const ActionScope* scopes = nullptr)
        : claim_keys(keys), claim_count(claims), action_claims(required), action_count(actions), action_scopes(scopes) {}
#endif
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
#ifndef OPXY_PLAIN_EXTENSIONS
void extensions_motion();
const char* extensions_monitor_owner();
const extensions::InputMonitorText* extensions_monitor_text();
bool extensions_monitor_available();
#endif
// Once, while the stock USB host stack starts: every extension that takes the event
// adds its drivers to the registry.
void extensions_usb_host_ready(const usb::Registry& registry);
// The 1.2.1 adapter's USB host glue (opxy-1.2.1/usb_host.cpp): the stack's startup hook
// calls usb_host_start each time the host starts.
void usb_host_start();

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
#ifndef OPXY_PLAIN_EXTENSIONS
extensions::Motion extension_motion();
extensions::InputMonitor extension_input_monitor();
bool extension_monitor_input(bool enabled);
#endif
}}
