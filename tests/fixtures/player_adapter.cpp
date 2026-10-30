// Execute the generated player methods through their actual vtable. The stock
// call boundary records releases instead of jumping into firmware on the host.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "player_object_probeplayer.cpp"

#define require(x) do { if (!(x)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)
namespace {
opxy::player::Controls controls = {};
opxy::player::Plan sounded = {};
opxy::u32 released = 0, releases = 0;
}
void stock_release(void*, opxy::u32 note) { released = note; ++releases; }
namespace opxy { namespace platform {
void* player_state(void* object) { return object; }
player::Controls player_controls(void*) { return controls; }
void play_plan(void*, u32, const player::Plan& plan) { sounded = plan; }
}}
extern "C" const char probeplayer_player_id[] = "";

int main() {
    const auto* vtable = probeplayer_player()->vtable;
    // Before this fix the inherited query reads the fourth control in stock.
    require(vtable[3] != reinterpret_cast<const void*>(0x8d57d4a4));
    auto mode = reinterpret_cast<opxy::u32(*)(void*)>(const_cast<void*>(vtable[3]));
    auto on = reinterpret_cast<opxy::u32(*)(void*, opxy::u32, opxy::u32, opxy::u32, opxy::u32)>(const_cast<void*>(vtable[5]));
    auto off = reinterpret_cast<void(*)(void*, opxy::u32)>(const_cast<void*>(vtable[6]));
    for (unsigned combinations = 0; combinations < 16; ++combinations) {
        probeplayer::State state = {};
        for (unsigned i = 0; i < 4; ++i)
            controls.value[i] = combinations & (1u << i) ? 0x7fffffff : 0;
        releases = 0;
        require(mode(&state) == 0);
        require(on(&state, 65, 0, 0, 91) == 1 && sounded.count == 0);
        require(on(&state, 53, 0, 0, 91) == 1 && sounded.count == 1);
        require(sounded.tones[0].pitch == (controls.value[3] ? 65 : 53));
        require(sounded.tones[0].velocity == 91);
        off(&state, 53);
        require(releases == 1 && released == 53);
        off(&state, 65);
        require(releases == 1 && !state.modifier);
    }
    std::puts("player controls never select the stock mode; root and modifier release routes ok");
}
