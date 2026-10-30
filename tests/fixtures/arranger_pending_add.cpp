#include "../../sdk/platform/opxy-1.2.1/arranger_pending_add.hpp"
#include <cstdlib>
void require(bool value) { if (!value) std::abort(); }
using opxy::platform::ArrangerAddContext;
using opxy::platform::ArrangerPendingAdd;
int main() {
    opxy::arranger::Model model{}, before{};
    opxy::arranger::Clip clip{0, 1, 0, 7680, 0, 7680};
    require(model.insert(clip) >= 0);
    opxy::u32 count = 2;
    opxy::platform::arranger_cancel_added_clip(model, before, &count, 1, false);
    require(model.clip(0) && count == 2); // Stale project does not touch either owner.
    opxy::platform::arranger_cancel_added_clip(model, before, &count, 1, true);
    require(!model.clip(0) && count == 1);
    require(model.insert(clip) >= 0); count = 3;
    opxy::platform::arranger_cancel_added_clip(model, before, &count, 1, true);
    require(!model.clip(0) && count == 3); // Never shrink someone else's newer append.
    ArrangerPendingAdd queue{};
    const ArrangerAddContext initial{100, 7, 2, 7680, 1, -1};
    ArrangerAddContext changed = initial;
    require(!queue.enqueue(initial, false));
    changed.project = 0; require(!queue.enqueue(changed, true));
    require(queue.enqueue(initial, true));
    changed = initial; changed.cursor = 9000;
    require(!queue.enqueue(changed, true));
    require(queue.context_matches(initial)); // Repeat cannot replace request.
    for (unsigned field = 0; field < 6; ++field) {
        changed = initial;
        if (field == 0) ++changed.project;
        if (field == 1) ++changed.generation;
        if (field == 2) ++changed.track;
        if (field == 3) ++changed.cursor;
        if (field == 4) ++changed.pattern;
        if (field == 5) ++changed.selection;
        require(!queue.context_matches(changed));
        require(queue.bound(changed) == (field >= 2));
    }
    for (unsigned stage = ArrangerPendingAdd::project_check; stage <= ArrangerPendingAdd::draw; ++stage) {
        require(queue.stage == stage && !queue.ready(10000));
        queue.acknowledge(1000); queue.acknowledge(1199); // Repaints do not restart delay.
        require(!queue.ready(1199) && queue.ready(1200));
        if (stage != ArrangerPendingAdd::draw) queue.next();
    }
    queue.draw_requested = true;
    require(queue.pending); // Completion waits until normal frame returns.
    queue.cancel();
    require(!queue.pending && !queue.painted && !queue.draw_requested && !queue.ready(10000));
    require(queue.enqueue(initial, true));
    queue.acknowledge(0xffffff80u);
    require(!queue.ready(0x47u) && queue.ready(0x48u)); // Clock rollover.
    queue.cancel(); // Project restoration cancels even reused identities.
    require(queue.enqueue(initial, true) && !queue.ready(10000));
    require(!queue.record_after && !queue.draw_complete);
    queue.cancel();
    require(queue.enqueue(initial, true, true) && queue.record_after);
    require(!queue.enqueue(initial, true, false) && queue.record_after);
    queue.draw_complete = true;
    queue.cancel(); // STOP/project restore/failure clears deferred record intent too.
    require(!queue.record_after && !queue.draw_complete && !queue.pending);
    require(!queue.enqueue(initial, false, true) && !queue.record_after);
}
