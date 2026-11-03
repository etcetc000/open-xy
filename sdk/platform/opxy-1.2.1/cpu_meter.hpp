// The cpu meter's arithmetic, with no stock calls, so host tests can run it
// (tests/fixtures/cpu_meter.cpp); and the GUI interface cpu_meter.cpp gives the
// overlay. cpu_meter.cpp holds the board-specific parts.
#pragma once
#include <opxy/types.hpp>

namespace opxy { namespace platform { namespace cpu_meter {

const u32 kWindowShift = 9;
const u32 kWindow = 1u << kWindowShift;  // blocks per report, about 0.74 s
// Cycles per 64-sample block, the stock diagnostic's normalization budget
// (docs/profiling.md), used until a window has measured the period.
const u32 kBudget = 580352;
// Parts timed within a block: the eight tracks' synth voices, then the stages.
enum Part : u32 { kTrack1 = 0, kAux = 8, kTape, kFx1, kFx2, kMaster, kParts };
const u32 kTracks = kAux;
const u32 kHistory = 40;  // reports in "30s", about 30 s

inline u32 percent(u32 part, u32 whole) {
    const u32 hundredth = whole / 100;
    return hundredth ? part / hundredth : 0;
}
inline u32 field(u32 value, u32 bits) {
    const u32 top = (1u << bits) - 1;
    return value > top ? top : value;
}
// Decimal digits of n at p; returns the end.
inline char* digits(char* p, u32 n) {
    char d[10];
    u32 count = 0;
    do { d[count++] = static_cast<char>('0' + n % 10); n /= 10; } while (n && count < 10);
    while (count) *p++ = d[--count];
    return p;
}

// DSP board, audio: block timings in CYCLES, gathered into reports every kWindow
// blocks. Loads are percent of the measured block period; a late block took
// longer than one period, which drops audio. Reports, each 0 until the first
// window, bit 31 marking a report:
// - report: the window's load (7 bits), slowest block (9), late blocks (6), and
//   late blocks since power-on (9);
// - recent: over the last kHistory reports, the slowest block (9) and late blocks (10);
// - parts[4]: each part's load, 8 bits each, four a word.
// Each field saturates. Zero-initialised, as the stock loader runs no constructors.
struct Meter {
    u32 start, busy, blocks, peak, late, period, late_total;
    u32 pending[kParts];  // this block's part timings
    u32 cycles[kParts];   // the window's
    unsigned short history_peak[kHistory], history_late[kHistory];
    u32 history_next;
    volatile u32 report, recent, parts[4];
};

inline void time_part(Meter& m, u32 part, u32 cycles) {
    if (part < kParts) m.pending[part] += cycles;
}

// Publishes a full window; `start` is the next block's, so the window spans
// kWindow whole periods, start to start.
inline void close(Meter& m, u32 start) {
    const u32 elapsed = start - m.start;
    m.period = elapsed >> kWindowShift;
    const u32 now = percent(m.busy, elapsed), peak = percent(m.peak, m.period);
    m.report = 1u << 31 | field(now, 7) | field(peak, 9) << 7 | field(m.late, 6) << 16 | field(m.late_total, 9) << 22;
    m.history_peak[m.history_next] = static_cast<unsigned short>(field(peak, 9));
    m.history_late[m.history_next] = static_cast<unsigned short>(field(m.late, 10));
    m.history_next = m.history_next + 1 == kHistory ? 0 : m.history_next + 1;
    u32 worst = 0, late = 0;
    for (u32 i = 0; i < kHistory; ++i) {
        if (m.history_peak[i] > worst) worst = m.history_peak[i];
        late += m.history_late[i];
    }
    m.recent = 1u << 31 | field(worst, 9) | field(late, 10) << 9;
    u32 packed[4] = {0, 0, 0, 0};
    for (u32 p = 0; p < kParts; ++p) {
        packed[p / 4] |= field(percent(m.cycles[p], elapsed), 8) << (8 * (p % 4));
        m.cycles[p] = 0;
    }
    for (u32 i = 0; i < 4; ++i) m.parts[i] = packed[i];
    m.busy = m.peak = m.late = m.blocks = 0;
}

// One block's whole DSP callback, after its parts were timed.
inline void block(Meter& m, u32 start, u32 end) {
    if (m.blocks == kWindow) close(m, start);
    if (!m.blocks) m.start = start;
    const u32 busy = end - start;
    if (busy > (m.period ? m.period : kBudget)) {
        ++m.late;
        ++m.late_total;
    }
    m.busy += busy;
    if (busy > m.peak) m.peak = busy;
    ++m.blocks;
    for (u32 p = 0; p < kParts; ++p) {
        m.cycles[p] += m.pending[p];
        m.pending[p] = 0;
    }
}

struct Report { u32 now, peak, late, total; };
inline Report decode(u32 r) { return {r & 0x7f, r >> 7 & 0x1ff, r >> 16 & 0x3f, r >> 22 & 0x1ff}; }
inline u32 part(const u32 (&parts)[4], u32 p) { return parts[p / 4] >> (8 * (p % 4)) & 0xff; }

// GUI: the indicator. A report whose slowest block reaches kPushed percent, or
// that has a late block, shows the peak for `hold` polls; 0 (no report, or the
// indicator off) hides it at once.
const u32 kPushed = 90;
struct Alert {
    u32 held;
    bool late;
    char text[12];  // "cpu N%"
};
inline void update(Alert& a, u32 report, u32 hold) {
    if (!report) {
        a.held = 0;
        return;
    }
    const Report r = decode(report);
    if (r.peak < kPushed && !r.late) {
        if (a.held) --a.held;
        return;
    }
    if (!a.held) a.late = false;
    a.held = hold;
    a.late = a.late || r.late;
    char* p = a.text;
    for (const char* s = "cpu "; *s; ++s) *p++ = *s;
    p = digits(p, r.peak);
    *p++ = '%';
    *p = 0;
}

// GUI: Project > M4 > cpu. Under its header readout, two settings: the indicator
// (on or off) and the breakdown (open or closed). As on voices, input type 1 moves
// the highlight, which closes the breakdown, and types 2 and 3 change the
// highlighted setting. Zero-initialised: both off. The indicator's setting is the
// project's, saved in its companion (docs/projects.md, open-xy.cpu).
const i32 kIndicatorRow = 0, kBreakdownRow = 1;
struct Settings {
    bool indicator;
    bool breakdown;
};
inline void input(Settings& s, i32& selected, u32 type, i32 delta) {
    if (!delta) return;
    if (type == 1) {
        const i32 next = selected + delta;
        selected = next < kIndicatorRow ? kIndicatorRow : next > kBreakdownRow ? kBreakdownRow : next;
        s.breakdown = false;
    } else if (type == 2 || type == 3) {
        if (selected == kIndicatorRow) s.indicator = delta > 0;
        else if (selected == kBreakdownRow) s.breakdown = delta > 0;
    }
}

// The breakdown's rows: "now", six summary rows, the four aux stages, the tracks.
const u32 kSummaryRows = 6, kStageRows = 4;
const u32 kBreakdownRows = 1 + kSummaryRows + kStageRows + kTracks;
static_assert(kStageRows + 1 == kParts - kAux, "Every stage but master is a stage row");

// GUI, implemented in cpu_meter.cpp for the overlay (cpu_overlay.cpp).
u32 report();  // the latest report word, 0 for none
bool indicator_enabled();
bool breakdown_open();  // the cpu page is drawn with its breakdown open
// Fills kBreakdownRows keys and values; returns how many.
u32 breakdown(const char** keys, const char** values);

}}}
