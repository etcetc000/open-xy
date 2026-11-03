#include <extension_runtime.hpp>
#include <extension_collection.cpp>
using namespace opxy;
using namespace opxy::platform;
namespace { bool capture_enabled = true, observer_enabled = true; }
namespace opxy { namespace extensions {
namespace capture { extern constexpr Hooks hooks = {}; }
namespace observer { extern constexpr Hooks hooks = {}; }
}}
extern "C" const extensions::Hooks* sdk_extension_capture_hooks() {
    return capture_enabled ? &opxy::extensions::capture::hooks : nullptr;
}
extern "C" const extensions::Hooks* sdk_extension_observer_hooks() {
    return observer_enabled ? &opxy::extensions::observer::hooks : nullptr;
}
#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
int main() {
    u32 count = 99;
    const ExtensionEntry* entries = extension_collection(count);
    CHECK(count == 2 && entries[0].hooks == &opxy::extensions::capture::hooks &&
          entries[1].hooks == &opxy::extensions::observer::hooks);
    CHECK(sdk_extension_hooks_static_count == 2 &&
          sdk_extension_hooks_static[0] == &opxy::extensions::capture::hooks &&
          sdk_extension_hooks_static[1] == &opxy::extensions::observer::hooks);
    capture_enabled = false;
    entries = extension_collection(count);
    CHECK(count == 1 && entries[0].hooks == &opxy::extensions::observer::hooks && !entries[0].claims && !entries[0].actions);
    observer_enabled = false;
    entries = extension_collection(count);
    CHECK(entries && count == 0);
    capture_enabled = true;
    entries = extension_collection(count);
    CHECK(count == 1 && entries[0].hooks == &opxy::extensions::capture::hooks &&
          entries[0].claims == 1u && entries[0].actions == action_edit_notes);
    return 0;
}
