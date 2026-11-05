// Stock Drumline's second Shift row (drumline.hpp), built
// twice: with the build option drumlineRow2 (DRUMLINE_ON: the generated
// startup's sdk_drumline names the service) and without (null). The real
// shared hooks run against host doubles of stock 1.2.1: the per-key editor
// call (sdk_drum_edit), the generic turn (sdk_synth_turn) and its step count
// (sdk_param_steps), scramble and the frame's refresh, note start's payload
// builder and its publication (drum_locks.cpp), project load's track check,
// the dbox page's Shift texts and the dbox render wrapper.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <opxy/audio.hpp>
#include "stock.hpp"
#include "drumline.hpp"
#include "drum_locks.hpp"
#include "lock_handles.hpp"
#include "synth_controls.hpp"

extern "C" {
opxy::u32 sdk_drum_lock_note(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32)
    __asm__("sdk_drum_lock_note");
opxy::u32 sdk_drum_lock_publish(opxy::u32, opxy::u32, opxy::u32, opxy::i32*) __asm__("sdk_drum_lock_publish");
opxy::u32 sdk_synth_turn(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_synth_turn");
opxy::u32 sdk_synth_scramble(opxy::u32, opxy::u32, opxy::u32, opxy::i32) __asm__("sdk_synth_scramble");
opxy::u32 sdk_dbox_render(unsigned char*, opxy::i32*, const opxy::i32*, const opxy::i32*) __asm__("sdk_dbox_render");
void sdk_drumline_page(unsigned char*) __asm__("sdk_drumline_page");
typedef opxy::u32 (*DboxRender)(unsigned char*, opxy::i32*, const opxy::i32*, const opxy::i32*);
extern const DboxRender sdk_dbox_methods[4] __asm__("sdk_dbox_methods");
extern const opxy::platform::Drumline sdk_drumline_service __asm__("sdk_drumline_service");
}

#ifdef DRUMLINE_ON
const opxy::platform::Drumline* const sdk_drumline = &sdk_drumline_service;
#else
const opxy::platform::Drumline* const sdk_drumline = nullptr;
#endif
// drum_send.cpp's, which this fixture does not link.
opxy::u8 sdk_drum_send_dbox[opxy::platform::kDboxVoices];
// The step-lock handles (lock_handles.cpp), which this fixture does not link: a
// Drumline track is no handle client yet, so no step hands its note a handle.
opxy::u32 opxy::platform::step_locks::take_step(opxy::u32) { return 0; }
opxy::u32 opxy::platform::step_locks::handle_fields(opxy::u32, opxy::u32, opxy::i32*) { return 0; }
bool opxy::platform::step_locks::held_value(opxy::u32, opxy::u32, opxy::u32, opxy::i32&) { return false; }
bool opxy::platform::step_locks::held_set(opxy::u32, opxy::u32, opxy::u32, bool, opxy::i32) { return false; }

namespace {
using namespace opxy;
using namespace opxy::platform;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (0)

const u32 kProject = 0x86bd027c, kLive = kProject + 0x4080;
const u32 kLine = 2;   // tracks: 2 and 5 Drumline, 3 a padMix kit, 6 the drum sampler
#ifdef DRUMLINE_ON
const u32 kKit = 3, kOther = 5, kSampler = 6;
#endif
u32 engines[16] = {0, 0, 4, 41, 0, 4, 3, 0};
u32 selected = kLine, key = 53, page_word, controller = 0x8400dff8, loading;
bool shifted, held;
i32 live[8 * 58];
i32 held_words[58];
// Each track's pattern track object: its stored words at +0x3864, its 24 pads at +0x394c.
const u32 kObject = 0x10000000, kObjectBytes = 0x4600;
unsigned char objects[8][kObjectBytes];
u32 stock_edits, edited_control, dirty, republished, republished_key;
u32 turns, turned_track, turned_index, stock_asks, sets, live_sets;
i32 payload[8], built[8], published[8];
u32 publications, published_track, published_kind;

i32* stored(u32 track) { return reinterpret_cast<i32*>(objects[track] + 0x3864); }
unsigned char* record(u32 track, u32 note) { return objects[track] + 0x394c + (note - 53) * 0x80; }
u32& fields(u32 track, u32 note) { return *reinterpret_cast<u32*>(record(track, note)); }

int ready() { return 1; }
void defaults(u32, u8* model, u8* controls) { *model = 0; for (u32 i = 0; i < 8; ++i) controls[i] = 64; }
const char* label(u32, u32) { return "control"; }
const char* name(u32) { return "model"; }
u32 count(u32) { return 8; }
bool format(u32, i32, char*) { return false; }

u32 get_engine(u32 track) { return track < 16 ? engines[track] : 0; }
u32 get_key(u32) { return key; }
u32 get_track() { return selected; }
u32 get_shift() { return shifted; }
const u32* top() { return &controller; }
void click(u32) {}
void* region(u32 track, u32 note, u32* scratch, u32 nearest) {
    CHECK(track < 8 && note >= 53 && note < 77 && !nearest);
    *scratch = note - 53;
    return record(track, note);
}
u32 canonical(u32, u32 note) { return note; }
void changed() { ++dirty; }
void republish(u32 note) { ++republished; republished_key = note; }
u32 stock_edit(u32, u32 control, const u32*, u32) { ++stock_edits; edited_control = control; return 1234; }
u32 turn(u32 project, u32 track, u32 index, u32) {
    CHECK(project == kProject);
    ++turns; turned_track = track; turned_index = index;
    return 0x700 + index;
}
const char* title(u32, u32) { return "stock"; }
// The page's text nodes, by handle: 0x50000000 + n.
const u32 kNodes = 0x50000000;
char texts[16][16];
u32 text_sets;
unsigned char icons[6][0x60];
u32 set_node_text(void* node, const char* text) {
    const u32 n = static_cast<u32>(static_cast<char(*)[16]>(node) - texts);
    CHECK(n < 16 && std::strlen(text) < 16);
    std::strcpy(texts[n], text);
    ++text_sets;
    return 0;
}
u32 step_held() { return held; }
i32 held_word(u32 project, u32 track, u32 index) { CHECK(project == kProject && track < 8 && index < 58); return held_words[index]; }
u32 track_object(u32 track) { CHECK(track < 8); return kObject + track * kObjectBytes; }
void live_set(u32 project, u32 track, u32 index, i32 value) {
    CHECK(project == kProject && track < 8 && index < 58);
    live[track * 58 + index] = value; ++live_sets;
}
u32 stock_set(u32, u32 track, u32 index, i32 value) { CHECK(track < 8 && index < 58); stored(track)[index] = value; ++sets; return 0; }
i32 scrambled(u32, u32) { return 0x1234; }
u32 stock_steps(u32, u32, u32* scale) { ++stock_asks; *scale = 2; return 0xffffffffu; }
i32 gain(i32 db) { return static_cast<i32>(std::pow(10.0, db / 20.0) / 16.0 * 2147483648.0); }
u32 publisher(u32 project, u32 track, u32 kind, i32* words) {
    CHECK(project == kProject && words == payload);
    std::memcpy(published, words, sizeof(published));
    published_track = track; published_kind = kind;
    return ++publications;
}
// 8d5ad660's Drumline branch (8d5ad6ce..8d5ad776), as stock fills 0x86bd025c,
// then its publication with the pad's slot (8d5ad766).
u32 builder(u32 project, u32 track, u32 note, u32, u32, u32, u32) {
    CHECK(project == kProject);
    const unsigned char* bytes = record(track, note);
    const i32* words = reinterpret_cast<const i32*>(bytes);
    payload[0] = static_cast<i32>((6 * (bytes[24] & 15u) + (bytes[24] >> 4)) * 0x01f07c1fu + 0x00f83e0fu);
    for (u32 i = 1; i < 5; ++i) payload[i] = words[i];
    payload[5] = static_cast<signed char>(bytes[0x1e]) * 0x00a3d70a + 0x40000000;
    payload[6] = words[5] > 0 ? words[5] : 0;
    payload[7] = gain(static_cast<signed char>(bytes[0x1d]));
    std::memcpy(built, payload, sizeof(built));
    return sdk_drum_lock_publish(project, track, note - 53, payload);
}
// 8d5acb9c: the tick's flush of a track's changed live words, which a padMix
// kit's note start runs before its payload (drum_locks.cpp); nothing to flush here.
void flush(u32 project, u32) { CHECK(project == kProject); }
void note(u32 track, u32 note) { sdk_drum_lock_note(kProject, track, note, engines[track], 0, 0, 0); }
void neutral_live() { for (u32 i = 0; i < 8 * 58; ++i) live[i] = 0x40000000; }

// The dbox render 11a06d28: what it was handed, and a ramp out scaled by the
// payload's level word, counting its block as the DSP does (+0x30).
i32 rendered[8];
u32 renders;
u32 dbox_render(unsigned char* self, i32* out, const i32* region, const i32*) {
    std::memcpy(rendered, region, sizeof(rendered));
    for (u32 i = 0; i < block_frames; ++i) out[i] = static_cast<i32>(i * 1000 + 7) + (region[7] >> 20);
    ++*reinterpret_cast<u32*>(self + 0x30);
    return ++renders;
}
unsigned char dbox_objects[3][0x230];
}
// The dbox objects' indices, as the test substitutes for stock's addresses.
u32 host_dbox_index(const unsigned char* self) {
    for (u32 i = 0; i < 3; ++i) if (self == dbox_objects[i]) return 4 + i;
    return kDboxVoices;
}

extern "C" {
static const u8 pad_bits[] = {5, 4};
static const char* const pad_labels[] = {"FILTER", "SRR"};
const DrumMachine sdk_drum_machines[] = {
    {reinterpret_cast<const char*>(41), ready, 1, defaults, label, name, count, 0, nullptr, nullptr, 1,
     2, pad_bits, 1, pad_labels, nullptr}
};
const u32 sdk_drum_machine_count = 1;
const SynthControls sdk_synth_controls[] = {
    {reinterpret_cast<const char*>(41), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, true}
};
const u32 sdk_synth_control_count = 1;
}

Address opxy::platform::host_stock_address(Address address) {
    switch (address) {
    case kSelectedTrack: return reinterpret_cast<Address>(&selected);
    case 0x080238bc: return reinterpret_cast<Address>(&page_word);
    case 0x840a0528: return reinterpret_cast<Address>(&loading);
    case 0x8d58781c: return reinterpret_cast<Address>(&get_engine);
    case 0x8d4d6c98: return reinterpret_cast<Address>(&get_key);
    case 0x8d4d6f14: return reinterpret_cast<Address>(&get_track);
    case 0x8d51f184: return reinterpret_cast<Address>(&get_shift);
    case 0x8c0944b0: return reinterpret_cast<Address>(&top);
    case 0x8d51f1fc: return reinterpret_cast<Address>(&click);
    case 0x8d58926c: return reinterpret_cast<Address>(&region);
    case 0x8d589210: return reinterpret_cast<Address>(&canonical);
    case 0x8d585ee8: return reinterpret_cast<Address>(&changed);
    case 0x8d4d5f18: return reinterpret_cast<Address>(&republish);
    case 0x8d4d5f60: return reinterpret_cast<Address>(&stock_edit);
    case 0x8d5ad5bc: return reinterpret_cast<Address>(&turn);
    case 0x8d506c4c: return reinterpret_cast<Address>(&title);
    case 0x8d6011f0: return reinterpret_cast<Address>(&set_node_text);
    case 0x8d5b7e34: return reinterpret_cast<Address>(&step_held);
    case 0x8d5ac93c: return reinterpret_cast<Address>(&held_word);
    case 0x8d587708: return reinterpret_cast<Address>(&track_object);
    case 0x8d5acea8: return reinterpret_cast<Address>(&live_set);
    case 0x8d5acf28: return reinterpret_cast<Address>(&stock_set);
    case 0x8c0b5230: return reinterpret_cast<Address>(&scrambled);
    case 0x8c0b2468: return reinterpret_cast<Address>(&stock_steps);
    case 0x8d5ad660: return reinterpret_cast<Address>(&builder);
    case 0x8d5ad194: return reinterpret_cast<Address>(&publisher);
    case 0x8d5ad604: return reinterpret_cast<Address>(&gain);
    case 0x8d5acb9c: return reinterpret_cast<Address>(&flush);
    case 0x11a06d28: return reinterpret_cast<Address>(&dbox_render);
    default:
        if (address >= kLive && address < kLive + sizeof(live)) return reinterpret_cast<Address>(live) + (address - kLive);
        if (address >= kObject && address < kObject + sizeof(objects))
            return reinterpret_cast<Address>(objects) + (address - kObject);
        if (address >= kNodes && address < kNodes + 16) return reinterpret_cast<Address>(texts[address - kNodes]);
        if (address >= kNodes + 0x100 && address < kNodes + 0x106) return reinterpret_cast<Address>(icons[address - kNodes - 0x100]);
        std::fprintf(stderr, "unmapped %08x\n", static_cast<unsigned>(address));
        std::abort();
    }
}

namespace {
const u32 up[3] = {1000, 1, 0};
#ifdef DRUMLINE_ON
const u32 down[3] = {1000, static_cast<u32>(-1), 0}, far_up[3] = {1000, 40, 0};
#endif
// M3's filter (16..19) and filter envelope (32..35), which act on Drumline.
const u32 kFilterWords[8] = {16, 17, 18, 19, 32, 33, 34, 35};

// Key 53 of a Drumline track: type 2 (hihat) in mute1, stock's four
// parameters, random at bucket 80, pan 30, volume -6 dB, and in +0x00 SRR 5,
// FX I's code 3, FILTER -4 and bits the option leaves alone.
void stock_pad(u32 track) {
    unsigned char* bytes = record(track, 53);
    i32* words = reinterpret_cast<i32*>(bytes);
    bytes[24] = 0x22;
    for (u32 i = 1; i < 5; ++i) words[i] = static_cast<i32>(0x11110000u * i + 0x1234u);
    words[5] = drumline_random_word(80);
    bytes[0x1e] = 30;
    bytes[0x1d] = static_cast<unsigned char>(-6);
    DrumlineFields f = {5, {3, 0}, -4};
    fields(track, 53) = drumline_store(0xabc00000u, f);
    bytes[26] = 53;
}

void packing() {
    DrumlineFields f = {15, {7, 6}, -16};
    const u32 word = drumline_store(0xffff8000u, f);
    CHECK(word == (0xffff8000u | 15u | 7u << 4 | 6u << 7 | 16u << 10));
    DrumlineFields back = drumline_fields(word);
    CHECK(back.srr == 15 && back.send[0] == 7 && back.send[1] == 6 && back.filter == -16);
    for (i32 filter = -16; filter <= 15; ++filter) {
        f.filter = filter;
        CHECK(drumline_fields(drumline_store(0, f)).filter == filter);
    }
    const DrumlineFields zero = drumline_fields(0xffff8000u);
    CHECK(!zero.srr && !zero.send[0] && !zero.send[1] && !zero.filter);   // an old pad's 0 is neutral
    // The carrier: word 0's low byte; word 6's bits 0..8 only with SRR or FILTER on.
    f.filter = -16;
    i32 words[8] = {0x12345678, 1, 2, 3, 4, 5, 0x7fffffff, 7};
    drumline_carry(words, f);
    CHECK(words[0] == static_cast<i32>(0x12345600u | 7u | 6u << 3 | 0x40u) && words[6] == static_cast<i32>(0x7ffffe00u | 15u | 16u << 4));
    CHECK(drumline_codes(words) == (7u | 6u << 3) && drumline_srr(words) == 15 && drumline_filter(words) == -16);
    const DrumlineFields filtered = {0, {0, 0}, 15};
    words[6] = 0x0abcdef9;
    drumline_carry(words, filtered);
    CHECK(drumline_srr(words) == 0 && drumline_filter(words) == 15 && (words[6] & ~0x1ff) == (0x0abcdef9 & ~0x1ff));
    const DrumlineFields off = {0, {1, 0}, 0};
    words[6] = 0x0abcdef9;
    drumline_carry(words, off);
    CHECK(words[0] == 0x12345601 && words[6] == 0x0abcdef9 && !drumline_srr(words) && !drumline_filter(words));
    CHECK(drumline_codes(words) == 1);
    // The DSP's decode of word 0 never sees the low byte: 66 bins from bits 8..31.
    for (u32 low = 0; low < 256; ++low)
        CHECK((((0x55aa1200u | low) >> 8) * 66u) >> 23 == ((0x55aa1200u >> 8) * 66u) >> 23);
    // A future lock's reach stays within stock's 255 steps; row 2's positions name FILTER, SRR, FX I, FX II.
    for (u32 i = 0; i < kDrumlineLocks; ++i) CHECK(2 * drumline_lock_reach(i) + 1 < 256);
    CHECK(drumline_shift_lock(0, true) == kLineFilter && drumline_shift_lock(1, true) == kLineSrr);
    CHECK(drumline_shift_lock(2, true) == kLineFx1 && drumline_shift_lock(3, true) == kLineFx2);
    CHECK(drumline_shift_lock(0, false) == kDrumlineNoLock && drumline_shift_lock(1, false) == kLineRandom);
    // The fold applies a step's offsets when the lock storage gives some.
    stock_pad(kLine);
    i32 payload_[8] = {0x12345600, 1, 2, 3, 4, drum_pan_word(30), drumline_random_word(80), 9};
    i32 offsets[kDrumlineLocks] = {50, 0, 20, 7, 2, 30, -20};
    drumline_fold(record(kLine, 53), offsets, payload_, gain);
    CHECK(payload_[5] == drum_pan_word(80) && drumline_srr(payload_) == 15 && drumline_filter(payload_) == -16);
    CHECK(drumline_codes(payload_) == (7u | 2u << 3) && (payload_[6] & ~0x1ff) == (drumline_random_word(140) & ~0x1ff));
    // The dbox object index: stock's 27 objects, nothing else.
    CHECK(dbox_index(0x84c50b9c) == 0 && dbox_index(0x84c50b9c + 26 * 0x230) == 26);
    CHECK(dbox_index(0x84c50b9c + 27 * 0x230) == kDboxVoices && dbox_index(0x84c50b9c + 0x231) == kDboxVoices);
    CHECK(dbox_index(0x84c50b98) == kDboxVoices && dbox_index(0) == kDboxVoices);
    for (u32 i = 0; i < kDboxVoices; ++i) CHECK(dbox_index(0x84c50b9c + i * 0x230) == i);
    CHECK(drumline_srr_hold(0) == 1 && drumline_srr_hold(1) == 2 && drumline_srr_hold(15) == 64);
    CHECK(!std::strcmp(drumline_srr_text(0), "OFF") && !std::strcmp(drumline_srr_text(15), "689"));
    std::puts("packing ok");
}

// With the option or without, a Drumline track's M3 filter and filter envelope
// words are stock's: nothing routes, steps, folds or resets them, and a held
// step's Shift turn is stock's own (it locks the engine's words 4..7).
void filter_words_are_stocks() {
    selected = kLine;
    stock_pad(kLine);
    shifted = held = true;
    for (u32 index = 4; index < 8; ++index) {
        sdk_synth_turn(kProject, kLine, index, 0);
        CHECK(turned_index == index && turned_track == kLine);
    }
    shifted = held = false;
    for (u32 index : kFilterWords) {
        u32 scale = 0;
        const u32 asks = stock_asks;
        CHECK(sdk_param_steps(kLine, index, &scale) == 0xffffffffu && scale == 2 && stock_asks == asks + 1);
    }
    // Live filter words, whatever they hold, reach no note's payload.
    neutral_live();
    for (u32 index : kFilterWords) live[kLine * 58 + index] = 0x13572468;
    note(kLine, 53);
    CHECK(!std::memcmp(published + 1, built + 1, 5 * sizeof(i32)) && published[7] == built[7]);
    CHECK((published[0] & ~0xff) == (built[0] & ~0xff));
    // Load, scramble and the frame's refresh leave every stored and live word alone.
    for (u32 i = 0; i < 58; ++i) stored(kLine)[i] = 0x1111 * static_cast<i32>(i + 1);
    CHECK(sdk_drum_load_engine(4, track_object(kLine)) == 4);
    sets = live_sets = 0;
    sdk_synth_scramble(kProject, kLine, 13, 77);
    sdk_send_refresh();
    CHECK(sets == 1 && live_sets == 0);
    for (u32 i = 0; i < 58; ++i) CHECK(stored(kLine)[i] == (i == 13 ? 77 : 0x1111 * static_cast<i32>(i + 1)));
    for (u32 index : kFilterWords) CHECK(live[kLine * 58 + index] == 0x13572468);
    neutral_live();
    std::puts("filter words ok");
}

#ifdef DRUMLINE_ON
// Row 1 is stock's; Shift and a press of encoder 2..4 switch to row 2:
// FILTER, SRR, FX I, FX II.
void rows() {
    selected = kLine;
    stock_pad(kLine);
    const u32 before = fields(kLine, 53), edits0 = stock_edits;
    for (u32 control = 0; control < 8; ++control) {
        const u32 edits = stock_edits;
        sdk_drum_edit(53, control, up, 0);
        CHECK(stock_edits == edits + 1 && edited_control == control);
    }
    CHECK(fields(kLine, 53) == before && !republished);
    sdk_drumline->press(kLine, 4);            // encoder 1: stock's play-mode cycle, no switch
    sdk_drum_edit(53, 4, up, 0);
    CHECK(stock_edits == edits0 + 9);
    sdk_drumline->press(kLine, 6);            // encoder 3: row 2
    // FILTER, -16..15.
    sdk_drum_edit(53, 4, up, 0);
    CHECK(drumline_fields(fields(kLine, 53)).filter == -3 && republished == 1 && republished_key == 53 && dirty == 1);
    sdk_drum_edit(53, 4, far_up, 0);
    CHECK(drumline_fields(fields(kLine, 53)).filter == 15 && republished == 2);
    sdk_drum_edit(53, 4, up, 0);              // at the top: nothing moves, nothing republished
    CHECK(republished == 2);
    const u32 minus[3] = {1000, static_cast<u32>(-40), 0};
    sdk_drum_edit(53, 4, minus, 0);
    CHECK(drumline_fields(fields(kLine, 53)).filter == -16 && republished == 3);
    // SRR, 0..15.
    sdk_drum_edit(53, 5, up, 0);
    CHECK(drumline_fields(fields(kLine, 53)).srr == 6 && republished == 4);
    sdk_drum_edit(53, 5, far_up, 0);
    CHECK(drumline_fields(fields(kLine, 53)).srr == 15);
    // FX I and FX II, codes 0..7.
    sdk_drum_edit(53, 6, far_up, 0);
    sdk_drum_edit(53, 7, down, 0);            // already TRK
    DrumlineFields f = drumline_fields(fields(kLine, 53));
    CHECK(f.send[0] == 7 && f.send[1] == 0 && f.filter == -16 && f.srr == 15 && republished == 6);
    sdk_drum_edit(53, 7, up, 0);
    CHECK(drumline_fields(fields(kLine, 53)).send[1] == 1);
    CHECK((fields(kLine, 53) & ~kDrumlineFieldMask) == 0xabc00000u);   // the other bits stay
    CHECK(stock_edits == edits0 + 9);
    // Unshifted encoders stay stock's on row 2.
    sdk_drum_edit(53, 2, up, 0);
    CHECK(stock_edits == edits0 + 10 && edited_control == 2);
    // Each track keeps its own row.
    selected = kOther;
    stock_pad(kOther);
    sdk_drum_edit(53, 4, up, 0);
    CHECK(stock_edits == edits0 + 11);
    selected = kLine;
    sdk_drumline->press(kLine, 5);            // encoder 2: back to row 1
    sdk_drum_edit(53, 4, up, 0);
    CHECK(stock_edits == edits0 + 12);
    // A kit is never Drumline, though classifiers alias it to 4.
    CHECK(sdk_drum_alias(41) == 4 && !drumline_track(kKit) && drumline_track(kLine) && !drumline_track(kSampler));
    CHECK(!drumline_track(8) && !drumline_track(14));
    stock_pad(kLine);
    std::puts("rows ok");
}

// Every publication of a Drumline pad carries its fields, a note's as any other's.
void publications_carry_the_fields() {
    neutral_live();
    stock_pad(kLine);
    note(kLine, 53);
    CHECK(published_track == kLine && published_kind == 0);
    CHECK((published[0] & ~0xff) == (built[0] & ~0xff) && (published[0] & 0xff) == (3 | 0x40));
    CHECK(!std::memcmp(published + 1, built + 1, 5 * sizeof(i32)) && published[7] == built[7]);
    CHECK(published[6] == static_cast<i32>((static_cast<u32>(built[6]) & ~0x1ffu) | 5u | 28u << 4));
    CHECK(drumline_codes(published) == 3 && drumline_srr(published) == 5 && drumline_filter(published) == -4);
    // SRR and FILTER off: word 6 is stock's own.
    DrumlineFields f = {0, {0, 6}, 0};
    fields(kLine, 53) = drumline_store(fields(kLine, 53), f);
    note(kLine, 53);
    CHECK(published[6] == built[6] && drumline_codes(published) == 6u << 3 && !drumline_srr(published));
    std::memcpy(payload, built, sizeof(payload));
    sdk_drum_lock_publish(kProject, kLine, 0, payload);   // the editor's and its previews'
    CHECK(published[6] == built[6] && drumline_codes(published) == 6u << 3);
    stock_pad(kLine);
    // A kit (aliased to 4) and the drum sampler publish as before: no carrier.
    std::memcpy(record(kKit, 53), record(kLine, 53), 0x80);
    note(kKit, 53);
    CHECK(published_track == kKit && !std::memcmp(published, built, sizeof(built)));
    std::memcpy(payload, built, sizeof(payload));
    sdk_drum_lock_publish(kProject, kSampler, 0, payload);
    CHECK(published_track == kSampler && !std::memcmp(published, built, sizeof(built)));
    std::puts("publications ok");
}

// The dbox page's Shift texts.
unsigned char page[0x100];
void page_nodes() {
    for (u32 i = 0; i < 4; ++i) {
        const u32 value = kNodes + i, title = kNodes + 4 + i;
        std::memcpy(page + 0x44 + 4 * i, &value, 4);
        std::memcpy(page + 0x54 + 4 * i, &title, 4);
    }
    for (u32 i = 0; i < 6; ++i) {
        const u32 icon = kNodes + 0x100 + i;
        std::memcpy(page + 0x6c + 4 * i, &icon, 4);
    }
}
// As stock's Shift branch leaves them: the type's icon shown, "random", "pan", "volume".
void stock_texts() {
    const char* stock[8] = {"", "40", "64", "47", "", "random", "pan", "volume"};
    for (u32 i = 0; i < 8; ++i) std::strcpy(texts[i], stock[i]);
    for (u32 i = 0; i < 6; ++i) icons[i][0x5c] = i == 2;
    text_sets = 0;
}
void page_texts() {
    page_nodes();
    selected = kLine;
    key = 53;
    stock_pad(kLine);
    stock_texts();
    held = true;
    sdk_drumline_page(page);
    CHECK(text_sets == 0 && icons[2][0x5c] == 1);       // row 1 is stock's, step held or not
    held = false;
    sdk_drumline_page(page);
    CHECK(text_sets == 0);
    // Row 2: FILTER, SRR, FX I, FX II; the icons hide.
    sdk_drumline->press(kLine, 6);
    stock_texts();
    sdk_drumline_page(page);
    CHECK(!std::strcmp(texts[4], "filter") && !std::strcmp(texts[0], "L3.6k"));
    CHECK(!std::strcmp(texts[5], "srr") && !std::strcmp(texts[1], "7.3k"));
    CHECK(!std::strcmp(texts[6], "fx i") && !std::strcmp(texts[2], "33"));
    CHECK(!std::strcmp(texts[7], "fx ii") && !std::strcmp(texts[3], "TRK"));
    for (u32 i = 0; i < 6; ++i) CHECK(icons[i][0x5c] == 0);
    // MD KIT's FILTER captions at its ends and centre; the sends' 0 and 100.
    const struct { i32 filter; u32 send; const char* caption; const char* amount; } cases[] = {
        {-16, 1, "L150", "0"}, {0, 7, "OPEN", "100"}, {1, 4, "H50", "50"}, {15, 2, "H4.0k", "17"}};
    for (const auto& c : cases) {
        DrumlineFields f = {0, {c.send, 0}, c.filter};
        fields(kLine, 53) = drumline_store(0, f);
        sdk_drumline_page(page);
        CHECK(!std::strcmp(texts[0], c.caption) && !std::strcmp(texts[2], c.amount) && !std::strcmp(texts[1], "OFF"));
    }
    sdk_drumline->press(kLine, 6);
    // Another engine's page is left alone.
    sdk_drumline->press(kKit, 6);
    selected = kKit;
    stock_texts();
    sdk_drumline_page(page);
    CHECK(text_sets == 0);
    sdk_drumline->press(kKit, 6);
    selected = kLine;
    stock_pad(kLine);
    std::puts("page ok");
}

// MD KIT's FILTER, worked out apart from the wrapper in 64-bit arithmetic.
i32 q31(int64_t a, int64_t b) {
    const int64_t product = a * b;
    return static_cast<i32>(product >= 0 ? product >> 31 : -((-product + ((int64_t(1) << 31) - 1)) >> 31));
}
void reference_filter(i32* out, i32 filter, i32& ic1, i32& ic2) {
    static const i32 c[2][3] = {{2128288038, 22743090, 243035}, {1614762720, 473001421, 138553077}};   // -16 and 15
    const i32* k = filter < 0 ? c[0] : c[1];
    const i32 limit = 0x7fffffff >> 4;
    for (u32 i = 0; i < block_frames; ++i) {
        const i32 v0 = q31(out[i], 103021760), v3 = v0 - ic2;
        const i32 v1 = q31(k[0], ic1) + q31(k[1], v3);
        const i32 v2 = ic2 + q31(k[1], ic1) + q31(k[2], v3);
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        const i32 y = filter > 0 ? v0 - 2 * q31(894784853, v1) - v2 : v2;
        out[i] = (y < -limit ? -limit : y > limit ? limit : y) * 16;
    }
}

// The render wrapper: each voice's payload from its first block, SRR then
// FILTER after stock's render, the send codes for the mixer.
void render_wrapper() {
    CHECK(sdk_dbox_methods[3] == &sdk_dbox_render && !sdk_dbox_methods[0]);
    i32 region[8] = {0x12345600, 1, 2, 3, 4, 0x40000000, 0x00500000, 0x08000000};
    drumline_carry(region, DrumlineFields{5, {3, 6}, 0});          // hold 6 samples
    const i32 params[58] = {};
    i32 out[block_frames], plain[block_frames];
    unsigned char* voice = dbox_objects[1];   // index 5
    std::memset(voice, 0, 0x230);
    sdk_dbox_render(voice, out, region, params);
    CHECK(!std::memcmp(rendered, region, sizeof(rendered)));
    CHECK(sdk_drum_send_dbox[5] == (3u | 6u << 3) && !sdk_drum_send_dbox[4]);
    for (u32 i = 0; i < block_frames; ++i) plain[i] = static_cast<i32>(i * 1000 + 7) + (region[7] >> 20);
    for (u32 i = 0; i < block_frames; ++i) CHECK(out[i] == plain[i - i % 6]);
    // The next block: the pad republished meanwhile; this voice keeps its
    // first block's payload, codes and hold.
    i32 next[8] = {0x12345600, 9, 9, 9, 9, 0x40000000, 0x00500000, 0x04000000};
    drumline_carry(next, DrumlineFields{0, {7, 7}, 0});
    sdk_dbox_render(voice, out, next, params);
    CHECK(!std::memcmp(rendered, region, sizeof(rendered)) && sdk_drum_send_dbox[5] == (3u | 6u << 3));
    // 64 = 10 x 6 + 4: the hold carries 2 samples of the last block's sixth into this one.
    CHECK(out[0] == plain[60] && out[1] == plain[60]);
    for (u32 i = 2; i < block_frames; ++i) CHECK(out[i] == plain[(i - 2) - (i - 2) % 6 + 2]);
    // A new note (stock's trigger zeroes +0x30): its payload; SRR and FILTER at
    // 0 leave stock's block bit for bit.
    std::memset(voice + 0x30, 0, 4);
    sdk_dbox_render(voice, out, next, params);
    CHECK(!std::memcmp(rendered, next, sizeof(rendered)) && sdk_drum_send_dbox[5] == (7u | 7u << 3));
    for (u32 i = 0; i < block_frames; ++i) CHECK(out[i] == static_cast<i32>(i * 1000 + 7) + (next[7] >> 20));
    // FILTER: MD KIT's response, its states carried across blocks and reset by a note.
    for (i32 filter : {-16, 15}) {
        i32 filtered[8] = {0x12345600, 1, 2, 3, 4, 0x40000000, 0x00500000, 0x7f000000};
        drumline_carry(filtered, DrumlineFields{0, {0, 0}, filter});
        std::memset(voice + 0x30, 0, 4);
        i32 ic1 = 0, ic2 = 0, expected[block_frames];
        for (u32 block = 0; block < 3; ++block) {
            sdk_dbox_render(voice, out, filtered, params);
            for (u32 i = 0; i < block_frames; ++i) expected[i] = static_cast<i32>(i * 1000 + 7) + (filtered[7] >> 20);
            reference_filter(expected, filter, ic1, ic2);
            CHECK(!std::memcmp(out, expected, sizeof(out)));
        }
    }
    // Low-pass at -16 smooths a fast alternation; high-pass at 15 removes a constant.
    {
        i32 a[block_frames], b[block_frames], s1 = 0, s2 = 0, t1 = 0, t2 = 0;
        for (u32 i = 0; i < block_frames; ++i) { a[i] = i & 1 ? 0x20000000 : -0x20000000; b[i] = 0x20000000; }
        for (u32 n = 0; n < 8; ++n) {
            for (u32 i = 0; i < block_frames; ++i) { a[i] = i & 1 ? 0x20000000 : -0x20000000; b[i] = 0x20000000; }
            reference_filter(a, -16, s1, s2);
            reference_filter(b, 15, t1, t2);
        }
        CHECK(std::abs(a[63]) < 0x01000000 && std::abs(b[63]) < 0x01000000);
    }
    // Voices are their own; an object outside stock's 27 gets stock's render untouched.
    std::memset(dbox_objects[0], 0, 0x230);
    sdk_dbox_render(dbox_objects[0], out, region, params);
    CHECK(sdk_drum_send_dbox[4] == (3u | 6u << 3));
    unsigned char stray[0x230] = {};
    sdk_dbox_render(stray, out, next, params);
    CHECK(!std::memcmp(rendered, next, sizeof(rendered)));
    std::puts("render ok");
}
#else
// Without the option the per-key editor is stock's and publications are the builder's own.
void stock_behaviour() {
    selected = kLine;
    stock_pad(kLine);
    const u32 before = fields(kLine, 53);
    for (u32 control = 0; control < 8; ++control) {
        const u32 edits = stock_edits;
        sdk_drum_edit(53, control, up, 0);
        CHECK(stock_edits == edits + 1 && edited_control == control);
    }
    CHECK(fields(kLine, 53) == before && !republished);
    neutral_live();
    note(kLine, 53);
    CHECK(!std::memcmp(published, built, sizeof(built)));
    std::puts("stock behaviour ok");
}
#endif
}

int main() {
    packing();
    filter_words_are_stocks();
#ifdef DRUMLINE_ON
    rows();
    publications_carry_the_fields();
    page_texts();
    render_wrapper();
#else
    stock_behaviour();
#endif
    return 0;
}
