// loop_bars in the runtime: granted by its claim, one change per callback,
// validated against the playing patterns, and ended without a claim by its holder;
// key labels from the claim's holder only, validated, passed on when they change,
// stock's own while Shift is held;
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
    extensions::BarLoop loop;
    for (u32 t = 0; t < extensions::sequencer_tracks; ++t) loop.bar[t] = extensions::no_bar;
    return loop;
}
u32 plays, played;
void looper(const extensions::Context& context, const extensions::Control& control, const extensions::Actions& actions) {
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
const char* label;
bool label_enabled = true;
u32 asked, unclaimed, labelled;
bool saw_shift;
// M3 gives `label`, M1 "first", M2 and M4 nothing (empty slots).
extensions::KeyLabel looper_label(const extensions::Context& c, u32 key) {
    ++asked; saw_shift = c.shift;
    if (key == 8) return {label, label_enabled};
    return {key == 6 ? "first" : nullptr, true};
}
extensions::KeyLabel observer_label(const extensions::Context&, u32) { ++unclaimed; return {"taken", true}; }
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
    extensions::Hooks().with_queued_control(looper).with_key_label(looper_label).with_sequencer_change(mover);
const extensions::Hooks second = extensions::Hooks()
    .with_queued_control(observer).with_key_label(observer_label).with_sequencer_change(observer_change);
// Claims: Record (50), the bar menu's M1-M4 (6-9) and Play (51); edit_notes
// needs the first, the others any claim.
const ClaimKey claim_keys[] = {{0, 50}, {1, 6}, {1, 7}, {1, 8}, {1, 9}, {2, 51}};
u32 action_claims[] = {1u, 0, 0, 0};
const ExtensionRegistry tables = {claim_keys, 6, 3, action_claims, 4};
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
bool shift_down;
bool extension_shift() { return shift_down; }
void extension_sequencer(extensions::Sequencer& s) { s = sequencer; }
bool extension_loop(const extensions::BarLoop& loop) { ++loops; taken = loop; return true; }
bool extension_labels(u32 key) { return key >= 6 && key <= 9; }
u32 starts, started_at;
bool extension_play_from(u32 tick) { ++starts; started_at = tick; return true; }
// What each slot (M1-M4) was last given: text, greyed out, or stock's own.
struct Shown { char text[16]; bool enabled, stock; u32 calls; };
Shown slots[4];
char* shown = slots[2].text;
void extension_key_label(u32 key, const char* text, bool enabled) {
    if (key < 6 || key > 9) return;
    Shown& s = slots[key - 6];
    ++s.calls;
    if (key == 8) ++labelled;
    s.stock = text == nullptr;
    s.enabled = enabled;
    u32 i = 0;
    for (; text && i < 15 && text[i]; ++i) s.text[i] = text[i];
    s.text[i] = 0;
}
}}
void control(u32 key, bool accepted) {
    const extensions::Control event = {key, true, accepted, false, 100, extensions::keys::bar, false, extensions::screens::bar_menu};
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
    // Labels: asked of the bar menu claim's holder only, for each of its keys,
    // after the context; passed on when they change. No text, or text too
    // long or unprintable, leaves the slot empty.
    CHECK(same_text(slots[0].text, "first") && slots[0].enabled && !slots[0].stock);
    CHECK(slots[1].calls == 1 && slots[1].text[0] == 0 && !slots[1].stock && slots[3].calls == 1);
    const u32 before_labels = labelled;
    label = nullptr; extensions_sync(); CHECK(asked && !unclaimed && labelled == before_labels);
    label = "loop off"; extensions_sync(); CHECK(labelled == before_labels + 1 && same_text(shown, "loop off"));
    extensions_sync(); CHECK(labelled == before_labels + 1);
    label = "loop on"; extensions_sync(); CHECK(labelled == before_labels + 2 && same_text(shown, "loop on"));
    label = "sixteen chars!!!"; extensions_sync(); CHECK(labelled == before_labels + 3 && shown[0] == 0);
    label = "fifteen chars!!"; extensions_sync(); CHECK(same_text(shown, "fifteen chars!!"));
    label = "tab\there"; extensions_sync(); CHECK(labelled == before_labels + 5 && shown[0] == 0);
    label = ""; extensions_sync(); CHECK(labelled == before_labels + 5);
    // A label may be greyed out: that is a change too.
    label = "loop on"; label_enabled = false; extensions_sync();
    CHECK(labelled == before_labels + 6 && same_text(shown, "loop on") && !slots[2].enabled);
    // While Shift is held every slot shows stock's own, without asking; back
    // to the holder's when it is let go.
    const u32 asked_before = asked;
    shift_down = true; extensions_sync();
    CHECK(asked == asked_before && slots[0].stock && slots[1].stock && slots[2].stock && slots[3].stock);
    CHECK(labelled == before_labels + 7);
    extensions_sync(); CHECK(labelled == before_labels + 7 && asked == asked_before);
    shift_down = false; extensions_sync();
    CHECK(!saw_shift && !slots[2].stock && same_text(shown, "loop on") && same_text(slots[0].text, "first"));
    CHECK(!unclaimed);
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
    return 0;
}
