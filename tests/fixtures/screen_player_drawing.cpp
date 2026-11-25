// A player's drawn page (SDK 1.26): the real renderer over host doubles.
#include <screen_player_drawing.hpp>
#include <cstdio>
#include <cstdlib>
#define require(x) do { if (!(x)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)
using namespace opxy;
static u32 row = 0, activation = 1, resets = 0, renders = 0;
static bool present = true, valid_frame = true;
static const char* caption = "";
static player::Controls controls_given = {};
static draw::Input input_seen = {};
static int vtable_marker[16];
namespace opxy { namespace platform {
u32 player_row(u32 track) { require(track == 2); return row; }
void* player_object(u32 track, const void* const* vtable) {
    require(track == 2 && vtable == reinterpret_cast<const void* const*>(vtable_marker));
    return present ? &controls_given : nullptr;
}
}}
extern "C" u32 sdk_screen_activation(u32 page, u32 track) { require(page == 5 && track == 2); return activation; }

template <bool Paged> struct Page {
    typedef draw::Frame<1, 2, 1, 0, 1> Frame;
    struct DisplayState { u32 frames; };
    static const bool paged = Paged;
    static const char* const ids[2];
    static const char* const titles[8];
    static const void* const* vtable() { return reinterpret_cast<const void* const*>(vtable_marker); }
    static player::Controls controls(void* object) { return *static_cast<player::Controls*>(object); }
    static void reset(DisplayState& state, Frame& frame) { ++resets; state.frames = 0; frame.paths[0].key = 0; }
    static void render(void* object, DisplayState& state, const player::Controls& controls, const draw::Input& input,
                       Frame& frame) {
        require(object == &controls_given && controls.value[5] == controls_given.value[5]);
        ++renders;
        ++state.frames;
        input_seen = input;
        frame = Frame();
        frame.paths[0].key = valid_frame ? state.frames : 0;
        draw::place(frame, 0, 240 << 10, 120 << 10, 1024, 0, draw::fill_and_stroke);
        std::strcpy(frame.header[2], caption);
    }
};
template <bool P> const char* const Page<P>::ids[2] = {"n", "p"};
template <bool P> const char* const Page<P>::titles[8] = {"rate", "chance", "scale", "gate", "deja vu", "length", "", ""};

template <bool Paged> static void run() {
    typedef screen::PlayerDrawing<Page<Paged>> Renderer;
    typename Renderer::Object object = {};
    screen::Leaf values[4] = {}, titles[4] = {};
    for (u32 i = 0; i < 4; ++i) { object.base.values[i] = &values[i]; object.base.titles[i] = &titles[i]; }
    object.base.page = 5;
    screen::fixture_track = 2;
    resets = renders = screen::fixture_applies = screen::fixture_binds = screen::fixture_draws = 0;
    row = 0; activation = 1; present = true; valid_frame = true; caption = "";
    for (u32 i = 0; i < 8; ++i) controls_given.value[i] = static_cast<i32>(0x10000000u * i);
    controls_given.shift = 1;

    // Before the artwork loads: nothing is drawn or bound.
    Renderer::init(&object);
    require(resets == 1 && screen::fixture_binds == 0);
    Renderer::frame(&object, 17);
    require(renders == 0 && screen::fixture_draws == 0);
    object.base.svg = &object;
    Renderer::init(&object);
    require(resets == 2 && screen::fixture_binds == 1);

    // A frame: the player's eight controls as the slots, the first page shown,
    // the frame time, the track's activity, envelope and Shift.
    screen::fixture_serial = 41;
    Renderer::frame(&object, 17);
    require(resets == 3);   // the first activation resets the display
    require(renders == 1 && screen::fixture_applies == 1 && screen::fixture_draws == 1 && object.delta == 17);
    for (u32 i = 0; i < 8; ++i) require(input_seen.slots[i] == controls_given.value[i]);
    for (u32 i = 0; i < 4; ++i) require(input_seen.shown[i] == i && input_seen.parameters[i] == controls_given.value[i]);
    require(input_seen.elapsed == 17 && input_seen.track == 2 && input_seen.note_serial == 41);
    require(input_seen.held_notes == 3 && input_seen.note_off_serial == 9 && input_seen.amp.release == 7);
    require(input_seen.shift == 1 && input_seen.line == 0 && input_seen.reduced_motion == 0);
    if (!Paged) {
        // A one-page player: the stock header's labels once, values as amounts.
        require(!std::strcmp(titles[0].text, "rate") && !std::strcmp(titles[3].text, "gate"));
        require(!std::strcmp(values[0].text, "0") && !std::strcmp(values[2].text, "25"));
    } else {
        for (u32 i = 0; i < 4; ++i) require(titles[i].writes == 0 && values[i].writes == 0);
    }
    // A frame's header caption replaces the amount; unchanged text is not rewritten.
    caption = "dor";
    Renderer::frame(&object, 17);
    Renderer::frame(&object, 17);
    require(resets == 3 && renders == 3);
    if (!Paged) {
        require(!std::strcmp(values[2].text, "dor") && values[2].writes == 2 && values[0].writes == 1);
        require(titles[0].writes == 1);
    }
    // The second page: slots 4..7 at the encoders, on a paged player only.
    row = 4;
    Renderer::frame(&object, 17);
    for (u32 i = 0; i < 4; ++i)
        require(input_seen.shown[i] == (Paged ? 4 + i : i) && input_seen.parameters[i] == controls_given.value[Paged ? 4 + i : i]);
    require(input_seen.line == (Paged ? 1 : 0));
    // An invalid frame is not applied, nor its header.
    valid_frame = false;
    caption = "x";
    const u32 applied = screen::fixture_applies;
    Renderer::frame(&object, 17);
    require(screen::fixture_applies == applied);
    if (!Paged) require(!std::strcmp(values[2].text, "dor"));
    valid_frame = true;
    // Coming back to the page resets the display and its path keys.
    object.keys[0] = 99;
    activation = 2;
    Renderer::frame(&object, 17);
    require(resets == 4 && object.keys[0] == 0);
    // A track without this player: drawn as it stands, not rendered.
    present = false;
    const u32 before = renders;
    Renderer::frame(&object, 17);
    require(renders == before && screen::fixture_draws > 0);
}

int main() {
    run<false>();
    run<true>();
    std::puts("player drawn page renderer");
    return 0;
}
