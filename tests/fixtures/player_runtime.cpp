// Execute the actual player bridge (players.cpp) against stock fakes: the step
// count, transport and held keys that let a player generate its own notes.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "players_under_test.cpp"
#define require(x) do { if (!(x)) { std::fprintf(stderr,"line %d\n",__LINE__); std::abort(); } } while(0)
using namespace opxy;
u32 fake_track=0;
volatile i32 sdk_track_octaves[8]={0,0,0,0,0,0,0,0};
Transport fake_transport={1200,960,true};
namespace {
alignas(16) unsigned char slot[0x10000];
const void* const owned_vtable[16]={};
i32 values[8]={1,2,3,4,5,6,7,8};
u32 offs=0, off_note=0;
platform::PlayerChord* records() { return reinterpret_cast<platform::PlayerChord*>(slot+0x10c); }
u32 held() { return *reinterpret_cast<u32*>(slot+0x34c); }
u32 sounding(u32 root) {
    for (u32 i=0;i<held();++i) if (records()[i].count && records()[i].root==root) return records()[i].count;
    return 0;
}
void* factory(u32 track,u32 type,u32) {
    require(type==4);
    std::memset(slot,0xa5,sizeof slot);  // stock leaves the SDK's area unspecified
    std::memset(slot,0,0x480);
    *reinterpret_cast<u32*>(slot+4)=track;
    return slot;
}
// On a 64-bit host the method-table pointer covers the track word at +4.
const i32* parameters(u32) { return values; }
// Stock's root release: note-off for the root's tones, then its record is free.
void release(void* object,u32 note) {
    require(object==slot); ++offs; off_note=note;
    for (u32 i=0;i<held();++i) if (records()[i].root==note) records()[i].count=0;
}
const platform::Player* probe() {
    static const platform::Player player={reinterpret_cast<const char*>(6),"probe",owned_vtable,nullptr,nullptr,{0,0,0,0}};
    return &player;
}
player::Update change(u32 root,player::UpdateMode mode,u32 count,u8 pitch) {
    player::Update u={};
    u.root=root; u.mode=mode; u.plan.count=count;
    for (u32 i=0;i<count;++i) u.plan.tones[i]={static_cast<u8>(pitch+i),90,0};
    return u;
}
}
extern "C" {
const platform::PlayerEntry sdk_players[]={{probe}};
const u32 sdk_player_count=1;
u32 (*sdk_screen_substitute)(u32)=nullptr;
}
const void* stock_address(u32 address) {
    if(address==0x8d57ee6c) return reinterpret_cast<const void*>(&factory);
    if(address==0x8d5863d8) return reinterpret_cast<const void*>(&parameters);
    require(address==platform::kMaestroRelease); return reinterpret_cast<const void*>(&release);
}
int main() {
    void* object=sdk_player_build(0,6,1);
    require(object==slot && *static_cast<const void* const* const*>(object)==owned_vtable);
    // A new player starts with no steps counted and no keys held.
    player::Controls c=platform::player_controls(object);
    require(c.steps==0 && c.value[0]==1 && c.value[3]==4 && c.lowest_key==platform::kLowestKey);
    require(c.transport.bpm_x10==1200 && c.transport.ticks96==960 && c.transport.playing);
    require(!platform::player_holds(object));
    for (u32 n=0;n<128;++n) platform::update_plan(object,change(n,player::Add,1,60));
    require(held()==0);

    // Steps move the count; presses, releases and updates never do.
    for (u32 i=0;i<3;++i) platform::player_step(object);
    platform::play_plan(object,60,change(60,player::Replace,0,0).plan);
    platform::player_release(object,60);
    require(platform::player_controls(object).steps==3);
    fake_transport.playing=false;
    require(!platform::player_controls(object).transport.playing);

    // A press taken silently holds its key: a later update may sound it.
    platform::play_plan(object,48,change(48,player::Replace,0,0).plan);
    require(held()==0 && offs==0 && platform::player_holds(object));
    platform::update_plan(object,change(48,player::Add,1,55));
    require(sounding(48)==1 && offs==0);
    // An empty Replace silences the root and keeps its key; a rest, not an end.
    platform::update_plan(object,change(48,player::Replace,0,0));
    require(sounding(48)==0 && offs==1 && off_note==48);
    platform::update_plan(object,change(48,player::Replace,2,57));
    require(sounding(48)==2 && offs==1);
    platform::update_plan(object,change(48,player::Replace,1,59));
    require(sounding(48)==1 && offs==2);

    // Once the key is up nothing starts it again, as before.
    release(object,48); platform::player_release(object,48);
    require(!platform::player_holds(object));
    const u32 after=offs;
    platform::update_plan(object,change(48,player::Replace,1,60));
    platform::update_plan(object,change(48,player::Add,1,60));
    require(sounding(48)==0 && offs==after);

    // Reset and all-notes-off release every held key.
    platform::play_plan(object,50,change(50,player::Replace,0,0).plan);
    platform::play_plan(object,52,change(52,player::Replace,1,52).plan);
    require(sounding(52)==1);
    require(platform::player_holds(object));
    platform::player_reset(object);
    require(!platform::player_holds(object));
    platform::update_plan(object,change(50,player::Add,1,62));
    require(sounding(50)==0);
    // A sounding record still takes changes until stock releases it.
    platform::update_plan(object,change(52,player::Add,1,64));
    require(sounding(52)==2);

    // Held silent roots still respect the eight records stock has.
    platform::player_reset(object);
    *reinterpret_cast<u32*>(slot+0x34c)=0;  // stock's reset empties its records
    for (u32 n=70;n<80;++n) platform::play_plan(object,n,change(n,player::Replace,0,0).plan);
    for (u32 n=70;n<80;++n) platform::update_plan(object,change(n,player::Add,1,40));
    require(held()==8 && sounding(77)==1 && sounding(78)==0 && sounding(79)==0);
    // An empty Replace frees a record for another held key at capacity.
    platform::update_plan(object,change(70,player::Replace,0,0));
    platform::update_plan(object,change(78,player::Add,1,40));
    require(held()==8 && sounding(70)==0 && sounding(78)==1);

    // Keys outside MIDI are never held.
    platform::play_plan(object,200,change(200,player::Replace,0,0).plan);
    platform::player_release(object,200);
    require(platform::player_holds(object));  // 70..79 are still down
    for (u32 n=70;n<80;++n) platform::player_release(object,n);
    require(!platform::player_holds(object));
    std::puts("player steps, transport and held silent roots follow the bridge contract");
}
