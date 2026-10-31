#include <cstdio>
#include <cstdlib>
#include <synth_controls.cpp>
using namespace opxy;
namespace opxy { namespace platform {
bool sample_display_capable(u32) { return false; }
bool sample_display_active(u32) { return false; }
}}
namespace {
void require(bool ok,const char* why) { if(!ok) { std::puts(why); std::exit(1); } }
u32 generation=1, writes;
i32 saved[16][58]={}, current[16][58]={};
u32 get_generation() { return generation; }
u32 get_engine(u32 track) { return track==8?40:0; }
u32 get_track(u32 track) { return 0x100000+track*0x10000; }
void set_value(u32 project,u32 track,u32 slot,i32 value) {
    require(project==0x86bd027c&&track<8&&slot==16,"macro escaped target");
    saved[track][slot]=value; ++writes;
}
int ready() { return 1; }
bool format(u32,i32,char*) { return false; }
}
const opxy::platform::SynthControls sdk_synth_controls[] = {
 {reinterpret_cast<const char*>(40),ready,{0,1,2,3,4,5,6,7},15,{0,0,0,0,0,0,0,0},format,false,false,nullptr,0,{0,0,0,0},nullptr,{16,-1,-1,-1},true}
};
const u32 sdk_synth_control_count=1;
opxy::platform::Address opxy::platform::host_stock_address(Address address) {
    if(address==0x8d584224) return reinterpret_cast<Address>(&get_generation);
    if(address==0x8d58781c) return reinterpret_cast<Address>(&get_engine);
    if(address==0x8d587708) return reinterpret_cast<Address>(&get_track);
    if(address==0x8d5acf28) return reinterpret_cast<Address>(&set_value);
    if(address>=0x86bd027c+0x4080&&address<0x86bd027c+0x4080+sizeof(saved)) return reinterpret_cast<Address>(saved)+address-(0x86bd027c+0x4080);
    for(u32 t=0;t<16;++t) {
        const Address base=get_track(t)+0x3864;
        if(address>=base&&address<base+sizeof(current[t])) return reinterpret_cast<Address>(current[t])+address-base;
    }
    std::abort();
}
int main() {
    current[8][4]=100;
    for(u32 t=0;t<8;++t) saved[t][16]=10+t;
    aux_refresh(); require(writes==0,"first observation must not overwrite instruments");
    current[8][4]=200; aux_refresh(); require(writes==8,"manual macro edit must reach instruments");
    // Native project storage can be reused: generation, not pointer, distinguishes loads.
    ++generation; current[8][4]=300;
    for(u32 t=0;t<8;++t) saved[t][16]=30+t;
    aux_refresh(); require(writes==8,"same-engine reload must not emit macro writes");
    for(u32 t=0;t<8;++t) require(saved[t][16]==static_cast<i32>(30+t),"loaded per-track edit lost");
    current[8][4]=400; aux_refresh(); require(writes==16,"edit after reload must still work");
    for(u32 t=0;t<8;++t) require(saved[t][16]==400,"macro did not reach track");
}
