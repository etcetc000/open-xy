// Settings rows reach their extension as the setting event: every row once its
// saved value is read, then each edit once, before the update's other events.
#include <extension_runtime.cpp>
#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
struct Delivery { u32 row, value; };
Delivery delivered[16];
u32 delivered_count, contexts, loads, delivered_before_context;
void setting(u32 row, u32 value) { delivered[delivered_count++] = {row, value}; }
void context(const extensions::Context&) { ++contexts; delivered_before_context = delivered_count; }
const extensions::Hooks plain = extensions::Hooks().with_context(context);
const extensions::Hooks configured = extensions::Hooks().with_context(context).with_setting(setting);
const char* keys_values[] = {"off", "1", "2"};
const char* const loops_values[] = {"off", "on"};
const char* info_values[] = {"play from bar"};
SettingRow rows[] = {{"loop keys", keys_values, 3, 2, "ext_tools_0"},
                     {"shift+play", loops_values, 2, 1, "ext_tools_1"},
                     {"shift+play", info_values, 1, 0, "ext_tools_2"}};
ExtensionSettings page = {"tools", rows, 3};
u32 claim_keys[] = {0}, action_claims[] = {0};
const ExtensionRegistry tables = {claim_keys, 0, action_claims, 0};
ExtensionEntry collection[] = {{"plain", 0, 0, &plain, nullptr}, {"tools", 0, 0, &configured, &page}};
const ExtensionEntry* list = collection;
u32 list_count = 2;
u32 saved_keys = 0;  // what stock's file holds for row 0
void reset() {
    initialized = false; entries = nullptr; settings_loaded = false;
    delivered_count = 0;
    for (u32 i = 0; i < 16; ++i) setting_changed[i] = 0;
}
}
namespace opxy { namespace platform {
const ExtensionEntry* extension_collection(u32& count) { count = list_count; return list; }
const ExtensionRegistry& extension_registry() { return tables; }
TrackPattern extension_pattern(u32) { return {}; }
RecordingContext extension_context(bool& p) { p = false; return {}; }
i32 extension_position(u32) { return 0; }
u32 extension_scaled_delta(u32, u32 delta) { return delta; }
i32 extension_collision_radius(u32) { return 0; }
u32 extension_write(u32, const RecordedNote*, u32, bool) { return 0; }
void extension_notify(const char*) {}
void extension_settings_load() {
    ++loads;
    extensions_setting(1, 0, saved_keys);
    extensions_setting(1, 1, 1);
}
}}
int main() {
    // The pages, in collection order, with their entry.
    u32 entry = 9;
    CHECK(extensions_settings_page(0, entry) == &page && entry == 1);
    CHECK(!extensions_settings_page(1, entry));
    // Stock's menu may call setters before the first update; the saved values
    // replace them, and every row is delivered once, before the context. The
    // info row (one value) is never delivered.
    extensions_setting(1, 0, 0);
    saved_keys = 1;
    extensions_sync();
    CHECK(loads == 1 && delivered_count == 2 && delivered_before_context == 2 && contexts == 2);
    CHECK(delivered[0].row == 0 && delivered[0].value == 1 && delivered[1].row == 1 && delivered[1].value == 1);
    // Later updates load nothing and deliver nothing new.
    extensions_sync();
    CHECK(loads == 1 && delivered_count == 2);
    // An edit is delivered once, at the next update; the same value is no change.
    extensions_setting(1, 0, 2);
    extensions_setting(1, 0, 2);
    CHECK(delivered_count == 2);
    extensions_sync();
    CHECK(delivered_count == 3 && delivered[2].row == 0 && delivered[2].value == 2);
    extensions_setting(1, 0, 2);
    extensions_sync();
    CHECK(delivered_count == 3);
    // Out-of-range rows, values and entries, and entries without settings, are ignored.
    extensions_setting(1, 0, 3);
    extensions_setting(1, 2, 0);
    extensions_setting(0, 0, 1);
    extensions_setting(2, 0, 1);
    extensions_setting(1, 2, 0);
    extensions_setting(1, 2, 1);
    extensions_sync();
    CHECK(delivered_count == 3);
    // Invalid settings disable the collection, as any other registry fault.
    reset(); rows[0].initial = 3;
    CHECK(!extensions_settings_page(0, entry));
    // An info row may be empty (a separator); a setting's values may not.
    reset(); rows[0].initial = 2; info_values[0] = "";
    CHECK(extensions_settings_page(0, entry) == &page);
    reset(); info_values[0] = "play from bar"; keys_values[1] = "";
    CHECK(!extensions_settings_page(0, entry));
    reset(); keys_values[1] = "1"; rows[0].initial = 2; rows[2].value_count = 0;
    CHECK(!extensions_settings_page(0, entry));
    reset(); rows[2].value_count = 1; page.row_count = 0;
    CHECK(!extensions_settings_page(0, entry));
    reset(); page.row_count = 13;
    CHECK(!extensions_settings_page(0, entry));
    reset(); page.row_count = 3; rows[1].values = nullptr;
    CHECK(!extensions_settings_page(0, entry));
    reset(); rows[1].values = loops_values; rows[1].key = nullptr;
    CHECK(!extensions_settings_page(0, entry));
    reset(); rows[1].key = "ext_tools_1"; page.title = "";
    CHECK(!extensions_settings_page(0, entry));
    // At most four pages per build.
    reset(); page.title = "tools";
    ExtensionEntry five[] = {collection[1], collection[1], collection[1], collection[1], collection[1]};
    const char* ids[] = {"a", "b", "c", "d", "e"};
    for (u32 i = 0; i < 5; ++i) five[i].id = ids[i];
    list = five; list_count = 5;
    CHECK(!extensions_settings_page(0, entry));
    reset(); list_count = 4;
    CHECK(extensions_settings_page(3, entry) == &page && entry == 3 && !extensions_settings_page(4, entry));
    return 0;
}
