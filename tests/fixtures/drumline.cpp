// Stock Drumline's second page (drumline.hpp), built twice: with a
// drumline_page extension (DRUMLINE_ON: the generated startup's
// sdk_drumline_get answers the service) and without (null). The real shared
// hooks run against host doubles of stock 1.2.1: the per-key editor call
// (sdk_drum_edit), the generic turn (sdk_synth_turn) and its step count
// (sdk_param_steps), scramble and the frame's refresh, note start's payload
// builder and its publication (drum_locks.cpp), project load's track check,
// the dbox page's texts and the dbox render wrapper. The extension's part, its
// page table (sdk_drumline_page_extension) and processor, is this fixture's:
// MD KIT's shape (FILTER, SRR, FX I, FX II) as tooling/extensions.py lays it
// out (test_drumline.py PAGE), with a processor that records its calls.
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
void sdk_synth_press(opxy::u32) __asm__("sdk_synth_press");
opxy::u32 sdk_synth_scramble(opxy::u32, opxy::u32, opxy::u32, opxy::i32) __asm__("sdk_synth_scramble");
opxy::u32 sdk_dbox_render(unsigned char*, opxy::i32*, const opxy::i32*, const opxy::i32*) __asm__("sdk_dbox_render");
void sdk_drumline_page(unsigned char*) __asm__("sdk_drumline_page");
typedef opxy::u32 (*DboxRender)(unsigned char*, opxy::i32*, const opxy::i32*, const opxy::i32*);
extern const DboxRender sdk_dbox_methods[4] __asm__("sdk_dbox_methods");
const void* sdk_drumline_service() __asm__("sdk_drumline_service");
}

// drum_send.cpp's, which this fixture does not link.
opxy::u8 sdk_drum_send_dbox[opxy::platform::kDboxVoices];
// The step-lock handles (lock_handles.cpp), which this fixture does not link:
// a double over the real pool, a handle for each held step it locks, and the
// handle the next note start takes (as the sequencer's step hook notes it).
namespace {
opxy::platform::step_locks::Pool lock_pool;
opxy::u32 step_handles[128], next_handle = 1, held_at = 5, noted_handle, held_sets;
bool holding;
}
opxy::u32 opxy::platform::step_locks::take_step(opxy::u32) {
    const opxy::u32 h = noted_handle;
    noted_handle = 0;
    return h;
}
opxy::u32 opxy::platform::step_locks::handle_fields(opxy::u32 h, opxy::u32 key, opxy::i32* values) {
    return h ? lock_pool.fields(h, key, values) : 0;
}
bool opxy::platform::step_locks::step_held() { return holding; }
bool opxy::platform::step_locks::held_value(opxy::u32, opxy::u32 key, opxy::u32 field, opxy::i32& value) {
    return holding && lock_pool.get(step_handles[held_at], key, field, value);
}
bool opxy::platform::step_locks::held_set(opxy::u32, opxy::u32 key, opxy::u32 field, bool present, opxy::i32 value) {
    if (!holding) return false;
    opxy::u32& h = step_handles[held_at];
    if (!h) h = next_handle++;
    if (present) lock_pool.set(h, key, field, value);
    else lock_pool.erase(h, key, field);
    ++held_sets;
    return true;
}
// The calls every build links from startup (lock_handles.hpp), which the kit
// paths of synth_controls.cpp and drum_values_stock.cpp make: here through the
// double, as the drum_locks service would answer them.
extern "C" opxy::u32 sdk_project_locks_held_value(opxy::u32 track, opxy::u32 key, opxy::u32 field, opxy::i32* value) {
    return value && opxy::platform::step_locks::held_value(track, key, field, *value) ? 1 : 0;
}
extern "C" opxy::u32 sdk_project_locks_held_set(opxy::u32 track, opxy::u32 key, opxy::u32 field, opxy::u32 present,
                                                 opxy::i32 value) {
    return opxy::platform::step_locks::held_set(track, key, field, present != 0, value) ? 1 : 0;
}
extern "C" void sdk_project_locks_frame() {}

namespace {
using namespace opxy;
using namespace opxy::platform;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (0)

const u32 kProject = 0x86bd027c, kLive = kProject + 0x4080;
const u32 kLine = 2, kKit = 3;   // tracks: 2 and 5 Drumline, 3 a padMix kit, 6 the drum sampler
#ifdef DRUMLINE_ON
const u32 kOther = 5, kSampler = 6;
#endif
u32 engines[16] = {0, 0, 4, 41, 0, 4, 3, 0};
u32 selected = kLine, key = 53, page_word, controller = 0x8400dff8, loading;
bool shifted, held;
// A turn as stock hands it on, at a stand-in address (kTurn): [0] its
// continuous motion, [1] its detents, [2] its speed.
const u32 kTurn = 0x2000;
i32 turn_event[3];
#ifdef DRUMLINE_ON
u32 turning(i32 detents) {
    turn_event[0] = detents * 0x00a3d70a; turn_event[1] = detents; turn_event[2] = 0;
    return kTurn;
}
#endif
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
// The key stock edits (stock.hpp drum_edit_key): the one it holds for editing
// (0x8400dfc8, -1 none), else the last played (8d4d6cb0), `key`. The track's
// "selected" key (8d4d6c98) is another byte nothing may read.
u32 edit_latch = 0xffffffffu;
u32 get_key(u32) { return key; }
u32 get_selected(u32) { std::fputs("read 8d4d6c98\n", stderr); std::abort(); }
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
u32 step_held() { return held || holding; }
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
    case 0x8d4d6cb0: return reinterpret_cast<Address>(&get_key);
    case 0x8d4d6c98: return reinterpret_cast<Address>(&get_selected);
    case 0x8400dfc8: return reinterpret_cast<Address>(&edit_latch);
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
    case kTurn: return reinterpret_cast<Address>(turn_event);
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


// The extension's part. Its processor records each call: start's choices and
// whether it found its state zeroed, process's choices, and the state it
// keeps across a hit's blocks (start sets word 0 to 100, each block adds 1); it
// adds kProcessed to every sample, so a block shows whether it ran.
namespace {
const i32 kProcessed = 0x100;
u32 starts, processes, dirty_starts;
opxy::drumline::Choices started_with, processed_with;
i32 last_state;
void probe_start(opxy::drumline::Voice& voice, const opxy::drumline::Choices& choices) {
    ++starts;
    for (u32 i = 0; i < opxy::drumline::voice_words; ++i) if (voice.word[i]) ++dirty_starts;
    started_with = choices;
    for (u32 i = 0; i < opxy::drumline::voice_words; ++i) voice.word[i] = 7;
    voice.word[0] = 100;
}
void probe_process(opxy::drumline::Voice& voice, const opxy::drumline::Choices& choices, i32* block) {
    ++processes;
    processed_with = choices;
    last_state = ++voice.word[0];
    for (u32 i = 0; i < opxy::drumline::frames; ++i) block[i] += kProcessed;
}
const opxy::drumline::Processor probe = {probe_start, probe_process};

// The page as tooling/extensions.py generates it from test_drumline.py's PAGE:
// FILTER (32 choices, default 16), SRR (16, default 0), FX I and FX II; the
// sends take bits 0..5, FILTER 6..10 and SRR 11..14.
const char* const filter_choices[32] = {
    "L16", "L15", "L14", "L13", "L12", "L11", "L10", "L9", "L8", "L7", "L6", "L5", "L4", "L3", "L2", "L1",
    "OPEN", "H1", "H2", "H3", "H4", "H5", "H6", "H7", "H8", "H9", "H10", "H11", "H12", "H13", "H14", "H15"};
const char* const srr_choices[16] = {
    "OFF", "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8", "S9", "S10", "S11", "S12", "S13", "S14", "S15"};
const char* const send_choices[8] = {"TRK", "0", "17", "33", "50", "67", "83", "100"};
const DrumlinePageParameter line_parameters[4] = {
    {"FILTER", filter_choices, 32, 16, kDrumlineOwn, 6, 5},
    {"SRR", srr_choices, 16, 0, kDrumlineOwn, 11, 4},
    {"FX I", send_choices, 8, 0, kDrumlineSend1, 0, 3},
    {"FX II", send_choices, 8, 0, kDrumlineSend2, 3, 3}};
// Its owner tag, and another extension's.
const u32 kTag = 0xbeef, kForeign = 0x1234;
const DrumlinePage line_page = {kTag, &probe, line_parameters, 4};
#ifdef DRUMLINE_ON
// The same page's first two parameters only: encoders 2 and 3 have none.
const DrumlinePage short_page = {kTag, &probe, line_parameters, 2};
#endif
const DrumlinePage* shown_page = &line_page;
}

// The extension's object's accessor (tooling/extensions.py generate_drumline_page).
const DrumlinePage* sdk_drumline_page_extension() { return shown_page; }
// The generated startup's (tooling/startup.py): the service while the build links it.
const void* sdk_drumline_get() {
#ifdef DRUMLINE_ON
    return sdk_drumline_service();
#else
    return nullptr;
#endif
}

namespace {
opxy::drumline::Choices make_choices(u32 filter, u32 srr, u32 fx1, u32 fx2) {
    const opxy::drumline::Choices c = {{static_cast<u8>(filter), static_cast<u8>(srr), static_cast<u8>(fx1),
                                        static_cast<u8>(fx2)}};
    return c;
}
bool is(const opxy::drumline::Choices& c, u32 filter, u32 srr, u32 fx1, u32 fx2) {
    return static_cast<u32>(c.choice[0]) == filter && static_cast<u32>(c.choice[1]) == srr &&
           static_cast<u32>(c.choice[2]) == fx1 && static_cast<u32>(c.choice[3]) == fx2;
}
// A pad word holding these choices, and a pad word's choices, under the page's tag.
u32 pad_word(u32 filter, u32 srr, u32 fx1, u32 fx2) {
    return drumline_word(line_page, drumline_pack(line_page, make_choices(filter, srr, fx1, fx2)));
}
opxy::drumline::Choices choices_of_word(u32 word) {
    opxy::drumline::Choices c;
    drumline_unpack(line_page, drumline_codes_of(line_page, word), c);
    return c;
}
// Whether a payload carries these choices.
bool carries(const i32* words, u32 filter, u32 srr, u32 fx1, u32 fx2) {
    opxy::drumline::Choices c;
    drumline_unpack(line_page, drumline_carried(words), c);
    return is(c, filter, srr, fx1, fx2);
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
// parameters, random, pan 30, volume -6 dB, and in +0x00 the page's FILTER
// L4 (12), SRR S5, FX I 33 (3) and FX II TRK under the page's tag.
void stock_pad(u32 track) {
    unsigned char* bytes = record(track, 53);
    i32* words = reinterpret_cast<i32*>(bytes);
    bytes[24] = 0x22;
    for (u32 i = 1; i < 5; ++i) words[i] = static_cast<i32>(0x11110000u * i + 0x1234u);
    words[5] = 0x28000000;
    bytes[0x1e] = 30;
    bytes[0x1d] = static_cast<unsigned char>(-6);
    fields(track, 53) = pad_word(12, 5, 3, 0);
    bytes[26] = 53;
}

void packing() {
    const DrumlinePage& p = line_page;
    // Codes: each choice less its default, wrapped to its bits, in its place.
    CHECK(drumline_pack(p, make_choices(12, 5, 3, 0)) == 0x2f03u);
    CHECK(drumline_pack(p, make_choices(15, 15, 7, 7)) == 0x7fffu);
    CHECK(drumline_pack(p, make_choices(0, 0, 0, 0)) == 16u << 6);
    CHECK(drumline_pack(p, make_choices(16, 0, 0, 0)) == 0 && drumline_word(p, 0) == 0);   // every default: 0
    CHECK(pad_word(12, 5, 3, 0) == (kTag << 16 | 0x2f03u));
    CHECK(is(choices_of_word(kTag << 16 | 0x2f03u), 12, 5, 3, 0));
    for (u32 f = 0; f < 32; ++f)
        for (u32 s = 0; s < 16; s += 5)
            for (u32 a = 0; a < 8; a += 3) CHECK(is(choices_of_word(pad_word(f, s, a, 7 - a)), f, s, a, 7 - a));
    // The tag: a word without it (another extension's, or an old pad's) reads as every default.
    CHECK(drumline_codes_of(p, kTag << 16 | 0x2f03u) == 0x2f03u && !drumline_codes_of(p, kForeign << 16 | 0x2f03u));
    CHECK(is(choices_of_word(kForeign << 16 | 0x2f03u), 16, 0, 0, 0) && is(choices_of_word(0x2f03u), 16, 0, 0, 0));
    CHECK(is(choices_of_word(0), 16, 0, 0, 0));
    // A code past a parameter's choices reads as its default.
    const DrumlinePageParameter three = {"MODE", srr_choices, 3, 1, kDrumlineOwn, 0, 2};
    CHECK(drumline_code(three, 1) == 0 && drumline_code(three, 0) == 3 && drumline_code(three, 2) == 1);
    CHECK(drumline_choice(three, 0) == 1 && drumline_choice(three, 1) == 2 && drumline_choice(three, 3) == 0);
    CHECK(drumline_choice(three, 2) == 1);
    // The carrier: codes bits 0..6 in word 0's low byte, bit 7 set when word 6's
    // bits 0..8 carry bits 7..15.
    i32 words[8] = {0x12345678, 1, 2, 3, 4, 5, 0x7fffffff, 7};
    drumline_carry(words, 0x2f03u);
    CHECK(words[0] == static_cast<i32>(0x12345600u | 0x03u | 0x80u) && words[6] == static_cast<i32>(0x7ffffe00u | 0x5eu));
    CHECK(drumline_carried(words) == 0x2f03u && carries(words, 12, 5, 3, 0));
    drumline_carry(words, 0x7fffu);
    CHECK(words[0] == static_cast<i32>(0x123456ffu) && words[6] == static_cast<i32>(0x7ffffeffu));
    CHECK(drumline_carried(words) == 0x7fffu && carries(words, 15, 15, 7, 7));
    // The sends (bits 0..5) and FILTER's lowest bit (6) stay in word 0: word 6 is stock's own.
    words[6] = 0x0abcdef9;
    drumline_carry(words, 1u | 6u << 3);
    CHECK(words[0] == static_cast<i32>(0x12345631u) && words[6] == 0x0abcdef9 && drumline_carried(words) == 0x31u);
    drumline_carry(words, 1u << 6);
    CHECK(words[0] == static_cast<i32>(0x12345640u) && words[6] == 0x0abcdef9 && carries(words, 17, 0, 0, 0));
    drumline_carry(words, 0);
    CHECK(words[0] == static_cast<i32>(0x12345600u) && words[6] == 0x0abcdef9 && !drumline_carried(words));
    // Without bit 7 word 6 carries nothing, whatever it holds.
    words[0] = static_cast<i32>(0x12345631u);
    words[6] = 0x1ff;
    CHECK(drumline_carried(words) == 0x31u);
    // The DSP's decode of word 0 never sees the low byte: 66 bins from bits 8..31.
    for (u32 low = 0; low < 256; ++low)
        CHECK((((0x55aa1200u | low) >> 8) * 66u) >> 23 == ((0x55aa1200u >> 8) * 66u) >> 23);
    // A hit's sends, as a kit's codes (FX I bits 0..2, FX II 3..5), and whether the
    // processor runs: only for an own parameter off its default.
    CHECK(drumline_sends(p, make_choices(12, 5, 3, 6)) == (3u | 6u << 3) && !drumline_sends(p, make_choices(0, 15, 0, 0)));
    CHECK(!drumline_processed(p, make_choices(16, 0, 7, 7)) && drumline_processed(p, make_choices(17, 0, 0, 0)));
    CHECK(drumline_processed(p, make_choices(16, 1, 0, 0)) && drumline_processed(p, make_choices(0, 0, 0, 0)));
    // Locks: in the handles' pool at fields 8.., apart from a padMix kit's 0..5,
    // and within 16; the value is the choice + 1.
    for (u32 i = 0; i < opxy::drumline::max_parameters; ++i)
        CHECK(drumline_pool_field(i) >= kHitFields && drumline_pool_field(i) < step_locks::kFields);
    u32 choice = 99;
    CHECK(!drumline_lock_choice(p.parameters[0], 0, choice) && !drumline_lock_choice(p.parameters[0], 33, choice));
    CHECK(choice == 99 && drumline_lock_choice(p.parameters[0], 1, choice) && choice == 0);
    CHECK(drumline_lock_choice(p.parameters[0], 32, choice) && choice == 31);
    CHECK(!drumline_lock_choice(p.parameters[2], 9, choice) && drumline_lock_choice(p.parameters[2], 8, choice) && choice == 7);
    {
        // A kit's fields (0..5) lock nothing of Drumline's; the page's put their choices in place.
        i32 values[step_locks::kFields] = {50, -3, 7, 2, 4, 5};
        opxy::drumline::Choices c = make_choices(12, 5, 3, 0);
        drumline_lock_choices(p, 0x3fu, values, c);
        CHECK(is(c, 12, 5, 3, 0));
        values[drumline_pool_field(0)] = 14;
        values[drumline_pool_field(1)] = 40;   // past SRR's choices: ignored
        values[drumline_pool_field(3)] = 3;    // outside the mask: ignored
        drumline_lock_choices(p, 0x3fu | 1u << drumline_pool_field(0) | 1u << drumline_pool_field(1), values, c);
        CHECK(is(c, 13, 5, 3, 0));
        drumline_lock_choices(p, 1u << drumline_pool_field(3), values, c);
        CHECK(is(c, 13, 5, 3, 2));
    }
    // The dbox object index: stock's 27 objects, nothing else.
    CHECK(dbox_index(0x84c50b9c) == 0 && dbox_index(0x84c50b9c + 26 * 0x230) == 26);
    CHECK(dbox_index(0x84c50b9c + 27 * 0x230) == kDboxVoices && dbox_index(0x84c50b9c + 0x231) == kDboxVoices);
    CHECK(dbox_index(0x84c50b98) == kDboxVoices && dbox_index(0) == kDboxVoices);
    for (u32 i = 0; i < kDboxVoices; ++i) CHECK(dbox_index(0x84c50b9c + i * 0x230) == i);
    std::puts("packing ok");
}

// With the service or without, a Drumline track's M3 filter and filter envelope
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

// The dbox page's text nodes (both of stock's branches set them).
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
bool shows(u32 node, const char* text) { return !std::strcmp(texts[node], text); }
// The four value texts (nodes 0..3) and the four titles (4..7).
bool values(const char* a, const char* b, const char* c, const char* d) {
    return shows(0, a) && shows(1, b) && shows(2, c) && shows(3, d);
}

#ifdef DRUMLINE_ON
// Page 1 is stock's; a press of any encoder on the engine page switches to
// page 2, whose unshifted encoders edit the page's parameters, one choice a
// detent. Shift's encoders (4..7) stay stock's on either page.
void pages() {
    selected = kLine;
    stock_pad(kLine);
    const u32 before = fields(kLine, 53), edits0 = stock_edits;
    for (u32 control = 0; control < 8; ++control) {
        const u32 edits = stock_edits;
        sdk_drum_edit(53, control, up, 0);
        CHECK(stock_edits == edits + 1 && edited_control == control);
    }
    CHECK(fields(kLine, 53) == before && !republished);
    // Another page of the track (M2..M4) or another controller: no switch.
    page_word = 1;
    sdk_synth_press(2);
    page_word = 0;
    controller = 0x1234;
    sdk_synth_press(2);
    controller = 0x8400dff8;
    sdk_drum_edit(53, 0, up, 0);
    CHECK(stock_edits == edits0 + 9);
    sdk_synth_press(2);                       // the engine page: page 2
    // FILTER: 32 choices from L16, its default OPEN (16).
    sdk_drum_edit(53, 0, up, 0);
    CHECK(is(choices_of_word(fields(kLine, 53)), 13, 5, 3, 0) && republished == 1 && republished_key == 53 && dirty == 1);
    sdk_drum_edit(53, 0, far_up, 0);
    CHECK(is(choices_of_word(fields(kLine, 53)), 31, 5, 3, 0) && republished == 2);
    sdk_drum_edit(53, 0, up, 0);              // at the top: nothing moves, nothing republished
    CHECK(republished == 2);
    const u32 minus[3] = {1000, static_cast<u32>(-40), 0};
    sdk_drum_edit(53, 0, minus, 0);
    CHECK(is(choices_of_word(fields(kLine, 53)), 0, 5, 3, 0) && republished == 3);
    // SRR, 16 choices.
    sdk_drum_edit(53, 1, up, 0);
    CHECK(is(choices_of_word(fields(kLine, 53)), 0, 6, 3, 0) && republished == 4);
    sdk_drum_edit(53, 1, far_up, 0);
    CHECK(is(choices_of_word(fields(kLine, 53)), 0, 15, 3, 0) && republished == 5);
    // FX I and FX II, TRK and seven amounts.
    sdk_drum_edit(53, 2, far_up, 0);
    sdk_drum_edit(53, 3, down, 0);            // already TRK
    CHECK(is(choices_of_word(fields(kLine, 53)), 0, 15, 7, 0) && republished == 6);
    sdk_drum_edit(53, 3, up, 0);
    CHECK(is(choices_of_word(fields(kLine, 53)), 0, 15, 7, 1) && fields(kLine, 53) >> 16 == kTag);
    CHECK(stock_edits == edits0 + 9);
    // Shift's encoders stay stock's on page 2: TYPE, RANDOM, PAN, VOLUME.
    for (u32 control = 4; control < 8; ++control) {
        sdk_drum_edit(53, control, up, 0);
        CHECK(edited_control == control);
    }
    CHECK(stock_edits == edits0 + 13);
    // Back at every default the word is 0 again.
    fields(kLine, 53) = pad_word(17, 0, 0, 0);
    CHECK(fields(kLine, 53) == (kTag << 16 | 1u << 6));
    sdk_drum_edit(53, 0, down, 0);
    CHECK(fields(kLine, 53) == 0);
    // Another extension's word, or an old pad's: every default, and the first
    // edit makes it this page's.
    for (u32 w : {kForeign << 16 | 0x2f03u, 0x2f03u}) {
        fields(kLine, 53) = w;
        sdk_drum_edit(53, 1, up, 0);
        CHECK(fields(kLine, 53) == (kTag << 16 | 1u << 11));
    }
    // An encoder past the page's parameters: nothing, and not stock's either.
    shown_page = &short_page;
    {
        const u32 kept = fields(kLine, 53), reps = republished, edits = stock_edits;
        sdk_drum_edit(53, 2, up, 0);
        sdk_drum_edit(53, 3, down, 0);
        CHECK(fields(kLine, 53) == kept && republished == reps && stock_edits == edits);
    }
    shown_page = &line_page;
    // Each track keeps its own page.
    const u32 edits1 = stock_edits;
    selected = kOther;
    stock_pad(kOther);
    sdk_drum_edit(53, 0, up, 0);
    CHECK(stock_edits == edits1 + 1 && fields(kOther, 53) == pad_word(12, 5, 3, 0));
    selected = kLine;
    sdk_synth_press(0);                       // any encoder: back to page 1
    sdk_drum_edit(53, 0, up, 0);
    CHECK(stock_edits == edits1 + 2);
    // A kit is never Drumline, though classifiers alias it to 4; its press is its own.
    CHECK(sdk_drum_alias(41) == 4 && !drumline_track(kKit) && drumline_track(kLine) && !drumline_track(kSampler));
    CHECK(!drumline_track(8) && !drumline_track(14));
    stock_pad(kLine);
    std::puts("pages ok");
}

// Every publication of a Drumline pad carries its choices, a note's as any other's.
void publications_carry_the_choices() {
    neutral_live();
    stock_pad(kLine);
    note(kLine, 53);
    CHECK(published_track == kLine && published_kind == 0);
    CHECK((published[0] & ~0xff) == (built[0] & ~0xff) && (published[0] & 0xff) == 0x83);
    CHECK(!std::memcmp(published + 1, built + 1, 5 * sizeof(i32)) && published[7] == built[7]);
    CHECK(published[6] == static_cast<i32>((static_cast<u32>(built[6]) & ~0x1ffu) | 0x5eu));
    CHECK(carries(published, 12, 5, 3, 0));
    // The sends alone: word 6 is stock's own.
    fields(kLine, 53) = pad_word(16, 0, 1, 6);
    note(kLine, 53);
    CHECK(published[6] == built[6] && (published[0] & 0xff) == 0x31 && carries(published, 16, 0, 1, 6));
    std::memcpy(payload, built, sizeof(payload));
    sdk_drum_lock_publish(kProject, kLine, 0, payload);   // the editor's and its previews'
    CHECK(published[6] == built[6] && (published[0] & 0xff) == 0x31);
    // Every default, or a word another extension tagged: nothing carried.
    for (u32 w : {0u, kForeign << 16 | 0x2f03u}) {
        fields(kLine, 53) = w;
        note(kLine, 53);
        CHECK(published[6] == built[6] && (published[0] & 0xff) == 0 && (published[0] & ~0xff) == (built[0] & ~0xff));
    }
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

// The dbox page: page 2's titles and values from the page's table, blank past
// its parameters; Shift and page 1 stock's.
void page_texts() {
    page_nodes();
    selected = kLine;
    key = 53;
    stock_pad(kLine);
    stock_texts();
    held = true;
    sdk_drumline_page(page);
    CHECK(text_sets == 0 && icons[2][0x5c] == 1);       // page 1 is stock's, step held or not
    held = false;
    sdk_drumline_page(page);
    CHECK(text_sets == 0);
    // Page 2, unshifted: the page's labels and the key's choices; the icons hide.
    sdk_synth_press(1);
    stock_texts();
    sdk_drumline_page(page);
    CHECK(shows(4, "FILTER") && shows(5, "SRR") && shows(6, "FX I") && shows(7, "FX II"));
    CHECK(values("L4", "S5", "33", "TRK"));
    for (u32 i = 0; i < 6; ++i) CHECK(icons[i][0x5c] == 0);
    // Shift held: stock's own Shift row, on page 2 too.
    shifted = true;
    stock_texts();
    sdk_drumline_page(page);
    CHECK(text_sets == 0 && icons[2][0x5c] == 1);
    shifted = false;
    // Each parameter's captions at its ends.
    const struct { u32 filter, srr, fx1, fx2; const char* text[4]; } cases[] = {
        {0, 0, 1, 0, {"L16", "OFF", "0", "TRK"}}, {16, 15, 7, 2, {"OPEN", "S15", "100", "17"}},
        {31, 1, 4, 5, {"H15", "S1", "50", "67"}}};
    for (const auto& c : cases) {
        fields(kLine, 53) = pad_word(c.filter, c.srr, c.fx1, c.fx2);
        sdk_drumline_page(page);
        CHECK(values(c.text[0], c.text[1], c.text[2], c.text[3]));
    }
    // Another extension's word: every default.
    fields(kLine, 53) = kForeign << 16 | 0x2f03u;
    sdk_drumline_page(page);
    CHECK(values("OPEN", "OFF", "TRK", "TRK"));
    // The key stock edits: a pad held for editing (0x8400dfc8) shows, not the last played.
    stock_pad(kLine);
    std::memcpy(record(kLine, 60), record(kLine, 53), 0x80);
    fields(kLine, 60) = pad_word(16, 9, 0, 0);
    edit_latch = 60;
    sdk_drumline_page(page);
    CHECK(values("OPEN", "S9", "TRK", "TRK"));
    edit_latch = 0xffffffffu;
    sdk_drumline_page(page);
    CHECK(values("L4", "S5", "33", "TRK"));
    // A page of two parameters: encoders 2 and 3 show blank titles and values.
    shown_page = &short_page;
    stock_texts();
    sdk_drumline_page(page);
    CHECK(shows(4, "FILTER") && shows(5, "SRR") && shows(6, "") && shows(7, "") && values("L4", "S5", "", ""));
    shown_page = &line_page;
    sdk_synth_press(1);                       // back to page 1
    // Another engine's page is left alone.
    selected = kKit;
    stock_texts();
    sdk_drumline_page(page);
    CHECK(text_sets == 0);
    selected = kLine;
    stock_pad(kLine);
    std::puts("page ok");
}

// Page 2's step locks, in the handles' pool: with a step held, an unshifted
// encoder on page 2 locks its parameter for the key stock edits, one choice a
// detent from what the step plays (its lock, else the pad's own choice); back
// at the pad's own choice it unlocks. Page 1 and Shift keep stock's locks. A
// note on the step plays its key's locks; the page shows them while the step is held.
void locks() {
    selected = kLine;
    key = 53;
    stock_pad(kLine);                         // FILTER L4 (12), SRR S5, FX I 33 (3), FX II TRK
    page_nodes();
    neutral_live();
    held = holding = true;
    held_at = 21;
    // Page 1: stock's lock of the sound word the encoder shows.
    const u32 turns0 = turns, sets0 = held_sets;
    sdk_synth_turn(kProject, kLine, 2, 0);
    CHECK(turns == turns0 + 1 && turned_index == 2 && held_sets == sets0);
    sdk_synth_press(3);                       // page 2
    // FILTER from the pad's 12 to its last choice, a detent at a time, each the choice + 1.
    for (u32 n = 13; n < 32; ++n) {
        CHECK(sdk_synth_turn(kProject, kLine, 0, turning(1)) == 0);
        i32 v = 0;
        CHECK(step_locks::held_value(kLine, 53, drumline_pool_field(0), v) && v == static_cast<i32>(n) + 1);
    }
    CHECK(turns == turns0 + 1 && held_sets == sets0 + 19);
    sdk_synth_turn(kProject, kLine, 0, turning(1));   // its end: nothing moves
    CHECK(held_sets == sets0 + 19);
    stock_texts();
    sdk_drumline_page(page);
    CHECK(values("H15", "S5", "33", "TRK"));  // the step's lock and the pad's own
    // A fast turn down past the pad's own choice locks there; back at it, unlocked.
    i32 v = 0;
    sdk_synth_turn(kProject, kLine, 0, turning(-25));
    CHECK(step_locks::held_value(kLine, 53, drumline_pool_field(0), v) && v == 7);
    sdk_synth_turn(kProject, kLine, 0, turning(6));
    CHECK(!step_locks::held_value(kLine, 53, drumline_pool_field(0), v));
    CHECK(fields(kLine, 53) == pad_word(12, 5, 3, 0));   // the pad itself never moves
    // FX II turned down from TRK, its first choice: nothing to lock.
    const u32 sets1 = held_sets;
    sdk_synth_turn(kProject, kLine, 3, turning(-1));
    CHECK(held_sets == sets1 && !step_locks::held_value(kLine, 53, drumline_pool_field(3), v));
    // SRR +2, FX I -1, FX II +3, each its own field.
    for (u32 n = 0; n < 2; ++n) sdk_synth_turn(kProject, kLine, 1, turning(1));
    sdk_synth_turn(kProject, kLine, 2, turning(-1));
    sdk_synth_turn(kProject, kLine, 3, turning(3));
    CHECK(step_locks::held_value(kLine, 53, drumline_pool_field(1), v) && v == 8);
    CHECK(step_locks::held_value(kLine, 53, drumline_pool_field(2), v) && v == 3);
    CHECK(step_locks::held_value(kLine, 53, drumline_pool_field(3), v) && v == 4);
    stock_texts();
    sdk_drumline_page(page);
    CHECK(values("L4", "S7", "17", "33"));
    // Past a two-parameter page's parameters a turn does nothing, stock's lock neither.
    shown_page = &short_page;
    {
        const u32 t = turns, s = held_sets;
        CHECK(sdk_synth_turn(kProject, kLine, 2, turning(1)) == 0 && sdk_synth_turn(kProject, kLine, 3, turning(-1)) == 0);
        CHECK(turns == t && held_sets == s);
    }
    shown_page = &line_page;
    // Shift: stock's own lock of words 4..7, as before.
    shifted = true;
    sdk_synth_turn(kProject, kLine, 6, 0);
    CHECK(turned_index == 6 && turns == turns0 + 2);
    shifted = false;
    // A note on the step plays its key's locks; another key, none; a live note, none.
    noted_handle = step_handles[21];
    note(kLine, 53);
    CHECK(carries(published, 12, 7, 2, 3));
    std::memcpy(record(kLine, 54), record(kLine, 53), 0x80);
    noted_handle = step_handles[21];
    note(kLine, 54);
    CHECK(carries(published, 12, 5, 3, 0));
    note(kLine, 53);
    CHECK(carries(published, 12, 5, 3, 0));
    // A kit's handle on a Drumline step (a copied pattern) moves nothing; nor
    // does a value past a parameter's choices.
    const u32 kit_handle = next_handle++;
    for (u32 field = 0; field < kHitFields; ++field) lock_pool.set(kit_handle, 53, field, 5);
    lock_pool.set(kit_handle, 53, drumline_pool_field(0), 33);
    noted_handle = kit_handle;
    note(kLine, 53);
    CHECK(carries(published, 12, 5, 3, 0) && published[5] == built[5] && published[7] == built[7]);
    // Drumline is a handle client with the service; a kit always.
    CHECK(step_locks::handle_client(kLine) && step_locks::handle_client(kKit) && !step_locks::handle_client(kSampler));
    // With the step released the page shows the key's own choices.
    held = holding = false;
    stock_texts();
    sdk_drumline_page(page);
    CHECK(values("L4", "S5", "33", "TRK"));
    sdk_synth_press(3);                       // back to page 1
    held_at = 5;
    std::puts("locks ok");
}

// What stock's render gives a payload (dbox_render), and with the processor's mark.
bool plain(const i32* out, const i32* words, i32 added = 0) {
    for (u32 i = 0; i < block_frames; ++i)
        if (out[i] != static_cast<i32>(i * 1000 + 7) + (words[7] >> 20) + added) return false;
    return true;
}

// The render wrapper: each voice's payload and choices from its first block,
// the processor on its blocks only while an own parameter is off its default
// (start once, on a zeroed state), and the send codes for the mixer.
void render_wrapper() {
    CHECK(sdk_dbox_methods[3] == &sdk_dbox_render && !sdk_dbox_methods[0]);
    const i32 params[58] = {};
    i32 out[block_frames];
    unsigned char* voice = dbox_objects[1];   // index 5
    std::memset(voice, 0, 0x230);
    // A hit with only its sends set: stock's block bit for bit, and its codes.
    i32 region[8] = {0x12345600, 1, 2, 3, 4, 0x40000000, 0x00500000, 0x08000000};
    drumline_carry(region, drumline_pack(line_page, make_choices(16, 0, 3, 6)));
    const u32 starts0 = starts, processes0 = processes;
    sdk_dbox_render(voice, out, region, params);
    CHECK(!std::memcmp(rendered, region, sizeof(rendered)));
    CHECK(sdk_drum_send_dbox[5] == (3u | 6u << 3) && !sdk_drum_send_dbox[4]);
    CHECK(plain(out, region) && starts == starts0 && processes == processes0);
    // The next block: the pad republished meanwhile; the voice keeps its first
    // block's payload and choices.
    i32 next[8] = {0x12345600, 9, 9, 9, 9, 0x40000000, 0x00500000, 0x04000000};
    drumline_carry(next, drumline_pack(line_page, make_choices(16, 3, 7, 7)));
    sdk_dbox_render(voice, out, next, params);
    CHECK(!std::memcmp(rendered, region, sizeof(rendered)) && sdk_drum_send_dbox[5] == (3u | 6u << 3));
    CHECK(plain(out, region) && processes == processes0);
    // A new note (stock's trigger zeroes +0x30) takes its payload. SRR off its
    // default runs the processor: start once, then process, with the hit's choices.
    std::memset(voice + 0x30, 0, 4);
    sdk_dbox_render(voice, out, next, params);
    CHECK(!std::memcmp(rendered, next, sizeof(rendered)) && sdk_drum_send_dbox[5] == (7u | 7u << 3));
    CHECK(starts == starts0 + 1 && processes == processes0 + 1 && !dirty_starts);
    CHECK(is(started_with, 16, 3, 7, 7) && is(processed_with, 16, 3, 7, 7) && last_state == 101);
    CHECK(plain(out, next, kProcessed));
    // Its later blocks: process alone, the voice's state kept, the latched payload played.
    for (u32 block = 0; block < 2; ++block) {
        sdk_dbox_render(voice, out, region, params);
        CHECK(!std::memcmp(rendered, next, sizeof(rendered)) && plain(out, next, kProcessed));
    }
    CHECK(starts == starts0 + 1 && processes == processes0 + 3 && last_state == 103);
    // The next note's start finds the state zeroed again; FILTER alone runs it too.
    i32 filtered[8] = {0x12345600, 1, 2, 3, 4, 0x40000000, 0x00500000, 0x7f000000};
    drumline_carry(filtered, drumline_pack(line_page, make_choices(30, 0, 0, 0)));
    std::memset(voice + 0x30, 0, 4);
    sdk_dbox_render(voice, out, filtered, params);
    CHECK(starts == starts0 + 2 && !dirty_starts && is(started_with, 30, 0, 0, 0) && last_state == 101);
    CHECK(plain(out, filtered, kProcessed) && !sdk_drum_send_dbox[5]);
    // A hit at the defaults (its sends aside) is stock's again.
    std::memset(voice + 0x30, 0, 4);
    sdk_dbox_render(voice, out, region, params);
    CHECK(plain(out, region) && starts == starts0 + 2 && processes == processes0 + 4);
    // Voices are their own; an object outside stock's 27 gets stock's render untouched.
    std::memset(dbox_objects[0], 0, 0x230);
    sdk_dbox_render(dbox_objects[0], out, region, params);
    CHECK(sdk_drum_send_dbox[4] == (3u | 6u << 3) && sdk_drum_send_dbox[5] == (3u | 6u << 3));
    unsigned char stray[0x230] = {};
    const u32 processes1 = processes;
    sdk_dbox_render(stray, out, filtered, params);
    CHECK(!std::memcmp(rendered, filtered, sizeof(rendered)) && plain(out, filtered) && processes == processes1);
    std::puts("render ok");
}
#else
// Without the service the per-key editor and the dbox page are stock's, a
// press switches nothing, publications are the builder's own and Drumline is
// no step-lock handle client.
void stock_behaviour() {
    selected = kLine;
    stock_pad(kLine);
    const u32 before = fields(kLine, 53);
    sdk_synth_press(2);
    for (u32 control = 0; control < 8; ++control) {
        const u32 edits = stock_edits;
        sdk_drum_edit(53, control, up, 0);
        CHECK(stock_edits == edits + 1 && edited_control == control);
    }
    CHECK(fields(kLine, 53) == before && !republished);
    neutral_live();
    note(kLine, 53);
    CHECK(!std::memcmp(published, built, sizeof(built)));
    page_nodes();
    stock_texts();
    sdk_drumline_page(page);
    CHECK(text_sets == 0 && icons[2][0x5c] == 1 && values("", "40", "64", "47"));
    CHECK(!drumline_track(kLine) && !step_locks::handle_client(kLine) && step_locks::handle_client(kKit));
    std::puts("stock behaviour ok");
}
#endif
}

int main() {
    packing();
    filter_words_are_stocks();
#ifdef DRUMLINE_ON
    pages();
    publications_carry_the_choices();
    page_texts();
    locks();
    render_wrapper();
#else
    stock_behaviour();
#endif
    return 0;
}
