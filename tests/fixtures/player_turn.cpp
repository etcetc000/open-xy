// Execute the actual GUI editor with stock calls replaced by recording fakes.
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "players_under_test.cpp"
#define require(x) do { if (!(x)) { std::fprintf(stderr,"line %d\n",__LINE__); std::abort(); } } while(0)
using namespace opxy;
u32 fake_track=0;
int fake_register=1;
volatile i32 sdk_track_octaves[8]={};
namespace {
u32 selected=6, writes=0, continuous=0, edited=99;
// The stock object, State at 0x800 and the GUI-written input after it: a build
// writes the input, so a smaller slot would let it run into the variables after.
alignas(void*) char slot[0x800+player::state_bytes+sizeof(platform::PlayerInput)];
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
// Whether stock placed the paged player's own page.
int placed=1;
int page_ready() { return placed; }
// Its second page: an enum of three, a continuous control, an absent slot, a continuous control.
const platform::Player* pages() {
    static const platform::Player player={reinterpret_cast<const char*>(9),"pages",nullptr,nullptr,&page_ready,
                                          {0,4,13,2,3,0,1,0},true};
    return &player;
}
void* build(u32 track,u32 type,u32 flag) { require(track==fake_track && type==4 && !flag); return slot; }
}
extern "C" {
static const platform::PlayerEntry sdk_players_rows[]={{enums},{linear},{absent},{pages}};
const platform::PlayerEntry* sdk_players = sdk_players_rows;
u32 sdk_player_count =4;
u32 (*sdk_screen_substitute)(u32)=nullptr;
void (*sdk_player_press)(u32)=nullptr;
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
    // A press reaches the service whether or not stock placed the page, and switches
    // only a paged player's page. Maestro keeps its encoder 4 click otherwise.
    fake_register=0;
    require(!sdk_player_press && platform::register_player_page(nullptr,nullptr)==0 && sdk_player_press);
    sdk_player_press=nullptr; fake_register=1;
    require(platform::register_player_page(nullptr,nullptr)==1 && sdk_player_press);
    sdk_player_press(0);
    require(platform::player_row(0)==0 && sdk_player_click()==0);
    selected=9;
    sdk_player_press(0);
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
    sdk_player_press(0);
    require(platform::player_row(0)==0 && sdk_player_turn(1,one)==1 && edited==1);
    // A player is built on its first page.
    sdk_player_press(0);
    require(platform::player_row(0)==4 && sdk_player_build(0,9,1)==slot && platform::player_row(0)==0);
    // Only the eight instrument tracks have a page to switch.
    fake_track=8;
    sdk_player_press(0);
    require(platform::player_row(8)==0 && sdk_player_click()==0);
    fake_track=0;
    // A paged player whose page stock did not place keeps stock's Maestro page,
    // which shows the first four: a press switches nothing, encoder 4's click
    // stays stock's and the encoders edit the first four, even on the second row.
    selected=9; placed=0;
    sdk_player_press(0);
    require(platform::player_row(0)==0 && sdk_player_click()==0);
    require(sdk_player_turn(0,one)==1 && edited==0);
    placed=1; sdk_player_press(0);
    require(platform::player_row(0)==4 && sdk_player_click()==1);
    placed=0;
    require(sdk_player_click()==0 && sdk_player_turn(1,one)==1 && edited==1);
    placed=1;
    require(sdk_player_turn(1,one)==1 && edited==5);
    sdk_player_press(0);
    require(platform::player_row(0)==0);
    // Every parameter word reaches the player's callbacks, the second page's too:
    // player_controls hands press, update and display all eight.
    fake_track=0;
    for(u32 i=0;i<8;++i) values[i]=static_cast<i32>(0x01010101u*(i+1));
    const player::Controls delivered=platform::player_controls(slot);
    for(u32 i=0;i<8;++i) require(delivered.value[i]==values[i]);
    std::puts("owned player controls bypass stock hold mode; enum detents and stock fallback passed");
}

// The machine tables are this fixture's own (above); startup would build them on the device.
extern "C" void sdk_machine_tables() __asm__("sdk_machine_tables");
void sdk_machine_tables() {}
