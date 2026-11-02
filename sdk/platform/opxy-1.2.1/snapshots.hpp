// The snapshot store's SDK-internal side (snapshots.cpp), part of the control service.
#pragma once
#include <opxy/types.hpp>
#include "../snapshot_menu.hpp"

namespace opxy { namespace platform {
// The encoding's most bytes: a version byte and two slot bytes, then per slot
// eight engine words and 8 x 58 values of at most five bytes each.
const u32 kSnapshotBytes = 3 + 10 * (8 * 4 + 8 * 58 * 5);
// Controller path only: fills a slot from every instrument track's stored values.
bool snapshot_take(u32 slot);
// After a new project: no snapshots.
void snapshots_clear();
// Writes a slot's values back into every track that still holds the engine it
// held then, through stock's own parameter setter. False for an empty slot.
bool snapshot_load(u32 slot);
// How many instrument tracks still hold the engine they held when `slot` was taken.
u32 snapshot_fits(u32 slot);

// The snapshot menu (stock's bar menu, for snapshots): opened by the claim of an
// extension holding the snapshot action, while its key is held or, for a claim
// with a partner key, from one press of the pair to the next. The extension
// runtime routes keys here (sdk_snapshot_menu, snapshot_menu.hpp); the overlay draws it.
// The bottom row shows after the menu key is held this long, as on the bar menu.
const i32 kMenuStripMs = 400;
// No slot, for MenuView's slots.
const u32 kNoSlot = 10;
// What the menu last did, shown for kMenuFlashMs.
const u32 kFlashNone = 0, kFlashSaved = 1, kFlashLoaded = 2, kFlashCleared = 3, kFlashClearedAll = 4;
const i32 kMenuFlashMs = 1200;
// One encoder step per this many raw encoder counts (a detent).
const i32 kMenuKnobCounts = 4;
struct MenuView {
    bool open, strip;
    u32 selected, taken, fits;
    // The slot the tracks match now (every track holding the slot's engine holds
    // its values), else kNoSlot; the slot last saved or loaded, else kNoSlot.
    u32 current, active;
    u32 flash, flash_slot;
    bool operator==(const MenuView& o) const {
        return open == o.open && strip == o.strip && selected == o.selected && taken == o.taken && fits == o.fits &&
               current == o.current && active == o.active && flash == o.flash && flash_slot == o.flash_slot;
    }
};
MenuView snapshot_menu_view();
bool snapshot_menu_open();
// The overlay's frame while the menu shows: milliseconds since the last.
void snapshot_menu_frame(i32 elapsed_ms);

// The saved form (docs/projects.md): bytes written, or 0 for none.
u32 snapshots_encode(u8* out, u32 capacity);
// Replaces every slot with what a project kept, or with none.
void snapshots_decode(const u8* data, u32 bytes);
}}

extern "C" {
// The extension runtime's snapshot action, through startup.cpp: snapshot_take in a
// build with the control service, a stand-in that refuses in one without it.
bool sdk_snapshot_take(opxy::u32 slot) __asm__("sdk_snapshot_take");
// The snapshot menu's input: snapshot_menu.hpp.
}
