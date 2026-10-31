// A passthrough owner for exercising project persistence; no contributor DSP.
#pragma once
#include <master_filter_state.cpp>
namespace opxy { namespace platform { namespace master_filter {
bool test_master;
u32 test_parameter_count=1;
void no_reset() {
}
void no_prepare(Parameters) {
}
void no_process(i32 *, i32 *) {
}
const master::Filter test_filter = {"probe", no_reset, no_prepare, no_process};
const master::Filter *descriptor() {
    return test_master ? &test_filter : nullptr;
}
const Parameter* parameters(u32& count) {
    static const Parameter p[]={{"sweep","filter",0x40000000,0,0},{"gain","gain",100,1,0},{"mix","mix",200,2,0},{"mode","mode",0,3,3},{"alt","alt",300,0,0}};
    count=test_master ? test_parameter_count : 0;return p;
}
const char *owner() {
    return test_master ? "probe" : "";
}
void test_enable(u32 packet) {
    test_master = true;menu.initialized=true;
    menu.type = menu.candidate = (packet>>30)&1;
    menu.values[0] = static_cast<i32>((packet&1023)<<21);
}
}}} // namespace opxy::platform::master_filter
