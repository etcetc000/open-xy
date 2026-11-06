// OS 1.2.1 key labels for claimed keys. Stock menus label M1-M4 through one
// object per screen (8d56a758: labels, slot, text, shown, enabled). The bar
// menu builds its labels once (8d546de8) and leaves M3 empty, hidden and
// disabled (8d547004). That call is replaced: it records the bar menu's label
// object and shows the claim holder's text instead, if there is one. A label
// that changes later is set through the same stock call.
#include "../extension_runtime.hpp"
#include "stock.hpp"

extern "C" void sdk_extensions_bar_label(opxy::u32, opxy::u32, const char*, opxy::u32, opxy::u32)
    __asm__("sdk_extensions_bar_label");

namespace opxy { namespace platform {
namespace label_stock {
using SetLabel = void(u32, u32, const char*, u32, u32);
void set_label(u32 labels, u32 slot, const char* text, u32 shown, u32 enabled) {
    stock<SetLabel>(0x8d56a758)(labels, slot, text, shown, enabled);
}
const u32 kBarSlot = 2;  // M3
// The bar menu's label object and stock's own M3 label, once the menu is built.
struct BarLabel {
    u32 labels;
    const char* text;
    u32 shown, enabled;
};
BarLabel bar;
char text[16];
void show() {
    if (!bar.labels) return;
    if (text[0]) set_label(bar.labels, kBarSlot, text, 1, 1);
    else set_label(bar.labels, kBarSlot, bar.text, bar.shown, bar.enabled);
}
}

bool extension_labels(u32 key) { return key == extensions::keys::m3; }
void extension_key_label(u32 key, const char* label) {
    using namespace label_stock;
    if (key != extensions::keys::m3) return;
    u32 i = 0;
    for (; i < 15 && label[i]; ++i) text[i] = label[i];
    text[i] = 0;
    show();
}
}}

void sdk_extensions_bar_label(opxy::u32 labels, opxy::u32 slot, const char* text, opxy::u32 shown,
                              opxy::u32 enabled) {
    using namespace opxy::platform::label_stock;
    if (slot != kBarSlot) {
        set_label(labels, slot, text, shown, enabled);
        return;
    }
    bar = {labels, text, shown, enabled};
    opxy::platform::extension_bar_menu_built();
    show();
}
