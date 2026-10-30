#pragma once
#include <opxy/types.hpp>
namespace opxy { namespace platform {
struct ArrangerAddContext {
    u32 project, generation, track, cursor, pattern;
    i32 selection;
};
// GUI-owned single request. Taking it clears ownership before any mutation,
// so reentrant polls and repeated presses cannot execute it twice.
struct ArrangerPendingAdd {
    bool pending;
    ArrangerAddContext request;
    bool enqueue(const ArrangerAddContext& value, bool permitted) {
        if (pending || !permitted || !value.project) return false;
        request = value; pending = true; return true;
    }
    void cancel() { pending = false; }
    bool take(const ArrangerAddContext& current, bool permitted, ArrangerAddContext& out) {
        if (!pending) return false;
        pending = false;
        if (!permitted || request.project != current.project || request.generation != current.generation ||
            request.track != current.track || request.cursor != current.cursor ||
            request.pattern != current.pattern || request.selection != current.selection) return false;
        out = request; return true;
    }
};
}}
