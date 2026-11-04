// Stock Drumline's second Shift row and per-hit locks (drumline.hpp), built
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
#include <cstring>
#include <initializer_list>
#include <opxy/audio.hpp>
#include "stock.hpp"
#include "drumline.hpp"
#include "drum_locks.hpp"
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
void note(u32 track, u32 note) { sdk_drum_lock_note(kProject, track, note, engines[track], 0, 0, 0); }
i32* locks(u32 track) { return live + track * 58; }
void lock(u32 track, u32 which, i32 offset) { locks(track)[drumline_lock_word(which)] = drum_lock_word(offset, drumline_lock_reach(which)); }
void neutral_live() { for (u32 i = 0; i < 8 * 58; ++i) live[i] = kDrumLockNeutral; }

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
const u32 down[3] = {1000, static_cast<u32>(-1), 0}, far_up[3] = {1000, 9, 0};
#endif

// Key 53 of a Drumline track: type 2 (hihat) in mute1, stock's four
// parameters, random at bucket 80, pan 30, volume -6 dB, and in +0x00 SRR 5,
// FX I's code 3 and bits the option leaves alone.
void stock_pad(u32 track) {
    unsigned char* bytes = record(track, 53);
    i32* words = reinterpret_cast<i32*>(bytes);
    bytes[24] = 0x22;
    for (u32 i = 1; i < 5; ++i) words[i] = static_cast<i32>(0x11110000u * i + 0x1234u);
    words[5] = drumline_random_word(80);
    bytes[0x1e] = 30;
    bytes[0x1d] = static_cast<unsigned char>(-6);
    DrumlineFields f = {5, {3, 0}};
    fields(track, 53) = drumline_store(0xabc00000u, f);
    bytes[26] = 53;
}

void packing() {
    DrumlineFields f = {15, {7, 6}};
    const u32 word = drumline_store(0xfffffc00u, f);
    CHECK(word == (0xfffffc00u | 15u | 7u << 4 | 6u << 7));
    const DrumlineFields back = drumline_fields(word);
    CHECK(back.srr == 15 && back.send[0] == 7 && back.send[1] == 6);
    const DrumlineFields zero = drumline_fields(0xfffffc00u);
    CHECK(!zero.srr && !zero.send[0] && !zero.send[1]);   // an old pad's 0 is neutral
    // The carrier: word 0's low byte, word 6's low nibble only with SRR on.
    i32 words[8] = {0x12345678, 1, 2, 3, 4, 5, 0x7fffffff, 7};
    drumline_carry(words, f);
    CHECK(words[0] == static_cast<i32>(0x12345600u | 7u | 6u << 3 | 0x40u) && words[6] == 0x7fffffff);
    CHECK(drumline_codes(words) == (7u | 6u << 3) && drumline_srr(words) == 15);
    const DrumlineFields off = {0, {1, 0}};
    words[6] = 0x0abcdef9;
    drumline_carry(words, off);
    CHECK(words[0] == 0x12345601 && words[6] == 0x0abcdef9 && !drumline_srr(words) && drumline_codes(words) == 1);
    // The DSP's decode of word 0 never sees the low byte: 66 bins from bits 8..31.
    for (u32 low = 0; low < 256; ++low)
        CHECK((((0x55aa1200u | low) >> 8) * 66u) >> 23 == ((0x55aa1200u >> 8) * 66u) >> 23);
    // The lock words and their bucket counts (stock steps at most 255).
    const u32 lock_words[kDrumlineLocks] = {16, 17, 18, 19, 32, 33};
    const u32 buckets[kDrumlineLocks] = {201, 101, 31, 15, 15, 199};
    for (u32 i = 0; i < kDrumlineLocks; ++i) {
        CHECK(drumline_lock_word(i) == lock_words[i] && drumline_word_lock(lock_words[i]) == i);
        CHECK(drum_lock_buckets(drumline_lock_reach(i)) == buckets[i]);
        CHECK(drum_lock_offset(kDrumLockNeutral, drumline_lock_reach(i)) == 0);
        for (i32 o = -drumline_lock_reach(i); o <= drumline_lock_reach(i); ++o)
            CHECK(drum_lock_offset(drum_lock_word(o, drumline_lock_reach(i)), drumline_lock_reach(i)) == o);
    }
    for (u32 index : {0u, 4u, 7u, 15u, 20u, 31u, 34u, 41u}) CHECK(drumline_word_lock(index) == kDrumlineNoLock);
    // Row 1: TYPE nothing, RANDOM, PAN, VOLUME; row 2: SRR, the blank, FX I, FX II.
    CHECK(drumline_shift_lock(0, false) == kDrumlineNoLock && drumline_shift_lock(1, false) == kLineRandom);
    CHECK(drumline_shift_lock(2, false) == kLinePan && drumline_shift_lock(3, false) == kLineVolume);
    CHECK(drumline_shift_lock(0, true) == kLineSrr && drumline_shift_lock(1, true) == kDrumlineNoLock);
    CHECK(drumline_shift_lock(2, true) == kLineFx1 && drumline_shift_lock(3, true) == kLineFx2);
    // The dbox object index: stock's 27 objects, nothing else.
    CHECK(dbox_index(0x84c50b9c) == 0 && dbox_index(0x84c50b9c + 26 * 0x230) == 26);
    CHECK(dbox_index(0x84c50b9c + 27 * 0x230) == kDboxVoices && dbox_index(0x84c50b9c + 0x231) == kDboxVoices);
    CHECK(dbox_index(0x84c50b98) == kDboxVoices && dbox_index(0) == kDboxVoices);
    for (u32 i = 0; i < kDboxVoices; ++i) CHECK(dbox_index(0x84c50b9c + i * 0x230) == i);
    // SRR's holds and captions.
    CHECK(drumline_srr_hold(0) == 1 && drumline_srr_hold(1) == 2 && drumline_srr_hold(15) == 64);
    CHECK(!std::strcmp(drumline_srr_text(0), "OFF") && !std::strcmp(drumline_srr_text(5), "7.3k"));
    CHECK(!std::strcmp(drumline_srr_text(15), "689"));
    std::puts("packing ok");
}

#ifdef DRUMLINE_ON
// Row 1 is stock's; Shift and a press of encoder 2..4 switch to row 2.
void rows() {
    selected = kLine;
    stock_pad(kLine);
    const u32 before = fields(kLine, 53);
    for (u32 control = 0; control < 8; ++control) {
        const u32 edits = stock_edits;
        sdk_drum_edit(53, control, up, 0);
        CHECK(stock_edits == edits + 1 && edited_control == control);
    }
    CHECK(fields(kLine, 53) == before && !republished);
    sdk_drumline->press(kLine, 4);            // encoder 1: stock's play-mode cycle, no switch
    sdk_drum_edit(53, 4, up, 0);
    CHECK(edited_control == 4 && stock_edits == 9);
    sdk_drumline->press(kLine, 6);            // encoder 3: row 2
    // SRR, one a detent, within 0..15.
    sdk_drum_edit(53, 4, up, 0);
    CHECK(drumline_fields(fields(kLine, 53)).srr == 6 && republished == 1 && republished_key == 53 && dirty == 1);
    sdk_drum_edit(53, 4, far_up, 0);
    CHECK(drumline_fields(fields(kLine, 53)).srr == 15 && republished == 2);
    sdk_drum_edit(53, 4, up, 0);              // at the top: nothing moves, nothing republished
    CHECK(republished == 2);
    // The blank turns nothing; FX I and FX II, codes 0..7.
    sdk_drum_edit(53, 5, up, 0);
    CHECK(republished == 2 && stock_edits == 9);
    sdk_drum_edit(53, 6, far_up, 0);
    sdk_drum_edit(53, 7, down, 0);            // already TRK
    DrumlineFields f = drumline_fields(fields(kLine, 53));
    CHECK(f.send[0] == 7 && f.send[1] == 0 && republished == 3);
    sdk_drum_edit(53, 7, up, 0);
    CHECK(drumline_fields(fields(kLine, 53)).send[1] == 1);
    CHECK((fields(kLine, 53) & ~kDrumlineFieldMask) == 0xabc00000u);   // the other bits stay
    CHECK(stock_edits == 9);
    // Unshifted encoders stay stock's on row 2.
    sdk_drum_edit(53, 2, up, 0);
    CHECK(stock_edits == 10 && edited_control == 2);
    // Each track keeps its own row.
    selected = kOther;
    stock_pad(kOther);
    sdk_drum_edit(53, 4, up, 0);
    CHECK(stock_edits == 11);
    selected = kLine;
    sdk_drumline->press(kLine, 5);            // encoder 2: back to row 1
    sdk_drum_edit(53, 4, up, 0);
    CHECK(stock_edits == 12);
    // A kit is never Drumline, though classifiers alias it to 4.
    CHECK(sdk_drum_alias(41) == 4 && !drumline_track(kKit) && drumline_track(kLine) && !drumline_track(kSampler));
    CHECK(!drumline_track(8) && !drumline_track(14));
    stock_pad(kLine);
    std::puts("rows ok");
}

// With a step held, Shift and a turn lock the shown row's field.
void lock_routing() {
    selected = kLine;
    held = true;
    shifted = true;
    u32 result;
    // Row 1: TYPE nothing, RANDOM 33, PAN 16, VOLUME 17.
    turns = 0;
    CHECK(sdk_synth_turn(kProject, kLine, 4, 0) == 0 && turns == 0);
    result = sdk_synth_turn(kProject, kLine, 5, 0);
    CHECK(turns == 1 && turned_index == 33 && result == 0x700 + 33);
    sdk_synth_turn(kProject, kLine, 6, 0);
    CHECK(turned_index == 16 && turned_track == kLine);
    sdk_synth_turn(kProject, kLine, 7, 0);
    CHECK(turned_index == 17 && turns == 3);
    // Row 2: SRR 18, the blank nothing, FX I 19, FX II 32.
    sdk_drumline->press(kLine, 7);
    sdk_synth_turn(kProject, kLine, 4, 0);
    CHECK(turned_index == 18);
    CHECK(sdk_synth_turn(kProject, kLine, 5, 0) == 0 && turns == 4);
    sdk_synth_turn(kProject, kLine, 6, 0);
    CHECK(turned_index == 19);
    sdk_synth_turn(kProject, kLine, 7, 0);
    CHECK(turned_index == 32 && turns == 6);
    sdk_drumline->press(kLine, 7);
    // Unshifted, or off the engine page, or no step held: stock's own index.
    shifted = false;
    sdk_synth_turn(kProject, kLine, 2, 0);
    CHECK(turned_index == 2);
    shifted = true;
    page_word = 1;
    sdk_synth_turn(kProject, kLine, 5, 0);
    CHECK(turned_index == 5);
    page_word = 0;
    held = false;
    sdk_synth_turn(kProject, kLine, 5, 0);
    CHECK(turned_index == 5 && turns == 9);
    // A padMix kit keeps its own locks: its second encoder, CHOKE, locks nothing.
    held = true;
    CHECK(sdk_synth_turn(kProject, kKit, 5, 0) == 0 && turns == 9);
    sdk_synth_turn(kProject, kKit, 6, 0);
    CHECK(turned_track == kKit && turned_index == 16);
    held = shifted = false;
    // One unit a detent: each lock word's bucket count, scale 1; other words stock's.
    const u32 lock_words[kDrumlineLocks] = {16, 17, 18, 19, 32, 33};
    const u32 buckets[kDrumlineLocks] = {201, 101, 31, 15, 15, 199};
    for (u32 i = 0; i < kDrumlineLocks; ++i) {
        u32 scale = 0;
        CHECK(sdk_param_steps(kLine, lock_words[i], &scale) == buckets[i] && scale == 1);
    }
    u32 scale = 0;
    const u32 asks = stock_asks;
    CHECK(sdk_param_steps(kLine, 20, &scale) == 0xffffffffu && scale == 2 && stock_asks == asks + 1);
    CHECK(sdk_param_steps(kLine, 4, &scale) == 0xffffffffu && stock_asks == asks + 2);
    CHECK(sdk_param_steps(kKit, 32, &scale) == 0xffffffffu && stock_asks == asks + 3);   // not a kit's lock
    CHECK(sdk_param_steps(kKit, 18, &scale) == 63 && scale == 1);                      // the kit's FILTER
    CHECK(sdk_param_steps(kSampler, 16, &scale) == 0xffffffffu && stock_asks == asks + 4);
    std::puts("lock routing ok");
}

// Every publication of a Drumline pad carries its fields; a note's also its step's offsets.
void publications_carry_the_fields() {
    neutral_live();
    stock_pad(kLine);
    note(kLine, 53);
    CHECK(publications == 1 && published_track == kLine && published_kind == 0);
    // Stock's words, with the carrier: FX I 3, FX II TRK, SRR 5 in word 6's low nibble.
    CHECK((published[0] & ~0xff) == (built[0] & ~0xff) && (published[0] & 0xff) == (3 | 0x40));
    CHECK(!std::memcmp(published + 1, built + 1, 5 * sizeof(i32)) && published[7] == built[7]);
    CHECK(published[6] == ((built[6] & ~15) | 5));
    CHECK(drumline_codes(published) == 3 && drumline_srr(published) == 5);
    // SRR off: word 6 is stock's own.
    DrumlineFields f = {0, {0, 6}};
    fields(kLine, 53) = drumline_store(fields(kLine, 53), f);
    note(kLine, 53);
    CHECK(published[6] == built[6] && drumline_codes(published) == 6u << 3 && !drumline_srr(published));
    stock_pad(kLine);
    // A step's locks, each clamped to its field.
    lock(kLine, kLinePan, 50);       // 30 + 50
    lock(kLine, kLineVolume, -10);   // -6 - 10
    lock(kLine, kLineSrr, 15);       // 5 + 15, at 15
    lock(kLine, kLineFx1, 7);        // 3 + 7, at 7
    lock(kLine, kLineFx2, 2);        // TRK + 2
    lock(kLine, kLineRandom, 30);    // bucket 80 + 60
    note(kLine, 53);
    CHECK(published[5] == drum_pan_word(80) && published[7] == gain(-16));
    CHECK((published[6] & ~15) == (drumline_random_word(140) & ~15) && drumline_srr(published) == 15);
    CHECK(drumline_codes(published) == (7u | 2u << 3));
    CHECK(!std::memcmp(published + 1, built + 1, 4 * sizeof(i32)) && (published[0] & ~0xff) == (built[0] & ~0xff));
    lock(kLine, kLinePan, -100);
    lock(kLine, kLineVolume, 50);
    lock(kLine, kLineSrr, -15);
    lock(kLine, kLineFx1, -7);
    lock(kLine, kLineFx2, -7);
    lock(kLine, kLineRandom, -99);
    note(kLine, 53);
    CHECK(published[5] == drum_pan_word(-70) && published[7] == gain(20));
    CHECK(published[6] == drumline_random_word(0) && !drumline_srr(published) && !drumline_codes(published));
    lock(kLine, kLineVolume, -50);
    note(kLine, 53);
    CHECK(published[7] == gain(-30));
    // A stray 0 in a lock word offsets nothing.
    for (u32 i = 0; i < kDrumlineLocks; ++i) locks(kLine)[drumline_lock_word(i)] = 0;
    note(kLine, 53);
    CHECK(published[5] == built[5] && published[7] == built[7] && drumline_srr(published) == 5);
    // Every other publication (the editor's, its previews) carries the key's own fields.
    lock(kLine, kLinePan, 50);
    lock(kLine, kLineSrr, 3);
    std::memcpy(payload, built, sizeof(payload));
    sdk_drum_lock_publish(kProject, kLine, 0, payload);
    CHECK(published[5] == built[5] && drumline_srr(published) == 5 && drumline_codes(published) == 3);
    // Another track's locks are not this note's.
    neutral_live();
    lock(kOther, kLinePan, 50);
    note(kLine, 53);
    CHECK(published[5] == built[5]);
    // A kit (aliased to 4) and the drum sampler publish as before: no carrier.
    std::memcpy(record(kKit, 53), record(kLine, 53), 0x80);
    note(kKit, 53);
    CHECK(published_track == kKit && !std::memcmp(published, built, sizeof(built)));
    std::memcpy(payload, built, sizeof(payload));
    sdk_drum_lock_publish(kProject, kSampler, 0, payload);
    CHECK(published_track == kSampler && !std::memcmp(published, built, sizeof(built)));
    neutral_live();
    std::puts("publications ok");
}

// The lock words' stored base stays neutral: at load, after scramble, once a frame.
void neutral_base() {
    for (u32 i = 0; i < 58; ++i) stored(kLine)[i] = stored(kKit)[i] = stored(7)[i] = 0x1111 * static_cast<i32>(i + 1);
    CHECK(sdk_drum_load_engine(4, track_object(kLine)) == 4);
    for (u32 i = 0; i < 58; ++i)
        CHECK(stored(kLine)[i] == (drumline_word_lock(i) < kDrumlineLocks ? kDrumLockNeutral : 0x1111 * static_cast<i32>(i + 1)));
    CHECK(sdk_drum_load_engine(41, track_object(kKit)) == 4);   // a kit: its own four only
    CHECK(stored(kKit)[16] == kDrumLockNeutral && stored(kKit)[19] == kDrumLockNeutral && stored(kKit)[32] == 0x1111 * 33);
    CHECK(sdk_drum_load_engine(7, track_object(7)) == 7 && stored(7)[16] == 0x1111 * 17);
    // Scramble: stock's last write, then the six back to neutral, stored and live.
    for (u32 i = 0; i < kDrumlineLocks; ++i) stored(kLine)[drumline_lock_word(i)] = 0x2222;
    stored(kLine)[33] = kDrumLockNeutral;
    sets = live_sets = 0;
    neutral_live();
    for (u32 i = 0; i < kDrumlineLocks; ++i) live[kLine * 58 + drumline_lock_word(i)] = 0x2222;
    sdk_synth_scramble(kProject, kLine, 13, 77);
    CHECK(sets == 1 && stored(kLine)[13] == 77 && live_sets == 5);
    for (u32 i = 0; i < kDrumlineLocks; ++i) CHECK(stored(kLine)[drumline_lock_word(i)] == kDrumLockNeutral);
    for (u32 i = 0; i < 5; ++i) CHECK(live[kLine * 58 + drumline_lock_word(i)] == kDrumLockNeutral);
    // Once a frame, the selected Drumline track; never while a project loads.
    stored(kLine)[18] = 0x3333;
    loading = 1;
    sdk_send_refresh();
    CHECK(stored(kLine)[18] == 0x3333);
    loading = 0;
    sdk_send_refresh();
    CHECK(stored(kLine)[18] == kDrumLockNeutral && live[kLine * 58 + 18] == kDrumLockNeutral && live_sets == 6);
    std::puts("neutral base ok");
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
    for (u32 i = 0; i < 58; ++i) held_words[i] = kDrumLockNeutral;
    stock_texts();
    sdk_drumline_page(page);
    CHECK(text_sets == 0 && icons[2][0x5c] == 1);       // row 1, no step: stock's
    // Row 1 with a step held: each value as that step plays it.
    held = true;
    held_words[16] = drum_lock_word(50, 100);
    held_words[17] = drum_lock_word(-10, 50);
    held_words[33] = drum_lock_word(30, 99);
    sdk_drumline_page(page);
    CHECK(!std::strcmp(texts[1], "70") && !std::strcmp(texts[2], "89") && !std::strcmp(texts[3], "27"));
    CHECK(!std::strcmp(texts[5], "random") && texts[0][0] == 0 && icons[2][0x5c] == 1);
    // Stock's own numbers for the key's values, which the offsets start from.
    CHECK(drumline_random_shown(drumline_random_word(80)) == 40 && drumline_pan_shown(30) == 64);
    CHECK(drumline_volume_shown(-6) == 47 && drumline_pan_shown(-100) == 0 && drumline_volume_shown(20) == 99);
    held = false;
    // Row 2: SRR, the blank, FX I, FX II; the icons hide.
    sdk_drumline->press(kLine, 6);
    stock_texts();
    sdk_drumline_page(page);
    CHECK(!std::strcmp(texts[4], "srr") && !std::strcmp(texts[0], "7.3k"));
    CHECK(!std::strcmp(texts[5], "") && !std::strcmp(texts[1], ""));
    CHECK(!std::strcmp(texts[6], "fx i") && !std::strcmp(texts[2], "33"));
    CHECK(!std::strcmp(texts[7], "fx ii") && !std::strcmp(texts[3], "TRK"));
    for (u32 i = 0; i < 6; ++i) CHECK(icons[i][0x5c] == 0);
    held = true;
    held_words[18] = drum_lock_word(15, 15);
    held_words[19] = drum_lock_word(7, 7);
    held_words[32] = drum_lock_word(7, 7);
    sdk_drumline_page(page);
    CHECK(!std::strcmp(texts[0], "689") && !std::strcmp(texts[2], "100") && !std::strcmp(texts[3], "100"));
    held = false;
    sdk_drumline->press(kLine, 6);
    // Another engine's page is left alone.
    selected = kKit;
    stock_texts();
    sdk_drumline_page(page);
    CHECK(text_sets == 0);
    selected = kLine;
    std::puts("page ok");
}

// The render wrapper: each voice's payload from its first block, SRR after
// stock's render, the send codes for the mixer.
void render_wrapper() {
    CHECK(sdk_dbox_methods[3] == &sdk_dbox_render && !sdk_dbox_methods[0]);
    i32 region[8] = {0x12345600, 1, 2, 3, 4, 0x40000000, 0x00500000, 0x08000000};
    DrumlineFields f = {5, {3, 6}};          // hold 6 samples
    drumline_carry(region, f);
    const i32 params[58] = {};
    i32 out[block_frames], plain[block_frames];
    unsigned char* voice = dbox_objects[1];   // index 5
    std::memset(voice, 0, 0x230);
    sdk_dbox_render(voice, out, region, params);
    CHECK(renders == 1 && !std::memcmp(rendered, region, sizeof(rendered)));
    CHECK(sdk_drum_send_dbox[5] == (3u | 6u << 3) && !sdk_drum_send_dbox[4]);
    for (u32 i = 0; i < block_frames; ++i) plain[i] = static_cast<i32>(i * 1000 + 7) + (region[7] >> 20);
    for (u32 i = 0; i < block_frames; ++i) CHECK(out[i] == plain[i - i % 6]);
    // The next block: the pad republished meanwhile (another hit, an edit);
    // this voice keeps its first block's payload, codes and hold.
    i32 next[8] = {0x12345600, 9, 9, 9, 9, 0x40000000, 0x00500000, 0x04000000};
    drumline_carry(next, DrumlineFields{0, {7, 7}});
    sdk_dbox_render(voice, out, next, params);
    CHECK(!std::memcmp(rendered, region, sizeof(rendered)) && sdk_drum_send_dbox[5] == (3u | 6u << 3));
    // 64 = 10 x 6 + 4: the hold carries 2 samples of the last block's sixth into this one.
    CHECK(out[0] == plain[60] && out[1] == plain[60]);
    for (u32 i = 2; i < block_frames; ++i) CHECK(out[i] == plain[(i - 2) - (i - 2) % 6 + 2]);
    // A new note (stock's trigger zeroes +0x30): the new payload, a fresh hold;
    // SRR off leaves stock's block as it is.
    std::memset(voice + 0x30, 0, 4);
    sdk_dbox_render(voice, out, next, params);
    CHECK(!std::memcmp(rendered, next, sizeof(rendered)) && sdk_drum_send_dbox[5] == (7u | 7u << 3));
    for (u32 i = 0; i < block_frames; ++i) CHECK(out[i] == static_cast<i32>(i * 1000 + 7) + (next[7] >> 20));
    // Voices are their own: another object, its own payload and codes.
    std::memset(dbox_objects[0], 0, 0x230);
    sdk_dbox_render(dbox_objects[0], out, region, params);
    CHECK(sdk_drum_send_dbox[4] == (3u | 6u << 3) && sdk_drum_send_dbox[5] == (7u | 7u << 3));
    // An object outside stock's 27: stock's render with the region, untouched.
    unsigned char stray[0x230] = {};
    sdk_dbox_render(stray, out, next, params);
    CHECK(!std::memcmp(rendered, next, sizeof(rendered)));
    std::puts("render ok");
}
#else
// Without the option every hook reaches stock exactly as before.
void stock_behaviour() {
    selected = kLine;
    stock_pad(kLine);
    const u32 before = fields(kLine, 53);
    shifted = true;
    for (u32 control = 0; control < 8; ++control) {
        const u32 edits = stock_edits;
        sdk_drum_edit(53, control, up, 0);
        CHECK(stock_edits == edits + 1 && edited_control == control);
    }
    CHECK(fields(kLine, 53) == before && !republished);
    // A held step and Shift: stock locks the engine's words 4..7.
    held = true;
    for (u32 index = 4; index < 8; ++index) {
        sdk_synth_turn(kProject, kLine, index, 0);
        CHECK(turned_index == index);
    }
    held = shifted = false;
    for (u32 index : {16u, 17u, 18u, 19u, 32u, 33u}) {
        u32 scale = 0;
        const u32 asks = stock_asks;
        CHECK(sdk_param_steps(kLine, index, &scale) == 0xffffffffu && scale == 2 && stock_asks == asks + 1);
    }
    // Publications are the builder's own, whatever the live words hold.
    neutral_live();
    lock(kLine, kLinePan, 50);
    lock(kLine, kLineSrr, 3);
    note(kLine, 53);
    CHECK(!std::memcmp(published, built, sizeof(built)));
    // Load, scramble and the frame's refresh leave Drumline's words alone.
    for (u32 i = 0; i < 58; ++i) stored(kLine)[i] = 0x1111 * static_cast<i32>(i + 1);
    CHECK(sdk_drum_load_engine(4, track_object(kLine)) == 4);
    for (u32 i = 0; i < 58; ++i) CHECK(stored(kLine)[i] == 0x1111 * static_cast<i32>(i + 1));
    sets = live_sets = 0;
    sdk_synth_scramble(kProject, kLine, 13, 77);
    sdk_send_refresh();
    CHECK(sets == 1 && live_sets == 0 && stored(kLine)[16] == 0x1111 * 17);
    std::puts("stock behaviour ok");
}
#endif
}

int main() {
    packing();
#ifdef DRUMLINE_ON
    rows();
    lock_routing();
    publications_carry_the_fields();
    neutral_base();
    page_texts();
    render_wrapper();
#else
    stock_behaviour();
#endif
    return 0;
}
