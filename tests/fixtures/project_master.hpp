// A passthrough owner for exercising project persistence; no contributor DSP.
#pragma once
#include <master_filter_state.cpp>
namespace opxy { namespace platform { namespace master_filter {
bool test_master;
void no_reset() {
}
void no_prepare(u32) {
}
void no_process(i32 *, i32 *) {
}
const master::Filter test_filter = {"probe", nullptr, no_reset, no_prepare, no_process};
const master::Filter *descriptor() {
    return test_master ? &test_filter : nullptr;
}
const char *owner() {
    return test_master ? "probe" : "";
}
void test_enable(u32 packet) {
    test_master = true;
    const Controls controls = Controls::unpack(packet);
    menu.type = menu.candidate = controls.type;
    menu.values[0] = static_cast<i32>(controls.sweep << 21);
}
}}} // namespace opxy::platform::master_filter
