#include "live_input_transport.hpp"
#include "stock.hpp"
#include <opxy/live_input.hpp>

extern "C" {
const opxy::live_input::Controls* sdk_live_input_controls() __asm__("sdk_live_input_controls");
const opxy::live_input::ExtraControls* sdk_live_input_extra_controls() __asm__("sdk_live_input_extra_controls");
opxy::u32 sdk_live_input_page() __asm__("sdk_live_input_page");
bool sdk_live_input_toggle_page() __asm__("sdk_live_input_toggle_page");
bool sdk_live_input_turn(opxy::u32, const opxy::i32*) __asm__("sdk_live_input_turn");
const char* sdk_live_input_caption(opxy::u32, bool) __asm__("sdk_live_input_caption");
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
u32 automatic_scale;
live_input::ExtraValues extra;
bool extra_page;

const live_input::Controls* shift_controls() {
    const auto* controls = sdk_live_input_controls();
    if (!controls || !controls->target.labels || controls->target.count < 2 ||
        controls->target.count > live_input::max_target || !controls->retune.labels ||
        controls->retune.count != 2) return nullptr;
    return controls;
}

const live_input::ExtraControls* extra_controls() {
    const auto* controls = sdk_live_input_extra_controls();
    if (!controls) return nullptr;
    for (u32 row = 0; row < live_input::extra_rows; ++row) {
        const auto& choices = controls->choices[row];
        if (!controls->labels[row] || !choices.labels || !choices.count ||
            choices.count > live_input::extra_capacity[row]) return nullptr;
    }
    return controls;
}

u32 brain_scale() {
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
    return 1 + base + 12 * family;
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

opxy::u32 sdk_live_input_page() {
    if (byte(kSelectedTrack) != track || word(selected_page) != 0 ||
        !shift_held() || !shift_controls()) return 0;
    return extra_page ? 2 : 1;
}

bool sdk_live_input_toggle_page() {
    if (!sdk_live_input_page() || !extra_controls()) return false;
    extra_page = !extra_page;
    return true;
}

bool sdk_live_input_turn(opxy::u32 encoder, const opxy::i32* event) {
    if (encoder >= 8) return false;
    const u32 page = sdk_live_input_page();
    if (!page) return false;
    const u32 row = encoder % 4;
    if (page == 2) {
        const auto* controls = extra_controls();
        return controls && step(extra.values[row], controls->choices[row].count, event);
    }
    const auto& controls = *shift_controls();
    switch (row) {
    case 1: return step(selected_target, controls.target.count, event);
    case 2: return step(selected_retune, controls.retune.count, event);
    case 3: return step(selected_amount, live_input::max_amount + 1, event);
    default: return false;
    }
}

const char* sdk_live_input_caption(opxy::u32 encoder, bool label) {
    if (encoder >= live_input::extra_rows) return nullptr;
    if (extra_page) {
        const auto* controls = extra_controls();
        if (!controls) return nullptr;
        return label ? controls->labels[encoder] : controls->choices[encoder].labels[extra.values[encoder]];
    }
    const auto* controls = shift_controls();
    if (!controls) return nullptr;
    if (label) {
        static constexpr const char* labels[] = {nullptr, "tune", "retune", "amount"};
        return labels[encoder];
    }
    switch (encoder) {
    case 1: return controls->target.labels[selected_target];
    case 2: return controls->retune.labels[selected_retune];
    case 3: return amount_caption();
    default: return nullptr;
    }
}

opxy::u32 sdk_live_input_option_bits() {
    return option_bits(selected_target, selected_retune, selected_amount, automatic_scale, extra);
}

void sdk_live_input_publish() {
    if (extra_page && !sdk_live_input_page()) extra_page = false;
    const bool follows_brain = selected_target == 1 || (selected_target == 0 && extra_controls());
    automatic_scale = follows_brain ? brain_scale() : 0;
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
