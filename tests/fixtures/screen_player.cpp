#include <screen_player.hpp>
#include <cstdio>
#include <cstdlib>
#define require(x) do { if (!(x)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)
using namespace opxy;
static u32 row = 0, rows_read = 0;
namespace opxy { namespace platform {
u32 player_row(u32 track) { require(track == 0); ++rows_read; return row; }
} }
struct Page {
    static const bool paged = false;
    static const char* const ids[281];
    static const char* const titles[8];
    static player::Display shown;
    static bool display(u32 track, player::Display& out) { require(track == 0); out = shown; return true; }
};
const char* const Page::ids[281] = {};
const char* const Page::titles[8] = {"str", "dir", "voi", "spr", "", "", "", ""};
player::Display Page::shown = {};
// A player whose press switches between two pages.
struct Paged {
    static const bool paged = true;
    static const char* const ids[305];
    static const char* const titles[8];
    static bool display(u32 track, player::Display& out) { require(track == 0); out = Page::shown; return true; }
};
const char* const Paged::ids[305] = {};
const char* const Paged::titles[8] = {"str", "dir", "voi", "spr", "key", "scl", "", "lat"};

static bool text_is(const screen::Leaf& node, const char* text) { return std::strcmp(node.text, text) == 0; }

// Both pages show at once: the edited one bright on the bar, the other grey beside it.
static void paged_header() {
    typedef screen::PlayerPage<Paged> Renderer;
    Renderer::Object object = {};
    screen::PathNode bars[8] = {};
    screen::Leaf texts[16] = {};
    object.base.svg = &object;
    Renderer::init(&object);
    for (u32 i = 0; i < 8; ++i) object.header.bars[i] = &bars[i];
    for (u32 i = 0; i < 16; ++i) { object.header.texts[i] = &texts[i]; std::strcpy(texts[i].text, "-"); }
    Page::shown = {};
    const char* const values[8] = {"0", "up", "0", "off", "d", "min", "", "on"};
    for (u32 i = 0; i < 8; ++i) std::strcpy(Page::shown.values[i], values[i]);
    row = 0;
    Renderer::frame(&object, 0);
    for (u32 i = 0; i < 8; ++i)
        require(bars[i].leaf.matrix[6] == static_cast<i32>(i * 60 << 10) && bars[i].leaf.matrix[7] == 0);
    require(text_is(texts[0], "str") && text_is(texts[3], "spr") && text_is(texts[5], "up"));
    require(text_is(texts[8], "key") && texts[10].text[0] == 0 && text_is(texts[11], "lat"));
    require(text_is(texts[12], "d") && texts[14].text[0] == 0 && text_is(texts[15], "on"));
    require(texts[4].matrix[0] == 1024 && texts[4].matrix[7] < (20 << 10) && texts[12].matrix[7] == (33 << 10));
    // A press moves the bar to the second row within 0.15 s, and the rows change places.
    row = 4;
    Renderer::frame(&object, 0);
    require(bars[0].leaf.matrix[7] == 0);
    Renderer::frame(&object, 40);
    require(bars[0].leaf.matrix[7] > 0 && bars[0].leaf.matrix[7] < (18 << 10));
    Renderer::frame(&object, 154);
    for (u32 i = 0; i < 8; ++i) require(bars[i].leaf.matrix[7] == (18 << 10));
    require(text_is(texts[0], "key") && text_is(texts[3], "lat") && text_is(texts[4], "d") && text_is(texts[7], "on"));
    require(text_is(texts[8], "str") && text_is(texts[13], "up") && text_is(texts[15], "off"));
    require(texts[4].matrix[0] == 1024 && texts[4].matrix[7] > (20 << 10) && texts[12].matrix[7] < (20 << 10));
    // A changed value reaches the row it belongs to.
    std::strcpy(Page::shown.values[1], "down"); std::strcpy(Page::shown.values[4], "e");
    Renderer::frame(&object, 16);
    require(text_is(texts[4], "e") && text_is(texts[13], "down"));
    row = 0;
    Renderer::frame(&object, 0);
    Renderer::frame(&object, 154);
    require(bars[0].leaf.matrix[7] == 0 && text_is(texts[0], "str") && text_is(texts[5], "down") && text_is(texts[12], "e"));
}

int main() {
    typedef screen::PlayerPage<Page> Renderer;
    Renderer::Object object = {};
    screen::Leaf titles[4] = {}, values[4] = {}, line = {};
    screen::PathNode keys[128] = {}, notes[128] = {}, dots[24] = {};
    object.base.svg = &object;
    object.text = &line;
    object.lit = ~0u;
    for (u32 i = 0; i < 4; ++i) { object.base.titles[i] = &titles[i]; object.base.values[i] = &values[i]; }
    for (u32 i = 0; i < 128; ++i) { object.keys[i] = &keys[i]; object.played[i] = &notes[i]; }
    for (u32 i = 0; i < 24; ++i) {
        object.dots[i] = &dots[i]; object.home[i] = i * 1024;
        dots[i].leaf.matrix[6] = object.home[i];
    }
    Page::shown.lowest_key = 53;
    Page::shown.lit = (1u << 7) | (1u << 16) | (1u << 20);
    std::strcpy(Page::shown.text, "C minor 9");
    std::strcpy(Page::shown.values[0], "0"); std::strcpy(Page::shown.values[1], "up");
    std::strcpy(Page::shown.values[2], "0"); std::strcpy(Page::shown.values[3], "off");
    const u32 pitches[] = {60, 63, 67, 70, 74};
    for (u32 n : pitches) Page::shown.played[n / 32] |= 1u << (n % 32);
    Renderer::frame(&object, 0);
    require(std::strcmp(titles[0].text, "str") == 0 && std::strcmp(titles[3].text, "spr") == 0);
    require(std::strcmp(values[1].text, "up") == 0 && std::strcmp(line.text, "C minor 9") == 0);
    Renderer::centre(&line);
    require(line.anchor == 1 && line.matrix[6] == Renderer::kCentre);
    require(dots[7].leaf.matrix[6] == object.home[7] && dots[0].leaf.matrix[6] >= Renderer::kHidden);
    for (u32 n : pitches) require(notes[n].leaf.matrix[6] >= (18 << 10) && notes[n].leaf.matrix[6] < (462 << 10));
    require(notes[61].leaf.matrix[6] == Renderer::kHidden && keys[52].leaf.matrix[6] == Renderer::kHidden);
    require(keys[53].leaf.matrix[6] == (18 << 10));
    // Spread/voicing can exceed the physical keyboard; fit both extreme notes.
    Page::shown.played[0] = 1; Page::shown.played[3] = 1u << 31;
    Renderer::frame(&object, 0);
    require(notes[0].leaf.matrix[6] < notes[127].leaf.matrix[6] && notes[127].leaf.matrix[6] < (462 << 10));
    for (u32 i = 0; i < 4; ++i) Page::shown.played[i] = 0;
    Renderer::frame(&object, 0);
    for (u32 i = 0; i < 128; ++i) require(notes[i].leaf.matrix[6] == Renderer::kHidden);
    // The stock header starts with "0" in each value: an empty first value overwrites it.
    Renderer::Object fresh = {};
    screen::Leaf fresh_titles[4] = {}, fresh_values[4] = {};
    fresh.base.svg = &fresh;
    Renderer::init(&fresh);
    for (u32 i = 0; i < 4; ++i) {
        fresh.base.titles[i] = &fresh_titles[i]; fresh.base.values[i] = &fresh_values[i];
        std::strcpy(fresh_values[i].text, "0");
    }
    Page::shown = {};
    std::strcpy(Page::shown.values[0], "+0");
    Renderer::frame(&fresh, 0);
    require(std::strcmp(fresh_values[0].text, "+0") == 0 && fresh_values[3].text[0] == 0);
    // A one-page player keeps the stock header and never asks which page shows.
    require(rows_read == 0);
    paged_header();
}
