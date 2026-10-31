#include <extension_runtime.cpp>
#include <extension_gui.cpp>
#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
RecordingContext stock_context = {1, 2, 0, 7680, 3, 255, true, true};
bool playing = true;
u32 reads, discoveries, gui_calls, completions, foreign_completions, snapshot_generation=2;
bool in_gui, result_success;
u32 completed_ticket, completed_count;
u32 state_writes;
const ProjectStateSpec state_spec = {"test.writer", 1, 16};
u32 writes, notices, observations, order[16], order_count, result, mode, written_track;
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
    extensions::PatternInfo info{};
    actions.discover_pattern(0,0,info);
}
void edit_control(const extensions::Context& context, const extensions::Control&, const extensions::Actions& actions) {
    saved = actions; saved_context = context;
    if(mode==12) {
        const extensions::PatternRef ref={1,2,3,1};
        result=actions.request_pattern_read(ref); return;
    }
    if (mode == 11) {
        extensions::PatternInfo info{}; RecordedNote out{}; u32 count=99;
        result = actions.discover_pattern(3,1,info) && actions.read_pattern(info.ref,&out,1,count) && count==1;
        return;
    }
    if (mode == 13) { result = actions.publish_project_state({1, 2}, nullptr, 0); return; }
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
}
void first_result(const extensions::PatternReadResult&) { ++foreign_completions; }
void second_result(const extensions::PatternReadResult& result) {
    ++completions;result_success=result.success;completed_ticket=result.request;completed_count=result.count;
    if(result.success && (!result.notes || result.notes[0].pitch!=good.pitch)) __builtin_trap();
    if(!result.success && (result.notes || result.count)) __builtin_trap();
}
const extensions::Hooks first = extensions::Hooks().with_recordable_note_on(first_note)
                                                   .with_queued_control(observe_control).with_pattern_read_result(first_result);
const extensions::Hooks second = extensions::Hooks().with_recordable_note_on(second_note)
                                                    .with_queued_control(edit_control).with_pattern_read_result(second_result);
// Two claims on different keys. edit_notes needs the first (or any claim once
// set to 0); notify needs any claim its holder owns.
u32 claim_keys[] = {50, 51}, action_claims[] = {1u, 0, 0, 0, 0};
const ExtensionRegistry tables = {claim_keys, 2, action_claims, 5};
ExtensionEntry collection[] = {{"observer", 0, 0, &first},
                               {"writer", 1u, action_edit_notes | action_notify, &second}};
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
void extension_barrier() {}
void extension_notify(const char*) { ++notices; }
bool extension_discover_pattern(u32 track,u32 index,extensions::PatternInfo& out) {
    ++discoveries; out={{1,snapshot_generation,track,index},7680,3}; return true;
}
bool extension_read_pattern(const extensions::PatternRef&,RecordedNote* out,u32,u32& count) {
    if(pending_read.state==1 && !in_gui) __builtin_trap();
    ++reads; *out=good;count=1;return true;
}
}}
extern "C" void sdk_stock_extension_gui_poll() { ++gui_calls;in_gui=true; }
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
    mode=11; control(51,true); CHECK(!result && !reads && !discoveries);
    collection[1].actions |= action_read_patterns;
    control(51,true); CHECK(result && reads==1 && discoveries==1);
    control(51,false); CHECK(!result && reads==1 && discoveries==1);
    extensions::PatternInfo info{}; RecordedNote out{};u32 count=99;
    CHECK(!saved.discover_pattern(3,1,info));
    CHECK(!saved.read_pattern(info.ref,&out,1,count) && count==0 && reads==1);
    mode=12;control(51,true);const u32 ticket=result;
    CHECK(ticket && pending_read.state==1 && reads==1 && !completions);
    control(51,true);CHECK(!result && reads==1); // bounded one-request admission
    sdk_extension_gui_poll();in_gui=false;
    CHECK(gui_calls==1 && reads==2 && !completions && pending_read.state==2);
    sdk_extension_gui_poll();in_gui=false;CHECK(reads==2); // no repeat processing
    extensions_sync();CHECK(completions==1 && completed_ticket==ticket && result_success && completed_count==1);
    CHECK(!foreign_completions && !pending_read.state);
    control(51,true);const u32 stale_ticket=result;sdk_extension_gui_poll();in_gui=false;
    ++snapshot_generation;extensions_sync();
    CHECK(completions==2 && completed_ticket==stale_ticket && !result_success && !completed_count);
    CHECK(!foreign_completions && !pending_read.state);
    CHECK(!saved.request_pattern_read({1,2,3,1})); // retained callback cannot submit
    // An action whose claim its holder lacks disables the collection.
    initialized = false; entries = nullptr; action_claims[0] = 1u;
    control(51, true); CHECK(writes == before + 1);
    initialized = false; entries = nullptr; action_claims[0] = 0;
    collection[1].state = &state_spec;
    collection[1].actions |= action_project_state;
    mode = 13;
    control(51, true); CHECK(result == 1 && state_writes == 1);
    CHECK(!saved.publish_project_state({1, 2}, nullptr, 0) && state_writes == 1);
    control(51, false); CHECK(!result && state_writes == 1);
    control(12, true); CHECK(!result && state_writes == 1);
    return 0;
}

namespace {
const extensions::ArrangementSurface* test_surface;
extensions::ArrangementIntent test_intent;
bool intent_pending, reply_pending;
u32 reply_size; u8 reply_bytes[2048];
}
extern "C" const extensions::ArrangementSurface* sdk_arrangement_owner() { return test_surface; }
extern "C" void sdk_arrangement_poll() {}
bool sdk_arrangement_take_intent(extensions::ArrangementIntent* out) {
    if (!intent_pending) return false;
    intent_pending = false; *out = test_intent;
    return out->origin.project == snapshot_generation && out->origin.identity == stock_context.project;
}
bool sdk_arrangement_post_reply(const extensions::ArrangementContext& context, const u8* bytes, u32 count) {
    if (reply_pending || count > 2048 || context.project != snapshot_generation || context.identity != stock_context.project) return false;
    for (u32 i = 0; i < count; ++i) reply_bytes[i] = bytes[i];
    reply_size = count; reply_pending = true; return true;
}

namespace opxy { namespace platform {
const char* project_state_status() { return nullptr; }
void project_state_observe(const ProjectStateSpec&, void (*)(const extensions::ProjectState&)) {}
bool project_state_publish(const ProjectStateSpec& spec, const extensions::ProjectToken& token, const u8*, u32) {
    if (&spec != &state_spec || token.generation != 1 || token.identity != 2) return false;
    ++state_writes; return true;
}
}}
