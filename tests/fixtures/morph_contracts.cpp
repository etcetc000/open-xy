// SDK bridge contracts only; no Morph machine source or firmware inputs.
#include <opxy/morph_aux_events.hpp>
#include <opxy/morph_pad_input.hpp>
#include <opxy/morph_midi_input.hpp>
#include <opxy/morph_store_bridge.hpp>
#include <opxy/morph_hud_raw_turn.hpp>
#include <opxy/morph_hud_contract.hpp>
#include <cstdio>
#include <cstdlib>
static unsigned checks;
static void require(bool value) { ++checks; if (!value) { std::fprintf(stderr,"Morph contract %u failed\n",checks); std::exit(1); } }
int main() {
    using opxy::u32; using opxy::i32;
    opxy::platform::MorphAuxEvents events={};
    events.click(0); events.click(3); events.click(4);
    events.turn(0,0x7fffffff); events.turn(0,1); events.turn(1,(-0x7fffffff-1)); events.turn(1,-1);
    u32 clicks=0; i32 turns[4]={}; events.take(&clicks,turns);
    require(clicks==9 && turns[0]==32767 && turns[1]==-32768);
    events.take(&clicks,turns); require(clicks==0 && turns[0]==0 && turns[1]==0);
    opxy::morphpad::Input pad={};
    pad.request(true,10); require(!pad.event(15)); require(!pad.event(0));
    require(pad.owned); require(pad.event(20));
    pad.request(false,20); require(pad.event(0)); require(!pad.owned); require(!pad.event(20));
    for(u32 track=0;track<16;++track) for(u32 cc=0;cc<128;++cc)
        require(opxy::morphmidi::matches(track,cc,127)==(track==11 && cc==15));
    require(!opxy::morphmidi::matches(11,15,128));
    opxy::morphport::Session session={}; u32 blocks[8]={1,2,3,4,5,6,7,8};
    require(!session.matches(1,0,blocks)); session.begin(1,2,blocks); require(session.matches(1,2,blocks));
    require(!session.matches(2,2,blocks)); require(!session.matches(1,3,blocks));
    for(u32 i=0;i<8;++i){ ++blocks[i];require(!session.matches(1,2,blocks));--blocks[i]; }
    session.clear();require(!session.matches(1,2,blocks));
    morph_raw_turn::Sampler sampler={};u32 raw[4]={};i32 steps[4]={};
    sampler.sample(raw,false,false,steps);raw[0]=1;sampler.sample(raw,false,false,steps);require(steps[0]==0);
    raw[0]=2;sampler.sample(raw,false,false,steps);require(steps[0]==1);
    raw[0]=3;sampler.sample(raw,true,false,steps);require(steps[0]==0);
    raw[0]=4;sampler.sample(raw,true,true,steps);require(steps[0]==0);
    require(morphcapture::hud::event_valid({morphcapture::hud::SetS,7}));
    require(!morphcapture::hud::event_valid({morphcapture::hud::SetS,8}));
    std::printf("PASS: %u Morph bridge contract checks\n",checks);
}
