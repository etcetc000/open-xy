#include <player_activity.hpp>
#include <cstdio>
#include <cstdlib>
#define require(x) do { if (!(x)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)
using namespace opxy;
using namespace opxy::platform;
int main() {
    PlayerChord records[kPlayerChords] = {};
    u32 played[4];
    records[0].count = 3;
    records[0].tones[0] = {-1, 60, 90, {0, 0}};
    records[0].tones[1] = {0, 63, 90, {0, 0}};
    records[0].tones[2] = {2, 67, 90, {0, 0}};
    player_activity(records, 1, played);
    require(played[1] == (1u << 28) && played[2] == 0);
    records[0].tones[1].delay = -1;
    records[0].tones[2].delay = -1;
    player_activity(records, 1, played);
    require(played[1] == ((1u << 28) | (1u << 31)) && played[2] == 8);
    // Another held root shares a tone; releasing one must retain the other.
    records[1].count = 1;
    records[1].tones[0] = {-9, 67, 90, {0, 0}};
    player_activity(records, 2, played);
    require(played[2] == 8);
    records[0] = records[1];
    player_activity(records, 1, played);
    require(played[1] == 0 && played[2] == 8);
    player_activity(records, 0, played);
    require(!(played[0] | played[1] | played[2] | played[3]));
    // Corrupt/transient counts and pitches never index beyond fixed arrays.
    records[0].count = 1000;
    records[0].tones[0].pitch = 255;
    records[1] = {};
    player_activity(records, 1000, played);
    require(!(played[0] | played[1] | played[2] | played[3]));
    for (u32 lowest = 17; lowest <= 89; lowest += 12) {
        for (u32 note = 0; note < 128; ++note) {
            for (u32 i = 0; i < 4; ++i) played[i] = 0;
            played[note / 32] = 1u << (note % 32);
            const PlayerRange r = player_range(played, lowest);
            require(r.low <= note && note <= r.high && r.high < 128);
            require(r.low <= lowest && r.high >= lowest + 23 && r.whites >= 14);
            require(!player_black(r.low) && !player_black(r.high));
        }
    }
    for (u32 i = 0; i < 4; ++i) played[i] = 0;
    played[0] = 1; played[3] = 1u << 31;
    const PlayerRange all = player_range(played, 53);
    require(all.low == 0 && all.high == 127 && all.whites == 75);
    std::puts("player emission, release and complete keyboard range ok");
}
