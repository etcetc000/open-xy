#include <screen_player.hpp>
#include <cstdio>
#include <cstdlib>
#define require(x) do { if (!(x)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)
using namespace opxy;
struct Page {
    static const char* const ids[281];
    static const char* const titles[4];
    static player::Display shown;
    static bool display(u32 track, player::Display& out) { require(track == 0); out = shown; return true; }
};
const char* const Page::ids[281] = {};
const char* const Page::titles[4] = {"str", "dir", "voi", "spr"};
player::Display Page::shown = {};
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
}
