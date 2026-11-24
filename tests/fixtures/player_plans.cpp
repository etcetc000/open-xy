#include <player_plans.hpp>
#include <cstdio>
#include <cstdlib>
#define require(x) do { if (!(x)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)
using namespace opxy;
using namespace opxy::platform;
int main() {
    PlayerChord records[kPlayerChords] = {};
    volatile u32 held = 0;
    u32 releases = 0, released = 0;
    auto off = [&](u32 root) {
        ++releases; released=root;
        for (u32 i=0;i<held;++i) if(records[i].root==root) records[i].count=0;
    };
    player::Plan triad = {3, {{60,90,0},{64,90,4},{67,90,8}}};
    player_store(records,held,60,triad,false,false,off);
    require(held==1 && records[0].count==3 && releases==0);
    records[0].tones[0].delay=-3; // root has sounded, other tones still queued
    player::Plan extension = {2, {{60,70,0},{70,70,0}}};
    player_store(records,held,60,extension,true,true,off);
    require(records[0].count==4 && releases==0);
    require(records[0].tones[0].delay==-3 && records[0].tones[0].velocity==90);
    require(records[0].tones[1].delay==4 && records[0].tones[2].delay==8);
    require(records[0].tones[3].pitch==70 && records[0].tones[3].delay==0);
    u32 played[4]; player_activity(records,held,played);
    require(played[1]==(1u<<28) && played[2]==0); // extension dot waits for emission
    records[0].tones[3].delay=-1; player_activity(records,held,played);
    require(played[2]==(1u<<6));
    // Eight occupied root records must still allow replacements and additions.
    for(u32 n=61;n<68;++n) player_store(records,held,n,triad,false,false,off);
    require(held==8);
    player::Plan minor = {3,{{60,90,0},{63,90,0},{67,90,0}}};
    player_store(records,held,60,minor,true,false,off);
    require(held==8 && releases==1 && released==60 && records[0].count==3);
    require(records[0].tones[1].pitch==63 && records[1].tones[1].pitch==64);
    for(u32 i=0;i<100;++i) player_store(records,held,60,triad,true,false,off);
    require(held==8 && records[0].count==3 && releases==101);
    player_store(records,held,68,triad,false,false,off); require(held==8);
    off(61); player_store(records,held,68,triad,false,false,off);
    require(held==8 && records[1].root==68 && records[1].count==3);
    off(60);
    const u32 after_release=releases;
    player_store(records,held,60,triad,true,false,off);
    require(records[0].count==0 && releases==after_release); // stale update cannot resurrect a root
    player::Plan bad=triad; bad.count=9;
    player_store(records,held,68,bad,true,false,off);
    require(releases==after_release && records[1].count==3);
    bad=triad; bad.tones[0].pitch=255;
    player_store(records,held,68,bad,true,false,off);
    require(releases==after_release && records[1].count==3);
    player::Plan full = {8,{{0,90,0},{1,90,0},{2,90,0},{3,90,0},{4,90,0},{5,90,0},{6,90,0},{7,90,0}}};
    player_store(records,held,68,full,true,true,off);
    require(records[1].count==8 && records[1].tones[7].pitch==4);
    player::Plan empty={}; player_store(records,held,68,empty,true,false,off);
    require(records[1].count==0);
    std::puts("live chord edits preserve attacks, queues, root ownership and fixed capacity");
}
