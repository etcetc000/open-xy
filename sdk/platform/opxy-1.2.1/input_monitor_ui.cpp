// Reuse stock's centered send control and its animation, on a private document.
#include "input_monitor.hpp"
#include "input_monitor_popup.hpp"
using namespace opxy;
using namespace opxy::platform;
namespace {
using input_monitor_popup::Popup;
Popup popup;
bool constructed, bound, visible;
}
void sdk_input_monitor_frame(u32 painted, u32 selected) {
    if (selected != 63 || !sdk_input_monitor_shift_visible()) { visible = false; return; }
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
    for (u32 i = 0; i < 4; ++i) {
        if (popup.rows[i]) popup.rows[i]->visible = i == 0;
        if (popup.icons[i]) popup.icons[i]->visible = 0;
    }
    if (popup.rows[0]) {
        i32 matrix[9];
        for (u32 i = 0; i < 9; ++i) matrix[i] = popup.rows[0]->matrix[i];
        matrix[7] = 60 * 1024 + input_monitor_popup::motion_q10(popup.motion[0]);
        stock<void(void*,const i32*)>(0x8d5f06a6)(popup.rows[0], matrix);
    }
    if (popup.values[0]) {
        const i32 matrix[9] = {1024,0,0,0,1024,0,155*1024,57*1024+512,1024};
        stock<void(void*,const i32*)>(0x8d5f06a6)(popup.values[0], matrix);
        set_text(popup.values[0], sdk_input_monitor_choice() ? "tilt mic   auto" : "tilt mic   off");
    }
    stock<void(Popup*,u32)>(0x8d51fe1c)(&popup, 0);
}
