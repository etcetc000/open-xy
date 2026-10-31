// Actual aux selector state changes with a bounded stock-memory boundary.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <aux_modules.cpp>
using namespace opxy;
namespace {
void require(bool ok, const char* why) { if (!ok) { std::puts(why); std::exit(1); } }
alignas(4) u8 project[0x54076c];
u32 project_pointer=0x100000, dirty, selected=8;
u32* engine(u32 track,u32 pattern) { return reinterpret_cast<u32*>(project+0xd7c+track*0x53f44+pattern*0x53f4+0x18); }
u32 get_engine(u32 track) { return *engine(track,0); }
void changed(u32 track) { require(track==8,"wrong dirty track"); ++dirty; }
int ready() { return 1; }
}
const opxy::platform::AuxModule sdk_aux_modules[]={{reinterpret_cast<const char*>(40),reinterpret_cast<const char*>(70),ready,"aux",{-1,-1,-1,-1},0,nullptr,nullptr}};
const u32 sdk_aux_module_count=1;
const opxy::platform::SynthControls* sdk_synth_controls_of(u32,bool) { return nullptr; }
namespace opxy { namespace platform { namespace stock_browser { namespace detail {
bool open(Session&,Row*,u32,const char*,const char* const*,u32,Callbacks,i32) { return false; }
}}}}
opxy::platform::Address opxy::platform::host_stock_address(Address address) {
    if(address==0x840a0524) return reinterpret_cast<Address>(&project_pointer);
    if(address==0x840a0534) return reinterpret_cast<Address>(&selected);
    if(address==0x8d58781c) return reinterpret_cast<Address>(&get_engine);
    if(address==0x8d587668) return reinterpret_cast<Address>(&changed);
    if(address>=project_pointer&&address<project_pointer+sizeof(project)) return reinterpret_cast<Address>(project)+address-project_pointer;
    std::abort();
}
int main() {
    *reinterpret_cast<u32*>(project+0xd7c+8*0x53f44+0x53f40)=2;
    *engine(8,0)=*engine(8,1)=18;
    place(8,1);
    require(*engine(8,0)==40&&*engine(8,1)==40&&dirty==1,"assignment must persist every pattern");
    place(8,0);
    require(*engine(8,0)==18&&*engine(8,1)==18&&dirty==2,"removal must dirty project without a macro setter");
    place(8,0); require(dirty==2,"unchanged selection must not dirty");
    place(7,1); place(14,1); require(dirty==2,"only aux tracks may change");
}
