// Step locks at note start (drum_locks.cpp): the two hooks inside a host model
// of stock 1.2.1's note start. Note start calls the pad payload builder
// 8d5ad660 (hooked at 8d595b50), which fills the eight payload words from the
// played pad's record as stock does for Drumline (8d5ad6ec..8d5ad74a) and
// publishes them through 8d5ad194 (hooked at 8d5ad776). The GUI's live words
// 16..19 hold the step's locks, as the sequencer's lock pass leaves them.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "stock.hpp"
#include "drum_locks.hpp"

extern "C" {
opxy::u32 sdk_drum_lock_note(opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32, opxy::u32)
    __asm__("sdk_drum_lock_note");
opxy::u32 sdk_drum_lock_publish(opxy::u32, opxy::u32, opxy::u32, opxy::i32*) __asm__("sdk_drum_lock_publish");
}

namespace {
using namespace opxy;
using namespace opxy::platform;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (0)

const u32 kProject = 0x86bd027c, kLive = kProject + 0x4080;
i32 live[8 * 58];
i32 records[24][8];
i32 payload[8], published[8];
u32 publications, published_track, built;
const u32 kTrack = 2;

int ready() { return 1; }
// Key 53's default is model 1, every control 30 + its slot.
void defaults(u32 key, u8* model, u8* controls) {
    *model = static_cast<u8>((key - 53) % 3);
    for (u32 i = 0; i < 8; ++i) controls[i] = static_cast<u8>(30 + i);
}
const char* label(u32, u32) { return "control"; }
const char* name(u32) { return "model"; }
u32 count(u32) { return 8; }

void* region(u32 track, u32 key, u32* scratch, u32 nearest) {
    CHECK(track == kTrack && key >= 53 && key < 77 && !nearest);
    *scratch = key - 53;
    return records[key - 53];
}
u32 canonical(u32, u32 key) { return key; }
// 8d5ad604: whole dB to Q27 gain, 0 dB unity.
i32 gain(i32 db) { return static_cast<i32>(std::pow(10.0, db / 20.0) / 16.0 * 2147483648.0); }
u32 publish(u32 project, u32 track, u32, i32* words) {
    CHECK(project == kProject && words == payload);
    std::memcpy(published, words, sizeof(published));
    published_track = track;
    return ++publications;
}
// 8d5ad660 for a Drumline pad (the kit's alias), as stock fills 0x86bd025c.
u32 builder(u32 project, u32 track, u32 key, u32 engine, u32, u32, u32) {
    CHECK(project == kProject && engine >= 34);
    u32 scratch;
    const i32* record = static_cast<const i32*>(region(track, key, &scratch, 0));
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(record);
    const u32 type = bytes[24] & 15, group = bytes[24] >> 4;
    payload[0] = static_cast<i32>((6 * type + group) * 0x01f07c1fu + 0x00f83e0fu);
    for (u32 i = 1; i < 5; ++i) payload[i] = record[i];
    payload[5] = static_cast<signed char>(bytes[0x1e]) * 0x00a3d70a + 0x40000000;
    payload[6] = record[5] > 0 ? record[5] : 0;
    payload[7] = gain(static_cast<signed char>(bytes[0x1d]));
    ++built;
    return sdk_drum_lock_publish(project, track, 0, payload);   // its call at 8d5ad776
}
// One note on `key`, as note start makes it.
void note(u32 engine, u32 key) { sdk_drum_lock_note(kProject, kTrack, key, engine, 0, 0, 0); }
i32* locks(u32 track = kTrack) { return live + track * 58 + kDrumLockWord; }
void lock(i32 pan, i32 level, i32 filter, i32 srr) {
    locks()[0] = drum_lock_word(pan, 100); locks()[1] = drum_lock_word(level, 30);
    locks()[2] = drum_lock_word(filter, 31); locks()[3] = drum_lock_word(srr, 15);
}
unsigned char* bytes(u32 key) { return reinterpret_cast<unsigned char*>(records[key - 53]); }
}

extern "C" {
// Engine 41: padMix with FILTER (5 bits, signed) and SRR (4); 40: padMix
// without pad controls; 34: a kit without padMix.
static const u8 pad_bits[] = {5, 4};
static const char* const pad_labels[] = {"FILTER", "SRR"};
const DrumMachine sdk_drum_machines[] = {
    {reinterpret_cast<const char*>(34), ready, 3, defaults, label, name, count},
    {reinterpret_cast<const char*>(40), ready, 3, defaults, label, name, count, 0, nullptr, nullptr, 1},
    {reinterpret_cast<const char*>(41), ready, 3, defaults, label, name, count, 0, nullptr, nullptr, 1,
     2, pad_bits, 1, pad_labels, nullptr}
};
const u32 sdk_drum_machine_count = 3;
}

Address opxy::platform::host_stock_address(Address address) {
    switch (address) {
    case 0x8d5ad660: return reinterpret_cast<Address>(&builder);
    case 0x8d5ad194: return reinterpret_cast<Address>(&publish);
    case 0x8d5ad604: return reinterpret_cast<Address>(&gain);
    case 0x8d58926c: return reinterpret_cast<Address>(&region);
    case 0x8d589210: return reinterpret_cast<Address>(&canonical);
    default:
        if (address >= kLive && address < kLive + sizeof(live)) return reinterpret_cast<Address>(live) + (address - kLive);
        std::abort();
    }
}

int main() {
    for (u32 i = 0; i < 8 * 58; ++i) live[i] = kDrumLockNeutral;
    // Key 53: marked (type 7, choke group mute1), pan 70, level -6 dB, FILTER
    // -3, SRR 9, FX I code 5, FX II 6 and stock's low half.
    const u8 stored[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(drum_pack(2, stored, records[0] + 1));
    bytes(53)[24] = drum_mark(0x20);
    bytes(53)[0x1e] = 70;
    bytes(53)[0x1d] = static_cast<unsigned char>(-6);
    records[0][5] = static_cast<i32>(((32u - 3u) << 16) | (9u << 21) | (5u << 25) | (6u << 28) | 0x1234u);
    // Key 54: unmarked, a stock Drumline pad (type 3, group 4) with stock's own
    // values in its words and its pad word.
    for (u32 i = 1; i < 5; ++i) records[1][i] = 0x0abc0000 * static_cast<i32>(i);
    bytes(54)[24] = 0x43;
    bytes(54)[0x1e] = static_cast<unsigned char>(-20);
    records[1][5] = 0x7a5a1234;

    // Neutral words, or none locked: the payload is stock's.
    note(41, 53);
    i32 base[8];
    std::memcpy(base, published, sizeof(base));
    CHECK(publications == 1 && built == 1 && published_track == kTrack && drum_marked_q31(base[0]));
    CHECK(base[5] == drum_pan_word(70) && base[7] == gain(-6));
    // A stray 0 (an old preset's fx.params) offsets nothing.
    for (u32 i = 0; i < kDrumLocks; ++i) locks()[i] = 0;
    note(41, 53);
    CHECK(!std::memcmp(published, base, sizeof(base)));

    // A step's locks: each offset into its word, clamped to the field.
    lock(50, -10, 31, -15);
    note(41, 53);
    CHECK(published[5] == drum_pan_word(100));                         // 70 + 50, at the right end
    CHECK(published[7] == gain(-16));                                  // -6 - 10
    CHECK(drum_field_get(published[6], 16, 5, true) == 15);            // -3 + 31, at the top
    CHECK(drum_field_get(published[6], 21, 4, false) == 0);            // 9 - 15, at the bottom
    CHECK((published[6] & ~static_cast<i32>(0x1ffu << 16)) == (base[6] & ~static_cast<i32>(0x1ffu << 16)));  // sends, low half
    CHECK(!std::memcmp(published, base, 5 * sizeof(i32)));            // the model and its controls
    lock(-50, 10, -5, 3);
    note(41, 53);
    CHECK(published[5] == drum_pan_word(20) && published[7] == gain(0));   // a cut never becomes a boost
    CHECK(drum_field_get(published[6], 16, 5, true) == -8 && drum_field_get(published[6], 21, 4, false) == 12);
    lock(-100, -30, 0, 0);
    note(41, 53);
    CHECK(published[5] == drum_pan_word(-30) && published[7] == gain(-30) && published[6] == base[6]);
    // A stored boost plays at 0 dB: an offset cuts from there.
    bytes(53)[0x1d] = 12;
    lock(0, -3, 0, 0);
    note(41, 53);
    CHECK(published[7] == gain(-3) && published[5] == base[5]);
    bytes(53)[0x1d] = static_cast<unsigned char>(-6);

    // Two keys on one step: each note gets the step's offsets on its own pad.
    lock(10, 0, 0, 0);
    note(41, 53);
    CHECK(published[5] == drum_pan_word(80));
    note(41, 54);
    CHECK(published[5] == drum_pan_word(-10));

    // An unmarked pad: pan and level need nothing else; its words stay stock's.
    lock(10, -2, 0, 0);
    note(41, 54);
    CHECK(!drum_marked_q31(published[0]) && published[1] == records[1][1] && published[6] == 0x7a5a1234);
    CHECK(published[7] == gain(-2));
    // A pad-control offset gives the payload a marked pad's form: the key's
    // defaults packed, stock's choke group kept, the pad fields and sends
    // cleared before the offset. It plays as before, plus the offset.
    lock(0, 0, -4, 2);
    note(41, 54);
    CHECK(drum_marked_q31(published[0]) && published[0] == drum_marked_word(0x43));
    u8 model = 99, controls[8];
    CHECK(drum_unpack(published + 1, &model, controls) && model == 1);
    for (u32 i = 0; i < 8; ++i) CHECK(controls[i] == 30 + i);
    CHECK(drum_field_get(published[6], 16, 5, true) == -4 && drum_field_get(published[6], 21, 4, false) == 2);
    CHECK((static_cast<u32>(published[6]) >> 25 & 63) == 0 && (published[6] & 0xffff) == 0x1234);
    CHECK(records[1][1] == 0x0abc0000 && bytes(54)[24] == 0x43);       // the record itself is untouched

    // A padMix kit without pad controls: their words offset nothing.
    lock(5, 0, 31, 15);
    note(40, 53);
    CHECK(published[5] == drum_pan_word(75) && published[6] == base[6]);
    // A kit without padMix: stock's payload.
    note(34, 53);
    CHECK(!std::memcmp(published, base, sizeof(base)));
    // Another track's locks are not this note's.
    for (u32 i = 0; i < 8 * 58; ++i) live[i] = kDrumLockNeutral;
    lock(40, 0, 0, 0);
    for (u32 i = 0; i < kDrumLocks; ++i) { locks(3)[i] = locks()[i]; locks()[i] = kDrumLockNeutral; }
    note(41, 53);
    CHECK(!std::memcmp(published, base, sizeof(base)));
    // Every other caller of the builder (the per-key editor, its previews)
    // publishes the key's own values.
    lock(40, -5, 3, 3);
    std::memcpy(payload, base, sizeof(payload));
    sdk_drum_lock_publish(kProject, kTrack, 0, payload);
    CHECK(!std::memcmp(published, base, sizeof(base)));
    // And a note's offsets end with its own publication.
    note(41, 53);
    CHECK(published[5] == drum_pan_word(100));
    std::memcpy(payload, base, sizeof(payload));
    sdk_drum_lock_publish(kProject, kTrack, 0, payload);
    CHECK(!std::memcmp(published, base, sizeof(base)));
    std::puts("note start locks ok");
    return 0;
}
