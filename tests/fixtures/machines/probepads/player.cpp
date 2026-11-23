#include <opxy/player.hpp>
// Test fixture for Controls.instrument (SDK 1.27): on a drum track every key plays the one
// pad the first control chooses (pads are keys 53..76); on any other track a key plays its
// note snapped down to C major.
namespace probepads {
struct State { opxy::u32 held; };
opxy::u32 pitch(const opxy::player::Controls& controls, opxy::u32 note) {
    if (controls.instrument == opxy::player::DrumInstrument)
        return 53 + opxy::player::choice(controls.value[0], 24);
    static const opxy::u8 below[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    return note - below[note % 12];
}
bool press(State& state, const opxy::player::Controls& controls, opxy::u32 note, opxy::u32 velocity,
           opxy::player::Plan& plan) {
    ++state.held;
    plan.count = 1;
    plan.tones[0].pitch = static_cast<opxy::u8>(pitch(controls, note));
    plan.tones[0].velocity = static_cast<opxy::u8>(velocity);
    plan.tones[0].delay = 0;
    return true;
}
bool release(State& state, opxy::u32) { if (state.held) --state.held; return false; }
void display(const State&, const opxy::player::Controls& controls, opxy::player::Display& display) {
    const char* text = controls.instrument == opxy::player::DrumInstrument ? "pads" : "scale";
    for (opxy::u32 i = 0; text[i]; ++i) display.text[i] = text[i];
}
}
