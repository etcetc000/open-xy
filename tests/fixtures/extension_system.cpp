// The System-row adapter with a host double for its one authenticated vector
// copy. It proves stock rows are preserved and the appended trigger reaches
// the public SystemEntry callback without a claimed control.
#include <extension_runtime.cpp>
#include <extension_system_stock.cpp>

#define CHECK(value) do { if (!(value)) return __LINE__; } while (false)
using namespace opxy;
using namespace opxy::platform;
namespace {
struct Value { u32 methods, reserved; const char* name; i32 minimum, maximum, value; };
struct FixtureRow { const char* label; Value* value; u32 display; void (*changed)(Value*); };
FixtureRow copied[9];
u32 copied_count, opens, choices;
const char* const labels[] = {"warm", "cool"};
void choose(u32) {}
const extensions::Choice choice = {"theme", labels, 2, 0, choose};
void open(const extensions::Context&, const extensions::SystemActions& actions) {
    ++opens; if (actions.select_choice(choice)) ++choices;
}
const extensions::SystemEntry entry = {"theme", open};
const extensions::Hooks hooks = extensions::Hooks().system_entry(&entry);
ExtensionEntry collection[] = {{"theme", 0, 0, &hooks}};
const ExtensionRegistry registry_tables = {nullptr, 0, nullptr, 0};

void copy_rows(void* destination, const void* source, u32 count) {
    FixtureRow* out = static_cast<FixtureRow*>(destination);
    const FixtureRow* in = static_cast<const FixtureRow*>(source);
    copied_count = count;
    for (u32 i = 0; i < count; ++i) { copied[i] = in[i]; out[i] = in[i]; }
}
}

namespace opxy { namespace platform {
const ExtensionEntry* extension_collection(u32& count) { count = 1; return collection; }
const ExtensionRegistry& extension_registry() { return registry_tables; }
TrackPattern extension_pattern(u32) { return {}; }
RecordingContext extension_context(bool& playing) { playing = false; return {}; }
i32 extension_position(u32) { return 0; }
u32 extension_scaled_delta(u32, u32) { return 0; }
i32 extension_collision_radius(u32) { return 0; }
bool extension_record_held() { return false; }
u32 extension_write(u32, const RecordedNote*, u32, bool) { return 0; }
void extension_notify(const char*) {}
bool extension_select_choice(const extensions::Choice&) { return true; }
void extension_palette_sync(const ExtensionEntry*, u32) {}
extensions::Time extension_monotonic_time() { return 7; }
Address host_stock_address(Address address) {
    return address == 0x8c097f0c ? reinterpret_cast<Address>(&copy_rows) : 0;
}
}}

int main() {
    FixtureRow stock_rows[6] = {};
    for (u32 i = 0; i < 6; ++i) {
        stock_rows[i].label = i == 0 ? "brightness" : "stock";
        stock_rows[i].display = 0x100 + i;
    }
    FixtureRow result[9] = {};
    sdk_extensions_system_entries(result, stock_rows, 6);
    CHECK(copied_count == 7);
    for (u32 i = 0; i < 6; ++i)
        CHECK(result[i].label == stock_rows[i].label && result[i].display == stock_rows[i].display);
    CHECK(result[6].label == entry.label && result[6].value && result[6].changed);
    result[6].changed(result[6].value);
    CHECK(opens == 1 && choices == 1 && result[6].value->value == 0);
    // An unexpected stock ABI is forwarded without adding an entry.
    sdk_extensions_system_entries(result, stock_rows, 5);
    CHECK(copied_count == 5);
    return 0;
}
