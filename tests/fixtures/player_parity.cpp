// A note player's own callbacks (opxy/player.hpp), driven through one seeded sequence of
// controls, presses, releases, updates, resets and displays, printing everything the player
// decides. Included after the player's own source with `namespace subject = <its namespace>;`
// (tests/test_players.py). Built with OPXY_PLAYER_INSTRUMENT, Controls.instrument is the kind
// INSTRUMENT names (0 other, 1 drum, 2 a mix of both): a player that never reads it prints the
// same as without the define, whatever the kind.
#include <cstdio>
#include <cstring>

namespace {
using opxy::u32;
u32 seed = 0x2545f491u;
u32 next() { seed = seed * 1664525u + 1013904223u; return seed >> 8; }
template<class S> auto next_update(S& s, const opxy::player::Controls& c, opxy::player::Update& u, int)
    -> decltype(update(s, c, u)) { return update(s, c, u); }
template<class S> bool next_update(S&, const opxy::player::Controls&, opxy::player::Update&, long) { return false; }
template<class S> auto clear_state(S& s, int) -> decltype(reset(s), void()) { reset(s); }
template<class S> void clear_state(S& s, long) { std::memset(&s, 0, sizeof s); }
template<class S> auto draw(const S& s, const opxy::player::Controls& c, opxy::player::Display& d, int)
    -> decltype(display(s, c, d), void()) { display(s, c, d); }
template<class S> void draw(const S&, const opxy::player::Controls&, opxy::player::Display&, long) {}
void print_plan(const opxy::player::Plan& plan) {
    std::printf(" %u:", static_cast<unsigned>(plan.count));
    for (u32 i = 0; i < plan.count && i < opxy::player::max_tones; ++i)
        std::printf(" %u/%u/%u", plan.tones[i].pitch, plan.tones[i].velocity, plan.tones[i].delay);
}
subject::State state;
}

int main() {
    opxy::player::Controls controls;
    std::memset(&controls, 0, sizeof controls);
    std::memset(&state, 0, sizeof state);
    bool held[128] = {};
    for (u32 step = 0; step < 40000; ++step) {
        if (step % 64 == 0) {
            for (u32 i = 0; i < 8; ++i) {
                controls.value[i] = static_cast<opxy::i32>(next() << 7 & 0x7fffffff);
                controls.shift_value[i] = static_cast<opxy::i32>(next() << 7 & 0x7fffffff);
                controls.shift_turns[i] = static_cast<opxy::i32>(next() % 9) - 4;
            }
            for (u32 i = 0; i < 4; ++i) controls.clicks[i] = static_cast<opxy::u8>(next());
            controls.lowest_key = 41 + 12 * (next() % 3);
            controls.bpm_x10 = next() % 4 ? 600 + next() % 1800 : 0;
            controls.shift = next() & 1;
        }
#if OPXY_PLAYER_INSTRUMENT
        // Its own sequence, so that the rest is the same whatever the kind.
        controls.instrument = INSTRUMENT == 2 ? step * 2654435761u >> 31 : INSTRUMENT;
#endif
        const u32 action = next() % 100;
        if (action < 30) {
            const u32 note = 24 + next() % 72, velocity = 1 + next() % 127;
            opxy::player::Plan plan;
            std::memset(&plan, 0, sizeof plan);
            const bool taken = subject::press(state, controls, note, velocity, plan);
            held[note] = true;
            std::printf("press %u %u -> %d", note, velocity, taken);
            if (taken) print_plan(plan);
            std::printf("\n");
        } else if (action < 55) {
            u32 note = 24 + next() % 72;
            for (u32 i = 0; i < 72 && !held[note]; ++i) note = 24 + (note - 24 + 1) % 72;
            held[note] = false;
            std::printf("release %u -> %d\n", note, subject::release(state, note));
        } else if (action < 92) {
            opxy::player::Update change;
            for (u32 i = 0; i < 8; ++i) {
                std::memset(&change, 0, sizeof change);
                if (!next_update(state, controls, change, 0)) break;
                std::printf("update %u %u", change.root, static_cast<unsigned>(change.mode));
                print_plan(change.plan);
                std::printf("\n");
            }
        } else if (action < 94) {
            clear_state(state, 0);
            std::memset(held, 0, sizeof held);
            std::printf("reset\n");
        } else {
            opxy::player::Display shown;
            std::memset(&shown, 0, sizeof shown);
            draw(state, controls, shown, 0);
            shown.text[sizeof shown.text - 1] = 0;
            std::printf("display %u [%s]", shown.lit, shown.text);
            for (u32 i = 0; i < 8; ++i) {
                shown.values[i][sizeof shown.values[i] - 1] = 0;
                std::printf(" [%s]", shown.values[i]);
            }
            std::printf("\n");
        }
    }
    return 0;
}
