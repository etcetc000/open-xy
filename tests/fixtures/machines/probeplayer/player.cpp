#include <opxy/player.hpp>

namespace probeplayer {
// The key held, which the page draws; the key sounds as played.
struct State { opxy::u32 held, note; };
inline bool press(State& state, const opxy::player::Controls&, opxy::u32 note, opxy::u32 velocity,
                  opxy::player::Plan& plan) {
    state.held = 1;
    state.note = note;
    plan.count = 1;
    plan.tones[0].pitch = static_cast<opxy::u8>(note);
    plan.tones[0].velocity = static_cast<opxy::u8>(velocity);
    plan.tones[0].delay = 0;
    return true;
}
inline bool release(State& state, opxy::u32 note) {
    if (state.note == note) state.held = 0;
    return false;
}
}
