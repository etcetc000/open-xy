#define main baseline_main
#include "extension_runtime.cpp"
#undef main
namespace {
u32 intents_seen, results_seen, accepted_ticket;
bool forbidden_write, forbidden_read, result_posted, state_published, result_state_denied;
extensions::ArrangementContext origin;
extensions::Actions retained;
void surface_intent(const extensions::Context& c, const extensions::ArrangementIntent& request, const extensions::Actions& a) {
    ++intents_seen; origin = request.origin; retained = a;
    state_published = a.publish_project_state({request.values[0], request.values[1]}, nullptr, 0);
    if (request.code == 2) return;
    forbidden_write = a.insert_notes(c, 0, &good, 1) || a.replace_notes(c, 0, &good, 1);
    extensions::PatternInfo info{};
    if (a.discover_pattern(3, 1, info)) accepted_ticket = a.request_pattern_read(info.ref);
}
void surface_result(const extensions::PatternReadResult& r, const extensions::Actions& a) {
    ++results_seen;
    result_state_denied = !a.publish_project_state({1, 2}, nullptr, 0);
    extensions::PatternInfo info{}; u32 count = 0; RecordedNote note{};
    forbidden_read = a.discover_pattern(3, 1, info) || a.read_pattern(r.ref, &note, 1, count) || a.request_pattern_read(r.ref);
    forbidden_write |= a.insert_notes(saved_context, 0, &good, 1) || a.replace_notes(saved_context, 0, &good, 1);
    const u8 response[] = {11, 22};
    result_posted = a.post_arrangement_reply(origin, response, 2);
    if (a.post_arrangement_reply(origin, response, 2)) __builtin_trap(); // reply-full
}
const extensions::ArrangementSurface surface = {nullptr, nullptr, {0,0,0}, 0, nullptr, nullptr, nullptr, nullptr,
    surface_intent, nullptr, surface_result};
const extensions::Hooks surface_hooks = extensions::Hooks().surface(&surface);
}
int main() {
    CHECK(baseline_main() == 0);
    initialized = false; entries = nullptr; action_claims[0] = 0;
    collection[1].hooks = &surface_hooks;
    collection[1].actions = action_read_patterns | action_edit_notes | action_surface_reply | action_project_state;
    collection[1].state = &state_spec;
    test_surface = &surface;
    test_intent = {{snapshot_generation, stock_context.project, 0, 0, true}, 1, {3,1,0,0}};
    intent_pending = true; extensions_sync();
    CHECK(intents_seen == 1 && accepted_ticket && !forbidden_write);
    CHECK(pending_read.state == 1);
    sdk_extension_gui_poll(); in_gui = false; extensions_sync();
    CHECK(results_seen == 1 && !forbidden_read && !forbidden_write && result_posted);
    CHECK(reply_pending && reply_size == 2 && reply_bytes[0] == 11);
    reply_pending = false;
    CHECK(!retained.post_arrangement_reply(origin, reply_bytes, 1)); // outside scope
    intent_pending = true; extensions_sync();
    CHECK(intents_seen == 2 && pending_read.state == 1);
    sdk_extension_gui_poll(); in_gui = false;
    ++snapshot_generation; extensions_sync();
    CHECK(results_seen == 2 && !result_posted && !reply_pending); // generation changed before reply
    intent_pending = true; extensions_sync();
    CHECK(intents_seen == 2); // old project intent never executes
    CHECK(result_state_denied);
    // Surface callback's declared namespace is available only for this intent.
    test_intent = {{snapshot_generation, stock_context.project, 0, 0, true}, 2, {1,2,0,0}};
    const u32 before_state = state_writes;
    intent_pending = true; extensions_sync();
    CHECK(state_published && state_writes == before_state + 1);
    CHECK(!retained.publish_project_state({1, 2}, nullptr, 0));
    test_intent.values[0] = 0; // stale state token, even though intent is current
    intent_pending = true; extensions_sync();
    CHECK(!state_published && state_writes == before_state + 1);
    collection[1].actions &= ~action_project_state;
    test_intent.values[0] = 1;
    intent_pending = true; extensions_sync();
    CHECK(!state_published && state_writes == before_state + 1);
    CHECK(transaction_state == nullptr);
    return 0;
}
