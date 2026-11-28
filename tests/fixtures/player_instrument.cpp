// Execute the actual player bridge (players.cpp, compiled with OPXY_PLAYER_INSTRUMENT as a
// build with an SDK 1.25 player compiles it) with stock calls replaced by fakes: each
// instrument track's kind of instrument, published by the GUI once per compositor frame and
// read into Controls.instrument by every callback's Controls, for a drum sampler, a drum
// synth, an SDK kit, a synth, a sampler, a sample engine and an empty track, a track changing
// its instrument, and tracks outside the eight instrument tracks.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "players_under_test.cpp"
#define require(x) do { if (!(x)) { std::fprintf(stderr,"line %d\n",__LINE__); std::abort(); } } while(0)
using namespace opxy;
u32 fake_track=0;
bool fake_shift=false;
int fake_register=0;
volatile i32 sdk_track_octaves[8]={};
namespace {
// The track's engine as stock's getter (8d58781c) returns it: 3 the drum sampler,
// 4 the drum synth, 40 and 41 two SDK kits, 20 a synth, 2 the keyboard sampler,
// 50 a sample engine, 33 no instrument.
const u32 kKitA=40, kKitB=41;
u32 engines[8]={3,4,kKitA,20,2,50,33,kKitB};
u32 engine_calls=0, predicate_calls=0;
u32 engine_of(u32 track) { require(track<8); ++engine_calls; return engines[track]; }
// Stock's drum predicate (8d5b79f0) as the drums service's hook leaves it: an SDK kit
// aliased to Drumline (4) first (drum_hooks.s), then 1 for engine 3 or 4.
u32 drum_predicate(u32 engine) {
    ++predicate_calls;
    if (engine==kKitA || engine==kKitB) engine=4;
    return engine==3 || engine==4 ? 1 : 0;
}
i32 values[8]={};
const i32* parameters(u32) { return values; }
u32 selected_type() { return 6; }
// A player object: State at 0x800 and the GUI-written input after it; the track
// at +4, written here rather than by the factory (whose host vtable is 8 bytes).
alignas(8) char slot[0x800+player::state_bytes+sizeof(platform::PlayerInput)];
player::Controls on(u32 track) {
    std::memcpy(slot+4,&track,4);
    return platform::player_controls(slot);
}
u32 kind(u32 track) { return on(track).instrument; }
}
extern "C" {
static const platform::PlayerEntry sdk_players_rows[]={{nullptr}};
const platform::PlayerEntry* sdk_players = sdk_players_rows;
u32 sdk_player_count =0;
u32 (*sdk_screen_substitute)(u32)=nullptr;
void (*sdk_player_press)(u32)=nullptr;
}
const void* stock_address(u32 address) {
    if(address==0x8d58781c) return reinterpret_cast<const void*>(&engine_of);
    if(address==0x8d5b79f0) return reinterpret_cast<const void*>(&drum_predicate);
    if(address==0x8d5863d8) return reinterpret_cast<const void*>(&parameters);
    require(address==0x8d57efb8); return reinterpret_cast<const void*>(&selected_type);
}
// Appended: the members before keep their offsets, and the word follows clicks.
static_assert(offsetof(player::Controls,value)==0 && offsetof(player::Controls,lowest_key)==32 &&
              offsetof(player::Controls,clicks)==108 && offsetof(player::Controls,instrument)==112 &&
              sizeof(player::Controls)==116, "Controls.instrument is appended after clicks");
static_assert(player::OtherInstrument==0 && player::DrumInstrument==1, "a zeroed Controls is another instrument");
int main() {
    // Before the GUI's first frame every track reads as another instrument, and no
    // callback asks stock anything about the track's engine.
    for(u32 t=0;t<8;++t) require(kind(t)==player::OtherInstrument);
    require(engine_calls==0 && predicate_calls==0);

    // One frame publishes all eight tracks, on any page, once each.
    require(sdk_player_substitute(10)==0 && engine_calls==8 && predicate_calls==8);
    const u32 expected[8]={1,1,1,0,0,0,0,1};
    for(u32 t=0;t<8;++t) require(kind(t)==expected[t]);
    require(engine_calls==8 && predicate_calls==8);   // reading Controls asks stock nothing
    // The rest of Controls is as before.
    values[3]=0x12345678;
    require(on(0).value[3]==0x12345678 && on(0).lowest_key==53 && on(0).bpm_x10==1200 && on(0).shift==0);

    // A track changing its instrument: the callbacks see the old kind until the next
    // frame, the new one from it. A synth becomes a drum sampler, a kit a synth.
    engines[3]=3; engines[2]=20;
    require(kind(3)==player::OtherInstrument && kind(2)==player::DrumInstrument);
    require(sdk_player_substitute(46)==0);
    require(kind(3)==player::DrumInstrument && kind(2)==player::OtherInstrument);
    // Emptied, then a kit again.
    engines[3]=33; engines[2]=kKitB;
    sdk_player_substitute(46);
    require(kind(3)==player::OtherInstrument && kind(2)==player::DrumInstrument);
    require(engine_calls==24 && predicate_calls==24);

    // Tracks outside the eight instrument tracks (a player slot of an aux track, a
    // corrupt index) read as another instrument, and the publication never asks
    // stock about them (engine_of requires track < 8).
    for(u32 t: {8u,9u,15u,16u,0xffffffffu}) require(kind(t)==player::OtherInstrument);
    // Every engine value the predicate does not call a drum reads as another instrument.
    for(u32 e=0;e<300;++e) {
        engines[5]=e;
        sdk_player_substitute(10);
        require(kind(5)==((e==3 || e==4 || e==kKitA || e==kKitB) ? 1u : 0u));
    }
    std::puts("player instrument kind published per track passed");
    return 0;
}

// The machine tables are this fixture's own (above); startup would build them on the device.
extern "C" void sdk_machine_tables() __asm__("sdk_machine_tables");
void sdk_machine_tables() {}
