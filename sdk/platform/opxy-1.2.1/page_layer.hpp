// Page layers (extension.hpp, Layer): which page claim's page shows now.
#pragma once
#include "../extension_runtime.hpp"

namespace opxy { namespace platform { namespace page_layer {
// Tracks 1-8 show the instrument controller (its vtable on top of the GUI
// stack, 8c0944b0); its page word is 0 on the engine page, 1 and 2 on the
// envelope and filter pages, and 3 on the LFO page (M4, 8c0b19c4).
const u32 kInstrumentController = 0x8400dff8;
const u32 kInstrumentPage = 0x080238bc;
const u32 kLfoPage = 3;
// page_lfo while a track's LFO page shows, else 0 (extension_stock.cpp).
u32 shown();
}}}
