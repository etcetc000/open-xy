// Execute the real MIX hooks against stock ABI boundary doubles.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "stock.hpp"
#include "master_filter.hpp"
#include "stock_browser.hpp"
using namespace opxy;
using namespace opxy::platform;
static void require(bool ok, const char *why) {
    if (!ok) {
        std::fprintf(stderr, "%s\n", why);
        std::exit(1);
    }
}
static bool shifted;
static u32 dirty_marks;
static u32 stock_buttons, stock_shift_buttons, stock_turns, stock_frames, stock_receives, stock_audio;
static u32 sent_id, sent_packet, constructors, draws, strokes;
namespace browser_abi = opxy::platform::stock_browser::detail;
static browser_abi::Delegate *active_browser;
static u32 browser_opens, browser_selected, screen_id;
static u32 browser_storage[40], browser_screen[4];
static void *get_browser() {
    return browser_storage;
}
static void *get_browser_screen() {
    return browser_screen;
}
static void heading(void *screen, i32 track, const char *label) {
    require(screen == browser_screen && track == -1 && std::strcmp(label, "filter") == 0,
            "Stock browser heading ABI");
}
static void preset_bank(void *screen, u32 show) {
    require(screen == browser_screen && show == 0, "No preset-bank switch");
}
static void show_browser(void *object, browser_abi::Catalog *data, i32 selected,
                         browser_abi::Delegate *delegate, u32 mode) {
    require(object == browser_storage && mode == 0 && data->filesystem == 0,
            "Stock non-filesystem browser API");
    require(data->rows.end - data->rows.begin == 2 && data->rows.capacity == data->rows.end,
            "Exactly two rows");
    for (u32 i = 0; i < 2; ++i) {
        const browser_abi::Row &row = data->rows.begin[i];
        require(row.id == static_cast<i32>(i) && row.flags == 0 && row.name.allocator == 0 &&
                    row.name.capacity == 15,
                "Stock inline row ABI");
        require(row.name.length == std::strlen(row.name.text) &&
                    std::strcmp(row.name.text, i ? "probe filter" : "original eq") == 0,
                "Stock labels");
        require(!row.items.begin && !row.items.end && !row.items.capacity && !row.children.begin &&
                    !row.children.end && !row.children.capacity && !row.parent,
                "No presets, folders or borrowed stock catalog storage");
    }
    screen_id = 9;
    active_browser = delegate;
    browser_selected = static_cast<u32>(selected);
    ++browser_opens;
    require(delegate->methods->busy(delegate) == 0 && delegate->methods->page_key(delegate) == 0,
            "Stock navigation enabled");
    browser_abi::Vector<browser_abi::String> options;
    delegate->methods->options(&options, delegate);
    require(!options.begin && !options.end && !options.capacity && options.allocator == 0,
            "Hidden-return vector ABI");
}
static void browser_highlight_id(u32 mode, i32 id) {
    require(active_browser != nullptr, "Stock browser owns selection input");
    active_browser->methods->highlight(active_browser, mode, id);
}
static void browser_close() {
    active_browser->methods->leave(active_browser);
    active_browser = nullptr;
    screen_id = 0;
}
static void browser_click_encoder(u32 encoder) {
    if (!active_browser->methods->click(active_browser, encoder))
        browser_close();
}
static u32 context_words[9];
extern "C" {
u32 sdk_master_stock_buttons(void *c, const u32 *e) {
    ++stock_buttons;
    if (c && e && e[0] == 7)
        static_cast<u32 *>(c)[0x88 / 4] = 1;
    return 42;
}
u32 sdk_master_stock_shift_buttons(void *, const u32 *) {
    ++stock_shift_buttons;
    return 24;
}
void sdk_master_stock_turn(void *, u32, const i32 *) {
    ++stock_turns;
}
void sdk_master_stock_frame(void *, i32) {
    ++stock_frames;
}
}
static u32 shift() {
    return shifted;
}
static void send(u32 id, i32 value) {
    sent_id = id;
    sent_packet = static_cast<u32>(value);
}
static i32 step(i32 v, const i32 *e, u32 kind, i32 bound) {
    require(kind == 3 && bound == -1, "Stock continuous stepping ABI");
    return v + e[1] * 0x1000000;
}
static void construct(void *, i32, i32, u32 size, const u32 *color, u32 align) {
    require((size == 14 || size == 20) && align == 0 && (*color == 0xffffffffu || *color == 0xff000000u),
            "Text constructor ABI");
    ++constructors;
}
static void set(void *, const char *) {
}
static void draw(void *, const u32 *c) {
    require(c[0] == 0 && c[10] == 0 && c[11] == 0, "Render context ABI");
    ++draws;
}
static void stroke(i32 *, i32 *, i32 thickness, const u32 *) {
    require(thickness > 0, "Positive stroke");
    ++strokes;
}
static void receive(u32 base, u32 id, i32 value) {
    require(base == 123 && id == 2 && value == 456, "Stock EQ pass through");
    ++stock_receives;
}
static void audio(void *, i32 *l, i32 *r) {
    ++stock_audio;
    for (u32 i = 0; i < 64; ++i) {
        l[i] /= 2;
        r[i] /= 2;
    }
}
static u32 project() {
    return 567;
}
static void dirty() {
    ++dirty_marks;
}
namespace opxy { namespace platform {
Address host_stock_address(Address a) {
    switch (a) {
    case 0x8d585ee8:
        return reinterpret_cast<Address>(&dirty);
    case kScreen:
        return reinterpret_cast<Address>(&screen_id);
    case 0x8c09dc28:
        return reinterpret_cast<Address>(&get_browser);
    case 0x8d550e70:
        return reinterpret_cast<Address>(&get_browser_screen);
    case 0x8d551c48:
        return reinterpret_cast<Address>(&heading);
    case 0x8c0a0b90:
        return reinterpret_cast<Address>(&show_browser);
    case 0x8d5501ec:
        return reinterpret_cast<Address>(&preset_bank);
    case 0x8d51f184:
        return reinterpret_cast<Address>(&shift);
    case 0x8d4dc84c:
        return reinterpret_cast<Address>(&send);
    case 0x8d4d6cc8:
        return reinterpret_cast<Address>(&step);
    case 0x8d600e20:
        return reinterpret_cast<Address>(&construct);
    case 0x8d6011f0:
        return reinterpret_cast<Address>(&set);
    case 0x8d600a5c:
        return reinterpret_cast<Address>(&draw);
    case 0x8d5fe884:
        return reinterpret_cast<Address>(&stroke);
    case 0x8d50eb8c:
        return reinterpret_cast<Address>(&receive);
    case 0x11a02c10:
        return reinterpret_cast<Address>(&audio);
    case 0x8d584210:
        return reinterpret_cast<Address>(&project);
    default:
        if (a >= 0x08014dd0 && a < 0x08014dd0 + sizeof(context_words))
            return reinterpret_cast<Address>(context_words) + (a - 0x08014dd0);
        require(false, "Unexpected stock boundary");
        return 0;
    }
}
}} // namespace opxy::platform
namespace opxy { namespace platform { namespace master_filter {
static u32 frames, resets, follows, projects;
void frame(u32, i32) {
    ++frames;
}
void reset_visual() {
    ++resets;
}
void follow_visual(u32) {
    ++follows;
}
void reset_project() {
    ++projects;
}
}}} // namespace opxy::platform::master_filter
namespace opxy { namespace platform { namespace master_filter {
void reset_audio() {
}
void prepare_audio(u32) {
}
void process_audio(i32 *, i32 *) {
}
const master::Filter probe_filter = {"probe filter", nullptr, reset_audio, prepare_audio, process_audio};
const master::Filter *descriptor() {
    return &probe_filter;
}
const char *owner() {
    return "probe";
}
}}} // namespace opxy::platform::master_filter
extern "C" void sdk_project_stock_new() {
}
#include "master_filter_stock_fixture.cpp"
// Stock 8c0b5dc8..5dde selects separate vtable methods for shifted presses.
static u32 press(void *c, const u32 *event) {
    return shifted ? sdk_master_shift_buttons(c, event) : sdk_master_buttons(c, event);
}
int main() {
    require(q31::wide(-2147483647 - 1, -2147483647 - 1) == 4611686018427387904LL &&
                q31::wide(-2147483647 - 1, -1) == 2147483648LL &&
                q31::wide(2147483647, 2147483647) == 4611686014132420609LL,
            "Full-width product preserves signed endpoints");
    u32 controller[35] = {};
    u32 key[1] = {7};
    i32 turn[3] = {256, 1, 0};
    shifted = false;
    require(press(controller, key) == 42 && controller[34] == 1, "Shortcut outside EQ delegates");
    sdk_master_frame(nullptr, 16);
    require(stock_frames == 1 && constructors == 0, "Ordinary M2 retains stock EQ screen");
    shifted = true;
    require(press(controller, key) == 1, "Shift M2 reaches shifted method and opens selector on EQ");
    shifted = false; // Shift release goes to stock browser, not MIX.
    sdk_master_frame(nullptr, 16);
    require(active_browser && browser_opens == 1 && browser_selected == 0 && constructors == 0 && draws == 0,
            "Stock selector owns rendering; custom selector text is absent");
    browser_highlight_id(0, 1); // Stock left encoder selection is unchanged.
    browser_highlight_id(0, 99);
    browser_highlight_id(1, 0);
    require(sent_packet == 0, "Highlighting never publishes audio changes");
    browser_click_encoder(1);
    require(active_browser != nullptr, "Other clicks retain stock selector");
    browser_click_encoder(0);
    require(!active_browser && sent_id == message_id && Controls::unpack(sent_packet).type == 1,
            "Stock left click confirms replacement through B3");
    sdk_master_receive(123, sent_id, static_cast<i32>(sent_packet));
    sdk_master_frame(nullptr, 16);
    require(master_filter::frames == 1 && constructors == 0,
            "replacement page draws only its panel callback");
    const u32 before = sent_packet;
    sdk_master_turn(controller, 1, turn);
    sdk_master_turn(controller, 2, turn);
    sdk_master_turn(controller, 3, turn);
    require(sent_packet == before && stock_turns == 0, "Only first replacement encoder edits");
    sdk_master_turn(controller, 0, turn);
    require(Controls::unpack(sent_packet).sweep > 512, "First encoder moves replacement sweep");
    key[0] = 11;
    press(controller, key);
    require(Controls::unpack(sent_packet).sweep > 512, "Second encoder click does not alter the filter");
    key[0] = 10;
    press(controller, key);
    require(Controls::unpack(sent_packet).sweep == 512, "First click restores neutral");
    require(master_filter::resets == 1 && master_filter::follows == 1,
            "GUI reset and follow reach presentation state");
    shifted = true;
    key[0] = 7;
    press(controller, key);
    shifted = false;
    require(active_browser && browser_selected == 1, "Stock selector restores current processor");
    browser_highlight_id(0, 0);
    require(active_browser->methods->key(active_browser, key) == 1, "M2 cancellation consumes page key");
    browser_close();
    require(Controls::unpack(sent_packet).type == 1, "Cancel retains current audio selection");
    // Stock left selection can be confirmed by either physical encoder click.
    for (u32 keycode = 10; keycode <= 13; keycode += 3) {
        for (u32 target = 0; target < 2; ++target) {
            shifted = true;
            key[0] = 7;
            press(controller, key);
            shifted = false;
            browser_highlight_id(0, static_cast<i32>(target));
            browser_click_encoder(keycode - 10);
            require(!active_browser && Controls::unpack(sent_packet).type == target,
                    "Left and white clicks both confirm EQ or replacement");
        }
    }
    menu.values[0] = 0x40000000;
    publish();
    sdk_master_receive(123, sent_id, static_cast<i32>(sent_packet));
    i32 l[64], r[64];
    for (u32 block = 0; block < 20; ++block) {
        for (u32 i = 0; i < 64; ++i) {
            l[i] = 10000;
            r[i] = -20000;
        }
        sdk_master_process(nullptr, l, r);
    }
    require(l[63] == 10000 && r[63] == -20000, "replacement centre is stereo dry after transition");
    const u32 calls = stock_audio;
    sdk_master_process(nullptr, l, r);
    require(stock_audio == calls, "Settled replacement bypasses stock EQ");
    sdk_master_receive(123, message_id, -1);
    sdk_master_receive(123, 2, 456);
    require(stock_receives == 1, "Virtual IDs including malformed snapshots never reach stock setter");
    require(sdk_master_project() == 567 && sent_packet == initial_packet,
            "New project resets GUI and publishes EQ");
    sdk_master_receive(123, message_id, static_cast<i32>(sent_packet));
    for (u32 block = 0; block < 20; ++block) {
        for (u32 i = 0; i < 64; ++i) {
            l[i] = 10000;
            r[i] = -20000;
        }
        sdk_master_process(nullptr, l, r);
    }
    require(l[63] == 5000 && r[63] == -10000, "EQ restored after project change");
    sdk_master_frame(nullptr, 16);
    require(stock_frames == 3, "Original EQ screen restored");
    controller[34] = 2;
    sdk_master_turn(controller, 0, turn);
    require(stock_turns == 1, "Other MIX pages delegate");
    require(stock_buttons == 1, "Handled replacement clicks never reset saved stock EQ");
    shifted = true;
    key[0] = 7;
    for (u32 page = 0; page < 4; ++page) {
        if (page == 1)
            continue;
        controller[34] = page;
        require(press(controller, key) == 24 && controller[34] == page,
                "Shift M2 outside EQ preserves stock shifted behavior");
    }
    require(stock_shift_buttons == 3, "Only unhandled shifted events reach stock shifted method");
    const u32 dirty_before = dirty_marks;
    loaded(567, (1u << 30) | 123u);
    sdk_master_project();
    require(sent_packet == ((1u << 30) | 123u) && sdk_master_snapshot() == sent_packet,
            "Project restores selected replacement and position");
    require(master_filter::follows > 1 && dirty_marks == dirty_before,
            "Restore updates presentation without marking dirty");
    controller[34] = 1;
    shifted = false;
    key[0] = 11;
    press(controller, key);
    require(dirty_marks == dirty_before, "Unused click does not dirty project");
    key[0] = 10;
    press(controller, key);
    require(dirty_marks == dirty_before + 1 && Controls::unpack(sent_packet).sweep == 512,
            "Actual neutral reset marks project modified");
    press(controller, key);
    require(dirty_marks == dirty_before + 1, "Repeated neutral reset is not an edit");
    loaded(567, 77);
    sdk_master_project();
    require(sent_packet == 77 && menu.type == 0, "EQ selection retains saved replacement position");
    sdk_master_project();
    require(sent_packet == 512, "Missing companion resets only the extension to EQ/neutral");
}
