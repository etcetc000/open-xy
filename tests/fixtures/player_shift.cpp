// Execute the actual player bridge's Shift layer (players.cpp) with stock
// calls replaced by fakes: shift-row turns, Shift turn sums, the published
// Shift flag and encoder clicks, all beside State in the player object.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "players_under_test.cpp"
#define require(x) do { if (!(x)) { std::fprintf(stderr,"line %d\n",__LINE__); std::abort(); } } while(0)
using namespace opxy;
u32 fake_track=0;
bool fake_shift=false;
int fake_register=0;
volatile i32 sdk_track_octaves[8]={};
namespace {
u32 selected=6, writes=0;
i32 values[8]={};
alignas(8) char slot[0x1100];
i32 centre(u32 choice, u32 choices) { const u32 w=0x7fffffffu/choices; return static_cast<i32>(choice*w+w/2); }
u32 selected_type() { return selected; }
// player_controls reads the track at object+4, which the 64-bit host vtable overlaps.
const i32* parameters(u32) { return values; }
void set_parameter(u32 track,u32 index,i32 value) { require(track==fake_track && index<4); values[index]=value; ++writes; }
void edit_continuous(u32,u32,const i32*) {}
void* factory(u32 track,u32 type,u32 flag) { require(track==0 && type==4 && flag==0); return slot; }
const void* const shifted_vtable[16]={};
const void* const plain_vtable[16]={};
const platform::PlayerShift row={2,{3,7,0,0},{centre(0,3),centre(3,7),0,0}};
const platform::Player* shifted() {
    static const platform::Player player={reinterpret_cast<const char*>(6),"shifted",shifted_vtable,nullptr,nullptr,{4,0,0,0},&row};
    return &player;
}
const platform::Player* plain() {
    static const platform::Player player={reinterpret_cast<const char*>(7),"plain",plain_vtable,nullptr,nullptr,{4,0,0,0}};
    return &player;
}
// A player whose second main slot is absent, with a four-control shift row, built
// over bytes of 1: whatever follows steps[] (padding here, the shift pointer on the
// device) reads as an absent slot to code that indexes steps[] past its four entries.
const void* const gapped_vtable[16]={};
const platform::PlayerShift row4={4,{3,3,3,3},{centre(0,3),centre(0,3),centre(0,3),centre(0,3)}};
alignas(platform::Player) unsigned char gapped_bytes[sizeof(platform::Player)];
const platform::Player* gapped() {
    static platform::Player* player=nullptr;
    if(!player) {
        std::memset(gapped_bytes,1,sizeof gapped_bytes);
        player=new(gapped_bytes) platform::Player;  // members assigned below; the rest stays 1
        player->type_id=reinterpret_cast<const char*>(8); player->name="gapped"; player->vtable=gapped_vtable;
        player->page=nullptr; player->page_ready=nullptr; player->shift=&row4;
        player->steps[0]=4; player->steps[1]=1; player->steps[2]=0; player->steps[3]=0;
    }
    return player;
}
}
extern "C" {
const platform::PlayerEntry sdk_players[]={{shifted},{plain},{gapped}};
const u32 sdk_player_count=3;
u32 (*sdk_screen_substitute)(u32)=nullptr;
void (*sdk_player_press)(u32)=nullptr;
}
const void* stock_address(u32 address) {
    if(address==0x8d57efb8) return reinterpret_cast<const void*>(&selected_type);
    if(address==0x8d5863d8) return reinterpret_cast<const void*>(&parameters);
    if(address==0x8d586474) return reinterpret_cast<const void*>(&set_parameter);
    if(address==0x8d57ee6c) return reinterpret_cast<const void*>(&factory);
    require(address==0x8d586538); return reinterpret_cast<const void*>(&edit_continuous);
}
int main() {
    // The stock table of each track's player object, at its OS 1.2.1 address.
    const __UINTPTR_TYPE__ page=0x84ef0000;  // 64 KiB aligned for VirtualAlloc
#if defined(_WIN32)
    void* table=VirtualAlloc(reinterpret_cast<void*>(page),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
    void* table=mmap(reinterpret_cast<void*>(page),0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    if(table==MAP_FAILED) table=nullptr;
#endif
    if(table!=reinterpret_cast<void*>(page)) { std::puts("SKIP: the player table address is not mappable here"); return 77; }
    *reinterpret_cast<void**>(page+0xc768)=slot;
    const volatile platform::PlayerInput* in=platform::player_input(slot);

    // Built: State zeroed, the shift row at its defaults, counters zero.
    slot[0x900]=1;
    require(sdk_player_build(0,6,0)==slot && slot[0x900]==0);
    require(in->shift_value[0]==centre(0,3) && in->shift_value[1]==centre(3,7) && in->shift_value[2]==0);
    require(in->shift_turns[1]==0 && in->clicks[2]==0);

    // Shift is published once per compositor frame.
    fake_shift=true;
    require(sdk_player_substitute(46)==0 && platform::player_controls(slot).shift==1);
    fake_shift=false;
    require(sdk_player_substitute(46)==0 && platform::player_controls(slot).shift==0);

    // Shift + turn edits the shift control and sums detents; the four words stay.
    fake_shift=true;
    const i32 two[3]={0x7fffffff,2,100}, back[3]={0,-1,0}, still[3]={0x7fffffff,0,100};
    require(sdk_player_turn(1,two)==1 && writes==0);
    require(player::choice(in->shift_value[1],7)==5 && in->shift_turns[1]==2);
    // Should stock pass Shift's row as 4..7, the same encoder takes it.
    require(sdk_player_turn(5,back)==1 && player::choice(in->shift_value[1],7)==4 && in->shift_turns[1]==1);
    // Beyond the row: counted only. A motion without detents changes nothing.
    require(sdk_player_turn(3,two)==1 && in->shift_turns[3]==2 && writes==0);
    require(sdk_player_turn(0,still)==1 && in->shift_value[0]==centre(0,3) && in->shift_turns[0]==0);
    // Clamped at the row's ends.
    for(int i=0;i<5;++i) sdk_player_turn(0,two);
    require(player::choice(in->shift_value[0],3)==2 && in->shift_turns[0]==10);
    const player::Controls c=platform::player_controls(slot);
    require(c.shift_value[0]==in->shift_value[0] && c.shift_value[1]==in->shift_value[1] &&
            c.shift_turns[3]==2 && c.shift_turns[0]==10);

    // Without Shift, turns edit the four controls as before.
    fake_shift=false;
    require(sdk_player_turn(0,two)==1 && writes==1 && player::choice(values[0],4)==2);
    require(sdk_player_turn(4,two)==0);

    // Encoder clicks count while the Maestro page (which the player's replaces) is selected.
    require(sdk_player_substitute(46)==0);
    sdk_player_click(2); sdk_player_click(2); sdk_player_click(4);
    require(in->clicks[2]==2 && platform::player_controls(slot).clicks[2]==2);
    require(sdk_player_substitute(10)==0);
    sdk_player_click(2);
    require(in->clicks[2]==2);

    // Every player's page registration installs the frame and click hooks, whether
    // or not stock placed the page.
    platform::Record record; platform::Screen screen;
    fake_register=0;
    require(platform::register_player_page(&record,&screen)==0);
    require(sdk_screen_substitute==&sdk_player_substitute && sdk_player_press==&sdk_player_click);

    // An absent main slot leaves Shift on its encoder working, and nothing reads
    // past the four steps for Shift's row (4..7).
    selected=8;
    require(sdk_player_build(0,8,0)==slot);
    fake_shift=true;
    require(sdk_player_turn(1,two)==1 && in->shift_turns[1]==2 && player::choice(in->shift_value[1],3)==2);
    require(sdk_player_turn(5,back)==1 && in->shift_turns[1]==1 && player::choice(in->shift_value[1],3)==1);
    require(sdk_player_turn(6,two)==1 && in->shift_turns[2]==2);
    fake_shift=false;
    const u32 before=writes;
    require(sdk_player_turn(1,two)==1 && writes==before);  // absent: the encoder turns nothing
    require(sdk_player_turn(5,two)==0);

    // A player without a shift row keeps today's turns, even with Shift held.
    selected=7;
    require(sdk_player_build(0,7,0)==slot && in->shift_value[0]==0 && in->clicks[2]==0);
    fake_shift=true;
    require(sdk_player_turn(1,two)==1 && writes==2 && values[1]!=0);
    require(sdk_player_turn(5,two)==0 && in->shift_turns[1]==0);
    std::puts("player shift row, Shift turn sums, published Shift and clicks passed");
    return 0;
}
