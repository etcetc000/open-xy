// Authenticated COM -> System integration for stock OS 1.2.1. The stock
// initializer builds six 16-byte descriptors, then copies them into the
// System page vector. This replacement copies those descriptors verbatim and
// appends at most three SDK-owned rows, matching prefs.svg's nine row slots.
#include "../extension_runtime.hpp"
#include "stock.hpp"

extern "C" void sdk_extensions_system_entries(void*, const void*, opxy::u32)
    __asm__("sdk_extensions_system_entries");

namespace opxy { namespace platform {
// This is the small bounded numeric state stock's generic preferences
// dispatcher expects at descriptor +4. Its constructor writes the same
// layout: a methods pointer, a borrowed name, and Q10 min/max/value words.
// Static storage deliberately outlives the stock System page and requires no
// stock heap allocation or release path.
struct SystemValue {
    u32 methods, reserved;
    const char* name;
    i32 minimum, maximum, value;
};
struct SystemRow {
    const char* label;
    SystemValue* value;
    u32 display;
    void (*changed)(SystemValue*);
};
namespace {
const u32 kStockRows = 6, kPageRows = 9, kExtensionRows = kPageRows - kStockRows;

static_assert(sizeof(Address) != 4 || sizeof(SystemValue) == 24, "Stock System value ABI");
static_assert(sizeof(Address) != 4 || sizeof(SystemRow) == 16, "Stock System row ABI");

// 0x0803f760 is the stock bounded-preference numeric-state method table. The
// generic dispatcher uses its authenticated adjust/get methods directly; the
// table retains stock's normal screen semantics for the one-step trigger.
const u32 kValueMethods = 0x0803f760;
void copy_vector(void* destination, const SystemRow* source, u32 count) {
    stock<void(void*, const SystemRow*, u32)>(0x8c097f0c)(destination, source, count);
}
}
}}

// The authenticated System constructor already owns a 252-byte stack frame.
// The hook populates these fixed RAM descriptors in assembly, then calls the
// stock vector copy directly.  Keeping its construction off that frame avoids
// a nested C++ call and retains the stock constructor's stack headroom.
extern "C" void sdk_extensions_system_changed(opxy::platform::SystemValue*);
extern "C" {
opxy::platform::SystemValue sdk_extension_system_values[3] = {
    {0x0803f760, 0, nullptr, 0, 1 << 10, 0},
    {0x0803f760, 0, nullptr, 0, 1 << 10, 0},
    {0x0803f760, 0, nullptr, 0, 1 << 10, 0},
};
opxy::platform::SystemRow sdk_extension_system_rows[9] = {
    {}, {}, {}, {}, {}, {},
    {nullptr, &sdk_extension_system_values[0], 0, sdk_extensions_system_changed},
    {nullptr, &sdk_extension_system_values[1], 0, sdk_extensions_system_changed},
    {nullptr, &sdk_extension_system_values[2], 0, sdk_extensions_system_changed},
};
opxy::u32 sdk_extension_system_installed;
opxy::u32 sdk_extension_system_destination;
opxy::u32 sdk_extension_system_saved_p3;
}

extern "C" void sdk_extensions_system_changed(opxy::platform::SystemValue* value) {
    for (opxy::u32 i = 0; i < sdk_extension_system_installed; ++i) {
        if (value != &sdk_extension_system_values[i]) continue;
        // A System row is a one-step trigger. Returning it to zero means the
        // same row may open its selector again after the modal closes.
        value->value = 0;
        opxy::platform::extension_system_entry_open(i);
        value->value = 0;
        return;
    }
}

namespace opxy { namespace platform { namespace {
void populate_rows(const SystemRow* source, const extensions::SystemEntry* const* entries, u32 extension_count) {
    for (u32 i = 0; i < kStockRows; ++i) sdk_extension_system_rows[i] = source[i];
    sdk_extension_system_installed = extension_count;
    for (u32 i = 0; i < extension_count; ++i) {
        const extensions::SystemEntry* entry = entries[i];
        sdk_extension_system_values[i].name = entry->label;
        sdk_extension_system_values[i].value = 0;
        sdk_extension_system_rows[kStockRows + i].label = entry->label;
    }
}
}
}}

using namespace opxy;
using namespace opxy::platform;

void sdk_extensions_system_entries(void* destination, const void* source, u32 count) {
    const SystemRow* stock_rows = static_cast<const SystemRow*>(source);
    if (!destination || !stock_rows || count != kStockRows) {
        copy_vector(destination, stock_rows, count);
        return;
    }
    u32 extension_count = 0;
    const extensions::SystemEntry* const* entries = extension_system_entries(extension_count);
    if (!entries || extension_count > kExtensionRows) {
        copy_vector(destination, stock_rows, count);
        return;
    }
    populate_rows(stock_rows, entries, extension_count);
    copy_vector(destination, sdk_extension_system_rows, kStockRows + extension_count);
}
