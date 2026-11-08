#include "stock.hpp"
#include "live_input_transport.hpp"

extern "C" {
void sdk_live_input_reset() __asm__("sdk_live_input_reset");
void sdk_live_input_prepare(opxy::u32, opxy::u32, opxy::u32, opxy::u32,
                            const opxy::live_input::ExtraValues&) __asm__("sdk_live_input_prepare");
void sdk_live_input_process(opxy::i32*) __asm__("sdk_live_input_process");
void sdk_live_input_rendered(opxy::u32) __asm__("sdk_live_input_rendered");
void sdk_live_input_finish() __asm__("sdk_live_input_finish");
}

namespace {
using namespace opxy::platform::live_input_transport;
constexpr opxy::u32 line = 2, inactive = 5;
opxy::u32 previous = inactive;
bool rendered;
opxy::u32 settings;
}

void sdk_live_input_rendered(opxy::u32 source_word) {
    // Match stock's five-way Aux source selection (8d50fdf4).
    const opxy::u32 source = ((source_word >> 8) * 5u) >> 23;
    // Mic, headset mic and line are the three analog sources.
    if (source > line) return;
    if (source != previous) sdk_live_input_reset();
    previous = source;
    rendered = true;
    const opxy::u32 token = opxy::platform::word(0x84eab39c + (track * words + slot) * 4);
    if (tagged(token)) settings = token;
    sdk_live_input_prepare(target(settings), retune(settings), amount(settings), brain(settings), extra_values(settings));
    sdk_live_input_process(opxy::platform::at<opxy::i32>(0x11903600));
}

void sdk_live_input_finish() {
    if (!rendered) previous = inactive;
    rendered = false;
}
