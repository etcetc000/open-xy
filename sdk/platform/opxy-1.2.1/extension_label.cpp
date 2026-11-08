// OS 1.2.1 key labels for claimed keys. Stock menus label M1-M4 through one
// object per screen (8d56a758: labels, slot, text, shown, enabled). The bar
// menu builds its labels once (8d546de8): "clr notes" on M1 (8d546fca), "clr
// params" on M2 (8d546fe6), M3 empty, hidden and disabled (8d547004), "clr
// all" on M4 (8d547020). Those four calls are replaced: each records stock's
// arguments for its slot and shows the claim holder's label instead, if there
// is one. A label that changes later is set through the same stock call; a
// slot given back to stock (Shift held) shows stock's arguments again.
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
const u32 kSlots = 4;  // M1-M4
// Per slot: stock's own label as the menu built it, and the holder's (own):
// its text ("" for an empty slot) and whether it is enabled (else greyed out).
struct Slot {
    const char* text;
    u32 shown, enabled;
    bool own, own_enabled;
    char own_text[16];
};
Slot slots[kSlots];
// The bar menu's label object, once the menu is built.
u32 menu;
void show(u32 slot) {
    const Slot& s = slots[slot];
    if (!menu) return;
    if (!s.own) set_label(menu, slot, s.text, s.shown, s.enabled);
    else if (!s.own_text[0]) set_label(menu, slot, s.own_text, 0, 0);
    else set_label(menu, slot, s.own_text, 1, s.own_enabled ? 1 : 0);
}
}

bool extension_labels(u32 key) { return key >= extensions::keys::m1 && key <= extensions::keys::m4; }
void extension_key_label(u32 key, const char* label, bool enabled) {
    using namespace label_stock;
    if (!extension_labels(key)) return;
    Slot& s = slots[key - extensions::keys::m1];
    s.own = label != nullptr;
    s.own_enabled = enabled;
    u32 i = 0;
    for (; label && i < 15 && label[i]; ++i) s.own_text[i] = label[i];
    s.own_text[i] = 0;
    show(key - extensions::keys::m1);
}
}}

void sdk_extensions_bar_label(opxy::u32 labels, opxy::u32 slot, const char* text, opxy::u32 shown,
                              opxy::u32 enabled) {
    using namespace opxy::platform::label_stock;
    if (slot >= kSlots) {
        set_label(labels, slot, text, shown, enabled);
        return;
    }
    // M1 comes first: the menu is being built again.
    if (slot == 0) opxy::platform::extension_bar_menu_built();
    menu = labels;
    Slot& s = slots[slot];
    s.text = text;
    s.shown = shown;
    s.enabled = enabled;
    show(slot);
}
