// The snapshot menu's input, shared by the extension runtime, which routes keys
// to it, and the control service, which keeps the snapshots and draws the menu
// (opxy-1.2.1/snapshots.cpp).
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform {
// Open (arg 1 when a pair of keys opened it), close, select slot arg, step by
// encoder counts arg, act (arg: kMenuSave..kMenuClearAll).
const u32 kMenuOpen = 0, kMenuClose = 1, kMenuSelect = 2, kMenuStep = 3, kMenuAction = 4;
// Whether the hold that opened the menu was a hold (an action, a selection or
// kMenuStripMs), not a tap that leaves it open; the menu stays as it is.
const u32 kMenuHeld = 5;
// The bottom row, M1-M4.
const u32 kMenuSave = 0, kMenuLoad = 1, kMenuClear = 2, kMenuClearAll = 3;
}}

// The menu (snapshots.cpp), or startup's stand-in that answers 0 in a build
// without the control service. Open answers 1 when the menu opened; close and
// held answer 1 for a hold (the key's own stock function stays unheard), 0 for
// a bare tap.
extern "C" opxy::u32 sdk_snapshot_menu(opxy::u32 op, opxy::i32 arg) __asm__("sdk_snapshot_menu");
