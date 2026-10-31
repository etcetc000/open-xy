// Execute the actual GUI editor with stock calls replaced by recording fakes.
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "players_under_test.cpp"
#define require(x) do { if (!(x)) { std::fprintf(stderr,"line %d\n",__LINE__); std::abort(); } } while(0)
using namespace opxy;
u32 fake_track=0;
volatile i32 sdk_track_octaves[8]={};
namespace {
u32 selected=6, writes=0, continuous=0, edited=99;
i32 values[8]={0,0,0x40000000,0};
u32 selected_type() { return selected; }
const i32* parameters(u32 track) { require(track==fake_track); return values; }
void set_parameter(u32 track,u32 index,i32 value) {
    require(track==fake_track && index<4); values[index]=value; edited=index; ++writes;
}
void edit_continuous(u32 track,u32 index,const i32* event) {
    require(track==fake_track && index==2 && event); ++continuous;
}
const platform::Player* enums() {
    static const platform::Player player={reinterpret_cast<const char*>(6),"enums",nullptr,nullptr,nullptr,{0,4,13,2}};
    return &player;
}
const platform::Player* linear() {
    static const platform::Player player={reinterpret_cast<const char*>(7),"linear",nullptr,nullptr,nullptr,{0,0,0,0}};
    return &player;
}
}
extern "C" {
const platform::PlayerEntry sdk_players[]={{enums},{linear}};
const u32 sdk_player_count=2;
u32 (*sdk_screen_substitute)(u32)=nullptr;
}
const void* stock_address(u32 address) {
    if(address==0x8d57efb8) return reinterpret_cast<const void*>(&selected_type);
    if(address==0x8d5863d8) return reinterpret_cast<const void*>(&parameters);
    if(address==0x8d586474) return reinterpret_cast<const void*>(&set_parameter);
    require(address==0x8d586538); return reinterpret_cast<const void*>(&edit_continuous);
}
int main() {
    const i32 one[3]={0x7fffffff,1,100}; // huge continuous/acceleration words must not skip enum choices
    require(sdk_player_turn(2,one)==1 && edited==2);
    require(((static_cast<u32>(values[2])>>16)*13>>15)==7);
    require(sdk_player_turn(3,one)==1 && edited==3 && values[3]>0x40000000);
    // Strum is not an enum. With Spread on it must still be handled, so the
    // assembly shim skips Maestro's legacy hold-mode transition callback.
    require(sdk_player_turn(0,one)==1 && edited==0);
    require(sdk_player_turn(1,one)==1 && edited==1);
    const u32 before=writes;
    const i32 still[3]={0x7fffffff,0,100};
    require(sdk_player_turn(2,still)==1 && writes==before);
    selected=7;
    require(sdk_player_turn(2,one)==1 && continuous==1);
    require(sdk_player_turn(0,one)==1);
    for(u32 stock : {2u,3u,4u,99u}) {
        selected=stock; require(sdk_player_turn(2,one)==0);
    }
    selected=6; fake_track=8;
    require(sdk_player_turn(2,one)==0);
    fake_track=0;
    require(sdk_player_turn(4,one)==0 && sdk_player_turn(2,nullptr)==0);
    std::puts("owned player controls bypass stock hold mode; enum detents and stock fallback passed");
}
