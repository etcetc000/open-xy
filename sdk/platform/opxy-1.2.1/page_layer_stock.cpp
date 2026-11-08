// The instrument controller's input methods (vtable 0x8400dff8: +0x0c keys,
// +0x10 keys while Shift is held, +0x18 encoder turns), entered before stock
// (page_layer_hooks.s replays each method's first eight bytes). While a page
// layer is open (its page shows), its owner sees each key press and turn
// first; one it takes never reaches stock.
#include "page_layer.hpp"
#include "stock.hpp"

using namespace opxy;
using namespace opxy::platform;

extern "C" {
u32 sdk_page_buttons(void*, const u32*) __asm__("sdk_page_buttons");
u32 sdk_page_shift_buttons(void*, const u32*) __asm__("sdk_page_shift_buttons");
void sdk_page_turn(void*, u32, const i32*) __asm__("sdk_page_turn");
u32 sdk_page_stock_buttons(void*, const u32*) __asm__("sdk_page_stock_buttons");
u32 sdk_page_stock_shift_buttons(void*, const u32*) __asm__("sdk_page_stock_shift_buttons");
void sdk_page_stock_turn(void*, u32, const i32*) __asm__("sdk_page_stock_turn");
}

namespace {
// The layer follows the page now, not as of the last controller update: a
// page key just pressed has already moved it.
void follow() { extensions_page(page_layer::shown(), byte(kSelectedTrack)); }
}

// Stock's key event: {key, ...}; only presses reach these methods.
u32 sdk_page_buttons(void* controller, const u32* event) {
    if (controller && event) {
        follow();
        if (extensions_page_key(event[0], true, false)) return 1;
    }
    return sdk_page_stock_buttons(controller, event);
}
u32 sdk_page_shift_buttons(void* controller, const u32* event) {
    if (controller && event) {
        follow();
        if (extensions_page_key(event[0], true, true)) return 1;
    }
    return sdk_page_stock_shift_buttons(controller, event);
}
// Stock's turn: position 0-3 (4-7 on a second row), event[1] the detents.
void sdk_page_turn(void* controller, u32 position, const i32* event) {
    if (controller && event && position < 8 && event[1]) {
        follow();
        if (extensions_page_turn(position & 3u, event[1], shift_held())) return;
    }
    sdk_page_stock_turn(controller, position, event);
}
