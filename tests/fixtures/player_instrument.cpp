// Execute the track instrument (track_instrument.cpp, linked with the track_instrument
// service) beside the player bridge (players.cpp, compiled as every build compiles it),
// with stock calls replaced by fakes. This file is compiled as a player declaring
// resources.trackInstrument compiles its own units, with OPXY_PLAYER_INSTRUMENT: its
// player_controls is the declaring player's (players.hpp). Each instrument track's kind of
// instrument, published by the GUI at each compositor frame's first slot and read into
// Controls.instrument, for a drum sampler, a drum synth, an SDK kit, a synth, a sampler, a
// sample engine and an empty track, a track changing its instrument, and tracks outside the
// eight instrument tracks; the rest of Controls is players.cpp's.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "players.hpp"
#include "screen_order.hpp"
#define require(x) do { if (!(x)) { std::fprintf(stderr,"line %d\n",__LINE__); std::abort(); } } while(0)
using namespace opxy;
u32 fake_track=0;
bool fake_shift=false;
int fake_register=0;
volatile i32 sdk_track_octaves[8]={};
extern "C" {
u32 sdk_instrument_next(platform::ScreenCursor* cursor, u32 selected) __asm__("sdk_instrument_next");
u32 sdk_screen_next(platform::ScreenCursor* cursor, u32 selected) __asm__("sdk_screen_next");
u32 sdk_player_substitute(u32 selected) __asm__("sdk_player_substitute");
}
namespace {
// The track's engine as stock's getter (8d58781c) returns it: 3 the drum sampler,
// 4 the drum synth, 40 and 41 two SDK kits, 20 a synth, 2 the keyboard sampler,
// 50 a sample engine, 33 no instrument.
const u32 kKitA=40, kKitB=41;
u32 engines[8]={3,4,kKitA,20,2,50,33,kKitB};
u32 engine_calls=0, predicate_calls=0, next_calls=0;
platform::ScreenCursor* next_cursor=nullptr;
u32 next_selected=0;
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
// One compositor frame: its first slot, then a later one; each goes on to the compositor's step.
void frame(u32 selected) {
    platform::ScreenCursor first={0,0}, later={5,0};
    const u32 before=next_calls;
    require(sdk_instrument_next(&first,selected)==7 && next_cursor==&first && next_selected==selected);
    require(sdk_instrument_next(&later,selected)==7 && next_cursor==&later && next_calls==before+2);
}
}
extern "C" {
static const platform::PlayerEntry sdk_players_rows[]={{nullptr}};
const platform::PlayerEntry* sdk_players = sdk_players_rows;
u32 sdk_player_count =0;
u32 (*sdk_screen_substitute)(u32)=nullptr;
void (*sdk_player_press)(u32)=nullptr;
void sdk_machine_tables() __asm__("sdk_machine_tables");
void sdk_machine_tables() {}
// The compositor's step (screen_order.cpp.in), which the track instrument's entry goes on to.
u32 sdk_screen_next(platform::ScreenCursor* cursor, u32 selected) {
    ++next_calls; next_cursor=cursor; next_selected=selected;
    return 7;
}
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

    // One frame publishes all eight tracks, on any page, once each, at its first slot.
    frame(10);
    require(engine_calls==8 && predicate_calls==8);
    const u32 expected[8]={1,1,1,0,0,0,0,1};
    for(u32 t=0;t<8;++t) require(kind(t)==expected[t]);
    require(engine_calls==8 && predicate_calls==8);   // reading Controls asks stock nothing
    // The rest of Controls is players.cpp's, Shift as its substitute publishes it.
    values[3]=0x12345678;
    require(on(0).value[3]==0x12345678 && on(0).lowest_key==53 && on(0).bpm_x10==1200 && on(0).shift==0);
    fake_shift=true;
    require(sdk_player_substitute(10)==0 && on(0).shift==1 && on(0).instrument==player::DrumInstrument);
    require(engine_calls==8 && predicate_calls==8);   // players.cpp asks stock nothing about it

    // A track changing its instrument: the callbacks see the old kind until the next
    // frame, the new one from it. A synth becomes a drum sampler, a kit a synth.
    engines[3]=3; engines[2]=20;
    require(kind(3)==player::OtherInstrument && kind(2)==player::DrumInstrument);
    frame(46);
    require(kind(3)==player::DrumInstrument && kind(2)==player::OtherInstrument);
    // Emptied, then a kit again.
    engines[3]=33; engines[2]=kKitB;
    frame(46);
    require(kind(3)==player::OtherInstrument && kind(2)==player::DrumInstrument);
    require(engine_calls==24 && predicate_calls==24);

    // Tracks outside the eight instrument tracks (a player slot of an aux track, a
    // corrupt index) read as another instrument, and the publication never asks
    // stock about them (engine_of requires track < 8).
    for(u32 t: {8u,9u,15u,16u,0xffffffffu}) require(kind(t)==player::OtherInstrument);
    // Every engine value the predicate does not call a drum reads as another instrument.
    for(u32 e=0;e<300;++e) {
        engines[5]=e;
        frame(10);
        require(kind(5)==((e==3 || e==4 || e==kKitA || e==kKitB) ? 1u : 0u));
    }
    std::puts("player instrument kind published per track passed");
    return 0;
}
