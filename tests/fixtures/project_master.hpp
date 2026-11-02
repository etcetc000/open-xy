// A passthrough owner for exercising project persistence; no contributor DSP.
#pragma once
#include <master_filter_state.cpp>
namespace opxy { namespace platform { namespace master_processor {
bool test_master, test_second, test_reverse;
u32 test_parameter_count=1;
void no_reset() {
}
void no_prepare(Parameters) {
}
void no_process(i32 *, i32 *) {
}
const master::Processor test_processor = {"probe",no_reset,no_prepare,no_process};
u32 processor_count(){return test_master ? (test_second ? 2 : 1) : 0;}
Registration registration(u32 type) {
    static const Parameter p[]={{"sweep","filter",0x40000000,0,0},{"gain","gain",100,1,0},{"mix","mix",200,2,0},{"mode","mode",0,3,3},{"alt","alt",300,0,0}};
    static const Parameter second[]={{"depth","depth",1234,0,0},{"rate","rate",4321,1,0}};
    if(test_reverse && test_second && type)type=3-type;
    if(test_master && test_second && type==2)return {"second",&test_processor,second,2,nullptr};
    if(test_master && type==1)return {"probe",&test_processor,p,test_parameter_count,nullptr};
    return {};
}
void test_enable(u32 packet) {
    test_master = true;menu.initialized=true;
    menu.type = menu.candidate = (packet>>30)&1;
    menu.values[0] = static_cast<i32>((packet&1023)<<21);
}
}}} // namespace opxy::platform::master_filter
