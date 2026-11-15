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
alignas(void*) char slot[0x800+player::state_bytes];
i32 values[8]={0,0,0x40000000,0};
u32 selected_type() { return selected; }
const i32* parameters(u32 track) { require(track==fake_track); return values; }
void set_parameter(u32 track,u32 index,i32 value) {
    require(track==fake_track && index<8); values[index]=value; edited=index; ++writes;
}
void edit_continuous(u32 track,u32 index,const i32* event) {
    require(track==fake_track && (index==2 || (index>=4 && index<8)) && event); edited=index; ++continuous;
}
const platform::Player* enums() {
    static const platform::Player player={reinterpret_cast<const char*>(6),"enums",nullptr,nullptr,nullptr,{0,4,13,2}};
    return &player;
}
const platform::Player* linear() {
    static const platform::Player player={reinterpret_cast<const char*>(7),"linear",nullptr,nullptr,nullptr,{0,0,0,0}};
    return &player;
}
const platform::Player* absent() {
    static const platform::Player player={reinterpret_cast<const char*>(8),"absent",nullptr,nullptr,nullptr,{0,4,13,1}};
    return &player;
}
// Its second page: an enum of three, a continuous control, an absent slot, a continuous control.
const platform::Player* pages() {
    static const platform::Player player={reinterpret_cast<const char*>(9),"pages",nullptr,nullptr,nullptr,
                                          {0,4,13,2,3,0,1,0},true};
    return &player;
}
void* build(u32 track,u32 type,u32 flag) { require(track==fake_track && type==4 && !flag); return slot; }
}
extern "C" {
const platform::PlayerEntry sdk_players[]={{enums},{linear},{absent},{pages}};
const u32 sdk_player_count=4;
u32 (*sdk_screen_substitute)(u32)=nullptr;
void (*sdk_player_press)()=nullptr;
}
const void* stock_address(u32 address) {
    if(address==0x8d57efb8) return reinterpret_cast<const void*>(&selected_type);
    if(address==0x8d5863d8) return reinterpret_cast<const void*>(&parameters);
    if(address==0x8d586474) return reinterpret_cast<const void*>(&set_parameter);
    if(address==0x8d57ee6c) return reinterpret_cast<const void*>(&build);
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
    // An absent slot is handled and edits nothing; the other slots still edit.
    selected=8;
    const u32 absent_writes=writes;
    require(sdk_player_turn(3,one)==1 && writes==absent_writes);
    require(sdk_player_turn(2,one)==1 && writes==absent_writes+1);
    selected=6;
    require(sdk_player_turn(4,one)==0 && sdk_player_turn(2,nullptr)==0);
    // A press reaches the service once a player page is in place, and switches
    // only a paged player's page. Maestro keeps its encoder 4 click otherwise.
    require(!sdk_player_press && platform::register_player_page(nullptr,nullptr)==1 && sdk_player_press);
    sdk_player_press();
    require(platform::player_row(0)==0 && sdk_player_click()==0);
    selected=9;
    sdk_player_press();
    require(platform::player_row(0)==4 && sdk_player_click()==1);
    // The second page edits parameters 4..7: enums by detent, the rest continuously.
    require(sdk_player_turn(0,one)==1 && edited==4 && ((static_cast<u32>(values[4])>>16)*3>>15)==1);
    const u32 smooth=continuous, paged_writes=writes;
    require(sdk_player_turn(1,one)==1 && edited==5 && continuous==smooth+1);
    require(sdk_player_turn(2,one)==1 && edited==5);
    require(sdk_player_turn(3,one)==1 && edited==7 && continuous==smooth+2 && writes==paged_writes);
    // Another player on the track still edits its own four.
    selected=6;
    require(sdk_player_turn(2,one)==1 && edited==2);
    selected=9;
    sdk_player_press();
    require(platform::player_row(0)==0 && sdk_player_turn(1,one)==1 && edited==1);
    // A player is built on its first page.
    sdk_player_press();
    require(platform::player_row(0)==4 && sdk_player_build(0,9,1)==slot && platform::player_row(0)==0);
    // Only the eight instrument tracks have a page to switch.
    fake_track=8;
    sdk_player_press();
    require(platform::player_row(8)==0 && sdk_player_click()==0);
    std::puts("owned player controls bypass stock hold mode; enum detents and stock fallback passed");
}
