#include "live_input_transport.hpp"
#include "stock.hpp"
#include <opxy/live_input.hpp>

extern "C" {
const opxy::live_input::Controls* sdk_live_input_controls() __asm__("sdk_live_input_controls");
bool sdk_live_input_shift_visible() __asm__("sdk_live_input_shift_visible");
bool sdk_live_input_turn(opxy::u32, const opxy::i32*) __asm__("sdk_live_input_turn");
const char* sdk_live_input_caption(opxy::u32) __asm__("sdk_live_input_caption");
void sdk_live_input_publish() __asm__("sdk_live_input_publish");
opxy::u32 sdk_live_input_option_bits() __asm__("sdk_live_input_option_bits");
}

namespace {
using namespace opxy;
using namespace opxy::platform;
using namespace live_input_transport;

constexpr u32 gui_parameters = 0x86bd027c;
constexpr u32 selected_page = 0x0801eae4;
u32 selected_target, selected_retune, selected_amount = live_input::max_amount;
u32 automatic_mask;

const live_input::Controls* shift_controls() {
    const auto* controls = sdk_live_input_controls();
    if (!controls || !controls->target.labels || controls->target.count < 2 ||
        controls->target.count > live_input::max_target || !controls->retune.labels ||
        controls->retune.count != 2) return nullptr;
    return controls;
}

u32 brain_mask() {
    // The stock Brain M1 renderer selects its manual scale triple or the
    // resolved triple returned by 8d58b324. Its keyboard uses the first word
    // as the pitch-class base and the third as the scale family (8d5ab190).
    constexpr u32 project = 0x840a0524, manual = 0x84eb5584, resolved = 0x860bf520;
    if (!word(project) || word(project + 4)) return 0;
    const bool follows_brain = byte(manual) || byte(manual + 8);
    const u32 source = follows_brain
        ? resolved + (byte(resolved) ? 0x0c : byte(resolved + 8) ? 0x40 : 0x74)
        : manual + 0x74;
    const u32 base = word(source), family = word(source + 8);
    if (base >= 12 || family >= 2) return 0;
    const u32 intervals = family ? 0xb35u : 0xab5u;
    return ((intervals << base) | (intervals >> (12 - base))) & 0xfffu;
}

bool step(u32& value, u32 count, const i32* event) {
    if (event && event[0]) {
        if (event[0] > 0 && value + 1 < count) ++value;
        if (event[0] < 0 && value) --value;
        sdk_live_input_publish();
    }
    return true;
}

const char* amount_caption() {
    static constexpr const char* values[] = {
        "0%", "7%", "13%", "20%", "27%", "33%", "40%", "47%",
        "53%", "60%", "67%", "73%", "80%", "87%", "93%", "100%",
    };
    return values[selected_amount];
}
}

bool sdk_live_input_shift_visible() {
    return byte(kSelectedTrack) == track && word(selected_page) == 0 &&
           shift_held() && shift_controls();
}

bool sdk_live_input_turn(opxy::u32 encoder, const opxy::i32* event) {
    if (encoder >= 8 || !sdk_live_input_shift_visible()) return false;
    const auto& controls = *shift_controls();
    switch (encoder % 4) {
    case 1: return step(selected_target, controls.target.count, event);
    case 2: return step(selected_retune, controls.retune.count, event);
    case 3: return step(selected_amount, live_input::max_amount + 1, event);
    default: return false;
    }
}

const char* sdk_live_input_caption(opxy::u32 encoder) {
    const auto* controls = shift_controls();
    if (!controls) return nullptr;
    switch (encoder) {
    case 1: return controls->target.labels[selected_target];
    case 2: return controls->retune.labels[selected_retune];
    case 3: return amount_caption();
    default: return nullptr;
    }
}

opxy::u32 sdk_live_input_option_bits() {
    return option_bits(selected_target, selected_retune, selected_amount, automatic_mask);
}

void sdk_live_input_publish() {
    automatic_mask = selected_target == 1 ? brain_mask() : 0;
    const u32 options = sdk_live_input_option_bits();
    const u32 address = gui_parameters + 0x4080 + (track * words + slot) * 4;
    static bool configured;
    configured = configured || options != 0;
    if (!configured) return;
    const u32 previous = word(address);
    const u32 value = (monitoring(previous) ? previous & ~tuner_mask : tag) | options;
    if (previous != value)
        stock<void(u32, u32, u32, i32)>(0x8d5acea8)(gui_parameters, track, slot, static_cast<i32>(value));
}
