// The cpu meter. The OP-XY has two Blackfins sharing no memory, joined by stock's
// SPI bridge: the DSP board runs audio, and the main board runs the GUI and is the
// bridge's master, the only side that starts requests. So the GUI asks and the
// DSP board answers.
//
// - DSP board, audio: cpu_meter_hooks.s times each block's whole DSP callback and
//   its parts (cpu_meter.hpp keeps the window).
// - DSP board, bridge: the dispatcher hook answers the GUI's poll.
// - Main board, GUI: Project > M4 > cpu polls while it is drawn, and the overlay's
//   indicator (cpu_overlay.cpp) polls about twice a second.
#include "benchmark.hpp"
#include "cpu_meter.hpp"
#include "registration.hpp"
#include "stock.hpp"

extern "C" {
void sdk_cpu_block(opxy::u32 start, opxy::u32 end) __asm__("sdk_cpu_block");
void sdk_cpu_track_time(opxy::u32 part, opxy::u32 cycles) __asm__("sdk_cpu_track_time");
opxy::u32 sdk_cpu_request(void* request) __asm__("sdk_cpu_request");
// The project's indicator setting, for its companion (project_companion.cpp).
bool sdk_cpu_indicator() __asm__("sdk_cpu_indicator");
void sdk_cpu_indicator_load(bool on) __asm__("sdk_cpu_indicator_load");
}

namespace {
using opxy::i32;
using opxy::u32;
using opxy::platform::stock;
using opxy::platform::word;
namespace meter = opxy::platform::cpu_meter;
static_assert(meter::kTracks == opxy::platform::kTracks, "The meter times every instrument track");

// Bridge command 0x34, a two-argument request kind (8c0c4668 rebuilds it); only
// requests also carrying kMagic as their 16-bit third argument are the meter's.
// Their first argument picks the answer. Each is an invalid handle, so were the
// hook ever to miss, stock's own 0x34 (8c0753b4, a filesystem call) would refuse it.
const u32 kCommand = 0x34, kMagic = 0x5ec5, kNoAction = 0x31;
const u32 kAskReport = 0xffffffff, kAskRecent = 0xfffffffe, kAskParts = 0xfffffffd;  // parts: 4 words down
const u32 kBridge = 0x11b00530;      // the bridge object, null on the DSP board
const u32 kBridgeLock = 0x8400fdf0;  // stock's wrappers lock this object around each request
// A request as 8c0c3c3c builds it: command byte +8, first argument +0xc, result
// +0x14, third argument (16 bits) +0x20. Stock's wrappers keep one in 36 bytes.
struct Request {
    u32 stock0[2];
    unsigned char command, stock9[3];
    u32 first, stock10;
    u32 result;
    u32 stock18[2];
    unsigned short third, stock22;
    u32 stock24[7];
};
static_assert(__builtin_offsetof(Request, command) == 8 && __builtin_offsetof(Request, first) == 0xc &&
              __builtin_offsetof(Request, result) == 0x14 && __builtin_offsetof(Request, third) == 0x20,
              "Stock bridge request layout");

meter::Meter dsp;  // DSP board, audio context
// DSP board: what the GUI's report request saw, so the requests that follow it
// in one refresh describe the same window.
u32 snapshot_recent, snapshot_parts[4];
}

// DSP board, audio: one timed part (cpu_meter_hooks.s, meter::Part).
void sdk_cpu_track_time(u32 part, u32 cycles) { meter::time_part(dsp, part, cycles); }

// DSP board, audio: 8c0c6ffe, the whole DSP callback for one block.
void sdk_cpu_block(u32 start, u32 end) { meter::block(dsp, start, end); }

// DSP board: 8c0c2384, the bridge dispatcher. Answers the meter's poll into its
// result word and gives the command to dispatch: the request's own, or one stock
// completes without acting, so the reply carries the answer back.
u32 sdk_cpu_request(void* raw) {
    Request* request = static_cast<Request*>(raw);
    const u32 command = static_cast<u32>(static_cast<i32>(static_cast<signed char>(request->command)));
    if (request->third != kMagic || request->command != kCommand) return command;
    const u32 which = request->first;
    if (which == kAskReport) {
        snapshot_recent = dsp.recent;
        for (u32 i = 0; i < 4; ++i) snapshot_parts[i] = dsp.parts[i];
        request->result = dsp.report;
    } else if (which == kAskRecent) {
        request->result = snapshot_recent;
    } else if (which <= kAskParts && which > kAskParts - 4) {
        request->result = snapshot_parts[kAskParts - which];
    } else {
        request->result = 0;
    }
    return kNoAction;
}

namespace {
// Main board, GUI context.

// Asks the DSP board as stock's bridge wrappers do: under the bridge lock, built
// at 8c0c3c3c and sent at 8c0c3a88, which waits for the reply and returns its
// result word. 0 when there is no bridge.
u32 poll(u32 which) {
    if (!word(kBridge)) return 0;
    Request request = {};
    opxy::platform::lock(kBridgeLock);
    stock<void(void*, u32, u32, u32, u32)>(0x8c0c3c3c)(&request, kCommand, which, 0, kMagic);
    const u32 result = stock<u32(void*)>(0x8c0c3a88)(&request);
    opxy::platform::unlock(kBridgeLock);
    return result;
}

// Project > M4 > cpu, beside general, tempo, voices and midi: a voices category
// (stock's 8c087168) with its own name and input, drawn by stock's voices renderer
// (8c09a474), whose hooks (cpu_meter_hooks.s) give it a "now" header readout and
// two settings rows in place of voices' tracks.
const u32 kVoicesTable = 0x0803ffac, kVoicesThunks = 0x0803ffd0;
const u32 kInputSlot = 1, kNameSlot = 7;  // in voices' method table
// Its second table, for the category's other base, is entered past offset-to-top
// and RTTI (word 2), where name comes first; the entry after the next draws
// (8c09a2f8, the renderer), so the name must go exactly here.
const u32 kThunkEntry = 2;
const u32 kCategoryBytes = 0x94;
const u32 kSelection = 0xc;       // the category's selected row, as voices' input keeps it (8c09a32c)
const u32 kStackRowBytes = 0x70;  // the renderer's row on its stack; its row vector follows
const u32 kListRowBytes = 0x38;   // a row in that vector
const u32 kPollCycles = 100000000;  // a quarter second (CCLK about 400 MHz)
const u32 kDrawnCycles = 40000000;  // the page is shown while the renderer asks within this
const char* const kSummaryKeys[meter::kSummaryRows] = {"other", "peak", "30s peak", "30s late", "total late", "master"};
const char* const kStageKeys[meter::kStageRows] = {"tape", "fx I", "fx II", "aux 5"};
const u32 kStageParts[meter::kStageRows] = {meter::kTape, meter::kFx1, meter::kFx2, meter::kAux};
const char* const kTrackKeys[meter::kTracks] = {"track 1", "track 2", "track 3", "track 4",
                                                "track 5", "track 6", "track 7", "track 8"};

u32 page_table[9], page_thunks[7];
u32 page_polled, page_last, page_drawn;
// The page's selection, as its input sets it. The page object is freed with the
// prefs screen's list, so nothing reads it.
i32 page_selected;
meter::Settings settings;
char now_format[16], now_value[16];
char summary_values[meter::kSummaryRows][16], stage_values[meter::kStageRows][16], track_values[meter::kTracks][16];

const char* page_name(void*) { return "cpu"; }
void page_input(void* self, u32 type, const i32* event) {
    if (!event) return;
    meter::input(settings, page_selected, type, event[1]);
    *reinterpret_cast<i32*>(static_cast<char*>(self) + kSelection) = page_selected;
}

void put(char* out, const char* s) {
    while (*s) *out++ = *s++;
    *out = 0;
}
void line(char* out, u32 value, const char* unit) { put(meter::digits(out, value), unit); }

// Polls when due and fills the texts. "other" is the load no timed part accounts
// for: mixing, untimed engines and the rest.
void refresh() {
    const u32 now = opxy::benchmark::cycles();
    if (page_polled && now - page_last < kPollCycles) return;
    page_polled = 1;
    page_last = now;
    const u32 r = poll(kAskReport), h = poll(kAskRecent);
    u32 parts[4];
    for (u32 i = 0; i < 4; ++i) parts[i] = poll(kAskParts - i);
    if (!(r >> 31)) {
        put(now_format, "wait");
        put(now_value, "wait");
        for (auto& v : summary_values) put(v, "-");
        for (auto& v : stage_values) put(v, "-");
        for (auto& v : track_values) put(v, "-");
        return;
    }
    const meter::Report report = meter::decode(r);
    const u32 worst = h & 0x1ff, recent_late = h >> 9 & 0x3ff;
    u32 timed = 0;
    for (u32 p = 0; p < meter::kParts; ++p) timed += meter::part(parts, p);
    line(now_format, report.now, "%%");  // a format: stock's snprintf prints it
    line(now_value, report.now, "%");
    line(summary_values[0], report.now > timed ? report.now - timed : 0, "%");
    line(summary_values[1], report.peak, "%");
    line(summary_values[2], worst, "%");
    line(summary_values[3], recent_late, recent_late == 0x3ff ? "+" : "");
    line(summary_values[4], report.total, report.total == 0x1ff ? "+" : "");
    line(summary_values[5], meter::part(parts, meter::kMaster), "%");
    for (u32 i = 0; i < meter::kStageRows; ++i) line(stage_values[i], meter::part(parts, kStageParts[i]), "%");
    for (u32 t = 0; t < meter::kTracks; ++t) line(track_values[t], meter::part(parts, meter::kTrack1 + t), "%");
}

using String = opxy::platform::registry::StockString;
void make_text(String* text, const char* source) { stock<void(void*, const char*)>(0x8c041788)(text, source); }

// Adds one row to the renderer's row vector, as stock builds its header row.
void push_row(void* rows, const char* key_text, const char* value_text) {
    String key, value;
    u32 row[kStackRowBytes / 4];
    make_text(&key, key_text);
    make_text(&value, value_text);
    stock<void(void*, String*, String*)>(0x8c086c58)(row, &key, &value);
    stock<void(void*, void*)>(0x8c087770)(rows, row);
    stock<void(void*)>(0x8c086d28)(row);
}
}

extern "C" {
// The page object, which the hooks compare with the renderer's this (P4).
opxy::u32 sdk_cpu_page_object __asm__("sdk_cpu_page_object");
// The header's key; 8c09a50c reads it 8 bytes past the address the hook gives.
extern const char sdk_cpu_page_key[] __asm__("sdk_cpu_page_key");
const char sdk_cpu_page_key[] = "now";
void sdk_cpu_page_add(opxy::u32 container) __asm__("sdk_cpu_page_add");
const char* sdk_cpu_page_format() __asm__("sdk_cpu_page_format");
void sdk_cpu_page_rows(void* header_row) __asm__("sdk_cpu_page_rows");
void sdk_cpu_page_tail(void* rows) __asm__("sdk_cpu_page_tail");
}

// 8c088456, the prefs screen's category list, after midi. Stock builds the list,
// with fresh categories freed alongside it, each time the screen opens.
void sdk_cpu_page_add(u32 container) {
    void* page = stock<void*(u32)>(0x8d4d2900)(kCategoryBytes);
    if (!page) return;
    stock<void*(void*, u32, u32)>(0x04022ea8)(page, 0, kCategoryBytes);
    stock<void(void*)>(0x8c087168)(page);  // a voices category
    for (u32 i = 0; i < 9; ++i) page_table[i] = word(kVoicesTable + 4 * i);
    for (u32 i = 0; i < 7; ++i) page_thunks[i] = word(kVoicesThunks + 4 * i);
    page_table[kInputSlot] = static_cast<u32>(reinterpret_cast<opxy::platform::Address>(&page_input));
    page_table[kNameSlot] = static_cast<u32>(reinterpret_cast<opxy::platform::Address>(&page_name));
    page_thunks[kThunkEntry] = static_cast<u32>(reinterpret_cast<opxy::platform::Address>(&page_name));
    u32* fields = static_cast<u32*>(page);
    fields[0] = static_cast<u32>(reinterpret_cast<opxy::platform::Address>(page_table));
    fields[1] = static_cast<u32>(reinterpret_cast<opxy::platform::Address>(page_thunks + kThunkEntry));
    sdk_cpu_page_object = static_cast<u32>(reinterpret_cast<opxy::platform::Address>(page));
    page_selected = *reinterpret_cast<i32*>(static_cast<char*>(page) + kSelection);
    settings.breakdown = false;
    stock<void(u32, void*)>(0x8c09b830)(container, page);
}

// 8c09a4fa: the header row's value format, "%d/24" on voices.
const char* sdk_cpu_page_format() {
    page_drawn = opxy::benchmark::cycles();
    refresh();
    return now_format;
}

// 8c09a544: after the header row is added (the row vector follows it), the settings.
void sdk_cpu_page_rows(void* header_row) {
    void* rows = static_cast<char*>(header_row) + kStackRowBytes;
    push_row(rows, "indicator", settings.indicator ? "on" : "off");
    push_row(rows, "breakdown", "open...");  // an action: while open, it covers the page
}

// 8c09a638: before the renderer counts the rows. The vector is {allocator, begin,
// end, capacity}; its last eight rows are the renderer's tracks, which the
// breakdown shows instead.
void sdk_cpu_page_tail(void* rows) {
    u32* vector = static_cast<u32*>(rows);
    const u32 kept = 3 * kListRowBytes;  // the header and the two settings
    for (u32 t = 0; t < meter::kTracks && vector[2] - vector[1] > kept; ++t) {
        vector[2] -= kListRowBytes;
        stock<void(void*)>(0x8c086d28)(reinterpret_cast<void*>(static_cast<opxy::platform::Address>(vector[2])));
    }
}

// GUI: what a save writes, and what a load gives: on only from a companion that says so.
bool sdk_cpu_indicator() { return settings.indicator; }
void sdk_cpu_indicator_load(bool on) { settings.indicator = on; }

namespace opxy { namespace platform { namespace cpu_meter {
u32 report() {
    const u32 r = poll(kAskReport);
    return r >> 31 ? r : 0;
}
bool indicator_enabled() { return settings.indicator; }
// Once the page stops drawing (left, or the prefs screen closed) the breakdown
// closes, so it reopens only from the page's own input.
bool breakdown_open() {
    if (opxy::benchmark::cycles() - page_drawn > kDrawnCycles) settings.breakdown = false;
    return settings.breakdown && page_selected == kBreakdownRow;
}
u32 breakdown(const char** keys, const char** values) {
    u32 n = 0;
    keys[n] = "now";
    values[n++] = now_value;
    for (u32 i = 0; i < kSummaryRows; ++i) { keys[n] = kSummaryKeys[i]; values[n++] = summary_values[i]; }
    for (u32 i = 0; i < kStageRows; ++i) { keys[n] = kStageKeys[i]; values[n++] = stage_values[i]; }
    for (u32 t = 0; t < kTracks; ++t) { keys[n] = kTrackKeys[t]; values[n++] = track_values[t]; }
    return n;
}
}}}
