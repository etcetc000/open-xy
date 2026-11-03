// The cpu meter's arithmetic (cpu_meter.hpp) on synthetic block timings: window
// load and peak, late blocks, the 30 s history, part loads, the indicator's
// trigger and the cpu page's settings. Bridge polling, the renderer hooks and the
// overlay's drawing need the device.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cpu_meter.hpp>

namespace {
namespace meter = opxy::platform::cpu_meter;
using opxy::u32;
using opxy::i32;

void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

// Blocks `period` cycles apart and `busy` long (the first `late_blocks` a cycle
// over the period) until a window closes, timing `cycles` to each part given.
// The block that closes a window also opens the next. Returns the next start.
u32 window(meter::Meter& m, u32 start, u32 period, u32 busy, u32 late_blocks = 0,
           const u32* cycles = nullptr) {
    for (u32 i = 0;; ++i, start += period) {
        if (cycles)
            for (u32 p = 0; p < meter::kParts; ++p) meter::time_part(m, p, cycles[p]);
        meter::block(m, start, start + (i < late_blocks ? period + 1 : busy));
        if (m.blocks == 1 && i) return start + period;
    }
}
}

int main() {
    // A window at half load: 50% now and peak, nothing late.
    static meter::Meter m;
    require(!m.report && !m.recent, "No report before the first window");
    u32 start = window(m, 1000, 1000, 500);
    meter::Report r = meter::decode(m.report);
    require(m.report >> 31 && r.now == 50 && r.peak == 50 && r.late == 0 && r.total == 0, "Half-load window");
    require(m.period == 1000, "The period is measured from the window");

    // Late blocks count in their window and since power-on; the next window resets
    // its own count but not the total.
    start = window(m, start, 1000, 500, 3);
    r = meter::decode(m.report);
    require(r.late == 3 && r.total == 3 && r.peak == 100, "Late blocks and their peak");
    start = window(m, start, 1000, 200);
    r = meter::decode(m.report);
    require(r.late == 0 && r.total == 3 && r.now == 20, "A later window keeps only the total");

    // The 30 s history keeps the slowest block and the late blocks of recent windows.
    require((m.recent & 0x1ff) == 100 && (m.recent >> 9 & 0x3ff) == 3, "Recent peak and late blocks");
    for (u32 i = 0; i < meter::kHistory; ++i) start = window(m, start, 1000, 200);
    require((m.recent & 0x1ff) == 20 && (m.recent >> 9 & 0x3ff) == 0, "Old windows leave the history");

    // Each part's load is percent of the window, packed four to a word.
    u32 cycles[meter::kParts] = {};
    cycles[0] = 100;  // track 1: 10%
    cycles[7] = 50;   // track 8: 5%
    cycles[12] = 20;  // master: 2%
    meter::time_part(m, 99, 900);  // not a part: ignored
    // The first such window still holds the block that closed the last; the second is all timed.
    start = window(m, start, 1000, 300, 0, cycles);
    start = window(m, start, 1000, 300, 0, cycles);
    u32 parts[4];
    for (u32 i = 0; i < 4; ++i) parts[i] = m.parts[i];
    require(meter::part(parts, 0) == 10 && meter::part(parts, 7) == 5 && meter::part(parts, 12) == 2 &&
            meter::part(parts, 1) == 0, "Part loads");

    // Fields saturate rather than wrap.
    require(meter::field(1000, 7) == 127 && meter::field(5, 7) == 5, "Saturating fields");

    // The indicator: a pushed or late window shows its peak for the hold, a quiet one
    // counts it down, and no report (or the indicator off) hides it at once.
    const u32 pushed = 1u << 31 | 40 | 95u << 7, quiet = 1u << 31 | 40 | 60u << 7, late = 1u << 31 | 40 | 70u << 7 | 1u << 16;
    meter::Alert a = {};
    meter::update(a, quiet, 3);
    require(!a.held, "A quiet window shows nothing");
    meter::update(a, pushed, 3);
    require(a.held == 3 && !a.late && !std::strcmp(a.text, "cpu 95%"), "A pushed window shows its peak");
    meter::update(a, quiet, 3);
    meter::update(a, quiet, 3);
    require(a.held == 1, "Quiet windows count the hold down");
    meter::update(a, late, 3);
    require(a.held == 3 && a.late && !std::strcmp(a.text, "cpu 70%"), "A late block shows even below the threshold");
    meter::update(a, pushed, 3);
    require(a.late, "The dot stays while the indicator is held");
    meter::update(a, 0, 3);
    require(!a.held, "No report hides it at once");
    meter::update(a, pushed, 3);
    require(!a.late, "A fresh showing starts without the dot");

    // Project > M4 > cpu: the highlight stays on the two settings, moving it closes
    // the breakdown, and the value encoder changes only the highlighted one.
    static meter::Settings s;
    require(!s.indicator && !s.breakdown, "Both start off");
    i32 selected = 0;
    meter::input(s, selected, 1, -1);
    require(selected == meter::kIndicatorRow, "The header is not selectable");
    meter::input(s, selected, 3, 1);
    require(s.indicator, "The value encoder turns the indicator on");
    meter::input(s, selected, 2, -1);
    require(!s.indicator, "and off");
    meter::input(s, selected, 2, 1);
    meter::input(s, selected, 1, 5);
    require(selected == meter::kBreakdownRow, "The highlight stops at the last setting");
    meter::input(s, selected, 2, 1);
    require(s.breakdown && s.indicator, "The value encoder opens the breakdown, nothing else");
    meter::input(s, selected, 2, 0);
    require(s.breakdown, "A zero turn changes nothing");
    meter::input(s, selected, 2, -1);
    require(!s.breakdown, "Turning back closes it");
    meter::input(s, selected, 2, 1);
    meter::input(s, selected, 1, -1);
    require(!s.breakdown && selected == meter::kIndicatorRow, "Moving the highlight closes it");
    meter::input(s, selected, 4, 1);
    require(!s.breakdown && s.indicator, "Other input changes nothing");
    return 0;
}
