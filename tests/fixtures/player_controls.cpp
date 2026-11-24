#include <cstdio>
#include <cstdlib>
#include "player_controls.hpp"
#define require(x) do { if (!(x)) { std::fprintf(stderr,"line %d\n",__LINE__); std::abort(); } } while(0)
using namespace opxy;
u32 bucket(i32 value, u32 choices) { return ((static_cast<u32>(value) >> 16) * choices) >> 15; }
int main() {
    using platform::player_enum_turn;
    // Round-trip every stored centre, in both directions, at all enum sizes.
    for (u32 choices=2; choices<256; ++choices) {
        i32 value=0;
        for(u32 at=1; at<choices; ++at) {
            value=player_enum_turn(value,choices,1); require(bucket(value,choices)==at);
        }
        require(bucket(player_enum_turn(value,choices,1),choices)==choices-1);
        for(u32 at=choices-1; at>0; --at) {
            value=player_enum_turn(value,choices,-1); require(bucket(value,choices)==at-1);
        }
        require(bucket(player_enum_turn(value,choices,-1),choices)==0);
        require(bucket(player_enum_turn(0x40000000,choices,0x7fffffff),choices)==choices-1);
        require(bucket(player_enum_turn(0x40000000,choices,static_cast<i32>(0x80000000u)),choices)==0);
    }
    require(player_enum_turn(0x40000000,13,0)==0x40000000);
    require(bucket(player_enum_turn(0x40000000,13,1),13)==7);
    require(bucket(player_enum_turn(0x40000000,13,-1),13)==5);
    require(bucket(player_enum_turn(0x40000000,13,2),13)==8);
    std::puts("player enums move one choice per detent and saturate without overflow");
}
