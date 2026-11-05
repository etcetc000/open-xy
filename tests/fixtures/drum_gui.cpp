#include <cstdio>
#include "stock.hpp"
#include "drum_machines.hpp"
#include "drum_storage.hpp"
#include "synth_controls.hpp"
#include "drum_locks.hpp"
#include "drum_lock_store.hpp"
#include "lock_pool.hpp"
#include <cstdlib>
#include <cstring>
#include <initializer_list>

extern "C" {
void sdk_synth_press(opxy::u32) __asm__("sdk_synth_press");
opxy::u32 sdk_synth_turn(opxy::u32, opxy::u32, opxy::u32, opxy::u32) __asm__("sdk_synth_turn");
opxy::u32 sdk_synth_scramble(opxy::u32, opxy::u32, opxy::u32, opxy::i32) __asm__("sdk_synth_scramble");
}
namespace {
using namespace opxy;
using namespace opxy::platform;
u32 track, key = 53, engine = 34, page, controller = 0x8400dff8;
u32 publications, dirty, clicks, fallback, last_key, turned_slot;
int ownership = 1;
bool shifted, shifted_octave;
i32 regions[24][32];
void require_at(bool ok, int line) { if (!ok) { std::fprintf(stderr, "line %d\n", line); std::abort(); } }
#define require(ok) require_at(ok, __LINE__)
int ready() { return ownership; }
void defaults(u32 note, u8* model, u8* values) {
    *model = static_cast<u8>((note - 53) % 2);
    for (u32 i = 0; i < 8; ++i) values[i] = static_cast<u8>(20 + i);
    values[7] = static_cast<u8>(note);
}
// The full model leaves slots 2 and 7 unnamed: gaps inside its count.
const char* label(u32 model, u32 slot) { return model ? "short" : slot == 2 || slot == 7 ? "" : "full"; }
const char* name(u32 model) { return model ? "short" : "full"; }
u32 count(u32 model) { return model ? 3 : 8; }
bool format(u32, i32, char*) { return false; }
// The send tracks (14, 15) hold no owned effect.
u32 get_engine(u32 selected) { if (selected >= 8) return 0; require(selected == track); return engine; }
// Per-hit locks (drum_lock_store.hpp): whether a step is held, the step-lock
// handles' held-step calls (a double over the real pool: a handle for each step
// it locks), the selected track's stored words (its track object at a
// stand-in address) and its live words as 8d5acea8 sets them.
bool held;
u32 held_step = 5, held_page = 2, written_step, written_index, written_value, writes;
// A turn as stock's controller hands it on (8d5ad2e8): [0] its continuous
// motion, a Q31 step stock adds to a continuous word (8d5ad254), [1] its signed
// detents, [2] its speed. A lock turn steps by detents, never by [0].
i32 turn_event[3];
void turn_by(i32 detents, i32 speed = 0) {
    turn_event[0] = detents * 0x00a3d70a;
    turn_event[1] = detents;
    turn_event[2] = speed;
}
const u32 kObject = 0x10000000;
i32 object[0x3864 / 4 + 58];
i32 live_set[58];
u32 live_sets, sets, loading, steps_asked;
u32 step_held() { return held; }
opxy::platform::step_locks::Pool lock_pool;
u32 step_handle[128], next_handle = 1;
u32 held_at() { return held_step + 16 * held_page; }
u32 track_object(u32 selected) { require(selected == track); return kObject; }
void live(u32 project, u32 selected, u32 index, i32 value) {
    require(project == 0x86bd027c && selected == track && index < 58);
    live_set[index] = value; ++live_sets;
}
u32 set(u32, u32 selected, u32 index, i32 value) { require(selected == track && index < 58); object[0x3864 / 4 + index] = value; ++sets; return 0; }
i32 scrambled(u32, u32) { return 0x1234; }
u32 stock_steps(u32, u32, u32* scale) { ++steps_asked; *scale = 2; return 0xffffffffu; }
// A kit's key (stock.hpp drum_edit_key): the key stock holds for editing
// (0x8400dfc8, -1 for none), else the track's selected key, `key`. The last
// played key (8d4d6cb0), which stock's turn falls back on and latches, only a
// track without an owned kit reads.
u32 edit_latch = 0xffffffffu, played_key = 70, played_asks;
u32 get_key(u32 selected) { require(selected == track); return key; }
u32 get_played(u32 selected) { require(selected == track); ++played_asks; return played_key; }
}
// The step-lock handles as every build reaches them (startup.cpp's calls into the
// drum_locks service).
extern "C" u32 sdk_project_locks_held_value(u32 selected, u32 note, u32 field, i32* value) {
    require(selected == track && value);
    return held && lock_pool.get(step_handle[held_at()], note, field, *value);
}
u32 frames;
extern "C" void sdk_project_locks_frame() { ++frames; }
extern "C" u32 sdk_project_locks_held_set(u32 selected, u32 note, u32 field, u32 present, i32 value) {
    require(selected == track && held);
    u32& h = step_handle[held_at()];
    if (!h) h = next_handle++;
    if (present) require(lock_pool.set(h, note, field, value));
    else lock_pool.erase(h, note, field);
    written_step = held_at(); written_index = field; written_value = static_cast<u32>(value); ++writes;
    return true;
}
namespace {
u32 get_track() { return track; }
u32 get_shift() { return shifted; }
u32 canonical(u32 selected, u32 note) { require(selected == track); return shifted_octave && note == 53 ? 65 : note; }
const u32* top() { return &controller; }
void press(u32) { ++clicks; }
void* region(u32 selected, u32 note, u32* scratch, u32 nearest) {
    note = canonical(selected, note);
    require(selected == track && note >= 53 && note < 77 && !nearest);
    *scratch = note - 53;
    return regions[note - 53];
}
void changed() { ++dirty; }
void publish(u32 note) { ++publications; last_key = note; }
u32 stock_edit(u32, u32, const u32*, u32) { ++fallback; return 1234; }
u32 turn(u32, u32 selected, u32 slot, u32) { require(selected == track); turned_slot = slot; return slot; }
const char* title(u32, u32) { return "stock"; }
u32 text(u32, const char*) { return 0; }
void shown_at(u32 a, u32 b, u32 c, u32 d, int line) {
    u8 slots[4]; sdk_synth_shown(track, slots);
    require_at(slots[0] == a && slots[1] == b && slots[2] == c && slots[3] == d, line);
}
#define shown(a, b, c, d) shown_at(a, b, c, d, __LINE__)
}

extern "C" {
// Every drum kit's press pages its whole row, as the generated table marks it.
// Engine 37 groups its five models as two and three; engine 38 has 33, so it
// stores its keys in the prefix code.
static const opxy::u8 group_sizes[] = {2, 3};
static const opxy::u8 pad_bits[] = {5, 4};
static const char* const pad_labels[] = {"FILTER", "SRR"};
static bool pad_text(opxy::u32, opxy::i32, char*) { return false; }
static const char* const group_names[] = {"ONE", "TWO"};
const opxy::platform::DrumMachine sdk_drum_machines[] = {
    {reinterpret_cast<const char*>(34), ready, 2, defaults, label, name, count},
    {reinterpret_cast<const char*>(37), ready, 5, defaults, label, name, count, 2, group_sizes, group_names},
    {reinterpret_cast<const char*>(38), ready, 33, defaults, label, name, count},
    // padMix: engine 39 with groups, engine 40 without.
    {reinterpret_cast<const char*>(39), ready, 5, defaults, label, name, count, 2, group_sizes, group_names, 1},
    {reinterpret_cast<const char*>(40), ready, 2, defaults, label, name, count, 0, nullptr, nullptr, 1},
    // padMix with groups and two pad controls: FILTER (5 bits, signed) and SRR (4).
    {reinterpret_cast<const char*>(41), ready, 5, defaults, label, name, count, 2, group_sizes, group_names, 1,
     2, pad_bits, 1, pad_labels, pad_text}
};
const opxy::u32 sdk_drum_machine_count = 6;
const opxy::platform::SynthControls sdk_synth_controls[] = {
    {reinterpret_cast<const char*>(34), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, true},
    {reinterpret_cast<const char*>(37), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, true},
    {reinterpret_cast<const char*>(38), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, true},
    {reinterpret_cast<const char*>(39), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, true},
    {reinterpret_cast<const char*>(40), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, true},
    {reinterpret_cast<const char*>(41), ready, {4,5,6,7,0,1,2,3}, 0, {}, format, false, true}
};
const opxy::u32 sdk_synth_control_count = 6;
}

namespace {
// Stock's preset import (8d4e35a8) into the current pattern (8d587708) of the
// track at the preset's +4: here a pattern block of its own, which the import
// fills as stock does for a Drumline region: params << 16, the model byte, the
// pad word max(random << 16, 0), the level and pan bytes.
alignas(4) unsigned char preset_pattern[0x394c + 24 * 0x80];
alignas(4) u32 preset_object[2];
struct ImportedRegion { u8 model_byte; u32 params[4]; i32 random; signed char gain, pan; bool listed; };
ImportedRegion imported[24];
u32 imports;
unsigned char* preset_pad(u32 k) { return preset_pattern + 0x394c + k * 0x80; }
u32 preset_import(u32 preset, u32) {
    require(preset == 0x9000);
    ++imports;
    std::memset(preset_pattern + 0x394c, 0, 24 * 0x80);
    for (u32 k = 0; k < 24; ++k) {
        if (!imported[k].listed) continue;
        i32* words = reinterpret_cast<i32*>(preset_pad(k));
        for (u32 i = 0; i < 4; ++i) words[1 + i] = static_cast<i32>(imported[k].params[i] << 16);
        const i32 random = static_cast<i32>(static_cast<u32>(imported[k].random) << 16);
        words[5] = random < 0 ? 0 : random;
        preset_pad(k)[24] = imported[k].model_byte;
        preset_pad(k)[26] = static_cast<u8>(53 + k);
        preset_pad(k)[0x1d] = static_cast<u8>(imported[k].gain);
        preset_pad(k)[0x1e] = static_cast<u8>(imported[k].pan);
    }
    return 7;
}
// The current pattern (8d587708): the preset tests' own block while they load,
// else the track object the lock tests use.
bool loading_preset;
Address current_pattern(u32 selected) {
    if (!loading_preset) return static_cast<Address>(track_object(selected));
    require(selected == preset_object[1]);
    return reinterpret_cast<Address>(preset_pattern);
}
}

opxy::platform::Address opxy::platform::host_stock_address(Address address) {
    switch (address) {
    case kSelectedTrack: return reinterpret_cast<Address>(&track);
    case 0x080238bc: return reinterpret_cast<Address>(&page);
    case 0x8d58781c: return reinterpret_cast<Address>(&get_engine);
    case 0x8d4d6c98: return reinterpret_cast<Address>(&get_key);
    case 0x8d4d6cb0: return reinterpret_cast<Address>(&get_played);
    case 0x8400dfc8: return reinterpret_cast<Address>(&edit_latch);
    case 0x8d4d6f14: return reinterpret_cast<Address>(&get_track);
    case 0x8d51f184: return reinterpret_cast<Address>(&get_shift);
    case 0x8c0944b0: return reinterpret_cast<Address>(&top);
    case 0x8d51f1fc: return reinterpret_cast<Address>(&press);
    case 0x8d58926c: return reinterpret_cast<Address>(&region);
    case 0x8d589210: return reinterpret_cast<Address>(&canonical);
    case 0x8d585ee8: return reinterpret_cast<Address>(&changed);
    case 0x8d4d5f18: return reinterpret_cast<Address>(&publish);
    case 0x8d4d5f60: return reinterpret_cast<Address>(&stock_edit);
    case 0x8d5ad5bc: return reinterpret_cast<Address>(&turn);
    case 0x8d4e35a8: return reinterpret_cast<Address>(&preset_import);
    case 0x8d587708: return reinterpret_cast<Address>(&current_pattern);
    case 0x9000: return reinterpret_cast<Address>(preset_object);
    case 0x9004: return reinterpret_cast<Address>(preset_object + 1);
    case 0x8d506c4c: return reinterpret_cast<Address>(&title);
    case 0x8d6011f0: return reinterpret_cast<Address>(&text);
    case 0x8d5b7e34: return reinterpret_cast<Address>(&step_held);
    case 0x8d5acea8: return reinterpret_cast<Address>(&live);
    case 0x8d5acf28: return reinterpret_cast<Address>(&set);
    case 0x8c0b5230: return reinterpret_cast<Address>(&scrambled);
    case 0x8c0b2468: return reinterpret_cast<Address>(&stock_steps);
    case 0x840a0528: return reinterpret_cast<Address>(&loading);
    case 0x1000: return reinterpret_cast<Address>(turn_event);
    default:
        if (address >= kObject && address < kObject + sizeof(object))
            return reinterpret_cast<Address>(object) + (address - kObject);
        if (address >= reinterpret_cast<Address>(preset_pattern) &&
            address < reinterpret_cast<Address>(preset_pattern) + sizeof(preset_pattern)) return address;
        std::abort();
    }
}

int main() {
    using namespace opxy;
    using namespace opxy::platform;
    for (u32 pad = 0; pad < 24; ++pad) {
        regions[pad][0] = 123456;
        regions[pad][5] = 987654;
        reinterpret_cast<u8*>(regions[pad])[26] = static_cast<u8>(53 + pad);
    }
    u32 presses = 0;
    shown(0,1,2,3);
    sdk_synth_press(1); ++presses; shown(4,5,6,7);   // a press pages the whole row
    const u32 event[3] = {1000, 3, 2};
    sdk_drum_edit(53, 1, event, 0);   // the second encoder's second row, slot 5
    u8 model = 99, raw[8];
    const u8* record = reinterpret_cast<const u8*>(regions[0]);
    require(drum_marked(record[24]) && drum_unpack(regions[0] + 1, &model, raw));
    require(model == 0 && raw[5] == 28 && raw[1] == 21 && raw[0] == 20 && raw[7] == 53);
    require(publications == 1 && dirty == 1 && last_key == 53);
    shifted = true;
    // Shift and the second encoder choose the choke group: none, then 1..3.
    // The edit above kept stock's play mode 0, gate, which reads as none.
    require(drum_choke(record[24]) == 0 && !std::strcmp(sdk_drum_choke(0), "NONE"));
    sdk_drum_edit(53, 5, event, 0);
    require(publications == 2 && (record[24] >> 4) == 5 && !std::strcmp(sdk_drum_choke(0), "3"));
    require(sdk_drum_choke_choice(0) == 3);
    sdk_drum_edit(53, 5, event, 0);   // mute3 is the limit
    require(publications == 2);
    const u32 none[3] = {0, static_cast<u32>(-5), 0};
    sdk_drum_edit(53, 5, none, 0);
    require(publications == 3 && drum_marked(record[24]) && (record[24] >> 4) == 1);
    require(!std::strcmp(sdk_drum_choke(0), "NONE"));
    // Shift and the third and fourth encoders turn nothing.
    sdk_drum_edit(53, 6, event, 0);
    sdk_drum_edit(53, 7, event, 0);
    require(publications == 3);
    require(drum_unpack(regions[0] + 1, &model, raw) && raw[5] == 28 && raw[1] == 21 && model == 0);
    require(regions[0][0] == 123456 && regions[0][5] == 987654 && record[26] == 53);
    sdk_synth_turn(0, 0, 5, 0);
    require(turned_slot == 5);
    // A gap: the third encoder's slot 2 is blank and inert; the second row
    // pages in whole, its gap (slot 7) included.
    shifted = false;
    sdk_synth_press(3); ++presses; shown(0,1,2,3);
    sdk_drum_edit(53, 2, event, 0);
    require(publications == 3 && drum_unpack(regions[0] + 1, &model, raw) && raw[2] == 22);
    sdk_synth_press(2); ++presses; shown(4,5,6,7);
    sdk_drum_edit(53, 3, event, 0);   // slot 7, the gap: inert
    require(publications == 3);
    sdk_synth_press(0); ++presses; shown(0,1,2,3);

    // Shift and the first encoder choose the model, as on Drumline; the
    // controls stay.
    const u32 one[3] = {0, 1, 0};
    sdk_drum_edit(53, 4, one, 0);
    require(sdk_drum_values(0, &model, raw) && model == 1 && raw[5] == 28 && publications == 4);
    shown(0,1,2,3);
    sdk_drum_edit(53, 4, event, 0);   // the last model is the limit
    require(publications == 4);
    shifted = false;

    key = 54; shown(0,1,2,3);
    sdk_synth_press(1); ++presses; shown(0,1,2,3);   // the shorter model has no second row: a press stays
    sdk_drum_edit(54, 3, event, 0);   // nor a slot 3: inert
    require(publications == 4);
    require(!std::strcmp(sdk_drum_title(34, 1), "short"));
    key = 53;
    const u32 back[3] = {0, static_cast<u32>(-1), 0};
    shifted = true;
    sdk_drum_edit(53, 4, back, 0);
    shifted = false;
    require(sdk_drum_values(0, &model, raw) && model == 0 && publications == 5);
    sdk_synth_press(1); ++presses; shown(4,5,6,7);
    key = 54; shown(0,1,2,3);   // a key without a second row returns to the first
    key = 53;
    sdk_synth_press(1); ++presses; shown(4,5,6,7);
    sdk_synth_press(1); ++presses; shown(0,1,2,3);

    const u32 negative[3] = {0, static_cast<u32>(-100000), 0};
    sdk_drum_edit(53, 0, negative, 0);   // the first encoder edits slot 0 now
    require(sdk_drum_values(0, &model, raw) && raw[0] == 0 && model == 0);
    const u32 zero[3] = {};
    sdk_drum_edit(53, 1, zero, 0);
    require(publications == 6 && dirty == 6);

    // Parent note 53 can select canonical pad 65 in the other octave. GUI
    // defaults use the selected record's key, matching the audio mapper.
    shifted_octave = true;
    u8 expected_model, expected[8];
    defaults(65, &expected_model, expected);
    require(sdk_drum_values(0, &model, raw) && model == expected_model && !std::memcmp(raw, expected, 8));
    // A missing record key uses the same stock mapping as playback.
    reinterpret_cast<u8*>(regions[12])[26] = 0;
    require(sdk_drum_values(0, &model, raw) && !std::memcmp(raw, expected, 8));
    // A pad stored by the earlier eight-value layout (tag 10, unmarked) and a
    // marked pad with a model the kit lacks both keep the key's defaults.
    regions[12][1] = 0x2a000000; regions[12][2] = 0x15550000; regions[12][3] = 0x0aaa0000; regions[12][4] = 0x50550000;
    reinterpret_cast<u8*>(regions[12])[24] = 0x22;
    require(sdk_drum_values(0, &model, raw) && model == expected_model && !std::memcmp(raw, expected, 8));
    const u8 seven[8] = {7, 7, 7, 7, 7, 7, 7, 7};
    require(drum_pack(9, seven, regions[12] + 1));
    reinterpret_cast<u8*>(regions[12])[24] = drum_mark(0x22);
    require(sdk_drum_values(0, &model, raw) && model == expected_model && !std::memcmp(raw, expected, 8));
    require(drum_pack(1, seven, regions[12] + 1));
    require(sdk_drum_values(0, &model, raw) && model == 1 && !std::memcmp(raw, seven, 8));
    require((reinterpret_cast<u8*>(regions[12])[24] >> 4) == 2);   // stock's choke group kept
    require(sdk_drum_selected_pad(0) == 65);   // the screen's pad is the audio's
    require(sdk_drum_pad(0, 53) == 65 && sdk_drum_pad(0, 54) == 54);   // a played note's, the same way
    require(!sdk_drum_pad(0, 30) && !sdk_drum_pad(0, 128) && !sdk_drum_pad(8, 53));
    shifted_octave = false;
    require(sdk_drum_selected_pad(0) == 53);

    {
        // Model groups: Shift's first encoder chooses the group, keeping the
        // model's place in it; the second the model in the group; the third
        // the choke group; the fourth nothing.
        engine = 37; key = 54; shifted = true;
        const u32 up[3] = {0, 1, 0}, many[3] = {0, 3, 0}, back5[3] = {0, static_cast<u32>(-5), 0};
        u8 m = 99, v[8];
        require(sdk_drum_values(0, &m, v) && m == 1);   // group ONE, its second
        sdk_drum_edit(54, 4, up, 0);
        require(sdk_drum_values(0, &m, v) && m == 3);   // group TWO, its second
        const u32 before = publications;
        sdk_drum_edit(54, 4, up, 0);   // TWO is the last group
        require(publications == before && sdk_drum_values(0, &m, v) && m == 3);
        sdk_drum_edit(54, 5, many, 0);
        require(sdk_drum_values(0, &m, v) && m == 4);   // the group's last model is the limit
        sdk_drum_edit(54, 5, back5, 0);
        require(sdk_drum_values(0, &m, v) && m == 2);   // and its first
        sdk_drum_edit(54, 4, back5, 0);
        require(sdk_drum_values(0, &m, v) && m == 0);   // ONE, its first
        const u8* pad = reinterpret_cast<const u8*>(regions[1]);
        require(drum_choke(pad[24]) == 0);
        sdk_drum_edit(54, 6, up, 0);
        require(drum_choke(pad[24]) == 1 && !std::strcmp(sdk_drum_choke(0), "1"));
        const u32 still = publications;
        sdk_drum_edit(54, 7, up, 0);
        require(publications == still);

        // A paged kit: a press moves the whole row, slot 7 too though this
        // model leaves it unnamed. A 33-model kit keeps every control at 7
        // bits: a detent is one unit, to 127, and slot 7 keeps its value.
        engine = 38; key = 55; shifted = false;
        shown(0,1,2,3);
        sdk_synth_press(2); ++presses; shown(4,5,6,7);
        sdk_drum_edit(55, 2, up, 0);
        require(sdk_drum_values(0, &m, v) && m == 0 && v[6] == 27 && v[7] == 55 && v[5] == 25);
        const u32 far[3] = {0, 100, 0};
        sdk_drum_edit(55, 2, far, 0);
        require(sdk_drum_values(0, &m, v) && v[6] == 127);
        sdk_drum_edit(55, 2, back5, 0);
        require(sdk_drum_values(0, &m, v) && v[6] == 122);
        sdk_synth_press(0); ++presses; shown(0,1,2,3);
        engine = 34; key = 53;
    }

    {
        // padMix: Shift's first row is the model (every model, through the
        // groups), the choke group, the key's stock pan and its level; its
        // second, every key's, the pad fields: the kit's own pad controls on
        // positions 0 and 1, then FX I and FX II. Pan moves as stock's editor
        // does, (event[2] + 1) * event[1] a turn, within -100..100; level one
        // dB a detent, a cut within -30..0. Once a key is set up neither
        // changes the stored words or the mark.
        engine = 39; key = 56; shifted = true;
        u8* pad = reinterpret_cast<u8*>(regions[3]);
        const DrumMachine* kit = sdk_drum_machine(39);
        require(kit && kit->second_shift_row() && !sdk_drum_machine(37)->second_shift_row());
        for (u32 id = 39; id <= 41; ++id) {
            const DrumMachine* m = sdk_drum_machine(id);
            require(m->shift(0, false) == DrumMachine::kShiftModel && m->shift(1, false) == DrumMachine::kShiftChoke &&
                    m->shift(2, false) == DrumMachine::kShiftPan && m->shift(3, false) == DrumMachine::kShiftLevel);
            require(m->shift(2, true) == DrumMachine::kShiftPad2 && m->shift(3, true) == DrumMachine::kShiftPad3);
            require(!std::strcmp(m->pad_label(2), "FX I") && !std::strcmp(m->pad_label(3), "FX II"));
        }
        require(kit->shift(0, true) == DrumMachine::kShiftNone && kit->shift(1, true) == DrumMachine::kShiftNone);
        // Without padMix nothing moves: the third choke (groups) or nothing, the fourth nothing.
        require(sdk_drum_machine(37)->shift(2, true) == DrumMachine::kShiftChoke && sdk_drum_machine(37)->shift(0, false) == DrumMachine::kShiftGroup);
        require(sdk_drum_machine(34)->shift(2, false) == DrumMachine::kShiftNone);
        // A key as an SDK 1.9 kit or stock left it: a cut of 6 dB in its level
        // byte, as stock's default kit has, and stock's value in the pad word.
        // It plays and shows 0 dB with every field neutral, until the kit's
        // first edit sets it up: neutral fields, 0 dB and the mark, the
        // model and controls kept.
        pad[0x1d] = static_cast<u8>(-6);
        require(!drum_marked(pad[24]) && regions[3][5] == 987654);
        u8 old_model, old_values[8], new_model, new_values[8];
        require(sdk_drum_values(0, &old_model, old_values));
        require(sdk_drum_pan(0) == 0 && sdk_drum_level(0) == 0);
        u32 before = publications;
        const u32 fast[3] = {0, 3, 2}, slow[3] = {0, 1, 0}, down[3] = {0, static_cast<u32>(-1), 0};
        sdk_drum_edit(56, 6, fast, 0);
        require(static_cast<signed char>(pad[0x1e]) == 9 && sdk_drum_pan(0) == 9 && publications == before + 1);
        require(drum_ready(pad) && regions[3][5] == static_cast<i32>(kDrumPadReady) && pad[0x1d] == 0 &&
                sdk_drum_level(0) == 0);
        require(sdk_drum_values(0, &new_model, new_values) && new_model == old_model &&
                !std::memcmp(old_values, new_values, 8));
        i32 words[5];
        std::memcpy(words, regions[3], sizeof(words));
        const u8 mark = pad[24];
        const u32 left[3] = {0, static_cast<u32>(-100), 3};
        sdk_drum_edit(56, 6, left, 0);
        require(sdk_drum_pan(0) == -100 && publications == before + 2);
        sdk_drum_edit(56, 6, left, 0);   // the left end is the limit
        require(publications == before + 2);
        const u32 far[3] = {0, 127, 99};
        sdk_drum_edit(56, 6, far, 0);
        require(sdk_drum_pan(0) == 100);
        const u32 back[3] = {0, static_cast<u32>(-100), 0};
        sdk_drum_edit(56, 6, back, 0);
        require(sdk_drum_pan(0) == 0 && pad[0x1e] == 0);   // 0 is the centre
        before = publications;
        sdk_drum_edit(56, 7, slow, 0);   // 0 dB is the top: no boost
        require(publications == before && pad[0x1d] == 0);
        sdk_drum_edit(56, 7, down, 0);
        require(static_cast<signed char>(pad[0x1d]) == -1 && sdk_drum_level(0) == -1 && publications == before + 1);
        const u32 many[3] = {0, static_cast<u32>(-50), 7};   // one dB a detent, whatever the speed
        sdk_drum_edit(56, 7, many, 0);
        require(sdk_drum_level(0) == -30 && static_cast<signed char>(pad[0x1d]) == -30);
        // A boost stock stored (up to +20 dB) shows and plays as 0 dB; turning
        // up keeps it, turning down cuts from 0 dB.
        pad[0x1d] = 12;
        require(sdk_drum_level(0) == 0);
        before = publications;
        sdk_drum_edit(56, 7, slow, 0);
        require(pad[0x1d] == 12 && publications == before);
        sdk_drum_edit(56, 7, down, 0);
        require(static_cast<signed char>(pad[0x1d]) == -1);
        require(!std::memcmp(words, regions[3], sizeof(words)) && pad[24] == mark);
        // The model scrolls every model, through the groups (5 models, groups
        // of 2 and 3); the choke group is on the second encoder.
        key = 53;
        const u32 up[3] = {0, 1, 0}, ten[3] = {0, 10, 0}, all_back[3] = {0, static_cast<u32>(-10), 0};
        sdk_drum_edit(53, 4, all_back, 0);
        u8 m, v[8];
        require(sdk_drum_values(0, &m, v) && m == 0);
        sdk_drum_edit(53, 4, up, 0); sdk_drum_edit(53, 4, up, 0);
        require(sdk_drum_values(0, &m, v) && m == 2);   // into the second group
        sdk_drum_edit(53, 4, ten, 0);
        require(sdk_drum_values(0, &m, v) && m == 4);   // the last model is the limit
        u8* first = reinterpret_cast<u8*>(regions[0]);
        const u32 choke_before = drum_choke(first[24]);
        sdk_drum_edit(53, 5, choke_before < 3 ? up : down, 0);
        require(drum_choke(first[24]) != choke_before);
        // The second row: every key has one, a short model's too (key 54).
        shifted = false;
        key = 54;
        sdk_synth_press(0); ++presses; shown(4,5,6,7);
        shifted = true;
        u8* other = reinterpret_cast<u8*>(regions[1]);
        i32* other_word = regions[1] + 5;
        before = publications;
        sdk_drum_edit(54, 4, up, 0);   // no pad controls here: positions 0 and 1 turn nothing
        sdk_drum_edit(54, 5, up, 0);
        require(publications == before);
        sdk_drum_edit(54, 6, up, 0);   // FX I
        sdk_drum_edit(54, 7, ten, 0);  // FX II, to its top
        require(publications == before + 2 && drum_marked(other[24]));
        require(((static_cast<u32>(*other_word) >> 25) & 7) == 1 && ((static_cast<u32>(*other_word) >> 28) & 7) == 7);
        i32 values[4];
        sdk_drum_pad_values(0, values);
        require(!values[0] && !values[1] && values[2] == 1 && values[3] == 7);
        sdk_drum_edit(54, 6, back, 0);   // back to 0: TRK, the track's send
        sdk_drum_pad_values(0, values);
        require(!values[2] && other[0x1e] == 0 && other[0x1d] == 0);
        shifted = false;
        sdk_synth_press(0); ++presses; shown(0,1,2,3);
        // padMix without groups: the same first row.
        engine = 40; key = 53; shifted = true;
        sdk_drum_edit(53, 6, up, 0);
        sdk_drum_edit(53, 7, down, 0);
        require(sdk_drum_pan(0) == 1 && sdk_drum_level(0) == -1);
        first[0x1e] = first[0x1d] = 0;
        engine = 34; key = 53; shifted = false;
        require(sdk_drum_pan(8) == 0 && sdk_drum_level(8) == 0);   // no instrument track
    }

    {
        // Pad controls: positions 0 and 1 of the second Shift row, packed from
        // bit 16 of the pad word +0x14 (FILTER 16..20, SRR 21..24), then the
        // sends (FX I 25..27, FX II 28..30). A key the kit has not set up reads
        // neutral whatever stock left there, marked or not; its first edit sets
        // it up, the word the set-up value alone with that edit applied.
        engine = 41; key = 57; shifted = true;
        const DrumMachine* kit = sdk_drum_machine(41);
        require(kit->shift(0, true) == DrumMachine::kShiftPad && kit->shift(1, true) == DrumMachine::kShiftPad1);
        require(!std::strcmp(kit->pad_label(0), "FILTER") && !std::strcmp(kit->pad_label(1), "SRR"));
        i32* word = regions[4] + 5;
        u8* pad = reinterpret_cast<u8*>(regions[4]);
        require(!drum_marked(pad[24]));
        *word = 0x7a5a1234;   // a value stock left: bit 31 clear, its fields nonzero
        pad[0x1d] = static_cast<u8>(-2);
        i32 values[4] = {9, 9, 9, 9};
        sdk_drum_pad_values(0, values);
        require(!values[0] && !values[1] && !values[2] && !values[3] && sdk_drum_level(0) == 0);
        // Marked by an SDK 1.9 kit: still not set up, still neutral. Stock's
        // default kit's last key and every value its Drumline editor writes.
        pad[24] = drum_mark(pad[24]);
        for (u32 b = 0; b <= 200; ++b) {
            *word = b == 200 ? 0x26660000 : static_cast<i32>(b * 10737418u + 5368709u);
            require(!drum_ready(pad));
            values[0] = values[1] = values[2] = values[3] = 9;
            sdk_drum_pad_values(0, values);
            require(!values[0] && !values[1] && !values[2] && !values[3] && sdk_drum_level(0) == 0);
        }
        *word = 0x7a5a1234;
        shifted = false;
        sdk_synth_press(0); ++presses; shown(4,5,6,7);
        shifted = true;
        const u32 left[3] = {0, static_cast<u32>(-3), 0}, up[3] = {0, 1, 0}, far[3] = {0, 100, 0}, back[3] = {0, static_cast<u32>(-100), 0};
        u32 before = publications;
        sdk_drum_edit(57, 4, left, 0);   // FILTER three steps low-pass
        require(drum_marked(pad[24]) && publications == before + 1);
        require(static_cast<u32>(*word) == (((32u - 3u) << 16) | kDrumPadReady) && pad[0x1d] == 0);
        sdk_drum_pad_values(0, values);
        require(values[0] == -3 && !values[1] && !values[2] && !values[3]);
        sdk_drum_edit(57, 4, back, 0);
        sdk_drum_pad_values(0, values);
        require(values[0] == -16);
        sdk_drum_edit(57, 4, far, 0);
        sdk_drum_edit(57, 5, far, 0);
        sdk_drum_edit(57, 6, far, 0);
        sdk_drum_edit(57, 7, up, 0);
        sdk_drum_pad_values(0, values);
        require(values[0] == 15 && values[1] == 15 && values[2] == 7 && values[3] == 1);
        require((static_cast<u32>(*word) >> 16 & 31) == 15 && (static_cast<u32>(*word) >> 21 & 15) == 15 &&
                (static_cast<u32>(*word) >> 25 & 7) == 7 && (static_cast<u32>(*word) >> 28 & 7) == 1);
        before = publications;
        sdk_drum_edit(57, 5, up, 0);     // the last step is the limit
        require(publications == before);
        // A project keeps the whole record. A preset keeps the word's top half
        // (`random`, restored shifted up 16), not the set-up value: the key it
        // restores plays every field neutral until the kit edits it again.
        const i32 saved = *word;
        *word = static_cast<i32>((static_cast<u32>(saved) >> 16) << 16);
        sdk_drum_pad_values(0, values);
        require(!drum_ready(pad) && !values[0] && !values[1] && !values[2] && !values[3]);
        *word = saved;
        // Another kit's edits leave the word alone.
        engine = 37; key = 57;
        const i32 kept = *word;
        sdk_drum_edit(57, 6, up, 0);
        require(*word == kept);   // another engine's track starts on its first row again
        shifted = false;
        engine = 34; key = 53;
    }

    {
        // A Shift press (stock's Shift-press action, through the hook at
        // 8c0b2730) switches a padMix kit's row, both ways, a short model's
        // key too; it never reaches stock's press recorder. Other kits, other
        // pages and other controllers keep stock's.
        engine = 39; key = 54; shifted = true;
        const u32 clicks_before = clicks;
        shown(0,1,2,3);
        require(sdk_drum_shift_press(0, 2)); shown(4,5,6,7);
        require(sdk_drum_shift_press(0, 0)); shown(0,1,2,3);
        require(!sdk_drum_shift_press(1, 0) && !sdk_drum_shift_press(0, 4));   // another track; no such encoder
        page = 1;
        require(!sdk_drum_shift_press(0, 0));
        page = 0;
        controller = 0x1234;
        require(!sdk_drum_shift_press(0, 0));
        controller = 0x8400dff8;
        shown(0,1,2,3);
        engine = 37;   // groups, no padMix: no second Shift row
        require(!sdk_drum_shift_press(0, 0));
        shown(0,1,2,3);
        require(clicks == clicks_before);
        engine = 34; key = 53; shifted = false;
    }

    {
        // Per-hit locks of a padMix kit's Shift fields (drum_locks.hpp). Stock
        // routes Shift's turns to sdk_drum_edit unless a step is held; then
        // here, where they lock the held step's hits for the selected key in
        // the step-lock handles' pool (drum_lock_store.hpp), PAN included.
        engine = 41; key = 57; shifted = true;
        u32 project = 0x86bd027c;
        const u32 event = 0x1000;
        turn_by(1);
        turned_slot = 99;
        // No step held: a Shift turn that reaches here is stock's fine turn of
        // the engine control the position shows, as before.
        sdk_synth_turn(project, 0, 6, event);
        require(turned_slot == 2);
        held = true;
        // First row: MACHINE and CHOKE lock nothing; PAN and LEVEL lock the held
        // step (5 + 16 x page 2) for key 57, none through stock's editor.
        for (u32 position = 0; position < 2; ++position) {
            turned_slot = 99;
            require(sdk_synth_turn(project, 0, 4 + position, event) == 0 && turned_slot == 99 && !writes);
        }
        turn_by(15);
        require(sdk_synth_turn(project, 0, 6, event) == 1 && turned_slot == 99 && writes == 1);
        require(written_step == 37 && written_index == kHitPan && written_value == 15);
        turn_by(-3);
        sdk_synth_turn(project, 0, 7, event);
        require(turned_slot == 99 && writes == 2 && written_index == kHitLevel);
        DrumHitLocks shown;
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(shown.offset[kHitPan] == 15 && shown.offset[kHitLevel] == -3 && !shown.offset[kHitSend1]);
        require(sdk_drum_level(0) == -3);
        turn_by(-40);   // clamped at -30
        sdk_synth_turn(project, 0, 7, event);
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(shown.offset[kHitLevel] == -30 && writes == 3);
        sdk_synth_turn(project, 0, 7, event);
        require(writes == 3);   // no change, no write
        // Second row: FILTER and SRR, then FX I and FX II.
        shifted = false;
        sdk_synth_press(0); ++presses; shown(4,5,6,7);
        shifted = true;
        turn_by(2);
        sdk_synth_turn(project, 0, 4, event);
        require(writes == 4 && written_index == kHitPad1);
        turn_by(-1);
        sdk_synth_turn(project, 0, 5, event);
        require(writes == 5 && written_index == kHitPad2);
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(shown.offset[kHitPad1] == 2 && shown.offset[kHitPad2] == -1 && shown.offset[kHitLevel] == -30);
        // A send lock starts from what the key plays: key 57, set up, plays FX I
        // at code 3 (lock 4), so a detent up locks 4 + 1; FX II from TRK (lock 1).
        u8* pad = reinterpret_cast<u8*>(regions[4]);
        pad[24] = drum_mark(0x20);
        regions[4][5] = static_cast<i32>((3u << 25) | kDrumPadReady);
        turn_by(1);
        sdk_synth_turn(project, 0, 6, event);
        require(writes == 6 && written_index == kHitSend1);
        sdk_synth_turn(project, 0, 7, event);
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(shown.offset[kHitSend1] == 5 && shown.offset[kHitSend2] == 2 && shown.offset[kHitLevel] == -30);
        turn_by(20);    // at most 100 (lock 8)
        sdk_synth_turn(project, 0, 7, event);
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(shown.offset[kHitSend2] == 8);
        // The Shift row shows what the step plays: the pad controls with their
        // offsets, a send with its override (code = lock - 1).
        i32 values[4];
        sdk_drum_pad_values(0, values);
        require(values[0] == 2 && values[1] == 0 && values[2] == 4 && values[3] == 7);   // SRR clamps at its 0
        // Turned back to 0, a field is unlocked: FILTER from 2 by -2.
        turn_by(-2);
        sdk_synth_turn(project, 0, 4, event);
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(writes == 9 && !shown.offset[kHitPad1] && shown.offset[kHitPad2] == -1);
        i32 stored_value;
        require(!lock_pool.own(step_handle[37], 57, kHitPad1, stored_value));
        // Per key: another selected key shows and locks its own.
        key = 58;
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(!shown.any());
        turn_by(4);
        sdk_synth_turn(project, 0, 4, event);
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(shown.offset[kHitPad1] == 4 && !shown.offset[kHitLevel]);
        key = 57;
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(!shown.offset[kHitPad1] && shown.offset[kHitLevel] == -30);
        // Another held step has locks of its own.
        held_step = 6;
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(!shown.any());
        held_step = 5;
        // A lock walks its whole range a detent at a time, as the unlocked
        // field does (overnight-001 read the event's continuous word as the
        // step, so every turn landed on one end or the other). On step 7:
        // FILTER (signed 5 bits, -31..31 as a lock) one a detent, both ways,
        // to its ends; LEVEL one dB; PAN (speed + 1) a detent.
        held_step = 7;
        {
            const u32 ahead = writes;
            for (i32 n = 1; n <= 31; ++n) {
                turn_by(1);
                sdk_synth_turn(project, 0, 4, event);
                drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
                require(shown.offset[kHitPad1] == n);
            }
            turn_by(1);
            sdk_synth_turn(project, 0, 4, event);   // at its end: no write
            require(writes == ahead + 31);
            for (i32 n = 30; n >= -31; --n) {
                turn_by(-1);
                sdk_synth_turn(project, 0, 4, event);
                drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
                require(shown.offset[kHitPad1] == n || (n == 0 && !shown.offset[kHitPad1]));
            }
            turn_by(-1);
            sdk_synth_turn(project, 0, 5, event);   // SRR one step
            drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
            require(shown.offset[kHitPad2] == -1);
        }
        // Back on the first row: LEVEL -1 dB, PAN 3 x (2 + 1) at speed 2.
        shifted = false;
        sdk_synth_press(0); ++presses; shown(0,1,2,3);
        shifted = true;
        turn_by(-1);
        sdk_synth_turn(project, 0, 7, event);
        turn_by(3, 2);
        sdk_synth_turn(project, 0, 6, event);
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(shown.offset[kHitLevel] == -1 && shown.offset[kHitPan] == 9);
        // A turn with no detents (motion only) locks nothing.
        {
            const u32 ahead = writes;
            turn_event[0] = 0x7fffffff; turn_event[1] = 0; turn_event[2] = 0;
            sdk_synth_turn(project, 0, 6, event);
            require(writes == ahead);
        }
        // The key stock edits: with a pad held for editing (0x8400dfc8) the Shift
        // row shows and locks it; without one, the selected key.
        edit_latch = 60;
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(!shown.any());
        turn_by(5);
        sdk_synth_turn(project, 0, 6, event);
        require(written_index == kHitPan);
        i32 latched;
        require(lock_pool.own(step_handle[held_at()], 60, kHitPan, latched) && latched == 5);
        require(sdk_drum_pan(0) == 5);
        edit_latch = 0xffffffffu;
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(shown.offset[kHitPan] == 9);
        // With none held, stock's turn falls back on the last played key
        // (8c0b14b4) and latches it (8c0b167a). On a kit's track the fallback is
        // the selected key and the latch keeps -1, so a turn edits the key the
        // screen shows, even in the frame a page or track change set -1, and it
        // follows the selected key; the SDK never writes the held key.
        require(sdk_drum_unheld_key(0) == 57 && sdk_drum_latch_key(0) == 0xffffffffu && !played_asks);
        edit_latch = sdk_drum_latch_key(0);   // the latch's store
        key = 58;
        require(sdk_drum_unheld_key(0) == 58 && drum_edit_key(0) == 58);
        const u32 frames_before = frames;
        sdk_send_refresh();
        require(edit_latch == 0xffffffffu && frames == frames_before + 1);   // and a frame for the handles
        // A pad held or chosen with Shift is stock's, whatever its key: one the
        // same as the selected key stays when the selected key moves.
        edit_latch = 58;
        key = 59;
        sdk_send_refresh();
        require(edit_latch == 58 && drum_edit_key(0) == 58);
        drum_lock_store_held(*sdk_drum_machine(41), 0, shown);
        require(!shown.any());                // key 58's locks, not 59's
        edit_latch = 0xffffffffu;             // a page change
        require(drum_edit_key(0) == 59);
        engine = 99;                          // not a kit: stock's own
        require(sdk_drum_unheld_key(0) == 70 && sdk_drum_latch_key(0) == 70 && played_asks == 2);
        engine = 41;
        key = 57;
        edit_latch = 0xffffffffu;
        shifted = false;
        sdk_synth_press(0); ++presses; shown(4,5,6,7);
        shifted = true;
        held_step = 5;
        // Unshifted, a held step locks the engine control shown, as before.
        shifted = false;
        sdk_synth_turn(project, 0, 1, event);
        require(turned_slot == 5);
        // A padMix kit without pad controls: FILTER and SRR's positions lock
        // nothing; FX I and FX II still lock.
        engine = 39;   // another engine: its encoders start on the first row
        sdk_synth_press(0); ++presses; shown(4,5,6,7);
        shifted = true;
        const u32 before = writes;
        turn_by(1);
        for (u32 position = 0; position < 2; ++position) {
            turned_slot = 99;
            require(sdk_synth_turn(project, 0, 4 + position, event) == 0 && turned_slot == 99);
        }
        require(writes == before);
        sdk_synth_turn(project, 0, 6, event);
        require(writes == before + 1);
        shifted = false;
        sdk_synth_press(0); ++presses; shown(0,1,2,3);
        // A kit without padMix keeps stock's fine-turn lock of the control shown.
        engine = 34; key = 53; shifted = true;
        sdk_synth_turn(project, 0, 6, event);
        require(turned_slot == 2);

        // No word is left to stock's editor: every lock word gets stock's own
        // step answer, M3's (16..19) included.
        engine = 41;
        u32 scale = 9, asked = steps_asked;
        const u32 words[] = {16, 17, 18, 19, 36, 37, 38};
        for (u32 index : words) {
            scale = 9;
            require(sdk_param_steps(0, index, &scale) == 0xffffffffu && steps_asked == ++asked);
        }

        const DrumMachine& kit = *sdk_drum_machine(41);
        i32 low, high;
        drum_lock_range(kit, kHitPad1, low, high);
        require(low == -31 && high == 31);
        drum_lock_range(kit, kHitPad2, low, high);
        require(low == -15 && high == 15);
        drum_lock_range(*sdk_drum_machine(39), kHitPad1, low, high);
        require(low == 0 && high == 0);
        // A stored value outside the field's range (a pool from elsewhere) shows as none.
        require(lock_pool.set(step_handle[37], 57, kHitPad2, 40));
        drum_lock_store_held(kit, 0, shown);
        require(!shown.offset[kHitPad2]);

        // PAN with a step held: the key's pan plus the step's offset, clamped.
        engine = 41; key = 57;
        pad[0x1e] = 90; pad[0x1d] = static_cast<u8>(-12);
        require(sdk_drum_pan(0) == 100 && sdk_drum_level(0) == -30);
        pad[0x1e] = static_cast<u8>(-20);
        require(sdk_drum_pan(0) == -5);
        held = false;
        require(sdk_drum_pan(0) == -20 && sdk_drum_level(0) == -12);
        sdk_drum_pad_values(0, values);
        require(values[0] == 0 && values[2] == 3 && values[3] == 0);
        pad[0x1e] = pad[0x1d] = 0;
        pad[24] = 0; regions[4][5] = 0;

        // Saved values: after scramble the per-hit words have no lock, stored
        // and live (stock's scramble leaves 36..41; whatever put one there).
        // Other kits keep stock's scramble.
        object[0x3864 / 4 + 36] = 0x10000; object[0x3864 / 4 + 37] = 0x20000; object[0x3864 / 4 + 38] = 0;
        object[0x3864 / 4 + 16] = 0x1111;
        live_sets = 0;
        sdk_synth_scramble(project, 0, 13, 77);
        require(!object[0x3864 / 4 + 36] && !object[0x3864 / 4 + 37] && object[0x3864 / 4 + 38] == kDrumPanTermNeutral);
        require(!live_set[36] && !live_set[37] && live_set[38] == kDrumPanTermNeutral && live_sets == 3);
        require(object[0x3864 / 4 + 13] == 77 && object[0x3864 / 4 + 16] == 0x1111);   // M3's are the scramble's
        engine = 34;
        object[0x3864 / 4 + 36] = 5;
        sdk_synth_scramble(project, 0, 13, 78);
        require(object[0x3864 / 4 + 36] == 5 && live_sets == 3);
        // Once a frame the selected kit's base is set back if it is not, never
        // while a project loads.
        engine = 41;
        loading = 1;
        sdk_send_refresh();
        require(object[0x3864 / 4 + 36] == 5 && live_sets == 3);
        loading = 0;
        sdk_send_refresh();
        require(!object[0x3864 / 4 + 36] && !live_set[36] && live_sets == 4);
        sdk_send_refresh();
        require(live_sets == 4);   // already neutral: nothing written
        // Project load's check of each pattern's track object: a padMix kit's
        // per-hit words with no lock, M3's words untouched, the alias as before.
        object[0x3864 / 4 + 36] = 9; object[0x3864 / 4 + 37] = 9; object[0x3864 / 4 + 38] = 0;
        object[0x3864 / 4 + 16] = 3;
        require(sdk_drum_load_engine(34, kObject) == 4 && object[0x3864 / 4 + 36] == 9);
        require(sdk_drum_load_engine(99, kObject) == 99 && object[0x3864 / 4 + 36] == 9);
        require(sdk_drum_load_engine(41, kObject) == 4);
        require(!object[0x3864 / 4 + 36] && !object[0x3864 / 4 + 37] && object[0x3864 / 4 + 38] == kDrumPanTermNeutral);
        require(object[0x3864 / 4 + 16] == 3 && object[0x3864 / 4 + 35] == 0 && object[0x3864 / 4 + 39] == 0);
        shifted = false; held = false;
        engine = 34; key = 53;
    }

    {
        // A padMix kit's preset: stock's import leaves each listed key's pad
        // fields in bits 16..30 (`random` << 16), its level and pan, and no
        // mark. Before, every such key played and showed neutral at 0 dB; now
        // the load sets up each key the preset marked, as loaded. Regions as
        // THUNDER's presets write them (brushes key 63, foundry key 60,
        // gamelan-shed key 55): FX II 33 (code 3); DRIVE 9; TUNE 2.
        const ImportedRegion kit_regions[3] = {
            {drum_mark(0x00), {7770, 18334, 24578, 10243}, 12288, -9, 30, true},
            {drum_mark(0x10), {11554, 19004, 1282, 12288}, 288, -13, 25, true},
            {drum_mark(0x00), {3919, 29741, 1439, 8192}, 2, -6, 10, true}};
        for (u32 k = 0; k < 24; ++k) imported[k] = ImportedRegion{};
        imported[10] = kit_regions[0];
        imported[7] = kit_regions[1];
        imported[2] = kit_regions[2];
        imported[4] = ImportedRegion{0x43, {0x0abc, 0, 0, 0}, 0x7a5a, -6, 0, true};   // a stock Drumline pad
        preset_object[1] = 0;
        loading_preset = true;
        // Stock's import alone (what a preset load did before): nothing set up.
        preset_import(0x9000, 0);
        for (u32 k : {10u, 7u, 2u}) require(drum_marked(preset_pad(k)[24]) && !drum_ready(preset_pad(k)));
        // Through the hooked calls: engine 41 (padMix, FILTER 5 signed, SRR 4).
        *reinterpret_cast<u32*>(preset_pattern + 0x18) = 41;
        require(sdk_drum_preset_load(0x9000, 0) == 7 && imports == 2);
        for (u32 k : {10u, 7u, 2u}) require(drum_ready(preset_pad(k)));
        const i32* fx2 = reinterpret_cast<const i32*>(preset_pad(10));
        require(fx2[5] == static_cast<i32>((12288u << 16) | kDrumPadReady));
        require((static_cast<u32>(fx2[5]) >> 28 & 7) == 3 && static_cast<signed char>(preset_pad(10)[0x1d]) == -9 &&
                static_cast<signed char>(preset_pad(10)[0x1e]) == 30);
        require((static_cast<u32>(reinterpret_cast<const i32*>(preset_pad(7))[5]) >> 21 & 15) == 9);
        require((static_cast<u32>(reinterpret_cast<const i32*>(preset_pad(2))[5]) >> 16 & 31) == 2);
        // The stock pad, and every key the preset left out, stay as stock left them.
        require(!drum_ready(preset_pad(4)) && reinterpret_cast<const i32*>(preset_pad(4))[5] == 0x7a5a0000);
        require(!drum_ready(preset_pad(0)) && !reinterpret_cast<const i32*>(preset_pad(0))[5]);
        // A kit without padMix, a synth and an aux track's preset set nothing up.
        for (u32 engine : {34u, 99u}) {
            *reinterpret_cast<u32*>(preset_pattern + 0x18) = engine;
            sdk_drum_preset_load(0x9000, 0);
            for (u32 k : {10u, 7u, 2u}) require(!drum_ready(preset_pad(k)));
        }
        *reinterpret_cast<u32*>(preset_pattern + 0x18) = 41;
        preset_object[1] = 9;
        const u32 before = imports;
        sdk_drum_preset_load(0x9000, 0);
        require(imports == before + 1 && !drum_ready(preset_pad(10)));
        preset_object[1] = 0;
        loading_preset = false;
    }

    ownership = 0;
    require(sdk_drum_edit(53, 0, one, 0) == 1234 && fallback == 1);
    require(!sdk_drum_values(0, &model, raw));
    require(!sdk_drum_selected_pad(0));
    shown(0,1,2,3);
    sdk_synth_press(0); ++presses; shown(0,1,2,3);
    require(clicks == presses);
    return 0;
}
