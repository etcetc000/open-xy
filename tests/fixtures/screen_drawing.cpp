// OPXY_SCREEN_KIT: 0 a synth's screen, 1 a drum kit's declaring SDK 1.21 or
// later (played_pad), 2 an older drum kit's (selected_pad).
#ifndef OPXY_SCREEN_KIT
#define OPXY_SCREEN_KIT 1
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
bool sdk_drum_kit_readout(opxy::u32, opxy::u32, opxy::u8*, const char**, char*) { return false; }
struct Machine {
    static const bool sample = false, percent_sends = true;
    static const bool played_pad = OPXY_SCREEN_KIT == 1, selected_pad = OPXY_SCREEN_KIT == 2;
    static const bool kit_controls = false, selected_key = false;
    static const char* const ids[2];
};
static opxy::u8 pitches[128], releases[128];
void sdk_note_pitches(opxy::u32, opxy::u8* held, opxy::u8* released) {
    for (unsigned i = 0; i < 128; ++i) { held[i] = pitches[i]; released[i] = releases[i]; }
}
#if OPXY_SCREEN_KIT == 1
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
    // No pad before the track's first hit, though stock's played key has one.
    require(binding::last_input.trigger_key == 0 && binding::last_input.trigger_model == 0);
    require(screen::display_state.ticks == 1);
    // The selected track's amp envelope reaches the drawing input.
    require(screen::display_input.amp.attack == 1 && screen::display_input.amp.release == 4);
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
    // Pitch 60 plays the selected pad 53, pitch 61 pad 54.
    fixture_pads[60] = 53; fixture_pads[61] = 54;
    controls.pad = 53; controls.model = 3;
    const bool filtered = OPXY_SCREEN_KIT == 2;
    auto serial = [&]() { Renderer::animate(&object, controls); return binding::last_input.note_serial; };
    const auto baseline = serial();
    // Another pad's live note: an older kit's screen is not struck, any other's is.
    pitches[61] = 1; ++screen::fixture_note_starts;
    const auto other = serial();
    require(other == baseline + (filtered ? 0 : 1));
    // The selected pad's note strikes every screen.
    pitches[60] = 1; ++screen::fixture_note_starts;
    const auto selected = serial();
    require(selected == other + 1);
    // A release alone strikes none.
    pitches[60] = 0; ++releases[60];
    require(serial() == selected);
    // The selected pad again.
    pitches[60] = 1; ++screen::fixture_note_starts;
    require(serial() == selected + 1);
    // Sequencer activity has no held-key transitions: a start no pitch accounts
    // for strikes an older kit as before, and is the track's serial otherwise.
    ++screen::fixture_note_starts;
    require(serial() == selected + 2);
    require(binding::last_input.model == controls.model);
    // Only a 1.21 kit's screen reads the played pad's snapshot; the rest stay zero.
    require(binding::last_input.trigger_key == (OPXY_SCREEN_KIT == 1 ? 54u : 0u));
    require(binding::last_input.trigger_model == (OPXY_SCREEN_KIT == 1 ? 9u : 0u));
    require(binding::last_input.trigger_controls[0] == (OPXY_SCREEN_KIT == 1 ? 0x70000000 : 0));
    // Without a selected pad (a synth's controls) every screen reads the track's serial.
    controls.pad = 0;
    const auto unselected = serial();
    require(unselected == screen::fixture_note_starts);
    pitches[61] = 0; ++releases[61]; pitches[61] = 1; ++screen::fixture_note_starts;
    require(serial() == unselected + 1);
    // A 1.21 kit's snapshot names no pad after an activation or a track change
    // until the track's serial moves, then the played pad until the next one.
    const auto none = [&]() {
        return binding::last_input.trigger_key == 0 && binding::last_input.trigger_model == 0 &&
               binding::last_input.trigger_controls[0] == 0;
    };
    const auto hit = [&]() {
        return binding::last_input.trigger_key == (OPXY_SCREEN_KIT == 1 ? 54u : 0u) &&
               binding::last_input.trigger_model == (OPXY_SCREEN_KIT == 1 ? 9u : 0u) &&
               binding::last_input.trigger_controls[0] == (OPXY_SCREEN_KIT == 1 ? 0x70000000 : 0);
    };
    serial(); require(hit());
    token = 4; serial(); require(none());
    serial(); require(none());
    ++screen::fixture_note_starts; serial(); require(hit());
    serial(); require(hit());
    controls.track = 1; serial(); require(none());
    ++screen::fixture_note_starts; serial(); require(hit());
    controls.track = 0; serial(); require(none());
    // A note start during an activation frame, right after the frame reads the
    // counter: the machine's serial moves next frame, and so must the snapshot.
    ++screen::fixture_note_starts; serial(); require(hit());
    token = 5; screen::fixture_start_after_read = true; serial(); require(none());
    const auto before = binding::last_input.note_serial;
    serial(); require(binding::last_input.note_serial != before && hit());
    // A re-created screen (init) names no pad, even before an activation token.
    token = 0; Renderer::init(&object); serial(); require(none());
    ++screen::fixture_note_starts; serial(); require(hit());
    std::puts("native display activation resets state and geometry caches only on a new active token");
}
