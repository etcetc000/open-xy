// Authenticated COM -> System integration for stock OS 1.2.1. The stock
// initializer builds six 16-byte descriptors, then copies them into the
// System page vector. This replacement copies those descriptors verbatim and
// appends at most three SDK-owned rows, matching prefs.svg's nine row slots.
#include "../extension_runtime.hpp"
#include "stock.hpp"

extern "C" void sdk_extensions_system_entries(void*, const void*, opxy::u32)
    __asm__("sdk_extensions_system_entries");

namespace opxy { namespace platform {
namespace {
const u32 kStockRows = 6, kPageRows = 9, kExtensionRows = kPageRows - kStockRows;

// This is the small bounded numeric state stock's generic preferences
// dispatcher expects at descriptor +4. Its constructor writes the same
// layout: a methods pointer, a borrowed name, and Q10 min/max/value words.
// Static storage deliberately outlives the stock System page and requires no
// stock heap allocation or release path.
struct Value {
    u32 methods, reserved;
    const char* name;
    i32 minimum, maximum, value;
};
struct Row {
    const char* label;
    Value* value;
    u32 display;
    void (*changed)(Value*);
};
static_assert(sizeof(Address) != 4 || sizeof(Value) == 24, "Stock System value ABI");
static_assert(sizeof(Address) != 4 || sizeof(Row) == 16, "Stock System row ABI");

// 0x0803f760 is the stock bounded-preference numeric-state method table. The
// generic dispatcher uses its authenticated adjust/get methods directly; the
// table retains stock's normal screen semantics for the one-step trigger.
const u32 kValueMethods = 0x0803f760;
Row rows[kPageRows];
Value values[kExtensionRows];
u32 installed;

void changed(Value* value) {
    for (u32 i = 0; i < installed; ++i) {
        if (value != &values[i]) continue;
        // A System row is a one-step trigger. Returning it to zero means the
        // same row may open its selector again after the modal closes.
        value->value = 0;
        extension_system_entry_open(i);
        value->value = 0;
        return;
    }
}

void copy_vector(void* destination, const Row* source, u32 count) {
    stock<void(void*, const Row*, u32)>(0x8c097f0c)(destination, source, count);
}
}
}}

using namespace opxy;
using namespace opxy::platform;

void sdk_extensions_system_entries(void* destination, const void* source, u32 count) {
    const Row* stock_rows = static_cast<const Row*>(source);
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
    for (u32 i = 0; i < kStockRows; ++i) rows[i] = stock_rows[i];
    installed = extension_count;
    for (u32 i = 0; i < installed; ++i) {
        const extensions::SystemEntry* entry = entries[i];
        values[i] = {kValueMethods, 0, entry->label, 0, 1 << 10, 0};
        rows[kStockRows + i] = {entry->label, &values[i], 0, changed};
    }
    copy_vector(destination, rows, kStockRows + installed);
}
