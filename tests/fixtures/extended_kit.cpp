// The extended renderer (extended_drawing.hpp) on a drum kit's screen (SDK 1.26) over host
// doubles (extended_kit_host.hpp): the kit's played-pad snapshot, selected key, Shift cells
// and held-pad strip as the plain renderer gives them; on a whole-screen kit the SDK header
// only while Shift is held; and a synth's whole-screen canvas as before.
#include <extended_drawing.hpp>
#include <cstdio>
#include <cstdlib>
#define require(x) do { if (!(x)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)
using namespace opxy;

static u32 token = 1, triggers = 0, readouts = 0;
extern "C" u32 sdk_screen_activation(u32 page, u32) { return page == 84 ? token : 0; }
extern "C" void sdk_drum_trigger(u32, draw::Input* input) { ++triggers; input->trigger_key = 54; input->trigger_model = 1; }
// The kit's Shift cell at position 2: its volume.
extern "C" bool sdk_drum_kit_readout(u32, u32 position, u8* choice, const char** title, char* value) {
    ++readouts;
    if (position != 2) return false;
    *choice = 36; *title = "VOLUME"; std::strcpy(value, "6dB");
    return true;
}
struct Machine {
    static const char* const ids[2];
    static const bool percent_sends = true;
    static const bool kit = OPXY_TEST_KIT;
    static const bool played_pad = OPXY_TEST_KIT, selected_key = OPXY_TEST_KIT, kit_controls = OPXY_TEST_KIT;
};
const char* const Machine::ids[2] = {"node", "palette"};
typedef screen::ExtendedDrawing<Machine> Renderer;

static bool bar_shown(u8 authored) {
    for (u32 i = 0; i < 8; ++i)
        if (screen::fixture_bar[i].bytes[92] != (authored ? authored : 0)) return false;
    return true;
}

int main() {
    Renderer::Object object = {};
    screen::Leaf values[4] = {}, titles[4] = {};
    for (u32 i = 0; i < 4; ++i) { object.base.values[i] = &values[i]; object.base.titles[i] = &titles[i]; }
    object.base.page = 84;
    for (u32 i = 0; i < 8; ++i) screen::fixture_bar[i].bytes[92] = 1;   // the authored visibility
    screen::fixture_controls.valid = true;
    screen::fixture_controls.track = 2;
    screen::fixture_controls.pad = 53 + 5;
    const bool kit = OPXY_TEST_KIT, canvas = OPXY_TEST_CANVAS, kit_canvas = kit && canvas;
    // A kit's screen, or a synth's whole-screen canvas (a synth's plain one is screen_drawing.cpp's).
    require(kit || canvas);

    // Before the artwork loads nothing is bound; then the stock header's texts, unless a
    // synth's canvas, which has none, and a whole-screen kit's bar, hidden.
    Renderer::init(&object);
    require(screen::fixture_binds == 0 && bar_shown(1));
    object.base.svg = &object;
    Renderer::init(&object);
    require(screen::fixture_binds == (kit || !canvas ? 1u : 0u));
    require(bar_shown(kit_canvas ? 0 : 1));
    if (kit_canvas)
        for (u32 i = 0; i < 4; ++i) require(!values[i].text[0] && !titles[i].text[0]);

    // A frame without Shift: a kit's page refreshes as any kit's; a synth's canvas reads its
    // controls without the header.
    Renderer::frame(&object, 17);
    require(binding::renders == 1 && binding::encoder_reads == 1 && screen::fixture_draws == 1);
    require(screen::fixture_refreshes == (kit || !canvas ? 1u : 0u));
    require(screen::fixture_canvas_reads == (kit || !canvas ? 0u : 1u));
    require(binding::last_input.selected_key == (kit ? 5 : 0));
    // No pad before the track's first hit since activation.
    require(binding::last_input.trigger_key == 0 && triggers == 0);
    // The held-pad strip over a kit's page, after it is drawn.
    require(screen::fixture_strips == (kit ? 1u : 0u) && (!kit || screen::fixture_strip_after_draw));
    // A kit's Shift cells are read every frame for its choices; position 2's is its volume.
    require(readouts == (kit ? 4u : 0u) && binding::last_input.choices[2] == (kit ? 36 : 0));
    // A whole-screen kit hides the header without Shift: blank texts over what refresh wrote,
    // and the frame's captions never reach it.
    if (kit_canvas) {
        require(bar_shown(0));
        for (u32 i = 0; i < 4; ++i) require(!values[i].text[0] && !titles[i].text[0]);
    } else if (kit) {
        require(!std::strcmp(titles[0].text, "title") && !std::strcmp(values[0].text, "50"));
    }
    require(screen::fixture_headers == (canvas ? 0u : 1u));

    // The track's serial moves: the played pad's snapshot.
    ++screen::fixture_serial;
    Renderer::frame(&object, 17);
    require(binding::last_input.trigger_key == (kit ? 54u : 0u) && triggers == (kit ? 1u : 0u));

    // Shift held: a whole-screen kit shows the bar and its Shift page's cells, the kit's own
    // cell at position 2 over refresh's.
    screen::fixture_controls.choosing = true;
    const u32 headers = screen::fixture_headers;
    Renderer::frame(&object, 17);
    require(binding::last_input.shift == (kit ? 1 : 0) && screen::fixture_headers == headers + (kit || canvas ? 0 : 1));
    if (kit) {
        require(!std::strcmp(titles[0].text, "SHIFT") && !std::strcmp(titles[2].text, "VOLUME"));
        require(!std::strcmp(values[2].text, "6dB"));
    }
    if (kit_canvas) require(bar_shown(1));

    // Released again: hidden and blank.
    screen::fixture_controls.choosing = false;
    Renderer::frame(&object, 17);
    if (kit_canvas) {
        require(bar_shown(0));
        for (u32 i = 0; i < 4; ++i) require(!values[i].text[0] && !titles[i].text[0]);
    }

    // A new activation resets the display and, on a kit, the snapshot until the next hit.
    token = 2;
    Renderer::frame(&object, 17);
    // Two inits, the first activation, this one.
    require(binding::resets == 4 && binding::last_input.trigger_key == 0);
    ++screen::fixture_serial;
    Renderer::frame(&object, 17);
    require(binding::last_input.trigger_key == (kit ? 54u : 0u));
    std::puts("extended renderer on a kit's screen");
    return 0;
}
