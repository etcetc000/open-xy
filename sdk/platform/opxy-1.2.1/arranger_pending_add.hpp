#pragma once
#include <opxy/arranger.hpp>
namespace opxy { namespace platform {
// Roll back only an unpublished append while its project still owns the model.
// Existing source identities are untouched; a concurrently extended pool is not shrunk.
inline void arranger_cancel_added_clip(arranger::Model& model, const arranger::Model& before,
                                      u32* count, u32 previous_count, bool bound) {
    if (!bound) return;
    model = before;
    if (*count == previous_count + 1) *count = previous_count;
}
struct ArrangerAddContext {
    u32 project, generation, track, cursor, pattern;
    i32 selection;
};
// GUI-owned staged request. Each native operation waits for the preceding
// progress frame and a 200ms settling interval, matching hardware-tested 015.
struct ArrangerPendingAdd {
    enum Stage { idle, project_check, history, create, commit, select, draw };
    bool pending, painted, draw_requested, record_after, draw_complete;
    Stage stage;
    u32 painted_at;
    ArrangerAddContext request;
    bool enqueue(const ArrangerAddContext& value, bool permitted, bool record = false) {
        if (pending || !permitted || !value.project) return false;
        request = value; pending = true; stage = project_check;
        painted = draw_requested = draw_complete = false; record_after = record; return true;
    }
    void cancel() { pending = painted = draw_requested = record_after = draw_complete = false; stage = idle; }
    bool bound(const ArrangerAddContext& current) const {
        return request.project == current.project && request.generation == current.generation;
    }
    bool context_matches(const ArrangerAddContext& current) const {
        return bound(current) && request.track == current.track && request.cursor == current.cursor &&
            request.pattern == current.pattern && request.selection == current.selection;
    }
    void acknowledge(u32 now) { if (pending && !painted) { painted = true; painted_at = now; } }
    bool ready(u32 now) const { return pending && painted && u32(now - painted_at) >= 200; }
    void next() { stage = static_cast<Stage>(stage + 1); painted = false; }
};
}}
