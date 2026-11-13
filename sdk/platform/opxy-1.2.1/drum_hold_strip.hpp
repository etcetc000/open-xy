// Stock OS 1.2.1: a drum kit's held pad shows Drumline's own slide-up strip
// (scramble, copy, paste, reset) over the kit's page, as stock Drumline does for a
// held pad and as the track-hold menu looks. GUI role only.
//
// Drumline's page (constructor 8d531ccc: page 8, dbox.svg) is one static object that
// 8d531d64 builds once (guard byte 0x84eb3dd4, object 0x84eb3ddc). It embeds a stock
// strip widget at +0xbc (8d56a790, tbuttons.svg; a 0x80-byte object that owns its
// own screen object and nodes). Stock's held-pad handler 8c0b2068 marks the page
// whose menu it shows, for engine 4, with 8d52dc12(page, on): on is the pad's
// key-down without Shift (8d51f184); a rising edge sets the delay at +0xb8 to 0x133
// (0.3 s in the dispatcher's Q10 seconds) and the flag byte at +0xb4 follows on. An
// owned kit answers that query as 4 (sdk_drum_held_pad_engine), so stock marks
// Drumline's page for a kit's pad too, but Drumline's frame (8d530710) does not run
// on a kit's track and nothing showed.
//
// Its frame drives the strip at 8d531192..8d531260, as hold_strip_frame does: while
// the flag is on, or the strip is still sliding in once it is off (8d56a308), it
// waits out the delay, then sets the four items (8d56a758(strip, i, text, 1, 1),
// the texts at 0x8405488c + 0x20, 0x2c, 0x34, 0x3c) and animates the slide
// (8d56ae28(strip, elapsed)), which ends by drawing the strip's own screen object
// (8d56abcc -> 8d51fe1c). Otherwise it parks the strip below the screen (8d56a2f4).
// The kit's page calls this after drawing itself, so the strip covers the kit's
// art as the track-hold strip covers a track's page.
#pragma once
#include "opxy/types.hpp"
#include "stock.hpp"

namespace opxy { namespace platform {

const u32 kDrumlinePageBuilt = 0x84eb3dd4;  // byte: 8d531d64's guard
const u32 kDrumlinePage = 0x84eb3ddc;       // Drumline's page object
const u32 kHoldFlag = 0xb4;                 // byte: a pad held for the strip (8d52dc12)
const u32 kHoldDelay = 0xb8;                // Q10 seconds left before it slides in
const u32 kHoldStrip = 0xbc;                // the embedded strip widget

// Stock's item texts: "   scramble" (stock's own leading spaces), "copy", "paste",
// "reset", at 0x8405488c + 0x20, 0x2c, 0x34 and 0x3c.
constexpr u32 hold_item_text(u32 index) { return 0x8405488c + (index == 0 ? 0x20 : 0x24 + 8 * index); }

// One frame of Drumline's held-pad strip on `page` (its flag, delay and strip).
// Strip supplies item(strip, index), animate(strip, elapsed), sliding(strip) and
// hide(strip), stock's widget calls.
template <class Strip> void hold_strip_frame(unsigned char* page, i32 elapsed, Strip& strip) {
    void* widget = page + kHoldStrip;
    if (!page[kHoldFlag] && !strip.sliding(widget)) {
        strip.hide(widget);
        return;
    }
    i32& delay = *reinterpret_cast<i32*>(page + kHoldDelay);
    if (delay > 0) {
        delay -= elapsed;
        return;
    }
    for (u32 i = 0; i < 4; ++i) strip.item(widget, i);
    strip.animate(widget, elapsed);
}

// Stock's strip widget methods, on Drumline's page's own strip.
struct StockHoldStrip {
    static void item(void* widget, u32 index) {
        stock<void(void*, u32, const char*, u32, u32)>(0x8d56a758)(widget, index, at<const char>(hold_item_text(index)), 1, 1);
    }
    static void animate(void* widget, i32 elapsed) { stock<void(void*, i32)>(0x8d56ae28)(widget, elapsed); }
    static bool sliding(void* widget) { return stock<u32(void*)>(0x8d56a308)(widget) != 0; }
    static void hide(void* widget) { stock<void(void*)>(0x8d56a2f4)(widget); }
};

// The strip for this frame, once stock has built Drumline's page (stock builds it at
// a kit's first pad press, 8c0b20b0). Call only on an owned kit's track.
inline void drum_hold_strip(i32 elapsed) {
    if (!byte(kDrumlinePageBuilt)) return;
    StockHoldStrip strip;
    hold_strip_frame(at<unsigned char>(kDrumlinePage), elapsed, strip);
}
}}
