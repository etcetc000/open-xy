// Reuse stock's send popup and its animation on Aux 5 M1.
#include "input_monitor.hpp"
#include "input_monitor_popup.hpp"
extern "C" {
bool sdk_live_input_shift_visible() __asm__("sdk_live_input_shift_visible");
const char* sdk_live_input_caption(opxy::u32) __asm__("sdk_live_input_caption");
}
using namespace opxy;
using namespace opxy::platform;
namespace {
using input_monitor_popup::Popup;
Popup popup;
bool constructed, bound, visible;

void caption(u32 row, const char* label, const char* value) {
    if (!popup.values[row] || !value) return;
    char text[32];
    u32 n = 0;
    while (*label && n + 1 < sizeof(text)) text[n++] = *label++;
    while (*value && n + 1 < sizeof(text)) text[n++] = *value++;
    text[n] = 0;
    const i32 matrix[9] = {1024,0,0,0,1024,0,155*1024,
                           static_cast<i32>((57 + row * 40) * 1024 + 512),1024};
    stock<void(void*,const i32*)>(0x8d5f06a6)(popup.values[row], matrix);
    set_text(popup.values[row], text);
}
}
void sdk_input_monitor_frame(u32 painted, u32 selected) {
    if (selected != 63) { visible = false; return; }
    const bool tilt = sdk_input_monitor_shift_visible();
    const bool tune = sdk_live_input_shift_visible();
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
    const i32 center = tune ? (tilt ? 0 : -20) : 60;
    for (u32 i = 0; i < 4; ++i) {
        const bool shown = i == 0 ? tilt : tune;
        if (popup.icons[i]) popup.icons[i]->visible = 0;
        if (!popup.rows[i]) continue;
        popup.rows[i]->visible = shown;
        if (!shown) continue;
        i32 matrix[9];
        for (u32 j = 0; j < 9; ++j) matrix[j] = popup.rows[i]->matrix[j];
        matrix[7] = center * 1024 + input_monitor_popup::motion_q10(popup.motion[i]);
        stock<void(void*,const i32*)>(0x8d5f06a6)(popup.rows[i], matrix);
    }
    if (tilt) caption(0, "tilt mic   ", sdk_input_monitor_choice() ? "auto" : "off");
    if (tune) {
        caption(1, "tune   ", sdk_live_input_caption(1));
        caption(2, "retune   ", sdk_live_input_caption(2));
        caption(3, "amount   ", sdk_live_input_caption(3));
    }
    stock<void(Popup*,u32)>(0x8d51fe1c)(&popup, 0);
}
