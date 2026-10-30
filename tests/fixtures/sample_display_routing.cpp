#include <cstdlib>
#include <sample_display_controls.cpp>
using namespace opxy;
namespace {
u32 selected, engine = 36, page, top = 0x8400dff8, loaded = 255, stock_calls;
bool shifted, sample_held;
u32* controller() { return &top; }
u32 get_engine(u32) { return engine; }
u32 get_selected() { return selected; }
u32 get_shift() { return shifted; }
u32 get_held(u32 key) { return key == 23 && sample_held; }
u32 stock_page(void*) { ++stock_calls; return 2; }
u32 stock_kind(u32 id) { return id == 2; }
u32 display() { return loaded; }
int ready() { return 1; }
platform::SampleMachine machine = {reinterpret_cast<const char*>(36), 2, ready, nullptr, nullptr,
    nullptr, nullptr, 0, nullptr, nullptr, nullptr, nullptr, display};
void require(bool x) { if (!x) std::abort(); }
}
const platform::SampleMachine* sdk_sample_machine(u32 id) { return id == 36 ? &machine : nullptr; }
u32 sdk_sample_alias(u32 id) { return id == 36 ? 2 : id; }
platform::Address platform::host_stock_address(platform::Address address) {
    if (address == 0x8c0944b0) return reinterpret_cast<Address>(&controller);
    if (address == 0x8d58781c) return reinterpret_cast<Address>(&get_engine);
    if (address == 0x8d4d6f14) return reinterpret_cast<Address>(&get_selected);
    if (address == 0x8d51f184) return reinterpret_cast<Address>(&get_shift);
    if (address == 0x8d5b7d7c) return reinterpret_cast<Address>(&get_held);
    if (address == 0x8c0b1198) return reinterpret_cast<Address>(&stock_page);
    if (address == 0x8d5b79c0) return reinterpret_cast<Address>(&stock_kind);
    if (address == 0x080238bc) return reinterpret_cast<Address>(&page);
    if (address == 0x840a0534) return reinterpret_cast<Address>(&selected);
    std::abort();
}
int main() {
    u32 instrument = 0x8400dff8, other = 123;
    require(sdk_sample_display_page(&instrument) == 255 && !stock_calls);
    loaded = 0; require(sdk_sample_display_page(&instrument) == 2);
    loaded = 255; top = 123;
    require(sdk_sample_display_page(&instrument) == 255); // Base under browser stays Granular.
    require(sdk_sample_display_page(&other) == 2);
    require(sdk_sample_display_page(nullptr) == 2);
    shifted = true; u32 browser_press[4] = {23, 1};
    require(!sdk_sample_display_key_event(browser_press)); // Input remains overlay-owned.
    top = 0x8400dff8; page = 1; require(sdk_sample_display_page(&instrument) == 2);
    page = 0; shifted = true;
    u32 down[4] = {23, 1}, up[4] = {23, 0};
    require(sdk_sample_display_key_event(down));
    require(sdk_sample_display_page(&instrument) == 2);
    require(sdk_sample_parameter_kind(36) == 0); // Trim view cannot change automation storage.
    require(sdk_sample_display_key_event(down)); // Repeat does not toggle.
    require(sdk_sample_display_page(&instrument) == 2);
    top = 123; shifted = false;
    require(sdk_sample_display_key_event(up)); // Owned release survives browser navigation.
    top = 0x8400dff8; shifted = true;
    require(sdk_sample_display_key_event(down));
    require(sdk_sample_display_page(&instrument) == 255);
    require(sdk_sample_display_key_event(up));
    engine = 2; require(sdk_sample_display_page(&instrument) == 2);
    require(!sdk_sample_display_key_event(down));
    engine = 36; selected = 8; require(sdk_sample_display_page(&instrument) == 2);
    selected = 0; sample_held = true; require(!sdk_sample_display_key_event(down));
}
