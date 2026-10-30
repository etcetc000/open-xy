// The stock GUI compositor's traversal of the screen slots. Stock paints the
// slots in ascending page order once per frame, so page numbers are layer
// order and an SDK page (numbered after stock's) would cover stock overlays
// such as the octave indicator. An owned SDK page that is selected is painted
// right after slot 0 instead; every other slot keeps its relative order.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
// The compositor's loop state: the page just painted and the address of the
// slot after it (stock's post-incremented slot pointer).
struct ScreenCursor { u32 page; u32 slot; };

struct ScreenActivation {
    u32 page, track, serial, observed;
    // Zero denotes an inactive page, including when the serial wraps.
    void advance_serial() { if (!++serial) ++serial; }
    void select(u32 next) {
        if (page != next) { page = next; observed = 0; advance_serial(); }
    }
    u32 token(u32 requested, u32 selected_track) {
        // Background renderers must not consume the selected track's change.
        if (!page || page != requested) return 0;
        if (!observed || track != selected_track) {
            track = selected_track; observed = 1; advance_serial();
        }
        return serial;
    }
};

template <u32 Slots> struct ScreenOrder {
    u32 promoted;  // this frame's owned selected page, or 0

    // Moves the cursor to the next page to paint; false once every slot has
    // been visited. owned(page) == 1 admits the selected page; it is asked
    // once per frame, when the frame's first slot has been painted.
    template <class Owned> bool advance(ScreenCursor& cursor, u32 selected, Owned owned) {
        const u32 painted = cursor.page;
        if (painted == 0)
            promoted = selected > 1 && selected < Slots && owned(selected) == 1 ? selected : 0;
        u32 next = painted + 1;
        if (promoted) {
            if (painted == 0) next = promoted;
            else if (painted == promoted) next = 1;
            else if (next == promoted) next = promoted + 1;
        }
        cursor.slot += (next - (painted + 1)) * 4;
        cursor.page = next;
        return next < Slots;
    }
};
}}
