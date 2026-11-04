#include <extension_runtime.hpp>
#include <extension_collection.cpp>
using namespace opxy;
using namespace opxy::platform;
namespace {
bool capture_enabled = true, observer_enabled = true;
const extensions::Hooks capture_hooks = {}, observer_hooks = {};
}
extern "C" const extensions::Hooks* sdk_extension_capture_hooks() {
    return capture_enabled ? &capture_hooks : nullptr;
}
extern "C" const extensions::Hooks* sdk_extension_observer_hooks() {
    return observer_enabled ? &observer_hooks : nullptr;
}
#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
int main() {
    u32 count = 99;
    const ExtensionEntry* entries = extension_collection(count);
    CHECK(count == 2 && entries[0].hooks == &capture_hooks && entries[1].hooks == &observer_hooks);
    capture_enabled = false;
    entries = extension_collection(count);
    CHECK(count == 1 && entries[0].hooks == &observer_hooks && !entries[0].claims && !entries[0].actions);
    observer_enabled = false;
    entries = extension_collection(count);
    CHECK(entries && count == 0);
    capture_enabled = true;
    entries = extension_collection(count);
    CHECK(count == 1 && entries[0].hooks == &capture_hooks &&
          entries[0].claims == 1u && entries[0].actions == action_edit_notes);
    return 0;
}
