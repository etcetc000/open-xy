#include <stdio.h>
#include "arrangement_input.hpp"
#include "arrangement.hpp"
#include "../fixtures/arrangement_probe/extension.cpp"
using namespace opxy;
static platform::ArrangementInput input;
static u32 presses, releases;
static bool shown, take = true;
static bool submit(u32, const u32*, u32) { return true; }
static const extensions::ArrangementActions actions = {submit};
static bool deliver(bool down, bool shift) {
    if (down) ++presses; else ++releases;
    if (shift && !input.shifted()) return false;
    return take;
}
static void require(bool good) { static int n; ++n; if (!good) { fprintf(stderr, "failed %d\n", n); __builtin_trap(); } }
int main() {
    require(!input.key(26, true, false, deliver));
    shown = true;
    require(!input.key(26, false, shown, deliver)); // stock note-off survives entry
    require(presses == 0 && releases == 0);
    require(input.key(54, true, shown, deliver));
    require(input.key(54, true, shown, deliver)); // autorepeat has one owner
    require(presses == 1);
    require(input.key(54, false, false, deliver)); // release survives leaving
    require(releases == 1);
    require(!input.key(54, false, true, deliver)); // duplicate release stays stock
    take = false;
    require(!input.key(51, true, true, deliver));
    take = true;
    require(!input.key(51, false, true, deliver)); // cannot steal forwarded pair
    require(!input.key(55, true, false, deliver));
    require(input.shifted());
    require(input.key(10, true, true, deliver));
    require(input.key(10, false, true, deliver));
    require(!input.key(55, false, true, deliver));
    require(!input.shifted());
    require(!input.key(72, true, true, deliver));
    const extensions::ArrangementContext context = {1, 123, 2, 0, true};
    using namespace extensions::arrangement_probe;
    require(hooks.arrangement == &arrangement);
    require(platform::arrangement_mask_valid(arrangement));
    auto bad = arrangement;
    bad.keys[0] |= 1u << 26; require(!platform::arrangement_mask_valid(bad));
    bad = arrangement; bad.keys[1] |= 1u << 17; require(!platform::arrangement_mask_valid(bad));
    bad = arrangement; bad.keys[2] |= 1u << 8; require(!platform::arrangement_mask_valid(bad));
    arrangement.key(context, {extensions::ArrangementKey::plus, true, false}, actions);
    require(cursor == 1);
    static extensions::ArrangementFrame frame;
    arrangement.render(context, 16, frame);
    require(draw::valid(frame));
    require(frame.nodes[0].x == draw::unit);
    arrangement.tick(context, actions);
    require(ticks == 1);
}
