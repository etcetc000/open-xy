// The cpu meter's part of the SDK's overlay (overlay.hpp), two groups in its
// artwork:
// - "indicator": while the DSP is pushed (cpu_meter.hpp, update), the window's
//   peak in red, bottom right, with a red dot when a block ran late;
// - "breakdown": every figure over Project > M4 > cpu while its breakdown is open,
//   in two columns, so nothing scrolls and nothing is selectable.
#include "benchmark.hpp"
#include "cpu_meter.hpp"
#include "overlay.hpp"
#include "screen.hpp"

extern "C" {
extern const opxy::platform::overlay::Client sdk_cpu_overlay __asm__("sdk_cpu_overlay");
}

namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::screen;
namespace meter = opxy::platform::cpu_meter;

const u32 kPollCycles = 200000000;  // the indicator polls about twice a second (CCLK about 400 MHz)
const u32 kHoldPolls = 6;           // and stays about 3 s
const u32 kTextCycles = 50000000;   // the breakdown's texts, about eight times a second
const i32 kAway = 1024 << 10;       // off screen, in Q10 (screen_player.hpp's kHidden)
const char* const kKeyIds[meter::kBreakdownRows] = {"bd_k0", "bd_k1", "bd_k2", "bd_k3", "bd_k4", "bd_k5",
    "bd_k6", "bd_k7", "bd_k8", "bd_k9", "bd_k10", "bd_k11", "bd_k12", "bd_k13", "bd_k14", "bd_k15", "bd_k16",
    "bd_k17", "bd_k18"};
const char* const kValueIds[meter::kBreakdownRows] = {"bd_v0", "bd_v1", "bd_v2", "bd_v3", "bd_v4", "bd_v5",
    "bd_v6", "bd_v7", "bd_v8", "bd_v9", "bd_v10", "bd_v11", "bd_v12", "bd_v13", "bd_v14", "bd_v15", "bd_v16",
    "bd_v17", "bd_v18"};

struct State {
    void* indicator;
    void* breakdown;
    Leaf* text;
    Leaf* dot;
    i32 dot_home;
    Leaf* keys[meter::kBreakdownRows];
    Leaf* values[meter::kBreakdownRows];
    u32 polled, last, written;
    meter::Alert alert;
};
State state;

void bind(void* raw) {
    Base* page = static_cast<Base*>(raw);
    state.indicator = find(page, "indicator");
    state.breakdown = find(page, "breakdown");
    state.text = text_node(find(page, "ind_cpu"));
    state.dot = static_cast<Leaf*>(find(page, "ind_alert"));
    state.dot_home = state.dot ? state.dot->matrix[6] : 0;
    for (u32 i = 0; i < meter::kBreakdownRows; ++i) {
        state.keys[i] = text_node(find(page, kKeyIds[i]));
        state.values[i] = text_node(find(page, kValueIds[i]));
    }
}

void poll() {
    const u32 now = opxy::benchmark::cycles();
    if (state.polled && now - state.last < kPollCycles) return;
    state.polled = 1;
    state.last = now;
    meter::update(state.alert, meter::indicator_enabled() ? meter::report() : 0, kHoldPolls);
}

bool wanted() {
    poll();
    return state.alert.held || meter::breakdown_open();
}

void write_breakdown() {
    const u32 now = opxy::benchmark::cycles();
    if (state.written && now - state.written < kTextCycles) return;
    state.written = now | 1;
    const char* keys[meter::kBreakdownRows] = {};
    const char* values[meter::kBreakdownRows] = {};
    const u32 n = meter::breakdown(keys, values);
    for (u32 i = 0; i < meter::kBreakdownRows; ++i) {
        if (state.keys[i]) set_text(state.keys[i], i < n ? keys[i] : "");
        if (state.values[i]) set_text(state.values[i], i < n ? values[i] : "");
    }
}

void frame(bool shown) {
    const bool open = shown && meter::breakdown_open(), pushed = shown && state.alert.held;
    if (open) write_breakdown();
    else state.written = 0;
    set_shown(state.breakdown, open);
    if (pushed && state.text) set_text(state.text, state.alert.text);
    set_shown(state.indicator, pushed);
    if (state.dot) {
        const u32 at[1] = {6};
        const i32 x[1] = {state.dot_home + (state.alert.late ? 0 : kAway)};
        set_words(state.dot, at, x);
    }
}
}

const opxy::platform::overlay::Client sdk_cpu_overlay = {bind, wanted, frame};
