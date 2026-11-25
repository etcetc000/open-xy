// Execute the actual player bridge's Shift layer (players.cpp) with stock
// calls replaced by fakes: shift-row turns, Shift turn sums, the published
// Shift flag and encoder clicks, all beside State in the player object, and
// how they meet a paged player's two pages.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>
#include <type_traits>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "players_under_test.cpp"
#define require(x) do { if (!(x)) { std::fprintf(stderr,"line %d\n",__LINE__); std::abort(); } } while(0)
using namespace opxy;
// The encoder 4 click hook calls sdk_player_click from assembly and takes a
// nonzero R0 as "skip stock's toggle": it must be the function that answers
// that, not a click counter of the same name. The press dispatch is separate.
static_assert(std::is_same<decltype(&sdk_player_click), u32 (*)()>::value, "sdk_player_click is the click hook's query");
static_assert(std::is_same<decltype(sdk_player_press), void (*)(u32)>::value, "the press dispatch takes the encoder");
u32 fake_track=0;
bool fake_shift=false;
u32 fake_bpm=1200;
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
void set_parameter(u32 track,u32 index,i32 value) { require(track==fake_track && index<8); values[index]=value; ++writes; }
void edit_continuous(u32,u32,const i32*) {}
void* factory(u32 track,u32 type,u32 flag) { require(track==0 && type==4 && flag==0); return slot; }
int placed=1;
int page_ready() { return placed; }
const void* const shifted_vtable[16]={};
const void* const plain_vtable[16]={};
const platform::PlayerShift row={2,{3,7,0,0},{centre(0,3),centre(3,7),0,0}};
const platform::Player* shifted() {
    static const platform::Player player={reinterpret_cast<const char*>(6),"shifted",shifted_vtable,nullptr,nullptr,{4,0,0,0},false,false,&row};
    return &player;
}
const platform::Player* plain() {
    static const platform::Player player={reinterpret_cast<const char*>(7),"plain",plain_vtable,nullptr,nullptr,{4,0,0,0}};
    return &player;
}
// A player whose second main slot is absent, with a four-control shift row, built
// over bytes of 1: whatever follows steps[] reads as an absent slot to code that
// indexes steps[] past its entries.
const void* const gapped_vtable[16]={};
const platform::PlayerShift row4={4,{3,3,3,3},{centre(0,3),centre(0,3),centre(0,3),centre(0,3)}};
alignas(platform::Player) unsigned char gapped_bytes[sizeof(platform::Player)];
const platform::Player* gapped() {
    static platform::Player* player=nullptr;
    if(!player) {
        std::memset(gapped_bytes,1,sizeof gapped_bytes);
        player=new(gapped_bytes) platform::Player;  // members assigned below; the rest stays 1
        player->type_id=reinterpret_cast<const char*>(8); player->name="gapped"; player->vtable=gapped_vtable;
        player->page=nullptr; player->page_ready=nullptr; player->shift=&row4; player->paged=false;
        player->steps[0]=4; player->steps[1]=1; player->steps[2]=0; player->steps[3]=0;
    }
    return player;
}
// A paged player with a row of eight: entries 0..3 under the first page, 4..7 under the second.
const void* const paged_vtable[16]={};
const platform::PlayerShift row8={8,{2,3,4,5,6,7,8,9},
    {centre(0,2),centre(0,3),centre(0,4),centre(0,5),centre(5,6),centre(0,7),centre(0,8),centre(8,9)}};
const platform::Player* paged8() {
    static const platform::Player player={reinterpret_cast<const char*>(9),"paged8",paged_vtable,nullptr,&page_ready,
                                          {0,0,0,0,0,0,0,0},true,false,&row8};
    return &player;
}
// A player of one page whose presses are its own clicks ("press": "click"): four
// enums, the fourth a two-choice switch it toggles on encoder 4's click, and a shift row.
const void* const clicker_vtable[16]={};
const platform::PlayerShift row2={2,{3,7,0,0},{centre(0,3),centre(3,7),0,0}};
const platform::Player* clicker() {
    static const platform::Player player={reinterpret_cast<const char*>(10),"clicker",clicker_vtable,nullptr,nullptr,
                                          {7,14,29,2},false,true,&row2};
    return &player;
}
}
extern "C" {
static const platform::PlayerEntry sdk_players_rows[]={{shifted},{plain},{gapped},{paged8},{clicker}};
const platform::PlayerEntry* sdk_players = sdk_players_rows;
u32 sdk_player_count =5;
u32 (*sdk_screen_substitute)(u32)=nullptr;
void (*sdk_player_press)(u32)=nullptr;
}
// Stock's Maestro key handler on an encoder 4 click (OS 1.2.1, 8c084e10..8c084e40):
// unless the click hook answers nonzero, its hold-mode query (parameter 3's word
// at or above 2^30) picks the opposite end, which it stores as parameter 3.
void stock_encoder_4() {
    if(!sdk_player_click()) set_parameter(fake_track,3,values[3]>=0x40000000?0:0x7fffffff);
}
// One click of encoder k on the player page as the GUI delivers it: the press
// dispatch (sdk_synth_press) first, then the top controller's key handler, which
// on encoder 4 is Maestro's above.
void click(u32 k) { sdk_player_press(k); if(k==3) stock_encoder_4(); }
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

    // Controls carry the audio clock's tempo word as it reads at each call.
    require(platform::player_controls(slot).bpm_x10==1200);
    fake_bpm=0;
    require(platform::player_controls(slot).bpm_x10==0);
    fake_bpm=1755;
    require(platform::player_controls(slot).bpm_x10==1755);
    fake_bpm=1200;

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
    // A player of one page never reaches the second half of the row.
    for(u32 i=4;i<8;++i) require(in->shift_value[i]==0 && in->shift_turns[i]==0 && c.shift_turns[i]==0);

    // Without Shift, turns edit the four controls as before.
    fake_shift=false;
    require(sdk_player_turn(0,two)==1 && writes==1 && player::choice(values[0],4)==2);
    require(sdk_player_turn(4,two)==0);

    // Every player's page registration installs the frame publication and the
    // press dispatch, whether or not stock placed the page.
    platform::Record record; platform::Screen screen;
    fake_register=0;
    require(platform::register_player_page(&record,&screen)==0);
    require(sdk_screen_substitute==&sdk_player_substitute && sdk_player_press);

    // Encoder clicks of a player of one page count while the Maestro page (which
    // the player's replaces) is selected; the encoder 4 click hook leaves them,
    // and stock's toggle, alone.
    require(sdk_player_substitute(46)==0);
    sdk_player_press(2); sdk_player_press(2); sdk_player_press(4);
    require(in->clicks[2]==2 && platform::player_controls(slot).clicks[2]==2);
    require(sdk_player_click()==0 && in->clicks[3]==0 && in->clicks[2]==2);
    sdk_player_press(3);
    require(in->clicks[3]==1);
    // The count wraps: 255 more clicks bring it to 0, the next to 1.
    for(int i=0;i<255;++i) sdk_player_press(3);
    require(in->clicks[3]==0 && platform::player_controls(slot).clicks[3]==0);
    sdk_player_press(3);
    require(in->clicks[3]==1 && platform::player_controls(slot).clicks[3]==1);
    require(sdk_player_substitute(10)==0);
    sdk_player_press(2);
    require(in->clicks[2]==2);
    // Without "press": "click", encoder 4's click is counted and stock still
    // toggles the fourth control, as without the SDK.
    require(sdk_player_substitute(46)==0);
    values[3]=0;
    click(3);
    require(in->clicks[3]==2 && values[3]==0x7fffffff);
    click(3);
    require(in->clicks[3]==3 && values[3]==0);

    // With "press": "click", each click of encoders 1..4 is counted once and
    // nothing else: stock's toggle no longer flips the switch the player toggles.
    selected=10;
    require(sdk_player_build(0,10,0)==slot && in->clicks[3]==0);
    const u32 clicker_writes=writes;
    values[3]=centre(0,2);
    for(u32 k=0;k<4;++k) click(k);
    require(in->clicks[0]==1 && in->clicks[1]==1 && in->clicks[2]==1 && in->clicks[3]==1);
    require(values[3]==centre(0,2) && writes==clicker_writes && sdk_player_click()==1);
    click(3); click(3);
    require(in->clicks[3]==3 && values[3]==centre(0,2) && platform::player_controls(slot).clicks[3]==3);
    // Turning encoder 4 still edits the switch.
    const i32 one[3]={0,1,0};
    require(sdk_player_turn(3,one)==1 && player::choice(values[3],2)==1);
    selected=6;
    require(sdk_player_build(0,6,0)==slot);

    // An absent main slot leaves Shift on its encoder working, and nothing reads
    // past the steps for Shift's row (4..7).
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
    require(sdk_player_turn(1,two)==1 && writes==before+1 && values[1]!=0);
    require(sdk_player_turn(5,two)==0 && in->shift_turns[1]==0);
    fake_shift=false;

    // A paged player: its row's entry row + k sits under encoder k of the page shown.
    selected=9;
    require(sdk_player_build(0,9,0)==slot && platform::player_row(0)==0);
    require(in->shift_value[4]==centre(5,6) && in->shift_value[7]==centre(8,9) && in->shift_turns[7]==0);
    require(sdk_player_substitute(46)==0);
    fake_shift=true;
    const u32 paged_writes=writes;
    require(sdk_player_turn(1,two)==1 && player::choice(in->shift_value[1],3)==2 && in->shift_turns[1]==2);
    require(in->shift_value[5]==centre(0,7) && in->shift_turns[5]==0 && writes==paged_writes);
    fake_shift=false;
    // Its press switches the page and is taken: no click is counted, and the
    // encoder 4 click hook skips stock's toggle.
    sdk_player_press(1);
    require(platform::player_row(0)==4 && in->clicks[1]==0 && sdk_player_click()==1);
    sdk_player_press(3);
    require(platform::player_row(0)==0 && in->clicks[3]==0);
    sdk_player_press(3);
    require(platform::player_row(0)==4);
    // On the second page Shift + encoder k turns entry 4 + k.
    fake_shift=true;
    require(sdk_player_turn(1,two)==1 && player::choice(in->shift_value[5],7)==2 && in->shift_turns[5]==2);
    require(sdk_player_turn(5,back)==1 && player::choice(in->shift_value[5],7)==1 && in->shift_turns[5]==1);
    require(sdk_player_turn(0,back)==1 && player::choice(in->shift_value[4],6)==4 && in->shift_turns[4]==-1);
    require(player::choice(in->shift_value[1],3)==2 && in->shift_turns[1]==2 && writes==paged_writes);
    const player::Controls p=platform::player_controls(slot);
    for(u32 i=0;i<8;++i) require(p.shift_value[i]==in->shift_value[i] && p.shift_turns[i]==in->shift_turns[i]);
    require(p.clicks[0]==0 && p.clicks[1]==0 && p.clicks[2]==0 && p.clicks[3]==0);
    fake_shift=false;
    // Its page not placed: one page, so a press is a click and Shift's row stays on 0..3.
    placed=0;
    require(sdk_player_click()==0);
    fake_shift=true;
    require(sdk_player_turn(1,two)==1 && player::choice(in->shift_value[1],3)==2 && in->shift_turns[1]==4);
    fake_shift=false;
    sdk_player_press(1);
    require(in->clicks[1]==1 && platform::player_row(0)==4);
    placed=1;
    std::puts("player shift row, Shift turn sums, published Shift, clicks and pages passed");
    return 0;
}

// The machine tables are this fixture's own (above); startup would build them on the device.
extern "C" void sdk_machine_tables() __asm__("sdk_machine_tables");
void sdk_machine_tables() {}
