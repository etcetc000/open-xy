#pragma once
#include <opxy/arrangement.hpp>
namespace opxy { namespace platform {
inline bool arrangement_mask_valid(const extensions::ArrangementSurface& surface) {
    // Keyboard26..49 also has an immediate path outside the queued controller
    // producer. Keep it stock until a separate single-owner route exists.
    return !(surface.keys[0] & 0xfc000000u) && !(surface.keys[1] & 0x0003ffffu) &&
        !(surface.keys[2] & 0xffffff00u) && !(surface.encoders & ~15u);
}
// Pair ownership survives page changes. Never insert a consumed press into
// stock's held-key list, and never swallow a release stock already owns.
struct ArrangementInput {
    u8 owners[72]; // 0 up, 1 stock, 2 surface
    bool shifted() const { return owners[55] != 0; }
    template<class Deliver> bool key(u32 key, bool down, bool visible, Deliver deliver) {
        if (key >= 72) return false;
        u8& owner = owners[key];
        if (down) {
            if (owner) return owner == 2;
            const bool consumed = visible && deliver(true, shifted() || key == 55);
            owner = consumed ? 2 : 1;
            return consumed;
        }
        const u8 previous = owner;
        const bool shift = shifted();
        owner = 0;
        if (previous == 2) { deliver(false, shift); return true; }
        return false;
    }
};
}}
