#ifndef OPXY_SCREEN_DRUM
#define OPXY_SCREEN_DRUM 1
#endif
// Exercise the actual native renderer's activation/reset/cache boundary.
#include <screen_drawing.hpp>
#include <cstdio>
#include <cstdlib>
#define require(value) do { if (!(value)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)

static opxy::u32 token = 1;
opxy::u32 sdk_screen_activation(opxy::u32 page, opxy::u32) { return page == 84 ? token : 0; }
void sdk_display_note_activity(opxy::u32, opxy::draw::Input*) {}
void sdk_sample_overview_update(opxy::u32) {}
struct Machine { static const bool sample = false, percent_sends = true; static const bool drum = OPXY_SCREEN_DRUM; static const char* const ids[2]; };
static opxy::u8 pitches[128], releases[128];
void sdk_note_pitches(opxy::u32, opxy::u8* held, opxy::u8* released) {
    for (unsigned i = 0; i < 128; ++i) { held[i] = pitches[i]; released[i] = releases[i]; }
}
#if OPXY_SCREEN_DRUM
extern "C" void sdk_drum_trigger(opxy::u32, opxy::draw::Input* input) {
    input->trigger_key = 54; input->trigger_model = 9;
    input->trigger_controls[0] = 0x70000000;
}
#endif
const char* const Machine::ids[2] = {"node", "palette"};

int main() {
    using namespace opxy;
    using Renderer = screen::Drawing<Machine>;
    Renderer::Object object = {};
    object.base.page = 84;
    screen::Controls controls = {}; controls.valid = true;
    object.keys[0] = 77;
    Renderer::animate(&object, controls);
    require(binding::resets == 1 && binding::renders == 1 && object.keys[0] == 0);
    require(screen::display_state.ticks == 1);
    // The selected track's amp envelope reaches the drawing input.
    require(binding::last_input.amp.attack == 1 && binding::last_input.amp.release == 4);
    // Same page/track must preserve state and the applied geometry key.
    object.keys[0] = 77;
    Renderer::animate(&object, controls);
    require(binding::resets == 1 && object.keys[0] == 77 && screen::display_state.ticks == 2);
    token = 2;
    Renderer::animate(&object, controls);
    require(binding::resets == 2 && object.keys[0] == 0 && screen::display_state.ticks == 1);
    // Invalid controls defer activation until a frame can render.
    object.keys[0] = 77; token = 3; controls.valid = false;
    Renderer::animate(&object, controls);
    require(binding::resets == 2 && binding::renders == 3 && object.keys[0] == 77);
    controls.valid = true;
    Renderer::animate(&object, controls);
    require(binding::resets == 3 && object.keys[0] == 0 && screen::display_state.ticks == 1);
    token = 0; object.keys[0] = 77;
    Renderer::animate(&object, controls);
    require(binding::resets == 3 && object.keys[0] == 77);
    controls.pad = 53;
    const auto baseline = screen::display_note_serial(controls);
    pitches[61] = 1; ++screen::fixture_note_starts;
    const auto other = screen::display_note_serial(controls);
    require(other == baseline+1);
    pitches[60] = 1; ++screen::fixture_note_starts;
    require(screen::display_note_serial(controls) == other+1);
    pitches[60] = 0; ++releases[60];
    require(screen::display_note_serial(controls) == other+1);
    pitches[60] = 1; ++screen::fixture_note_starts;
    require(screen::display_note_serial(controls) == other+2);
    // Sequencer activity has no held-key transitions. It still animates a
    // non-selected pad while the header retains the selected pad's model.
    ++screen::fixture_note_starts;
    Renderer::animate(&object,controls);
    require(binding::last_input.note_serial == other+3);
    require(binding::last_input.model == controls.model);
    require(binding::last_input.trigger_key == (OPXY_SCREEN_DRUM ? 54u : 0u));
    require(binding::last_input.trigger_model == (OPXY_SCREEN_DRUM ? 9u : 0u));
    std::puts("native display activation resets state and geometry caches only on a new active token");
}
