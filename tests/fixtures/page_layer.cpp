// Page layers and the lfo_copies action in the runtime: a page claim opens its
// owner's layer while its page shows, the layer takes keys and turns from
// stock, and only the layer's callbacks may show another of the four LFOs.
#include <extension_runtime.cpp>
#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
RecordingContext stock_context = {1, 2, 0, 7680, 3, 255, true, true};
u32 opens, closes, keys, releases, turns, notices, shows, last_track, last_lfo;
u32 last_key;
i32 last_delta;
bool last_shift, take = true, show_result;
u32 shown_result;
extensions::Actions saved;
u32 control_result;
u32 stock_shown[8];

bool layer(const extensions::Context&, const extensions::Layer& event, const extensions::Actions& actions) {
    saved = actions;
    last_track = event.track;
    switch (event.kind) {
    case extensions::Layer::open: ++opens; return false;
    case extensions::Layer::close: ++closes; return false;
    case extensions::Layer::keypress:
        last_key = event.key;
        last_shift = event.shift;
        if (!event.pressed) { ++releases; return false; }
        ++keys;
        return take;
    case extensions::Layer::turn: {
        ++turns;
        last_key = event.key;
        last_delta = event.delta;
        last_shift = event.shift;
        show_result = actions.show_lfo(event.track, 2);
        show_result = show_result && !actions.show_lfo(event.track, extensions::lfo_count);
        show_result = show_result && !actions.show_lfo(8, 0);
        shown_result = actions.shown_lfo(event.track);
        actions.notify("LFO 3");
        return take;
    }
    }
    return false;
}
// The other entry holds only a key claim: its queued control may not show LFOs.
void keyed(const extensions::Context&, const extensions::Control&, const extensions::Actions& actions) {
    control_result = actions.show_lfo(0, 1) ? 1 : 2;
}

const extensions::Hooks lfo_hooks = extensions::Hooks().with_layer(layer);
const extensions::Hooks keyed_hooks = extensions::Hooks().with_queued_control(keyed);
// Claim 0: Record (key 50). Claim 1: the LFO page. lfo_copies needs the page claim.
u32 claim_keys[] = {50, 0}, action_claims[] = {1u, 0, 2u}, claim_pages[] = {0, page_lfo};
const ExtensionRegistry tables = {claim_keys, 2, action_claims, 3, claim_pages};
ExtensionEntry collection[] = {
    {"keyed", 1u, action_edit_notes, &keyed_hooks},
    {"lfos", 2u, action_notify | action_lfo_copies, &lfo_hooks}};
}
namespace opxy { namespace platform {
const ExtensionEntry* extension_collection(u32& count) { count = 2; return collection; }
const ExtensionRegistry& extension_registry() { return tables; }
TrackPattern extension_pattern(u32) { return {stock_context.pattern, stock_context.length, stock_context.scale, true}; }
RecordingContext extension_context(bool& p) { p = true; return stock_context; }
i32 extension_position(u32) { return 0; }
u32 extension_scaled_delta(u32, u32 delta) { return delta; }
i32 extension_collision_radius(u32) { return 0; }
u32 extension_write(u32, const RecordedNote*, u32 count, bool) { return count; }
void extension_notify(const char*) { ++notices; }
bool extension_show_lfo(u32 track, u32 lfo) {
    ++shows; last_track = track; last_lfo = lfo; stock_shown[track] = lfo; return true;
}
u32 extension_shown_lfo(u32 track) { return stock_shown[track]; }
}}

int main() {
    extensions_sync();
    CHECK(extensions_hold(action_lfo_copies) && !extensions_hold(1u << 5));
    // No page, no layer: stock keeps every key and turn.
    extensions_page(0, 2);
    CHECK(opens == 0 && !extensions_page_key(extensions::keys::m4, true, false));
    CHECK(!extensions_page_turn(0, 1, true));
    // The LFO page on track 3 opens the owner's layer once.
    extensions_page(page_lfo, 2);
    extensions_page(page_lfo, 2);
    CHECK(opens == 1 && closes == 0 && last_track == 2);
    // A turn it takes shows one LFO, refusing bad LFOs and tracks.
    CHECK(extensions_page_turn(0, -2, true));
    CHECK(turns == 1 && last_key == 0 && last_delta == -2 && last_shift && show_result);
    CHECK(shows == 1 && last_track == 2 && last_lfo == 2 && shown_result == 2 && notices == 1);
    // Actions only last for their callback.
    CHECK(!saved.show_lfo(2, 0) && saved.shown_lfo(2) == 0 && shows == 1);
    // A key it takes: its release goes to the layer too. One it leaves: stock's.
    CHECK(extensions_page_key(extensions::keys::m4, true, true) && last_shift);
    CHECK(extensions_page_key(extensions::keys::m4, false, false) && releases == 1);
    take = false;
    CHECK(!extensions_page_key(extensions::keys::m1, true, false) && keys == 2);
    CHECK(!extensions_page_key(extensions::keys::m1, false, false) && releases == 1);
    CHECK(!extensions_page_turn(1, 1, false) && turns == 2);
    CHECK(!extensions_page_turn(4, 1, false) && turns == 2);
    take = true;
    // Another track reopens it; leaving the page closes it.
    extensions_page(page_lfo, 5);
    CHECK(closes == 1 && opens == 2 && last_track == 5);
    extensions_page(0, 5);
    CHECK(closes == 2 && !extensions_page_turn(0, 1, false));
    // Without the page claim, show_lfo is refused.
    const extensions::Control record = {50, false, true, true, 100};
    extensions_queued_control(record);
    CHECK(control_result == 2);
    CHECK(shows == 2);  // the one from the turn stock kept (encoder 2)
    return 0;
}
