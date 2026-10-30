#include "../../sdk/platform/opxy-1.2.1/arranger_pending_add.hpp"
#include <cstdlib>
void require(bool value) { if (!value) std::abort(); }
using opxy::platform::ArrangerAddContext;
using opxy::platform::ArrangerPendingAdd;
int main() {
    ArrangerPendingAdd queue{};
    const ArrangerAddContext initial{100, 7, 2, 7680, 1, -1};
    ArrangerAddContext out{}, changed = initial;
    require(!queue.enqueue(initial, false));
    changed.project = 0; require(!queue.enqueue(changed, true));
    require(queue.enqueue(initial, true));
    changed = initial; changed.cursor = 9000;
    require(!queue.enqueue(changed, true)); // Repeats neither duplicate nor replace.
    require(queue.take(initial, true, out));
    require(out.cursor == initial.cursor && out.project == initial.project);
    require(!queue.take(initial, true, out)); // Cleared before caller mutation.
    for (unsigned field = 0; field < 6; ++field) {
        changed = initial;
        if (field == 0) ++changed.project;
        if (field == 1) ++changed.generation;
        if (field == 2) ++changed.track;
        if (field == 3) ++changed.cursor;
        if (field == 4) ++changed.pattern;
        if (field == 5) ++changed.selection;
        require(queue.enqueue(initial, true));
        require(!queue.take(changed, true, out));
        require(!queue.pending);
    }
    require(queue.enqueue(initial, true));
    require(!queue.take(initial, false, out)); // View/transport/record/bounce/readiness changed.
    require(!queue.pending);
    require(queue.enqueue(initial, true)); queue.cancel();
    require(!queue.take(initial, true, out)); // Project restoration cancels even reused IDs.
    require(queue.enqueue(initial, true)); require(queue.take(initial, true, out));
}
