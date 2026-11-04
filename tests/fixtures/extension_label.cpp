// The 1.2.1 bar menu label hook with a stock label setter double (8d56a758):
// other slots and stock's empty M3 pass unchanged; a claim holder's label
// shows on M3 when the menu is built and whenever it changes afterwards.
#include <extension_runtime.hpp>
#include <extension_label.cpp>

#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
struct Call { u32 labels, slot; const char* text; u32 shown, enabled; };
Call last;
u32 calls;
const char stock_empty[] = "";
void set(u32 labels, u32 slot, const char* text, u32 shown, u32 enabled) {
    last = {labels, slot, text, shown, enabled};
    ++calls;
}
bool same(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
bool was(u32 slot, const char* text, u32 shown, u32 enabled) {
    return last.labels == 0x84ec0000 && last.slot == slot && same(last.text, text) && last.shown == shown &&
           last.enabled == enabled;
}
}

namespace opxy { namespace platform {
u32 menus_built;
void extension_bar_menu_built() { ++menus_built; }
Address host_stock_address(Address address) {
    if (address == 0x8d56a758) return reinterpret_cast<Address>(&set);
    __builtin_trap();
}
}}

int main() {
    // Only M3 has a slot.
    CHECK(extension_labels(extensions::keys::m3) && !extension_labels(extensions::keys::m1) &&
          !extension_labels(extensions::keys::record));
    // Before the menu exists a label is kept, not drawn.
    extension_key_label(extensions::keys::m3, "loop off");
    CHECK(calls == 0);
    // Building the menu: other slots pass unchanged, M3 shows the label.
    sdk_extensions_bar_label(0x84ec0000, 0, "   clr notes", 1, 1);
    CHECK(calls == 1 && was(0, "   clr notes", 1, 1));
    sdk_extensions_bar_label(0x84ec0000, 2, stock_empty, 0, 0);
    CHECK(calls == 2 && was(2, "loop off", 1, 1) && menus_built == 1);
    // Later changes go straight to the slot; another key's label is ignored.
    extension_key_label(extensions::keys::m3, "loop on");
    CHECK(calls == 3 && was(2, "loop on", 1, 1));
    extension_key_label(extensions::keys::m1, "nope");
    CHECK(calls == 3);
    // Giving the slot back restores stock's own M3, hidden and disabled.
    extension_key_label(extensions::keys::m3, "");
    CHECK(calls == 4 && was(2, "", 0, 0) && last.text == stock_empty);
    sdk_extensions_bar_label(0x84ec0000, 2, stock_empty, 0, 0);
    CHECK(calls == 5 && was(2, "", 0, 0));
    // The adapter keeps its own copy of the text.
    char text[16] = "loop on";
    extension_key_label(extensions::keys::m3, text);
    text[0] = 'x';
    CHECK(calls == 6 && was(2, "loop on", 1, 1));
    return 0;
}
