// The 1.2.1 bar menu label hooks with a stock label setter double (8d56a758):
// slots past M4 pass unchanged; M1-M4 show the claim holder's labels when the
// menu is built and whenever they change afterwards (empty slots hidden,
// disabled ones greyed out), and stock's own when given back.
#include <extension_runtime.hpp>
#include <extension_label.cpp>

#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
struct Call { u32 labels, slot; const char* text; u32 shown, enabled; };
Call last;
u32 calls;
const char clr_notes[] = "   clr notes", clr_params[] = "   clr params", stock_empty[] = "", clr_all[] = "   clr all";
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
// Stock builds the menu: M1, M2, M3 (empty, hidden, disabled), M4.
void build() {
    sdk_extensions_bar_label(0x84ec0000, 0, clr_notes, 1, 1);
    sdk_extensions_bar_label(0x84ec0000, 1, clr_params, 1, 1);
    sdk_extensions_bar_label(0x84ec0000, 2, stock_empty, 0, 0);
    sdk_extensions_bar_label(0x84ec0000, 3, clr_all, 1, 1);
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
    using extensions::keys::m1; using extensions::keys::m2; using extensions::keys::m3; using extensions::keys::m4;
    // M1-M4 have slots; other keys don't.
    CHECK(extension_labels(m1) && extension_labels(m4) && !extension_labels(extensions::keys::record));
    // Before the menu exists labels are kept, not drawn.
    extension_key_label(m1, "loop selected", true);
    extension_key_label(m2, "loop playing", false);
    extension_key_label(m3, "", true);
    CHECK(calls == 0);
    // Building the menu: M1 and M2 show the holder's (M2 greyed out), M3 stays
    // hidden, M4 (not given yet) shows stock's; the menu counts as built once.
    sdk_extensions_bar_label(0x84ec0000, 0, clr_notes, 1, 1);
    CHECK(calls == 1 && was(0, "loop selected", 1, 1) && menus_built == 1);
    sdk_extensions_bar_label(0x84ec0000, 1, clr_params, 1, 1);
    CHECK(calls == 2 && was(1, "loop playing", 1, 0));
    sdk_extensions_bar_label(0x84ec0000, 2, stock_empty, 0, 0);
    CHECK(calls == 3 && was(2, "", 0, 0));
    sdk_extensions_bar_label(0x84ec0000, 3, clr_all, 1, 1);
    CHECK(calls == 4 && was(3, "   clr all", 1, 1) && last.text == clr_all && menus_built == 1);
    // Slots past M4 pass unchanged.
    sdk_extensions_bar_label(0x84ec0000, 4, "x", 1, 0);
    CHECK(calls == 5 && was(4, "x", 1, 0));
    // Later changes go straight to their slot; other keys are ignored.
    extension_key_label(m4, "", true);
    CHECK(calls == 6 && was(3, "", 0, 0));
    extension_key_label(m2, "end loop", true);
    CHECK(calls == 7 && was(1, "end loop", 1, 1));
    extension_key_label(extensions::keys::record, "nope", true);
    CHECK(calls == 7);
    // Given back (Shift held): stock's own arguments, M3 hidden and disabled.
    extension_key_label(m1, nullptr, true);
    CHECK(calls == 8 && was(0, "   clr notes", 1, 1) && last.text == clr_notes);
    extension_key_label(m3, nullptr, true);
    CHECK(calls == 9 && was(2, "", 0, 0) && last.text == stock_empty);
    // A rebuilt menu keeps what each slot was last given.
    build();
    CHECK(calls == 13 && menus_built == 2 && was(3, "", 0, 0));
    // The adapter keeps its own copy of the text.
    char text[16] = "loop selected";
    extension_key_label(m1, text, true);
    text[0] = 'x';
    CHECK(calls == 14 && was(0, "loop selected", 1, 1));
    return 0;
}
