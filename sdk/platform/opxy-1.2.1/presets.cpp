// Stock preset scan (db2, 8d4eed56): each preset's top-level "type" is read
// before the preset is listed, and a preset without one is never listed. A
// factory preset (content/presets) whose engine this build does not install,
// such as the default preset of a machine since removed, reads as typeless and
// so is hidden. User presets (/presets) stay listed: stock's cleanup deletes a
// user folder that lists no presets, so hiding them could delete them.
#include "registration.hpp"
#include "stock.hpp"

using opxy::u32;
using opxy::platform::registry::StockString;
using opxy::platform::stock;

extern "C" {
void sdk_preset_type(StockString* type, const StockString* path, u32 flags) __asm__("sdk_preset_type");
// This build's machines and stock's engines (startup.cpp.in).
int sdk_engine_installed(const void* name) __asm__("sdk_engine_installed");
}

namespace {
const u32 kFactoryPass = 1;  // the scan's flags: content/presets rather than /presets
}

// Replaces the scan's call of read_preset_type(out, path) (8d4eead6); the
// flags are the scan's own, passed through in R2.
void sdk_preset_type(StockString* type, const StockString* path, u32 flags) {
    stock<void(StockString*, const StockString*)>(0x8d4eead6)(type, path);
    if (!(flags & kFactoryPass) || !type->length || sdk_engine_installed(type)) return;
    // Empty, keeping the capacity and any heap storage, which stock frees.
    char* text = type->capacity >= 16 ? *reinterpret_cast<char**>(type->text) : type->text;
    text[0] = 0;
    type->length = 0;
}
