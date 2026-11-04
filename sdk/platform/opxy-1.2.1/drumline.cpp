// Stock Drumline's second Shift row and per-hit Shift locks (drumline.hpp),
// linked only with the build option drumlineRow2. The GUI functions run from
// the shared hooks through sdk_drumline; the render wrapper and the page's
// texts from this unit's own hooks (drumline_hooks.s).
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
// The GUI's project, a track's live words (58 a track) and its stored ones.
const u32 kProject = 0x86bd027c;
const u32 kLive = kProject + 0x4080, kTrackWords = 0xe8;
const u32 kStored = 0x3864;
u32 track_object(u32 track) { return stock<u32(u32)>(0x8d587708)(track); }
// A track's pad record in slot 0..23 (8d58926c finds slot `key` of a Drumline track).
unsigned char* pad_slot(u32 track, u32 slot) {
    return slot < kDrumlinePads ? at<unsigned char>(track_object(track) + kDrumlineRecords + slot * kDrumlineRecordBytes)
                                : nullptr;
}
bool is_drumline(u32 t) { return t < kTracks && track_engine(t) == kDrumlineEngine; }
bool step_held() { return stock<u32()>(0x8d5b7e34)() != 0; }
i32 stock_gain(i32 db) { return stock<i32(i32)>(0x8d5ad604)(db); }

// Each track's Shift row: false the first (stock's), true the second.
bool second[kTracks];

// 8c0b147a on row 2, no step held: SRR, the blank, FX I and FX II of the key,
// one step a detent; stock's republication as its editor does it.
bool edit(u32 track, u32 key, u32 control, const u32* event) {
    if (control < opxy::page_controls || control >= 2 * opxy::page_controls || !second[track]) return false;
    unsigned char* record = static_cast<unsigned char*>(pad_region(track, key));
    const u32 position = control - opxy::page_controls;
    if (!record || !event || position == 1) return true;
    i32 delta = static_cast<i32>(event[1]);
    if (delta > 127) delta = 127;
    if (delta < -127) delta = -127;
    u32* word = reinterpret_cast<u32*>(record);
    DrumlineFields f = drumline_fields(*word);
    u32& field = position == 0 ? f.srr : f.send[position - 2];
    const i32 high = position == 0 ? kDrumlineSrrMax : kDrumlineSendMax;
    const u32 next = static_cast<u32>(drum_lock_clamp(static_cast<i32>(field) + delta, 0, high));
    if (next == field) return true;
    field = next;
    *word = drumline_store(*word, f);
    stock<void()>(0x8d585ee8)();
    stock<void(u32)>(0x8d4d5f18)(key);
    return true;
}

void press(u32 track, u32 button) {
    if (track < kTracks && button > 4 && button < 8) second[track] = !second[track];
}

// 8c0b15aa with a step held and Shift: the shown row's lock through stock's
// editor (8d5ad5bc), which writes the step's lock; TYPE and the blank turn nothing.
u32 lock(u32 project, u32 track, u32 position, u32 event) {
    const u32 which = track < kTracks ? drumline_shift_lock(position, second[track]) : kDrumlineNoLock;
    if (which == kDrumlineNoLock) return 0;
    return stock<u32(u32, u32, u32, u32)>(0x8d5ad5bc)(project, track, drumline_lock_word(which), event);
}

u32 steps(u32 index) {
    const u32 which = drumline_word_lock(index);
    return which < kDrumlineLocks ? drum_lock_buckets(drumline_lock_reach(which)) : 0;
}

// 8d5ad776: every publication of a Drumline pad carries its fields; a note's
// also its step's offsets, read from the live words the sequencer's lock pass
// has just written.
void publish(u32 track, u32 slot, i32* payload, bool note) {
    const unsigned char* record = pad_slot(track, slot);
    if (!record || !payload) return;
    i32 words[kDrumlineLocks], offsets[kDrumlineLocks];
    if (note)
        for (u32 i = 0; i < kDrumlineLocks; ++i)
            words[i] = *at<const volatile i32>(kLive + track * kTrackWords + drumline_lock_word(i) * 4);
    drumline_offsets(note ? words : nullptr, offsets);
    drumline_fold(record, offsets, payload, stock_gain);
}

void load(u32 object) {
    for (u32 i = 0; object && i < kDrumlineLocks; ++i)
        at<volatile i32>(object + kStored)[drumline_lock_word(i)] = kDrumLockNeutral;
}

// The stored word directly, the live one through 8d5acea8, which publishes it;
// not stock's set, which while recording would also record a lock.
void neutral(u32 project, u32 track) {
    if (!drumline_track(track)) return;
    for (u32 i = 0; i < kDrumlineLocks; ++i) {
        const u32 index = drumline_lock_word(i);
        i32* base = at<i32>(track_object(track) + kStored + index * 4);
        if (*base == kDrumLockNeutral) continue;
        *base = kDrumLockNeutral;
        stock<void(u32, u32, u32, i32)>(0x8d5acea8)(project, track, index, kDrumLockNeutral);
    }
}

// Two digits, as stock's "%02d" of 0..99.
void digits(char* text, i32 value) {
    value = drumline_shown(value);
    text[0] = static_cast<char>('0' + value / 10);
    text[1] = static_cast<char>('0' + value % 10);
    text[2] = 0;
}
// A send's caption: TRK, or the key's own amount 0..100.
void send_text(char* text, u32 code) {
    if (!code) {
        text[0] = 'T'; text[1] = 'R'; text[2] = 'K'; text[3] = 0;
        return;
    }
    u32 n = static_cast<u32>(drum_send_percent(code)), i = 0;
    char reverse[4];
    do { reverse[i++] = static_cast<char>('0' + n % 10); n /= 10; } while (n);
    u32 o = 0;
    while (i) text[o++] = reverse[--i];
    text[o] = 0;
}

// Per stock dbox voice: its payload from its first block and SRR's hold.
struct Voice {
    i32 payload[8];
    i32 held;
    u32 left;
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
const Drumline sdk_drumline_service = {is_drumline, edit, press, lock, steps, publish, load, neutral};
}

// The drum renderer's call of a stock dbox voice's render (8d51b55a, through
// sdk_dbox_methods): stock's render 11a06d28 with the voice's payload as its
// first block found it, so every per-hit value stays with its hit; then SRR on
// the voice's mono block, after stock's level and envelope and before the
// renderer pans it. The send hooks read the codes kept here. Audio role.
u32 sdk_dbox_render(unsigned char* self, i32* out, const i32* region, const i32* parameters) {
    typedef u32 Render(unsigned char*, i32*, const i32*, const i32*);
    const u32 index = dbox_index(static_cast<u32>(reinterpret_cast<Address>(self)));
    if (index >= kDboxVoices || !region) return stock<Render>(0x11a06d28)(self, out, region, parameters);
    Voice& v = voices[index];
    if (!*reinterpret_cast<const u32*>(self + kBlockCount)) {
        for (u32 i = 0; i < 8; ++i) v.payload[i] = region[i];
        v.held = 0;
        v.left = 0;
    }
    sdk_drum_send_dbox[index] = static_cast<u8>(drumline_codes(v.payload));
    const u32 result = stock<Render>(0x11a06d28)(self, out, v.payload, parameters);
    if (const u32 srr = drumline_srr(v.payload)) drumline_hold(out, opxy::block_frames, drumline_srr_hold(srr), v.held, v.left);
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
// and values replace them and the icons hide (stock sets them again on its next
// pass); with a step held each value is the key's plus the step's offset.
void sdk_drumline_page(unsigned char* page) {
    const u32 track = byte(kSelectedTrack);
    if (!page || !drumline_track(track)) return;
    const u32 key = stock<u32(u32)>(0x8d4d6c98)(track);
    const unsigned char* record = static_cast<const unsigned char*>(pad_region(track, key));
    if (!record) return;
    i32 offsets[kDrumlineLocks];
    for (u32 i = 0; i < kDrumlineLocks; ++i) offsets[i] = 0;
    if (step_held()) {
        i32 words[kDrumlineLocks];
        for (u32 i = 0; i < kDrumlineLocks; ++i)
            words[i] = stock<i32(u32, u32, u32)>(0x8d5ac93c)(kProject, track, drumline_lock_word(i));
        drumline_offsets(words, offsets);
    }
    // The page's node pointers, 32-bit words.
    void* values[4];
    void* titles[4];
    for (u32 i = 0; i < 4; ++i) {
        values[i] = at<void>(*reinterpret_cast<const u32*>(page + kValueNodes + 4 * i));
        titles[i] = at<void>(*reinterpret_cast<const u32*>(page + kTitleNodes + 4 * i));
    }
    static char text[4][8];
    if (!second[track]) {
        // Stock's row: only a held step's offsets change what it shows.
        if (offsets[kLineRandom]) {
            const i32 word = *reinterpret_cast<const i32*>(record + kDrumPadWord);
            const i32 bucket = drum_lock_clamp(drumline_random_bucket(word) + 2 * offsets[kLineRandom], 0,
                                               static_cast<i32>(kDrumlineRandomBuckets) - 1);
            digits(text[1], drumline_random_shown(drumline_random_word(bucket)));
            set_text(values[1], text[1]);
        }
        if (offsets[kLinePan]) {
            digits(text[2], drumline_pan_shown(drum_lock_clamp(drum_pan(record) + offsets[kLinePan], -kDrumPanLimit, kDrumPanLimit)));
            set_text(values[2], text[2]);
        }
        if (offsets[kLineVolume]) {
            digits(text[3], drumline_volume_shown(drum_lock_clamp(drumline_volume(record) + offsets[kLineVolume],
                                                                  kDrumlineVolumeFloor, kDrumlineVolumeCeiling)));
            set_text(values[3], text[3]);
        }
        return;
    }
    DrumlineFields f = drumline_fields(*reinterpret_cast<const u32*>(record));
    f.srr = static_cast<u32>(drum_lock_clamp(static_cast<i32>(f.srr) + offsets[kLineSrr], 0, kDrumlineSrrMax));
    for (u32 s = 0; s < 2; ++s)
        f.send[s] = static_cast<u32>(drum_lock_clamp(static_cast<i32>(f.send[s]) + offsets[kLineFx1 + s], 0, kDrumlineSendMax));
    for (u32 i = 0; i < kModeIcons; ++i)
        if (const u32 icon = *reinterpret_cast<const u32*>(page + kModeIconNodes + 4 * i)) at<unsigned char>(icon)[kNodeShown] = 0;
    send_text(text[2], f.send[0]);
    send_text(text[3], f.send[1]);
    set_text(titles[0], "srr");
    set_text(values[0], drumline_srr_text(f.srr));
    set_text(titles[1], "");
    set_text(values[1], "");
    set_text(titles[2], "fx i");
    set_text(values[2], text[2]);
    set_text(titles[3], "fx ii");
    set_text(values[3], text[3]);
}
