// Stock Drumline's second Shift row (drumline.hpp), linked only with the build
// option drumlineRow2. The GUI functions run from the shared hooks through
// sdk_drumline; the render wrapper and the page's texts from this unit's own
// hooks (drumline_hooks.s).
#include <opxy/q31.hpp>
#include "drumline.hpp"
#include "drum_send.hpp"
#include "stock.hpp"

using opxy::i32;
using opxy::u32;
using opxy::u8;
using namespace opxy::platform;

extern "C" {
u32 sdk_dbox_render(unsigned char* self, i32* out, const i32* region, const i32* parameters) __asm__("sdk_dbox_render");
void sdk_drumline_page(unsigned char* page) __asm__("sdk_drumline_page");
}

namespace {
u32 track_object(u32 track) { return stock<u32(u32)>(0x8d587708)(track); }
// A track's pad record in slot 0..23 (8d58926c finds slot `key` of a Drumline track).
unsigned char* pad_slot(u32 track, u32 slot) {
    return slot < kDrumlinePads ? at<unsigned char>(track_object(track) + kDrumlineRecords + slot * kDrumlineRecordBytes)
                                : nullptr;
}
bool is_drumline(u32 t) { return t < kTracks && track_engine(t) == kDrumlineEngine; }
i32 stock_gain(i32 db) { return stock<i32(i32)>(0x8d5ad604)(db); }

// Each track's Shift row: false the first (stock's), true the second.
bool second[kTracks];

// 8c0b147a on row 2, no step held: FILTER, SRR, FX I and FX II of the key, one
// step a detent; stock's republication as its editor does it.
bool edit(u32 track, u32 key, u32 control, const u32* event) {
    if (control < opxy::page_controls || control >= 2 * opxy::page_controls || !second[track]) return false;
    unsigned char* record = static_cast<unsigned char*>(pad_region(track, key));
    if (!record || !event) return true;
    const u32 position = control - opxy::page_controls;
    i32 delta = static_cast<i32>(event[1]);
    if (delta > 127) delta = 127;
    if (delta < -127) delta = -127;
    u32* word = reinterpret_cast<u32*>(record);
    DrumlineFields f = drumline_fields(*word);
    if (position == 0) {
        const i32 next = drum_lock_clamp(f.filter + delta, kDrumlineFilterMin, kDrumlineFilterMax);
        if (next == f.filter) return true;
        f.filter = next;
    } else {
        u32& field = position == 1 ? f.srr : f.send[position - 2];
        const i32 high = position == 1 ? kDrumlineSrrMax : kDrumlineSendMax;
        const u32 next = static_cast<u32>(drum_lock_clamp(static_cast<i32>(field) + delta, 0, high));
        if (next == field) return true;
        field = next;
    }
    *word = drumline_store(*word, f);
    stock<void()>(0x8d585ee8)();
    stock<void(u32)>(0x8d4d5f18)(key);
    return true;
}

void press(u32 track, u32 button) {
    if (track < kTracks && button > 4 && button < 8) second[track] = !second[track];
}

// Where per-hit locks of the Shift settings will come from: a held step's
// offsets, indexed by DrumlineLock. Stock Drumline reads M3's and the filter
// envelope's words, so no stock lock word is free for them; they wait for the
// SDK's handle-based lock storage (a handle a step in word 37, the values in an
// SDK pool), which fills `offsets` and returns true for a step that has any.
bool step_offsets(u32, i32*) { return false; }

// 8d5ad776: every publication of a Drumline pad carries its fields; a note's
// would also carry its step's offsets.
void publish(u32 track, u32 slot, i32* payload, bool note) {
    const unsigned char* record = pad_slot(track, slot);
    if (!record || !payload) return;
    i32 offsets[kDrumlineLocks];
    drumline_fold(record, note && step_offsets(track, offsets) ? offsets : nullptr, payload, stock_gain);
}

// Two digits, as stock's "%02d" of 0..99.
void digits(char* text, u32 value) {
    text[0] = static_cast<char>('0' + value / 10 % 10);
    text[1] = static_cast<char>('0' + value % 10);
    text[2] = 0;
}
// A send's caption: TRK, or the key's own amount 0..100.
void send_text(char* text, u32 code) {
    if (!code) {
        text[0] = 'T'; text[1] = 'R'; text[2] = 'K'; text[3] = 0;
        return;
    }
    const u32 n = static_cast<u32>(drum_send_percent(code));
    if (n >= 100) {
        text[0] = '1'; text[1] = '0'; text[2] = '0'; text[3] = 0;
    } else if (n >= 10) {
        digits(text, n);
    } else {
        text[0] = static_cast<char>('0' + n);
        text[1] = 0;
    }
}

// FILTER, as MD KIT's (Max's own code, machines/mdkit/engine.cpp pad_fx):
// a state-variable filter at one resonance, low-pass below 0 and high-pass
// above, its captions, its coefficients a1, a2, a3 (Q31) by value + 16 (16 is
// open), half its k = 1 / Q, and the input's share, under a sixteenth so the
// states stay in range; the output is given back sixteen times over.
const char* const filter_text[32] = {
    "L150", "L196", "L255", "L332", "L433", "L565", "L736", "L959", "L1.3k", "L1.6k", "L2.1k", "L2.8k", "L3.6k", "L4.7k",
    "L6.1k", "L8.0k", "OPEN", "H50", "H68", "H94", "H128", "H175", "H239", "H327", "H447", "H612", "H836", "H1.1k",
    "H1.6k", "H2.1k", "H2.9k", "H4.0k"};
const i32 filter_coefficients[32][3] = {
    {2128288038, 22743090, 243035}, {2122433141, 29566363, 411872}, {2114782444, 38404531, 697428},
    {2104779036, 49829889, 1179705}, {2091690046, 64561071, 1992710}, {2074549919, 83488560, 3359929},
    {2052085841, 107695064, 5651921}, {2022621153, 138460806, 9478490}, {1983953855, 177235797, 15833296},
    {1933211607, 225548624, 26314854}, {1866695103, 284803101, 43452627}, {1779741995, 355890268, 71166429},
    {1666676100, 438516317, 115377283}, {1520947531, 530118894, 184770372}, {1335604780, 624194313, 291716941},
    {1104279013, 707657675, 453489906}, {0, 0, 0}, {2141101139, 7626413, 27165},
    {2138751322, 10417896, 50746}, {2135534394, 14225393, 94759}, {2131128727, 19413685, 176850},
    {2125092072, 26474118, 329811}, {2116815364, 36064613, 614440}, {2105458249, 49058752, 1143106},
    {2089859025, 66602455, 2122577}, {2068409103, 90172191, 3931052}, {2038879717, 121619212, 7254588},
    {1998188157, 163166183, 13323672}, {1942097355, 217289978, 24311312}, {1864866263, 286370497, 43975304},
    {1758925876, 371906456, 78635725}, {1614762720, 473001421, 138553077}};
const i32 kFilterHalfK = 894784853, kFilterFeed = 103021760;
// MD KIT clamps to its half-scale ceiling, the SDK's rule for its machines;
// stock Drumline plays above it, so the filter stops only short of full scale.
const i32 kFilterLimit = opxy::q31::maximum >> 4;
i32 limit(i32 value) { return value < -kFilterLimit ? -kFilterLimit : value > kFilterLimit ? kFilterLimit : value; }
void filter_block(i32* out, i32 filter, i32& ic1_state, i32& ic2_state) {
    using opxy::q31::multiply;
    const i32* c = filter_coefficients[filter + 16];
    const i32 a1 = c[0], a2 = c[1], a3 = c[2];
    i32 ic1 = ic1_state, ic2 = ic2_state;
    if (filter > 0) {
        for (u32 i = 0; i < opxy::block_frames; ++i) {
            const i32 v0 = multiply(out[i], kFilterFeed), v3 = v0 - ic2;
            const i32 v1 = multiply(a1, ic1) + multiply(a2, v3);
            const i32 v2 = ic2 + multiply(a2, ic1) + multiply(a3, v3);
            ic1 = 2 * v1 - ic1;
            ic2 = 2 * v2 - ic2;
            out[i] = limit(v0 - 2 * multiply(kFilterHalfK, v1) - v2) * 16;
        }
    } else {
        for (u32 i = 0; i < opxy::block_frames; ++i) {
            const i32 v0 = multiply(out[i], kFilterFeed), v3 = v0 - ic2;
            const i32 v1 = multiply(a1, ic1) + multiply(a2, v3);
            const i32 v2 = ic2 + multiply(a2, ic1) + multiply(a3, v3);
            ic1 = 2 * v1 - ic1;
            ic2 = 2 * v2 - ic2;
            out[i] = limit(v2) * 16;
        }
    }
    ic1_state = ic1;
    ic2_state = ic2;
}

// Per stock dbox voice: its payload from its first block, SRR's hold and
// FILTER's two states.
struct Voice {
    i32 payload[8];
    i32 held;
    u32 left;
    i32 ic1, ic2;
};
Voice voices[kDboxVoices];
// The dbox object's block count: 0 from its note's start (8d513c0e) until its
// first block counts it (11a06fa6, 11a0748a).
const u32 kBlockCount = 0x30;
// The dbox page's nodes (8d531040..8d53118a): the four value texts, the four
// titles, and the six play-mode icons (8d53101e..8d531038), each shown while
// its byte +0x5c is set.
const u32 kValueNodes = 0x44, kTitleNodes = 0x54, kModeIconNodes = 0x6c, kModeIcons = 6, kNodeShown = 0x5c;
}

extern "C" {
extern const Drumline sdk_drumline_service __asm__("sdk_drumline_service");
const Drumline sdk_drumline_service = {is_drumline, edit, press, publish};
}

// The drum renderer's call of a stock dbox voice's render (8d51b55a, through
// sdk_dbox_methods): stock's render 11a06d28 with the voice's payload as its
// first block found it, so every per-hit value stays with its hit; then SRR
// and FILTER, in MD KIT's order, on the voice's mono block, after stock's level
// and envelope and before the renderer pans it. The send hooks read the codes
// kept here. Audio role.
u32 sdk_dbox_render(unsigned char* self, i32* out, const i32* region, const i32* parameters) {
    typedef u32 Render(unsigned char*, i32*, const i32*, const i32*);
    const u32 index = dbox_index(static_cast<u32>(reinterpret_cast<Address>(self)));
    if (index >= kDboxVoices || !region) return stock<Render>(0x11a06d28)(self, out, region, parameters);
    Voice& v = voices[index];
    if (!*reinterpret_cast<const u32*>(self + kBlockCount)) {
        for (u32 i = 0; i < 8; ++i) v.payload[i] = region[i];
        v.held = v.ic1 = v.ic2 = 0;
        v.left = 0;
    }
    sdk_drum_send_dbox[index] = static_cast<u8>(drumline_codes(v.payload));
    const u32 result = stock<Render>(0x11a06d28)(self, out, v.payload, parameters);
    if (v.payload[0] & static_cast<i32>(kDrumlineCarried)) {
        if (const u32 srr = drumline_srr(v.payload)) drumline_hold(out, opxy::block_frames, drumline_srr_hold(srr), v.held, v.left);
        if (const i32 filter = drumline_filter(v.payload)) filter_block(out, filter, v.ic1, v.ic2);
    }
    return result;
}

// The method table sdk_dbox_render_select gives a dbox voice: stock calls +12.
typedef u32 (*DboxRender)(unsigned char*, i32*, const i32*, const i32*);
extern "C" {
extern const DboxRender sdk_dbox_methods[4] __asm__("sdk_dbox_methods");
const DboxRender sdk_dbox_methods[4] = {nullptr, nullptr, nullptr, sdk_dbox_render};
}

// 8d531192: stock's dbox page has just set its Shift row's four titles (nodes
// at page + 0x54..0x60) and values (+0x44..0x50); its play-mode icons are the
// six nodes at +0x6c..0x80, shown by the byte +0x5c. On row 2 the SDK's titles
// and values replace them and the icons hide (stock sets them again on its
// next pass). Row 1 is stock's.
void sdk_drumline_page(unsigned char* page) {
    const u32 track = byte(kSelectedTrack);
    if (!page || !drumline_track(track) || !second[track]) return;
    const u32 key = stock<u32(u32)>(0x8d4d6c98)(track);
    const unsigned char* record = static_cast<const unsigned char*>(pad_region(track, key));
    if (!record) return;
    // The page's node pointers, 32-bit words.
    void* values[4];
    void* titles[4];
    for (u32 i = 0; i < 4; ++i) {
        values[i] = at<void>(*reinterpret_cast<const u32*>(page + kValueNodes + 4 * i));
        titles[i] = at<void>(*reinterpret_cast<const u32*>(page + kTitleNodes + 4 * i));
    }
    static char text[2][8];
    const DrumlineFields f = drumline_fields(*reinterpret_cast<const u32*>(record));
    for (u32 i = 0; i < kModeIcons; ++i)
        if (const u32 icon = *reinterpret_cast<const u32*>(page + kModeIconNodes + 4 * i)) at<unsigned char>(icon)[kNodeShown] = 0;
    send_text(text[0], f.send[0]);
    send_text(text[1], f.send[1]);
    set_text(titles[0], "filter");
    set_text(values[0], filter_text[f.filter + 16]);
    set_text(titles[1], "srr");
    set_text(values[1], drumline_srr_text(f.srr));
    set_text(titles[2], "fx i");
    set_text(values[2], text[0]);
    set_text(titles[3], "fx ii");
    set_text(values[3], text[1]);
}
