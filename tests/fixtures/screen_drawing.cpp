// Exercise the actual native renderer's activation/reset/cache boundary.
#include <screen_drawing.hpp>
#include <cstdio>
#include <cstdlib>
#define require(value) do { if (!(value)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)

static opxy::u32 token = 1;
opxy::u32 sdk_screen_activation(opxy::u32 page, opxy::u32) { return page == 84 ? token : 0; }
void sdk_display_note_activity(opxy::u32, opxy::draw::Input*) {}
void sdk_note_pitches(opxy::u32, opxy::u8*, opxy::u8*) {}
void sdk_sample_overview_update(opxy::u32) {}
struct Machine { static const bool sample = false, stock_artwork = false; static const char* const ids[2]; };
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
    // Stock artwork: elements by name, and by child index through a named group.
    using Retained = screen::RetainedDrawing<binding::Frame>;
    require(Retained::element(&object, "group") == static_cast<void*>(screen::group_child));
    require(Retained::element(&object, "group/3") == static_cast<void*>(&screen::group_child[3]));
    require(!Retained::element(&object, "group/4") && !Retained::element(&object, "group/3/0"));
    require(!Retained::element(&object, "other") && !Retained::element(&object, "other/1"));
    // A drumline voice's loop re-hit on the shown track strikes as a note start;
    // one posted for another track doesn't.
    controls.track = 2;
    const u32 before = screen::display_note_serial(controls);
    screen::posted_hit = 1u << 16 | 2u << 8 | 60;
    require(screen::display_note_serial(controls) == before + 1 && screen::display_note_serial(controls) == before + 1);
    screen::posted_hit = 2u << 16 | 3u << 8 | 60;
    require(screen::display_note_serial(controls) == before + 1);
    std::puts("native display activation resets state and geometry caches only on a new active token");
}
