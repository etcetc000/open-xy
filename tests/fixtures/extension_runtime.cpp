#include <extension_runtime.cpp>
#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
RecordingContext stock_context = {1, 2, 0, 7680, 3, 255, true, true};
bool playing = true;
u32 writes, notices, observations, order[16], order_count, result, mode, written_track, snaps, snapped_slot, taken;
u32 held_key, chord_presses, chord_releases, chord_key;
extensions::Actions saved;
extensions::Context saved_context;
const RecordedNote good = {-12, 60, 60, 100};
void first_note(const extensions::Context&, const extensions::LiveNote& note) {
    order[order_count++] = 1;
    observations += note.position == -12 && note.collision_radius == 180;
    // Reentrant callbacks are ignored, not delivered out of order.
    extensions_recordable_note_on(0, 60, 100);
}
void second_note(const extensions::Context&, const extensions::LiveNote&) { order[order_count++] = 2; }
void observe_control(const extensions::Context& context, const extensions::Control&, const extensions::Actions& actions) {
    result = actions.insert_notes(context, 0, &good, 1);
    actions.notify("unauthorized");
    taken += actions.take_snapshot(0);  // never granted: no claim
}
void edit_control(const extensions::Context& context, const extensions::Control& control, const extensions::Actions& actions) {
    saved = actions; saved_context = context;
    RecordedNote note = good;
    u32 track = context.recording.track;
    if (mode == 1) ++stock_context.pattern;
    if (mode == 2) note.pitch = 128;
    if (mode == 3) note.position = -241;
    if (mode == 4) note.length = stock_context.length + 1;
    if (mode == 8) track = 3;  // another instrument track
    if (mode == 9) track = 7;  // a track whose pattern cannot be edited
    if (mode == 10) track = 8;
    result = mode == 6 ? actions.replace_notes(context, track, &note, 1) : actions.insert_notes(context, track, &note, 1);
    actions.notify("done");
    actions.notify("duplicate");  // one notification per callback
    if (mode == 5) result += actions.insert_notes(context, track, &good, 1);
    if (mode == 7) result += actions.replace_notes(context, track, &good, 1);  // one write per callback
    // One snapshot per callback, of slots 0-7 only.
    if (mode == 11) taken += actions.take_snapshot(10) + actions.take_snapshot(5) + actions.take_snapshot(6);
    // A chord's black key: one snapshot, in the slot stock would number it.
    if (mode == 12) {
        chord_key = control.key;
        if (control.pressed) { ++chord_presses; taken += actions.take_snapshot(extensions::keys::black_index(control.key)); }
        else { ++chord_releases; taken += actions.take_snapshot(0); }
    }
}
const extensions::Hooks first = extensions::Hooks().with_recordable_note_on(first_note)
                                                   .with_queued_control(observe_control);
const extensions::Hooks second = extensions::Hooks().with_recordable_note_on(second_note)
                                                    .with_queued_control(edit_control);
// Two claims on different keys, the second a chord: black keys while key 1 is
// held. edit_notes needs the first (or any claim once set to 0); notify and
// snapshot need any claim their holder owns.
u32 claim_keys[] = {50, 1}, action_claims[] = {1u, 0, 0}, claim_chords[] = {0, chord_black_keys};
ExtensionRegistry tables = {claim_keys, 2, action_claims, 3, claim_chords, nullptr};
u32 claim_partners[] = {0, 54};
ExtensionEntry collection[] = {{"observer", 0, 0, &first},
                               {"writer", 1u, action_edit_notes | action_notify | action_snapshot, &second}};
}
// The control service's snapshot store.
bool sdk_snapshot_take(u32 slot) { ++snaps; snapped_slot = slot; return true; }
// Its menu: each operation and argument, and whether a close reports the hold used.
u32 menu_ops[16], menu_count; i32 menu_args[16]; bool menu_used;
u32 sdk_snapshot_menu(u32 op, i32 arg) {
    if (menu_count < 16) { menu_ops[menu_count] = op; menu_args[menu_count++] = arg; }
    return op == 1 || op == 5 ? menu_used : 1;
}
namespace opxy { namespace platform {
u32 held_key2;
bool extension_key_held(u32 key) { return key && (key == held_key || key == held_key2); }
const ExtensionEntry* extension_collection(u32& count) { count = 2; return collection; }
const ExtensionRegistry& extension_registry() { return tables; }
TrackPattern extension_pattern(u32 track) {
    return {stock_context.pattern, stock_context.length, stock_context.scale, track < 7};
}
RecordingContext extension_context(bool& p) { p = playing; return stock_context; }
i32 extension_position(u32) { return -12; }
u32 extension_scaled_delta(u32, u32 delta) { return delta << 8; }
i32 extension_collision_radius(u32) { return 180; }
bool replaced;
u32 extension_write(u32 track, const RecordedNote*, u32 count, bool replace) {
    ++writes; replaced = replace; written_track = track; return count;
}
void extension_notify(const char*) { ++notices; }
}}
void control(u32 key, bool accepted) {
    const extensions::Control event = {key, false, accepted, true, 100};
    extensions_queued_control(event);
}
int main() {
    extensions_recordable_note_on(0, 60, 100);
    CHECK(order_count == 2 && order[0] == 1 && order[1] == 2 && observations == 1);
    extensions_recordable_note_on(7, 60, 100); CHECK(order_count == 2);
    // Only the claim owner acts: one write and one notification per callback.
    control(50, true);
    CHECK(writes == 1 && notices == 1 && result == 1 && written_track == 0);
    CHECK(saved.insert_notes(saved_context, 0, &good, 1) == 0);
    saved.notify("late"); CHECK(notices == 1);
    // Refused writes; notify is its own action, so each callback may still post.
    for (mode = 1; mode <= 4; ++mode) {
        control(50, true);
        CHECK(result == 0 && writes == 1 && notices == 1 + mode);
    }
    mode = 5;
    control(50, true);
    CHECK(result == 1 && writes == 2 && notices == 6 && !replaced);
    mode = 6;
    control(50, true);
    CHECK(result == 1 && writes == 3 && replaced);
    mode = 7;
    control(50, true);
    CHECK(result == 1 && writes == 4 && !replaced);
    // Writes reach any editable instrument track, not only the selected one.
    mode = 8;
    control(50, true);
    CHECK(result == 1 && writes == 5 && written_track == 3);
    for (mode = 9; mode <= 10; ++mode) {
        control(50, true);
        CHECK(result == 0 && writes == 5);
    }
    // Nothing is granted for a key stock did not accept or the extension does not claim.
    mode = 0; const u32 posted = notices;
    control(50, false);
    control(12, true);
    CHECK(result == 0 && writes == 5 && notices == posted);
    // Snapshots: granted like any action, once per callback, slots 0-9, and only
    // the claim owner; a late call from a saved table is refused.
    CHECK(snaps == 0 && taken == 0);
    mode = 11;
    control(50, true);
    CHECK(snaps == 1 && snapped_slot == 5 && taken == 1);
    CHECK(!saved.take_snapshot(1) && snaps == 1);
    control(50, false);
    CHECK(snaps == 1);
    // Chords: a black key while the claimed key is held goes only to the claim's
    // owner, never to stock; its release follows it even once the key is let go.
    // (An owner without the snapshot action: one with it gets the SDK's menu.)
    initialized = false; entries = nullptr; collection[1].claims = 3u;
    collection[1].actions = action_edit_notes | action_notify;
    mode = 12; taken = 0;
    CHECK(extensions_key_input(27, true, 0) == key_stock);  // key 1 not held: stock's
    CHECK(extensions_key_input(1, true, 0) == key_stock);   // the claimed key itself
    held_key = 1;
    CHECK(!extensions_key_input(26, true, 0) && !extensions_key_input(26, false, 0));  // white: stock's
    CHECK(!extensions_key_input(50, true, 0));  // not the keyboard
    CHECK(extensions_key_input(36, true, 0) == key_taken);  // the fifth black key
    CHECK(chord_presses == 1 && chord_key == 36 && taken == 0 && menu_count == 0);
    CHECK(extensions_key_input(36, true, 0) && chord_presses == 1);  // a repeat is still withheld
    held_key = 0;
    CHECK(extensions_key_input(36, false, 0) && chord_releases == 1);
    CHECK(!extensions_key_input(36, false, 0) && chord_releases == 1);
    // A pair claim's chord needs both of its keys held.
    tables.claim_partners = claim_partners;
    initialized = false; entries = nullptr;
    held_key = 1;
    CHECK(extensions_key_input(36, true, 0) == key_stock && chord_presses == 1);
    CHECK(extensions_key_input(36, false, 0) == key_stock);
    held_key2 = 54;
    CHECK(extensions_key_input(36, true, 0) == key_taken && chord_presses == 2);
    held_key = held_key2 = 0;
    CHECK(extensions_key_input(36, false, 0) == key_taken && chord_releases == 2);
    tables.claim_partners = nullptr;
    chord_presses = chord_releases = 1;
    // The snapshot menu: the claim key opens it and is taken; while it is held the
    // black keys select, M1-M4 act and encoder 1 steps, all withheld from stock
    // with their releases; other keys stay stock's. Released, it closes.
    initialized = false; entries = nullptr;
    collection[1].actions = action_edit_notes | action_notify | action_snapshot;
    CHECK(!extensions_knob_input(0, 1));        // closed: stock's
    CHECK(extensions_key_input(1, true, 0) == key_taken && menu_count == 1 && menu_ops[0] == 0);
    CHECK(extensions_key_input(36, true, 0) == key_taken && menu_ops[1] == 2 && menu_args[1] == 4);
    CHECK(extensions_key_input(7, true, 0) == key_taken && menu_ops[2] == 4 && menu_args[2] == 1);  // M2
    CHECK(extensions_knob_input(0, -1) && menu_ops[3] == 3 && menu_args[3] == -1);
    CHECK(!extensions_knob_input(1, 1) && menu_count == 4);  // another encoder: stock's
    CHECK(extensions_key_input(14, true, 0) == key_stock && extensions_key_input(26, true, 0) == key_stock);
    CHECK(extensions_key_input(36, false, 0) == key_taken && extensions_key_input(7, false, 0) == key_taken);
    menu_used = true;
    CHECK(extensions_key_input(1, false, 0) == key_taken && menu_ops[4] == 1);
    CHECK(chord_presses == 1 && snaps == 1);    // nothing reached the extension or its action
    // A bare tap goes back to stock as the key's own press and release.
    menu_used = false;
    CHECK(extensions_key_input(1, true, 0) == key_taken);
    CHECK(extensions_key_input(1, false, 0) == key_tap);
    CHECK(extensions_key_input(1, false, 0) == key_stock && !extensions_knob_input(0, 1));
    // A claim with a partner: the pair pressed together opens the menu, either
    // key first; one pressed alone reaches stock as a tap when it is released.
    initialized = false; entries = nullptr; tables.claim_partners = claim_partners;
    menu_count = 0; menu_used = false;
    CHECK(extensions_key_input(1, true, 0) == key_taken && menu_count == 0);   // waits for 54
    CHECK(extensions_key_input(1, false, 0) == key_tap && menu_count == 0);    // alone: stock's
    CHECK(extensions_key_input(54, true, 0) == key_taken);
    CHECK(extensions_key_input(1, true, 0) == key_taken && menu_count == 1 && menu_ops[0] == 0 && menu_args[0] == 1);
    // Tapped (released at once, nothing done): it stays open.
    CHECK(extensions_key_input(54, false, 0) == key_taken && menu_count == 1);
    CHECK(extensions_key_input(1, false, 0) == key_taken && menu_ops[1] == 5 && menu_count == 2);
    // Open with nothing held: black keys, M1-M4 and encoder 1 are the menu's.
    CHECK(extensions_key_input(48, true, 0) == key_taken && menu_ops[2] == 2 && menu_args[2] == 9);
    CHECK(extensions_key_input(48, false, 0) == key_taken);
    CHECK(extensions_key_input(9, true, 0) == key_taken && menu_ops[3] == 4 && menu_args[3] == 3);  // M4
    CHECK(extensions_key_input(9, false, 0) == key_taken && extensions_knob_input(0, 2) && menu_ops[4] == 3);
    // The pair again closes it.
    CHECK(extensions_key_input(1, true, 0) == key_taken && extensions_key_input(54, true, 0) == key_taken);
    CHECK(menu_ops[5] == 1 && !extensions_knob_input(0, 1));
    CHECK(extensions_key_input(1, false, 0) == key_taken && extensions_key_input(54, false, 0) == key_taken);
    CHECK(extensions_key_input(48, true, 0) == key_stock && extensions_key_input(48, false, 0) == key_stock);
    // Held (the menu reports a hold): it closes when the pair is let go.
    menu_count = 0; menu_used = true;
    CHECK(extensions_key_input(54, true, 0) == key_taken && extensions_key_input(1, true, 0) == key_taken);
    CHECK(extensions_key_input(36, true, 0) == key_taken && extensions_key_input(36, false, 0) == key_taken);
    CHECK(extensions_key_input(1, false, 0) == key_taken && menu_count == 2);  // one still down
    CHECK(extensions_key_input(54, false, 0) == key_taken && menu_ops[2] == 5 && menu_ops[3] == 1);
    CHECK(!extensions_knob_input(0, 1));
    // Held, one pair key let go and pressed again is still the hold; stock hears nothing.
    menu_count = 0;
    CHECK(extensions_key_input(54, true, 0) == key_taken && extensions_key_input(1, true, 0) == key_taken);
    CHECK(extensions_key_input(1, false, 0) == key_taken && extensions_key_input(1, true, 0) == key_taken);
    CHECK(extensions_key_input(54, false, 0) == key_taken && menu_count == 1);
    CHECK(extensions_key_input(1, false, 0) == key_taken && menu_ops[1] == 5 && menu_ops[2] == 1);
    // Toggled open, any other key closes it and is stock's.
    menu_count = 0; menu_used = false;
    CHECK(extensions_key_input(1, true, 0) == key_taken && extensions_key_input(54, true, 0) == key_taken);
    CHECK(extensions_key_input(1, false, 0) == key_taken && extensions_key_input(54, false, 0) == key_taken);
    CHECK(extensions_knob_input(0, 1));
    CHECK(extensions_key_input(16, true, 0) == key_stock && menu_ops[menu_count - 1] == 1);
    CHECK(!extensions_knob_input(0, 1) && extensions_key_input(16, false, 0) == key_stock);
    CHECK(extensions_key_input(36, true, 0) == key_stock && extensions_key_input(36, false, 0) == key_stock);
    // Toggled open, - or + alone closes it too, and reaches stock as a tap.
    menu_count = 0;
    CHECK(extensions_key_input(1, true, 0) == key_taken && extensions_key_input(54, true, 0) == key_taken);
    CHECK(extensions_key_input(1, false, 0) == key_taken && extensions_key_input(54, false, 0) == key_taken);
    CHECK(extensions_key_input(54, true, 0) == key_taken && menu_ops[menu_count - 1] == 5);
    CHECK(extensions_key_input(54, false, 0) == key_tap && menu_ops[menu_count - 1] == 1);
    CHECK(!extensions_knob_input(0, 1));
    tables.claim_partners = nullptr;
    held_key = 0; mode = 0; collection[1].claims = 1u;
    // Guard the generated registry at runtime as well as build time.
    initialized = false; entries = nullptr; collection[0].claims = 1u;
    extensions_recordable_note_on(0, 60, 100); CHECK(order_count == 2);
    initialized = false; collection[0].claims = 0; collection[0].actions = action_edit_notes;
    extensions_recordable_note_on(0, 60, 100); CHECK(order_count == 2);
    initialized = false; collection[0].actions = 0; collection[0].id = "writer";
    extensions_recordable_note_on(0, 60, 100); CHECK(order_count == 2);
    // A second claim, on key 1, grants the same actions with no runtime change.
    initialized = false; entries = nullptr; collection[0].id = "observer";
    collection[1].claims = 2u; action_claims[0] = 0;  // edit_notes: any claim its holder owns
    const u32 before = writes;
    control(50, true); CHECK(writes == before);
    control(1, true); CHECK(writes == before + 1);
    control(1, false); CHECK(writes == before + 1);
    // An action whose claim its holder lacks disables the collection, chords too.
    initialized = false; entries = nullptr; action_claims[0] = 1u;
    control(1, true); CHECK(writes == before + 1);
    held_key = 1; CHECK(!extensions_key_input(27, true, 0));
    return 0;
}
