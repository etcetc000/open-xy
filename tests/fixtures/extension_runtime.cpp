#include <extension_runtime.cpp>
#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
RecordingContext stock_context = {1, 2, 0, 7680, 3, 255, true, true};
bool playing = true;
u32 writes, notices, selections, observations, order[16], order_count, result, mode, written_track;
extensions::Actions saved;
extensions::Context saved_context;
const RecordedNote good = {-12, 60, 60, 100};
const char* const choice_labels[] = {"one", "two"};
void chosen(u32) {}
const extensions::Choice choice = {"choice", choice_labels, 2, 0, chosen};
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
}
void edit_control(const extensions::Context& context, const extensions::Control&, const extensions::Actions& actions) {
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
    if (mode == 11) result = actions.select_choice(choice) ? 1 : 0;
    else result = mode == 6 ? actions.replace_notes(context, track, &note, 1) : actions.insert_notes(context, track, &note, 1);
    actions.notify("done");
    actions.notify("duplicate");  // one notification per callback
    if (mode == 5) result += actions.insert_notes(context, track, &good, 1);
    if (mode == 7) result += actions.replace_notes(context, track, &good, 1);  // one write per callback
}
const extensions::Hooks first = extensions::Hooks().with_recordable_note_on(first_note)
                                                   .with_queued_control(observe_control);
const extensions::Hooks second = extensions::Hooks().with_recordable_note_on(second_note)
                                                    .with_queued_control(edit_control);
// Two claims on different keys. edit_notes needs the first (or any claim once
// set to 0); notify needs any claim its holder owns.
u32 claim_keys[] = {50, 51}, action_claims[] = {1u, 0, 0};
const ExtensionRegistry tables = {claim_keys, 2, action_claims, 3};
ExtensionEntry collection[] = {{"observer", 0, 0, &first},
                               {"writer", 1u, action_edit_notes | action_notify | action_select_choice, &second}};
}
namespace opxy { namespace platform {
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
bool extension_select_choice(const extensions::Choice&) { ++selections; return true; }
void extension_palette_sync(const ExtensionEntry*, u32) {}
extensions::Time extension_monotonic_time() { return 123456789u; }
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
    mode = 11;
    control(50, true);
    CHECK(result == 1 && selections == 1);
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
    // Guard the generated registry at runtime as well as build time.
    initialized = false; entries = nullptr; collection[0].claims = 1u;
    extensions_recordable_note_on(0, 60, 100); CHECK(order_count == 2);
    initialized = false; collection[0].claims = 0; collection[0].actions = action_edit_notes;
    extensions_recordable_note_on(0, 60, 100); CHECK(order_count == 2);
    initialized = false; collection[0].actions = 0; collection[0].id = "writer";
    extensions_recordable_note_on(0, 60, 100); CHECK(order_count == 2);
    // A second claim, on key 51, grants the same actions with no runtime change.
    initialized = false; entries = nullptr; collection[0].id = "observer";
    collection[1].claims = 2u; action_claims[0] = 0;  // edit_notes: any claim its holder owns
    const u32 before = writes;
    control(50, true); CHECK(writes == before);
    control(51, true); CHECK(writes == before + 1);
    control(51, false); CHECK(writes == before + 1);
    // An action whose claim its holder lacks disables the collection.
    initialized = false; entries = nullptr; action_claims[0] = 1u;
    control(51, true); CHECK(writes == before + 1);
    return 0;
}
