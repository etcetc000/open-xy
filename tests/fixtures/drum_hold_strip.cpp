// A drum kit's held-pad strip follows Drumline's frame (8d531192..8d531260): the
// delay, the four items, the slide and the park below the screen, on a host page.
#include <drum_hold_strip.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define require(value) do { if (!(value)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)

using opxy::i32;
using opxy::u32;

// Stands in for stock's strip widget: a slide position that 8d56ae28 brings up and
// 8d56a2f4 parks, and the calls each frame made.
struct FakeStrip {
    void* widget;
    i32 position;  // Q10 px below its shown place; 0x7800 parked
    u32 items, animations, hides, item_mask, last_elapsed;
    void item(void* w, u32 index) { require(w == widget && index < 4); ++items; item_mask |= 1u << index; }
    void animate(void* w, i32 elapsed) {
        require(w == widget);
        ++animations;
        last_elapsed = static_cast<u32>(elapsed);
        position -= elapsed * 200;
        if (position < 0) position = 0;
    }
    bool sliding(void* w) { require(w == widget); return position > 0 && position < 0x7800; }
    void hide(void* w) { require(w == widget); ++hides; position = 0x7800; }
    void clear_counts() { items = animations = hides = item_mask = last_elapsed = 0; }
};

int main() {
    using namespace opxy::platform;
    alignas(4) static unsigned char page[0x13c];
    std::memset(page, 0, sizeof page);
    FakeStrip strip = {};
    strip.widget = page + kHoldStrip;
    strip.position = 0x7800;
    i32& delay = *reinterpret_cast<i32*>(page + kHoldDelay);
    const i32 frame = 17;  // Q10 seconds, about 60 frames a second

    // Nothing held: the strip stays parked and is never drawn.
    hold_strip_frame(page, frame, strip);
    require(strip.hides == 1 && strip.animations == 0 && strip.items == 0);

    // Stock's 8d52dc12 at a pad's key-down: the flag, and 0x133 on its rising edge.
    page[kHoldFlag] = 1;
    delay = 0x133;
    strip.clear_counts();
    u32 waited = 0;
    while (delay > 0) {
        hold_strip_frame(page, frame, strip);
        ++waited;
        require(strip.animations == 0 && strip.items == 0 && strip.hides == 0);
    }
    require(waited == (0x133 + frame - 1) / frame);
    // The delay is over: every frame names the four items and slides (and draws) it.
    hold_strip_frame(page, frame, strip);
    require(strip.item_mask == 0xf && strip.items == 4 && strip.animations == 1);
    require(strip.last_elapsed == static_cast<u32>(frame) && strip.position < 0x7800);
    while (strip.position > 0) hold_strip_frame(page, frame, strip);
    // Shown and held: still drawn each frame, never parked.
    strip.clear_counts();
    hold_strip_frame(page, frame, strip);
    require(strip.animations == 1 && strip.items == 4 && strip.hides == 0);

    // Released: 8c0b2068 clears the flag, and the shown strip is parked at once.
    page[kHoldFlag] = 0;
    strip.clear_counts();
    hold_strip_frame(page, frame, strip);
    require(strip.hides == 1 && strip.animations == 0 && strip.position == 0x7800);

    // Released mid-slide: it finishes sliding in, then parks, as on Drumline.
    page[kHoldFlag] = 1;
    delay = 0;
    strip.clear_counts();
    hold_strip_frame(page, frame, strip);
    require(strip.animations == 1 && strip.position > 0 && strip.position < 0x7800);
    page[kHoldFlag] = 0;
    u32 slides = 0;
    while (strip.position > 0) { hold_strip_frame(page, frame, strip); ++slides; }
    require(slides > 0 && strip.hides == 0);
    hold_strip_frame(page, frame, strip);
    require(strip.hides == 1 && strip.position == 0x7800);

    // A tap shorter than the delay never shows it.
    page[kHoldFlag] = 1;
    delay = 0x133;
    strip.clear_counts();
    hold_strip_frame(page, frame, strip);
    page[kHoldFlag] = 0;
    hold_strip_frame(page, frame, strip);
    require(strip.animations == 0 && strip.items == 0 && strip.hides == 1);

    // Stock's own item texts, as Drumline's frame passes them (0x8405488c + 0x20, 0x2c, 0x34, 0x3c).
    const u32 offsets[4] = {0x20, 0x2c, 0x34, 0x3c};
    for (u32 i = 0; i < 4; ++i) require(hold_item_text(i) == 0x8405488c + offsets[i]);
    std::puts("a held kit pad's strip waits 0.3 s, slides up and draws each frame, and parks on release");
}
