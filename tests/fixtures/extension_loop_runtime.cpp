// loop_bars in the runtime: granted by its claim, one change per callback,
// validated against the playing patterns, and ended without a claim by its holder;
// a claim's keys taken on its screen only;
// sequencer_change may move loops that hold, never start one.
#include <extension_runtime.cpp>
#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
RecordingContext stock_context = {1, 2, 0, 7680, 3, 255, true, true};
bool playing = true;
u32 loops, results, refusals, observed, controls, mode;
extensions::BarLoop taken;
extensions::Sequencer sequencer;

extensions::BarLoop none() {
    extensions::BarLoop loop = {};
    for (u32 t = 0; t < extensions::sequencer_tracks; ++t) loop.bar[t] = extensions::no_bar;
    return loop;
}
u32 plays, played;
u32 waits;
void looper(const extensions::Context& context, const extensions::Control& control, const extensions::Actions& actions) {
    if (mode == 7) {  // loop_at_bar_end: needs both grants, playing; one change and start per callback
        extensions::BarLoop loop = none(); loop.bar[2] = 1;
        waits += actions.loop_at_bar_end(context, loop, 7680);
        waits += 10 * actions.loop_at_bar_end(context, loop, 7680);
        waits += 100 * actions.loop_bars(context, loop);
        waits += 100 * actions.play_from(context, 0);
        loop.bar[2] = 4;  // track 2 has 4 bars
        if (control.key == 8) waits += 1000 * actions.loop_at_bar_end(context, loop, 7680);
        return;
    }
    if (mode == 8) {  // notify_for: notify's grant, one popup per callback, 100-10000 ms
        actions.notify_for("short", 99);
        actions.notify_for("long", 10001);
        actions.notify_for("hint", 2000);
        actions.notify_for("again", 2000);
        actions.notify("again");
        return;
    }
    if (mode == 6) {  // Shift+Play: one start per callback
        played += actions.play_from(context, 2 * 7680);
        plays += !actions.play_from(context, 7680);
        (void)control;
        return;
    }
    extensions::BarLoop loop = none();
    if (mode == 0 || mode == 4 || mode == 5) loop.bar[2] = context.sequencer.track[2].bar;
    if (mode == 2) loop.bar[2] = 4;                     // track 2 has 4 bars: 0-3
    if (mode == 3) loop.bar[5] = 0;                     // track 5 has no pattern
    if (mode == 4) ++stock_context.pattern;             // stale context
    if (mode == 5) sequencer.track[2].bars = 3;         // the pattern changed length since the event
    results += actions.loop_bars(context, loop);
    if (mode <= 1) refusals += !actions.loop_bars(context, none());  // one change per callback
}
void observer(const extensions::Context& context, const extensions::Control&, const extensions::Actions& actions) {
    observed += !actions.loop_bars(context, none());
}
u32 changes, moved, moves_refused, observer_changes;
void mover(const extensions::Context& context, const extensions::Actions& actions) {
    ++changes;
    extensions::BarLoop loop = none(); loop.bar[2] = 1;
    moved += actions.loop_bars(context, loop);
    moves_refused += !actions.loop_bars(context, loop);   // one change per callback
    actions.notify("nope");                                // no popup without a key
}
void observer_change(const extensions::Context& context, const extensions::Actions& actions) {
    ++observer_changes;
    extensions::BarLoop loop = none(); loop.bar[2] = 0;
    moves_refused += !actions.loop_bars(context, loop);   // it doesn't declare loop_bars
}
const extensions::Hooks first =
    extensions::Hooks().with_queued_control(looper).with_sequencer_change(mover);
const extensions::Hooks second = extensions::Hooks()
    .with_queued_control(observer).with_sequencer_change(observer_change);
// Claims: Record (50), keys 6-9 in the bar menu and Play (51) there with BAR
// (25) held, and Play with no screen; edit_notes needs the first, the others
// any claim.
const u32 none_held = extensions::keys::none;
const ClaimKey claim_keys[] = {{0, 50, 0, none_held}, {1, 6, 0x23, none_held}, {1, 7, 0x23, none_held},
                               {1, 8, 0x23, none_held}, {1, 9, 0x23, none_held}, {1, 51, 0x23, 25},
                               {2, 51, 0, none_held}};
u32 action_claims[] = {1u, 0, 0, 0};
const ExtensionRegistry tables = {claim_keys, 7, 3, action_claims, 4};
u32 key_down = extensions::keys::none;
ExtensionEntry collection[] = {{"looper", 6u, action_loop_bars | action_notify | action_play_from, &first},
                               {"observer", 0, 0, &second}};
}
namespace opxy { namespace platform {
const ExtensionEntry* extension_collection(u32& count) { count = 2; return collection; }
const ExtensionRegistry& extension_registry() { return tables; }
TrackPattern extension_pattern(u32) { return {stock_context.pattern, stock_context.length, stock_context.scale, true}; }
RecordingContext extension_context(bool& p) { p = playing; return stock_context; }
i32 extension_position(u32) { return 0; }
u32 extension_scaled_delta(u32, u32 delta) { return delta; }
i32 extension_collision_radius(u32) { return 180; }
u32 extension_write(u32, const RecordedNote*, u32, bool) { return 0; }
u32 popups;
void extension_notify(const char*) { ++popups; }
u32 popup_ms;
void extension_notify_for(const char*, u32 ms) { ++popups; popup_ms = ms; }
u32 waiting, waited_tick, waited_track;
bool extension_loop_at_bar_end(const extensions::BarLoop& loop, u32 tick, u32 track) {
    ++waiting; waited_tick = tick; waited_track = track; taken = loop; return true;
}
void extension_sequencer(extensions::Sequencer& s) { s = sequencer; }
bool extension_loop(const extensions::BarLoop& loop) { ++loops; taken = loop; return true; }
u32 starts, started_at;
bool extension_play_from(u32 tick) { ++starts; started_at = tick; return true; }
bool extension_key_held(u32 key) { return key == key_down; }
}}
void control(u32 key, bool accepted) {
    const extensions::Control event = {key, true, accepted, false, 100, extensions::keys::bar, false, extensions::screens::bar_menu, true};
    extensions_queued_control(event);
    ++controls;
}
int main() {
    for (u32 t = 0; t < extensions::sequencer_tracks; ++t) sequencer.track[t].loop = extensions::no_bar;
    sequencer.track[2].bars = 4; sequencer.track[2].bar = 2; sequencer.track[2].bar_ticks = 7680;
    // The context carries the sequencer.
    extensions_sync(); CHECK(current.sequencer.track[2].bars == 4 && current.sequencer.track[2].bar == 2);
    // M3, claimed and accepted: the bar is taken, once per callback. The
    // observer holds no loop_bars, so it cannot end the loop either.
    control(8, true);
    CHECK(loops == 1 && results == 1 && taken.bar[2] == 2 && taken.bar[0] == extensions::no_bar);
    CHECK(refusals == 1);
    // Not accepted, or another key: no grant to hold bars, but the holder may end them.
    control(8, false); CHECK(loops == 2 && results == 1 && taken.bar[2] == extensions::no_bar && refusals == 1);
    control(25, true); CHECK(loops == 3 && results == 1);
    // Bars outside the playing pattern, a stale context or changed patterns are refused.
    for (mode = 2; mode <= 5; ++mode) {
        const u32 before = loops;
        control(8, true);
        CHECK(loops == before && results == 1);
        sequencer.track[2].bars = 4;
        extensions_sync();
    }
    // Ending is valid for the holder in any callback, once.
    mode = 1; const u32 before = loops; control(8, true);
    CHECK(loops == before + 1 && results == 2 && refusals == 2 && taken.bar[2] == extensions::no_bar);
    CHECK(observed == controls);
    // A claim's keys are its holder's, with or without Shift, on its screen
    // only; a claim without one (51) never takes its key.
    CHECK(extensions_takes(6, 0x23) && extensions_takes(9, 0x23) && !extensions_takes(25, 0x23));
    CHECK(!extensions_takes(6, 0) && !extensions_takes(6, 5) && !extensions_takes(51, 0));
    // A claim with a held key takes its key only while that key is held.
    CHECK(!extensions_takes(51, 0x23) && extensions_needs_held(25, 0x23));
    CHECK(!extensions_needs_held(25, 0) && !extensions_needs_held(24, 0x23) && !extensions_needs_held(none_held, 0x23));
    key_down = 25; CHECK(extensions_takes(51, 0x23) && !extensions_takes(51, 0)); key_down = none_held;
    // sequencer_change: only while bars loop, only when a bar count or scale
    // changed; the holder may move the loop there, once, without a popup.
    sequencer.track[2].bar_ticks = 3840; extensions_sync(); CHECK(changes == 0);   // nothing loops
    sequencer.track[2].loop = 2; extensions_sync(); CHECK(changes == 0);           // nothing changed
    const u32 loops_before = loops, popups_before = popups;
    sequencer.track[2].bar_ticks = 15360; extensions_sync();
    CHECK(changes == 1 && observer_changes == 1 && moved == 1 && loops == loops_before + 1 && taken.bar[2] == 1);
    CHECK(moves_refused == 2 && popups == popups_before);
    extensions_sync(); CHECK(changes == 1);
    sequencer.track[2].bars = 3; extensions_sync(); CHECK(changes == 2 && moved == 2);
    sequencer.track[2].loop = extensions::no_bar; sequencer.track[2].bars = 4;
    extensions_sync(); CHECK(changes == 2);                                         // loop gone: no event
    sequencer.track[2].bar_ticks = 7680; extensions_sync();
    // play_from: granted by any claim of its holder, once per callback.
    mode = 6; playing = false; extensions_sync();
    control(51, true); CHECK(starts == 1 && started_at == 2 * 7680 && played == 1 && plays == 1);
    control(8, true); CHECK(starts == 2 && played == 2);      // M3, its other claim, grants it too
    control(25, true); CHECK(starts == 2);                     // an unclaimed key doesn't
    control(51, false); CHECK(starts == 2);                    // nor a key stock didn't take
    playing = true; extensions_sync();
    control(51, true); CHECK(starts == 3 && played == 3);     // playing: a restart
    // loop_at_bar_end: playing, both grants, once, for the selected track.
    mode = 7;
    control(51, true);
    CHECK(waiting == 1 && waits == 1 && waited_tick == 7680 && waited_track == stock_context.track && taken.bar[2] == 1);
    playing = false; extensions_sync();
    control(51, true); CHECK(waiting == 1);                     // stopped: refused
    playing = true; extensions_sync();
    control(51, false); CHECK(waiting == 1);                    // not taken by stock: no grant
    // notify_for: notify's grant, within 100-10000 ms, one popup per callback.
    mode = 8;
    const u32 shown_before = popups;
    control(51, true); CHECK(popups == shown_before + 1 && popup_ms == 2000);
    return 0;
}
