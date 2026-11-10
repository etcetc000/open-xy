// OS 1.2.1 settings pages for extensions. Stock's settings screen (COM,
// prefs_ctl, method table 8400c234) lists pages, its subscreens (a vector at
// 0801eff0, the one shown at index 0801efe4; knob 1 moves between them);
// 8c098ccc makes two of them, "system" and "midi" (8c097d00: page, title, menu,
// each page 0xb8 bytes from stock's new, 8d4d2900), and hands both to
// 8c0989f8(pages vector, array, count). The call at 8c098d36 is replaced: the
// stock pages, then one "open-xy" page, go to the same call.
//
// The open-xy page lists the extensions declaring settings, one row each
// ("press k2 to open" beside it). Clicking knob 2 on one shows its page in
// place (title and menu swapped, as stock draws both from the page each
// frame); M1 ("back" in stock's labels) or a knob 1 click there goes back to
// the list, on the row it came from. M1 keeps stock's action on the list. A page shown again (activate, vt+0x10)
// starts on the list. A click is stock's: a knob pressed and let go without a
// turn, its pending flag at 08023c4c + knob still set at the release (8c0b5c90
// then dispatches it as a press, which a settings page ignores).
//
// A menu (8c097f0c: menu, rows, count) copies rows of {label, setting, display,
// setter}. A setting (8c089110: setting, key, initial, min, max; 0x18 bytes)
// keeps its key's address and registers it, so stock saves and loads its
// value; 8c088da0 loads it and 8c088caa returns it. The menu shows a value with
// display(text, value), which builds the text with stock's string constructor
// (8c041788), and calls setter(value) after an edit.
#include "../extension_runtime.hpp"
#include "stock.hpp"

extern "C" void sdk_extensions_settings_pages(void*, char*) __asm__("sdk_extensions_settings_pages");

namespace opxy { namespace platform {
namespace settings_stock {
struct MenuRow { const char* label; void* setting; void (*display)(void*, i32); void (*setter)(i32); };
// Stock's menu object: a word stock leaves alone, then the rows' vector.
struct Menu { u32 unused; MenuRow* begin; MenuRow* end; MenuRow* capacity; };
// A settings page (8c097d00): its method table, its drawing interface's (page
// + 4), its title, selected row and menu; knob 2 moves `position` by `step`
// per row, and the row is position / step (8c097c96); activate (8c097cc4)
// selects row 0 and sets `last` from the row count.
struct Page {
    const Address* methods;
    const void* view;
    void* object;
    const char* title;
    u32 row;
    Menu* menu;
    u32 scroll[3];
    u32 step;
    u32 scroll_end;
    u32 last;
    u32 position;
};
#if __SIZEOF_POINTER__ == 4
static_assert(__builtin_offsetof(Page, title) == 0xc && __builtin_offsetof(Page, row) == 0x10 &&
              __builtin_offsetof(Page, menu) == 0x14 && __builtin_offsetof(Page, step) == 0x24 &&
              __builtin_offsetof(Page, position) == 0x30, "stock's page layout");
#endif
// Stock's subscreen vector, as Menu.
struct Subscreens { u32 unused; void** begin; void** end; void** capacity; };
const u32 kPrefsMethods = 0x8400c234;
const u32 kSubscreens = 0x0801eff0;
const u32 kSubscreenShown = 0x0801efe4;
const u32 kClickPending = 0x08023c4c;  // a byte per knob
const u32 kKnob1 = 10, kKnob2 = 11, kM1 = 6;
const u32 kKeyInput = 0x08023c48;     // byte: stock takes key input
const u32 kMethods = 9;                // a page's own methods; two words precede them
const char* const kTitle = "open-xy";
const u32 kSlots = kSettingPages * kSettingRows;
struct Slot { u32 entry, row; const SettingRow* spec; void* setting; };
Slot slots[kSlots];
u32 slot_count;
Menu menus[kSettingPages];
const char* titles[kSettingPages];
u32 page_count;
bool built;
// The open-xy page: its list (one row per page), the page as last built, the
// page shown (kSettingPages: the list) and the page's methods with activate
// replaced.
Menu list;
void* list_setting;
Page* open_page;
u32 inside = kSettingPages;
Address methods[2 + kMethods];

void* allocate(u32 size) { return stock<void*(u32)>(0x8d4d2900)(size); }

// One setter and one display per slot, as stock passes them only the value.
template <u32 n> void set(i32 value) {
    if (n < slot_count && value >= 0) extensions_setting(slots[n].entry, slots[n].row, static_cast<u32>(value));
}
// A value out of range (one saved by an earlier version of the row, which the
// extension never receives) shows the row's default, the value it has.
template <u32 n> void show(void* text, i32 value) {
    const SettingRow* row = n < slot_count ? slots[n].spec : nullptr;
    const bool known = row && value >= 0 && static_cast<u32>(value) < row->value_count;
    stock<void(void*, const char*)>(0x8c041788)(text, known ? row->values[value] : row ? row->values[row->initial] : "");
}
#define SDK_SLOTS(f) \
    f<0>, f<1>, f<2>, f<3>, f<4>, f<5>, f<6>, f<7>, f<8>, f<9>, f<10>, f<11>, f<12>, f<13>, f<14>, \
    f<15>, f<16>, f<17>, f<18>, f<19>, f<20>, f<21>, f<22>, f<23>, f<24>, f<25>, f<26>, f<27>, f<28>, \
    f<29>, f<30>, f<31>, f<32>, f<33>, f<34>, f<35>, f<36>, f<37>, f<38>, f<39>, f<40>, f<41>, f<42>, \
    f<43>, f<44>, f<45>, f<46>, f<47>
void (*const setters[])(i32) = {SDK_SLOTS(set)};
void (*const displays[])(void*, i32) = {SDK_SLOTS(show)};
#undef SDK_SLOTS
static_assert(sizeof(setters) / sizeof(setters[0]) == kSlots && sizeof(displays) / sizeof(displays[0]) == kSlots,
              "one setter and one display per slot");

void nothing(i32) {}
void hint(void* text, i32) { stock<void(void*, const char*)>(0x8c041788)(text, "press k2 to open"); }
void activate(Page* page) { stock<void(Page*)>(0x8c097cc4)(page); }
// The list, on `row`; or extension page `p`, on its first row.
void show_list(Page* page, u32 row) {
    page->title = kTitle;
    page->menu = &list;
    activate(page);
    page->row = row;
    page->position = row * page->step;
    inside = kSettingPages;
}
void show_page(Page* page, u32 p) {
    page->title = titles[p];
    page->menu = &menus[p];
    activate(page);
    page->position = 0;
    inside = p;
}
// Stock shows the page again (or for the first time): from the list.
void shown_again(Page* page) {
    if (page == open_page) return show_list(page, 0);
    activate(page);
}
// Once: each page's settings and menu, which live as long as stock's own, and
// the list of them. The list's rows and info rows, which nothing edits, share
// one setting, sdk_ext_list (0 only).
void build() {
    built = true;
    u32 entry = 0;
    if (!extensions_settings_page(0, entry) || !(list_setting = allocate(0x18))) return;
    stock<void(void*, const char*, i32, i32, i32)>(0x8c089110)(list_setting, "sdk_ext_list", 0, 0, 0);
    for (const ExtensionSettings* page; page_count < kSettingPages &&
         (page = extensions_settings_page(page_count, entry));) {
        MenuRow rows[kSettingRows];
        u32 count = 0;
        for (; count < page->row_count; ++count) {
            const SettingRow& spec = page->rows[count];
            void* setting = spec.key ? allocate(0x18) : list_setting;
            if (!setting) break;
            if (spec.key)
                stock<void(void*, const char*, i32, i32, i32)>(0x8c089110)(
                    setting, spec.key, static_cast<i32>(spec.initial), 0, static_cast<i32>(spec.value_count - 1));
            slots[slot_count] = {entry, count, &spec, setting};
            rows[count] = {spec.label, setting, displays[slot_count], setters[slot_count]};
            ++slot_count;
        }
        if (!count) break;
        stock<void(Menu*, const MenuRow*, u32)>(0x8c097f0c)(&menus[page_count], rows, count);
        titles[page_count++] = page->title;
    }
    if (!page_count) return;
    MenuRow rows[kSettingPages];
    for (u32 p = 0; p < page_count; ++p) rows[p] = {titles[p], list_setting, hint, nothing};
    stock<void(Menu*, const MenuRow*, u32)>(0x8c097f0c)(&list, rows, page_count);
}
// A page of stock's, shown as the open-xy page: the list, its methods stock's
// but activate, which starts it on the list.
void make_open_page(Page* page) {
    if (!methods[2]) {
        for (u32 i = 0; i < 2 + kMethods; ++i) methods[i] = (page->methods - 2)[i];
        methods[2 + 4] = reinterpret_cast<Address>(&shown_again);
    }
    page->methods = &methods[2];
    open_page = page;
    inside = kSettingPages;
}
// COM shows the open-xy page.
bool on_open_page() {
    const Address* controller = stock<const Address*()>(0x8c0944b0)();
    if (!controller || *controller != kPrefsMethods) return false;
    const Subscreens& shown = *at<const Subscreens>(kSubscreens);
    const u32 index = word(kSubscreenShown);
    return index < static_cast<u32>(shown.end - shown.begin) && shown.begin[index] == open_page;
}
}

// On the open-xy page: a knob 2 click opens the list's selected page; M1 or a
// knob 1 click goes back to the list. Called with every queued key, before
// stock's; true for an M1 press it took (stock's then never runs).
bool extension_settings_key(const u32* event) {
    using namespace settings_stock;
    if (!open_page || shift_held() || !byte(kKeyInput)) return false;
    if (event[0] == kM1 && event[1]) {
        if (inside == kSettingPages || !on_open_page()) return false;
        show_list(open_page, inside);
        return true;
    }
    if (event[1] || (event[0] != kKnob1 && event[0] != kKnob2) || !byte(kClickPending + event[0] - kKnob1) ||
        !on_open_page()) return false;
    if (event[0] == kKnob2 && inside == kSettingPages && open_page->row < page_count)
        show_page(open_page, open_page->row);
    else if (event[0] == kKnob1 && inside < kSettingPages)
        show_list(open_page, inside);
    return false;
}

void extension_settings_load() {
    using namespace settings_stock;
    for (u32 n = 0; n < slot_count; ++n) {
        if (!slots[n].spec->key) continue;  // an info row: never saved
        stock<void(void*)>(0x8c088da0)(slots[n].setting);
        const i32 value = stock<i32(void*)>(0x8c088caa)(slots[n].setting);
        if (value >= 0) extensions_setting(slots[n].entry, slots[n].row, static_cast<u32>(value));
    }
}
}}

// R0 is stock's pages vector; R1 is 8c098ccc's frame, whose two pages are at
// +8, as the replaced R1 += 8 and R2 = 2 leave them.
void sdk_extensions_settings_pages(void* vector, char* frame) {
    using namespace opxy;
    using namespace opxy::platform;
    using namespace opxy::platform::settings_stock;
    if (!built) build();
    void* pages[3];
    void* const* stock_pages = reinterpret_cast<void* const*>(frame + 8);
    u32 count = 0;
    for (; count < 2; ++count) pages[count] = stock_pages[count];
    if (page_count) {
        if (void* page = allocate(0xb8)) {
            stock<void(void*, const char*, Menu*)>(0x8c097d00)(page, kTitle, &list);
            make_open_page(static_cast<Page*>(page));
            pages[count++] = page;
        }
    }
    stock<void(void*, void* const*, u32)>(0x8c0989f8)(vector, pages, count);
}
