// Reuse stock's send popup and its animation on Aux 5 M1.
#include "input_monitor.hpp"
#include "input_monitor_popup.hpp"
extern "C" {
opxy::u32 sdk_live_input_page() __asm__("sdk_live_input_page");
const char* sdk_live_input_caption(opxy::u32, bool) __asm__("sdk_live_input_caption");
const char* sdk_live_input_page_hint() __asm__("sdk_live_input_page_hint");
}
using namespace opxy;
using namespace opxy::platform;
namespace {
using input_monitor_popup::Popup;
Popup popup;
bool constructed, bound, visible;

void caption(u32 row, const char* label, const char* value, const char* hint = nullptr) {
    if (!popup.values[row] || !label || !value) return;
    char text[32];
    u32 n = 0;
    while (*label && n + 1 < sizeof(text)) text[n++] = *label++;
    for (u32 i = 0; i < 3 && n + 1 < sizeof(text); ++i) text[n++] = ' ';
    while (*value && n + 1 < sizeof(text)) text[n++] = *value++;
    if (hint) {
        for (u32 i = 0; i < 3 && n + 1 < sizeof(text); ++i) text[n++] = ' ';
        while (*hint && n + 1 < sizeof(text)) text[n++] = *hint++;
    }
    text[n] = 0;
    const i32 scale = hint ? 717 : 1024; // 14 px in the row that shares its caption with the shortcut.
    const i32 baseline = hint ? 55 : 57;
    const i32 matrix[9] = {scale,0,0,0,scale,0,155*1024,
                           (baseline + static_cast<i32>(row) * 40) * 1024 + 512,1024};
    stock<void(void*,const i32*)>(0x8d5f06a6)(popup.values[row], matrix);
    set_text(popup.values[row], text);
}
}
void sdk_input_monitor_frame(u32 painted, u32 selected) {
    if (selected != 63) { visible = false; return; }
    const u32 page = sdk_live_input_page();
    const bool extra = page == 2;
    const bool tilt = !extra && sdk_input_monitor_shift_visible();
    const bool tune = page != 0;
    if (!tilt && !tune) { visible = false; return; }
    if (painted != 63) return;
    if (!constructed) {
        // Page zero does not register or replace stock's send screen.
        stock<void(Popup*,u32,const char*)>(0x8d520844)(&popup, 0, "fx_send.svg");
        constructed = true;
    }
    if (!popup.svg) stock<void(Popup*)>(0x8d520ce0)(&popup);
    if (!popup.svg) { visible = false; return; }
    if (!bound) { stock<void(Popup*)>(0x8d53b04c)(&popup); bound = true; }
    if (!visible) { stock<void(Popup*)>(0x8d53adc4)(&popup); visible = true; }

    // Stock updates only this presenter's text and motion and dims the page.
    // Defer its SVG draw until our caption, visibility and centering are set.
    const void* document = popup.svg;
    popup.svg = nullptr;
    stock<void(Popup*,i32)>(0x8d53ae00)(&popup, 0);
    popup.svg = document;
    const i32 center = tune ? (tilt || extra ? 0 : -20) : 60;
    for (u32 i = 0; i < 4; ++i) {
        const bool shown = i == 0 ? tilt || extra : tune;
        if (popup.icons[i]) popup.icons[i]->visible = 0;
        if (!popup.rows[i]) continue;
        popup.rows[i]->visible = shown;
        if (!shown) continue;
        i32 matrix[9];
        for (u32 j = 0; j < 9; ++j) matrix[j] = popup.rows[i]->matrix[j];
        matrix[7] = center * 1024 + input_monitor_popup::motion_q10(popup.motion[i]);
        stock<void(void*,const i32*)>(0x8d5f06a6)(popup.rows[i], matrix);
    }
    if (tilt) caption(0, "tilt mic", sdk_input_monitor_choice() ? "auto" : "off");
    if (tune) {
        const char* hint = sdk_live_input_page_hint();
        for (u32 row = extra ? 0 : 1; row < 4; ++row)
            caption(row, sdk_live_input_caption(row, true), sdk_live_input_caption(row, false),
                    row == 3 ? hint : nullptr);
    }
    stock<void(Popup*,u32)>(0x8d51fe1c)(&popup, 0);
}
