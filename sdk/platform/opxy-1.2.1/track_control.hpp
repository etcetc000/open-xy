// Synths with track control: generated table (track_control_table.cpp.in);
// the GUI-tick overlay lives in track_control.cpp.
#pragma once
#include <opxy/track_control.hpp>

namespace opxy { namespace platform {
// In a browser kit an unselected machine's ready resolves to a function returning 0.
struct ControlMachine {
    const char* id;  // absolute symbol: the engine ID, supplied at link time
    int (*ready)();
    // Each copy's state: copy 0 for a unique machine, else the copy's track.
    void (*reset)(u32 copy);
    void (*control)(u32 copy, const control::Input*, control::Overlay*);
    // What it keeps with each project (project_companion.cpp): its machine ID,
    // at most saved_bytes, and save/load, or 0 and null when it keeps nothing.
    const char* name;
    u32 saved_bytes;
    u32 (*save)(u8* data);
    void (*load)(const u8* data, u32 bytes);
    bool unique;  // runs only while one track holds it
    u32 engine() const { return static_cast<u32>(reinterpret_cast<__UINTPTR_TYPE__>(id)); }
};
// The marks track control shows over every page (track_control_overlay.cpp.in):
// a machine captures edits, or the selected track holds an overlay from one that
// marks; `machine` indexes sdk_control_machines. GUI only.
const u32 kMarkCapture = 1, kMarkOverlaid = 2;
struct Marks { u32 kind, machine; };
Marks control_marks();
}}

extern "C" {
extern const opxy::platform::ControlMachine sdk_control_machines[] __asm__("sdk_control_machines");
extern const opxy::u32 sdk_control_machine_count __asm__("sdk_control_machine_count");
void sdk_gui_tick() __asm__("sdk_gui_tick");
// Stock's step values reaching the live parameters: an overlay stays over them.
void sdk_param_step_set(opxy::u32 project, opxy::u32 track, opxy::u32 index, opxy::i32 value)
    __asm__("sdk_param_step_set");
// A project replacement invalidates transient edits/overlays without touching stock's buffers.
void sdk_control_forget() __asm__("sdk_control_forget");
// Whether each machine of the table has had its reset for this project: a unique
// machine keeps one state for the project, so arriving on a track doesn't reset it.
extern bool sdk_control_prepared[] __asm__("sdk_control_prepared");
// The GUI tick's half of a project load or new project (track_control_project.cpp).
void sdk_control_kept_apply() __asm__("sdk_control_kept_apply");
// Registers the marks' screen slot on the GUI role, after the machines' screens.
void sdk_overlay_startup() __asm__("sdk_overlay_startup");
void sdk_param_edit_set(opxy::u32 project, opxy::u32 track, opxy::u32 index, opxy::i32 value)
    __asm__("sdk_param_edit_set");
// The value stock's displayed-value getter shows before its LFO amount, through
// the jump hook sdk_param_shown_hook (track_control_hooks.s).
opxy::i32 sdk_param_shown(opxy::u32 project, opxy::u32 track, opxy::u32 index, opxy::u32 playing)
    __asm__("sdk_param_shown");
}
