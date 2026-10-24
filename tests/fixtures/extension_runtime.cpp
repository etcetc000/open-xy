#include <extension_runtime.cpp>
#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
RecordingContext stock_context = {1, 2, 0, 7680, 3, 255, true};
bool playing = true;
u32 writes, notices, observations, order[16], order_count, result, mode;
extensions::Actions saved;
extensions::Context saved_context;
const RecordedNote good = {-12, 60, 60, 100};
void first_note(const extensions::Context&, const extensions::LiveNote& note) {
    order[order_count++] = 1;
    observations += note.position == -12 && note.collision_radius == 180;
    // Reentrant callbacks are ignored, not delivered out of order.
    extensions_note_on(0, 60, 100);
}
void second_note(const extensions::Context&, const extensions::LiveNote&) { order[order_count++] = 2; }
void observe_control(const extensions::Context& context, const extensions::Control&, const extensions::Actions& actions) {
    result = actions.insert_notes(context, &good, 1);
    actions.notify("unauthorized");
}
void edit_control(const extensions::Context& context, const extensions::Control&, const extensions::Actions& actions) {
    saved = actions; saved_context = context;
    RecordedNote note = good;
    if (mode == 1) ++stock_context.pattern;
    if (mode == 2) note.pitch = 128;
    if (mode == 3) note.position = -241;
    if (mode == 4) note.length = stock_context.length + 1;
    result = actions.insert_notes(context, &note, 1);
    actions.notify("done");
    actions.notify("duplicate");
    if (mode == 5) result += actions.insert_notes(context, &good, 1);
}
const extensions::Hooks first = {nullptr, first_note, nullptr, nullptr, nullptr, observe_control};
const extensions::Hooks second = {nullptr, second_note, nullptr, nullptr, nullptr, edit_control};
ExtensionEntry collection[] = {{"observer", false, false, &first}, {"writer", true, true, &second}};
}
namespace opxy { namespace platform {
const ExtensionEntry* extension_collection(u32& count) { count = 2; return collection; }
RecordingContext extension_context(bool& p) { p = playing; return stock_context; }
i32 extension_position(u32) { return -12; }
u32 extension_scaled_delta(u32, u32 delta) { return delta << 8; }
i32 extension_collision_radius(u32) { return 180; }
u32 extension_write(u32, const RecordedNote*, u32 count) { ++writes; return count; }
void extension_notify(const char*) { ++notices; }
}}
int main() {
    extensions_note_on(0, 60, 100);
    CHECK(order_count == 2 && order[0] == 1 && order[1] == 2 && observations == 1);
    extensions_note_on(7, 60, 100); CHECK(order_count == 2);
    extensions_control(50, false, true, true, 100);
    CHECK(writes == 1 && notices == 1 && result == 1);
    CHECK(saved.insert_notes(saved_context, &good, 1) == 0);
    saved.notify("late"); CHECK(notices == 1);
    for (mode = 1; mode <= 4; ++mode) {
        extensions_control(50, false, true, true, 100);
        CHECK(result == 0 && writes == 1 && notices == 1);
    }
    mode = 5;
    extensions_control(50, false, true, true, 100);
    CHECK(result == 1 && writes == 2 && notices == 2);
    extensions_control(50, false, false, true, 100);
    extensions_control(12, false, true, true, 100);
    CHECK(result == 0 && writes == 2 && notices == 2);
    // Guard the generated registry at runtime as well as build time.
    initialized = false; entries = nullptr; collection[0].record_double_tap = true;
    extensions_note_on(0, 60, 100); CHECK(order_count == 2);
    initialized = false; collection[0].record_double_tap = false; collection[0].id = "writer";
    extensions_note_on(0, 60, 100); CHECK(order_count == 2);
    return 0;
}
