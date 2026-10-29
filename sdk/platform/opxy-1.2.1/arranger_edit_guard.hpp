#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
inline bool arranger_edit_binding_valid(bool editing, bool gui, u32 project, u32 current_project,
                                       u32 generation, u32 current_generation, u32 track,
                                       u32 selected_track, u32 pattern) {
    return editing && gui && project && project == current_project && generation == current_generation &&
           track < 8 && track == selected_track && pattern < 16;
}
inline bool arranger_edit_key_allowed(u32 key, bool step_held, bool exit_pending) {
    return !exit_pending && ((key >= 26 && key <= 52) || (key >= 56 && key < 72) || key == 55 ||
                            (key >= 10 && key <= 13 && step_held));
}
// Never suppress the release of a press already forwarded to the stock editor.
inline bool arranger_edit_swallow_release(u32 key, bool forwarded) {
    return key < 72 && !forwarded;
}
// Runs before stock held-list bookkeeping. Rejected downs must not become
// modifiers visible to the audio-side stock hold polling routine.
struct ArrangerRawEditGate {
    bool blocked[72];
    bool consume(u32 key, bool down, bool editing, bool allowed, bool already_held) {
        if (key < 72 && blocked[key]) {
            if (!down) blocked[key] = false;
            return true;
        }
        if (editing && down && !allowed) {
            if (key < 72 && !already_held) blocked[key] = true;
            return true;
        }
        return false;
    }
};
}}
