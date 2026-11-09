// The 1.2.1 settings pages with stock doubles: an open-xy page follows stock's
// two, listing the extensions with settings; knob clicks open an extension's
// page there and go back. Its menu holds a stock setting per row (key, default,
// range), each row shows its values' text, an edit reaches the extension, and
// the saved values are read once on the first update.
#include <extension_runtime.cpp>
#include <extension_settings.cpp>

#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
using opxy::platform::settings_stock::MenuRow;
using opxy::platform::settings_stock::Menu;
using opxy::platform::settings_stock::Page;
using opxy::platform::settings_stock::Subscreens;
namespace {
// The extensions: one without settings, one with a page of two rows, one with a
// page of one row.
struct Delivery { u32 row, value; };
Delivery delivered[16];
u32 delivered_count;
void setting(u32 row, u32 value) { delivered[delivered_count++] = {row, value}; }
const extensions::Hooks plain = extensions::Hooks();
const extensions::Hooks configured = extensions::Hooks().with_setting(setting);
const char* const keys_values[] = {"off", "1", "2"};
const char* const loops_values[] = {"off", "on"};
const SettingRow rows[] = {{"loop keys", keys_values, 3, 2, "ext_tools_0"},
                           {"shift+play", loops_values, 2, 1, "ext_tools_1"}};
const ExtensionSettings page = {"tools", rows, 2};
void more_setting(u32, u32) {}
const extensions::Hooks more = extensions::Hooks().with_setting(more_setting);
const SettingRow more_rows[] = {{"speed", loops_values, 2, 0, "ext_mores_0"}};
const ExtensionSettings more_page = {"more", more_rows, 1};
const u32 claim_keys[] = {0}, action_claims[] = {0};
const ExtensionRegistry tables = {claim_keys, 0, action_claims, 0};
const ExtensionEntry collection[] = {{"plain", 0, 0, &plain, nullptr}, {"tools", 0, 0, &configured, &page},
                                     {"mores", 0, 0, &more, &more_page}};

// Stock's doubles.
struct Setting { const char* key; i32 value, min, max; bool loaded; };
alignas(8) unsigned char heap[8][0xb8];
u32 allocated, allocations_left = 99;
void* allocate(u32 size) {
    if (!allocations_left || allocated == 8 || size > 0xb8) return nullptr;
    --allocations_left;
    return heap[allocated++];
}
void make_setting(void* setting, const char* key, i32 initial, i32 min, i32 max) {
    *static_cast<Setting*>(setting) = {key, initial, min, max, false};
}
// Stock's menus: each copies its rows.
u32 menus_made;
MenuRow copied[4][4];
void make_menu(Menu* menu, const MenuRow* rows, u32 count) {
    MenuRow* to = copied[menus_made++ % 4];
    for (u32 i = 0; i < count && i < 4; ++i) to[i] = rows[i];
    menu->begin = to;
    menu->end = to + count;
}
// Stock's page: its method table (two words, then nine methods), a scroll step
// per row; activate selects row 0.
void stock_method() {}
Address stock_methods[11];
u32 activations;
void activate_page(Page* page) { ++activations; page->row = 0; }
void make_page(void* at, const char* title, Menu* menu) {
    Page& page = *static_cast<Page*>(at);
    page = {};
    page.methods = &stock_methods[2];
    page.title = title;
    page.menu = menu;
    page.step = 100;
}
// prefs_ctl: the top controller, its subscreens, the one shown, the knobs'
// pending clicks and Shift.
Address top = 0x8400c234;
Address* top_controller() { return &top; }
void* subscreen_list[3];
Subscreens subscreens = {0, subscreen_list, subscreen_list + 3, subscreen_list + 3};
u32 shown_index = 2;
unsigned char click[4];
u32 input = 1;
u32 shift;
u32 shift_down() { return shift; }
void knob(u32 key) {
    const u32 press[4] = {key, 127, 0, 0}, release[4] = {key, 0, 0, 0};
    extension_settings_key(press);
    extension_settings_key(release);
}
// M1's press and release; whether the press was taken.
bool m1() {
    const u32 press[4] = {6, 127, 0, 0}, release[4] = {6, 0, 0, 0};
    const bool taken = extension_settings_key(press);
    return !extension_settings_key(release) && taken;
}
void* listed[8];
u32 listed_count;
void assign(void*, void* const* pages, u32 count) {
    listed_count = count;
    for (u32 i = 0; i < count && i < 8; ++i) listed[i] = pages[i];
}
char shown[32];
void make_string(void* text, const char* from) {
    u32 i = 0;
    for (; from[i] && i < 31; ++i) static_cast<char*>(text)[i] = from[i];
    static_cast<char*>(text)[i] = 0;
}
i32 saved[2] = {0, 0};  // stock's settings file, by row
u32 loads;
void load(void* setting) {
    Setting& s = *static_cast<Setting*>(setting);
    ++loads;
    s.loaded = true;
    s.value = saved[s.key[10] - '0'];
}
i32 value(void* setting) { return static_cast<Setting*>(setting)->value; }

Menu& list_menu() { return settings_stock::list; }
bool same(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
}

namespace opxy { namespace platform {
const ExtensionEntry* extension_collection(u32& count) { count = 3; return collection; }
const ExtensionRegistry& extension_registry() { return tables; }
TrackPattern extension_pattern(u32) { return {}; }
RecordingContext extension_context(bool& p) { p = false; return {}; }
i32 extension_position(u32) { return 0; }
u32 extension_scaled_delta(u32, u32 delta) { return delta; }
i32 extension_collision_radius(u32) { return 0; }
u32 extension_write(u32, const RecordedNote*, u32, bool) { return 0; }
void extension_notify(const char*) {}
Address host_stock_address(Address address) {
    switch (address) {
        case 0x8d4d2900: return reinterpret_cast<Address>(&allocate);
        case 0x8c089110: return reinterpret_cast<Address>(&make_setting);
        case 0x8c097f0c: return reinterpret_cast<Address>(&make_menu);
        case 0x8c097d00: return reinterpret_cast<Address>(&make_page);
        case 0x8c0989f8: return reinterpret_cast<Address>(&assign);
        case 0x8c041788: return reinterpret_cast<Address>(&make_string);
        case 0x8c088da0: return reinterpret_cast<Address>(&load);
        case 0x8c088caa: return reinterpret_cast<Address>(&value);
        case 0x8c097cc4: return reinterpret_cast<Address>(&activate_page);
        case 0x8c0944b0: return reinterpret_cast<Address>(&top_controller);
        case 0x8d51f184: return reinterpret_cast<Address>(&shift_down);
        case 0x0801eff0: return reinterpret_cast<Address>(&subscreens);
        case 0x0801efe4: return reinterpret_cast<Address>(&shown_index);
        case 0x08023c48: return reinterpret_cast<Address>(&input);
    }
    if (address >= 0x08023c4c && address < 0x08023c50) return reinterpret_cast<Address>(&click[address - 0x08023c4c]);
    __builtin_trap();
}
}}

int main() {
    for (u32 i = 0; i < 11; ++i) stock_methods[i] = reinterpret_cast<Address>(&stock_method) + i;
    // 8c098ccc's frame: its two pages at +8.
    int system_page = 0, midi_page = 0;
    alignas(8) char frame[8 + 2 * sizeof(void*)] = {};
    void* stock_pages[2] = {&system_page, &midi_page};
    for (u32 i = 0; i < sizeof(stock_pages); ++i) frame[8 + i] = reinterpret_cast<char*>(stock_pages)[i];
    int vector = 0;

    // Stock's pages, then open-xy's: a list with a row per extension with
    // settings, showing its title and how to open it.
    sdk_extensions_settings_pages(&vector, frame);
    CHECK(listed_count == 3 && listed[0] == &system_page && listed[1] == &midi_page);
    Page& open = *static_cast<Page*>(listed[2]);
    CHECK(same(open.title, "open-xy") && menus_made == 3 && open.menu->end - open.menu->begin == 2);
    const MenuRow& entry = open.menu->begin[0];
    CHECK(same(entry.label, "tools") && same(open.menu->begin[1].label, "more"));
    CHECK(static_cast<const Setting*>(entry.setting)->max == 0);
    entry.display(shown, 0); CHECK(same(shown, "press k2 to open"));
    // Its methods are stock's but activate (+0x10), and the two words before them.
    CHECK(open.methods != &stock_methods[2] && open.methods[-2] == stock_methods[0] && open.methods[-1] == stock_methods[1]);
    for (u32 i = 0; i < 9; ++i) CHECK((open.methods[i] == stock_methods[2 + i]) == (i != 4));

    // The extension's menu: each row its label and a stock setting keyed
    // ext_<id>_<row>, starting at its default, ranging over its values.
    const Menu& tools = settings_stock::menus[0];
    const MenuRow* menu_rows = tools.begin;
    CHECK(tools.end - tools.begin == 2);
    const Setting& keys = *static_cast<const Setting*>(menu_rows[0].setting);
    const Setting& loops = *static_cast<const Setting*>(menu_rows[1].setting);
    CHECK(same(menu_rows[0].label, "loop keys") && same(keys.key, "ext_tools_0") &&
          keys.value == 2 && keys.min == 0 && keys.max == 2);
    CHECK(same(menu_rows[1].label, "shift+play") && same(loops.key, "ext_tools_1") &&
          loops.value == 1 && loops.min == 0 && loops.max == 1);
    // Rows show their values' text; a value out of range (saved by an earlier
    // version of the row) shows the default.
    menu_rows[0].display(shown, 1); CHECK(same(shown, "1"));
    menu_rows[1].display(shown, 0); CHECK(same(shown, "off"));
    menu_rows[1].display(shown, 2); CHECK(same(shown, "on"));
    menu_rows[0].display(shown, -1); CHECK(same(shown, "2"));

    // On COM with the open-xy page shown, a knob 2 click opens the selected
    // extension's page in place, on its first row; knob 2 does nothing more there.
    subscreen_list[0] = &system_page; subscreen_list[1] = &midi_page; subscreen_list[2] = &open;
    click[1] = 1; knob(11);
    CHECK(same(open.title, "tools") && open.menu == &tools && open.row == 0 && open.position == 0 && activations == 1);
    open.row = 1; open.position = 100;
    knob(11); CHECK(open.menu == &tools && open.row == 1 && activations == 1);
    // A knob 1 click goes back to the list, on the extension's row.
    click[0] = 1; knob(10);
    CHECK(same(open.title, "open-xy") && open.menu == &list_menu() && open.row == 0 && open.position == 0 && activations == 2);
    knob(10); CHECK(activations == 2);  // on the list, knob 1 does nothing
    open.row = 1; open.position = 100; knob(11);
    CHECK(same(open.title, "more") && open.menu == &settings_stock::menus[1] && open.position == 0 && activations == 3);
    knob(10); CHECK(open.menu == &list_menu() && open.row == 1 && open.position == 100 && activations == 4);
    // M1 ("back" in stock's labels) goes back too, its press taken from stock;
    // on the list, and with Shift, it stays stock's.
    knob(11); CHECK(open.menu == &settings_stock::menus[1] && activations == 5);
    CHECK(m1() && open.menu == &list_menu() && open.row == 1 && activations == 6);
    CHECK(!m1() && activations == 6);
    knob(11); shift = 1; CHECK(!m1() && open.menu == &settings_stock::menus[1]); shift = 0;
    top = 0x8400c000; CHECK(!m1()); top = 0x8400c234;
    input = 0; CHECK(!m1()); input = 1;
    CHECK(m1() && open.menu == &list_menu());
    open.row = 0; open.position = 0;
    // Not a click (a turn cleared it), with Shift, another screen or another
    // page shown: nothing.
    click[1] = 0; knob(11); CHECK(open.menu == &list_menu());
    click[1] = 1; shift = 1; knob(11); shift = 0; CHECK(open.menu == &list_menu());
    top = 0x8400c000; knob(11); top = 0x8400c234; CHECK(open.menu == &list_menu());
    shown_index = 1; knob(11); shown_index = 7; knob(11); shown_index = 2; CHECK(open.menu == &list_menu());
    // A press alone isn't a click (stock clicks on the release).
    { const u32 press[4] = {11, 127, 0, 0}; extension_settings_key(press); CHECK(open.menu == &list_menu()); }
    // Shown again by stock (activate) while inside an extension's page: the list.
    knob(11); CHECK(open.menu == &tools);
    reinterpret_cast<void (*)(Page*)>(open.methods[4])(&open);
    CHECK(same(open.title, "open-xy") && open.menu == &list_menu() && open.row == 0);

    // The first update reads the saved values and delivers every row once.
    saved[0] = 0;
    saved[1] = 1;
    menu_rows[0].setter(1);  // stock's menu may apply a value before then
    extensions_sync();
    CHECK(loads == 3 && keys.loaded && loops.loaded && delivered_count == 2);
    CHECK(delivered[0].row == 0 && delivered[0].value == 0 && delivered[1].row == 1 && delivered[1].value == 1);
    // An edit reaches the extension at the next update; a refused value doesn't.
    menu_rows[1].setter(0);
    menu_rows[0].setter(-1);
    extensions_sync();
    CHECK(loads == 3 && delivered_count == 3 && delivered[2].row == 1 && delivered[2].value == 0);

    // The screen built again: a new open-xy page on the same list, no new settings.
    const u32 before = allocated;
    sdk_extensions_settings_pages(&vector, frame);
    CHECK(listed_count == 3 && allocated == before + 1 && menus_made == 3 &&
          static_cast<const Page*>(listed[2])->menu == &list_menu());
    // Without memory for a page, stock's pages still go through.
    allocations_left = 0;
    sdk_extensions_settings_pages(&vector, frame);
    CHECK(listed_count == 2 && listed[0] == &system_page && listed[1] == &midi_page);
    return 0;
}
