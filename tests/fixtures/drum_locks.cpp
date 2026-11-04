// Per-hit locks at note start (drum_locks.cpp): the two hooks inside a host
// model of stock 1.2.1's note start. Note start calls the pad payload builder
// 8d5ad660 (hooked at 8d595b50), which fills the eight payload words from the
// played pad's record as stock does for Drumline (8d5ad6ec..8d5ad74a) and
// publishes them through 8d5ad194 (hooked at 8d5ad776), after the track's
// changed live words (8d5acb9c). The GUI's live words 36..38 hold the step's
// locks (drum_lock_store.hpp), as the sequencer's lock pass leaves them.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "stock.hpp"
#include "drum_locks.hpp"
#include "drum_lock_store.hpp"

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
u32 publications, published_track, built, flushes, flushed_at;
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
void flush(u32 project, u32 track) {
    CHECK(project == kProject && track == kTrack);
    ++flushes;
    flushed_at = publications;
}
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
i32* words(u32 track = kTrack) { return live + track * 58; }
// A step's locks as the interim backend keeps them: LEVEL, FX I and FX II in
// 36, the pad controls in 37 (each 0 for none), stock's pan term in 38.
void lock(i32 level, i32 filter, i32 srr, i32 fx1 = 0, i32 fx2 = 0, i32 pan_term = kDrumPanTermNeutral) {
    u32 mix = drum_hit_put(0, kHitLevel, level);
    mix = drum_hit_put(mix, kHitSend1, fx1);
    mix = drum_hit_put(mix, kHitSend2, fx2);
    words()[kDrumHitMixWord] = static_cast<i32>(mix << 16);
    words()[kDrumHitPadWord] = static_cast<i32>(drum_hit_put(drum_hit_put(0, kHitPad1, filter), kHitPad2, srr) << 16);
    words()[kDrumHitPanWord] = pan_term;
}
void unlock(u32 track = kTrack) {
    words(track)[kDrumHitMixWord] = words(track)[kDrumHitPadWord] = 0;
    words(track)[kDrumHitPanWord] = kDrumPanTermNeutral;
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
    case 0x8d5acb9c: return reinterpret_cast<Address>(&flush);
    case 0x8d5ad604: return reinterpret_cast<Address>(&gain);
    case 0x8d58926c: return reinterpret_cast<Address>(&region);
    case 0x8d589210: return reinterpret_cast<Address>(&canonical);
    default:
        if (address >= kLive && address < kLive + sizeof(live)) return reinterpret_cast<Address>(live) + (address - kLive);
        std::abort();
    }
}

int main() {
    for (u32 t = 0; t < 8; ++t) unlock(t);
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

    // No lock: the payload is stock's. The track's changed live words go out
    // first (8d5acb9c), ahead of the note's payload.
    note(41, 53);
    i32 base[8];
    std::memcpy(base, published, sizeof(base));
    CHECK(publications == 1 && built == 1 && published_track == kTrack && drum_marked_q31(base[0]));
    CHECK(base[5] == drum_pan_word(70) && base[7] == gain(-6));
    CHECK(flushes == 1 && flushed_at == 0);
    // PAN's lock is stock's own pan term, applied by stock at the voice's
    // start (8d51a688): nothing goes into the payload for it.
    lock(0, 0, 0, 0, 0, 0x60000000);
    note(41, 53);
    CHECK(!std::memcmp(published, base, sizeof(base)) && flushes == 2 && flushed_at == 1);

    // A step's locks: each into its word, clamped to the field.
    lock(-10, 31, -15);
    note(41, 53);
    CHECK(published[5] == base[5]);                                    // the pad's own pan
    CHECK(published[7] == gain(-16));                                  // -6 - 10
    CHECK(drum_field_get(published[6], 16, 5, true) == 15);            // -3 + 31, at the top
    CHECK(drum_field_get(published[6], 21, 4, false) == 0);            // 9 - 15, at the bottom
    CHECK((published[6] & ~static_cast<i32>(0x1ffu << 16)) == (base[6] & ~static_cast<i32>(0x1ffu << 16)));  // sends, low half
    CHECK(!std::memcmp(published, base, 5 * sizeof(i32)));            // the model and its controls
    lock(10, -5, 3);
    note(41, 53);
    CHECK(published[7] == gain(0));                                    // a cut never becomes a boost
    CHECK(drum_field_get(published[6], 16, 5, true) == -8 && drum_field_get(published[6], 21, 4, false) == 12);
    lock(-30, 0, 0);
    note(41, 53);
    CHECK(published[7] == gain(-30) && published[6] == base[6]);
    // A stored boost plays at 0 dB: an offset cuts from there.
    bytes(53)[0x1d] = 12;
    lock(-3, 0, 0);
    note(41, 53);
    CHECK(published[7] == gain(-3) && published[5] == base[5]);
    bytes(53)[0x1d] = static_cast<unsigned char>(-6);

    // Per-hit sends: an override of the key's code, TRK (1) or an amount (2..8);
    // the other send and the pad controls stay the key's.
    lock(0, 0, 0, 1, 0);
    note(41, 53);
    CHECK(drum_send_codes(published) == (0u | 6u << 3) && (published[6] & 0x1ffffff) == (base[6] & 0x1ffffff));
    lock(0, 0, 0, 8, 2);
    note(41, 53);
    CHECK(drum_send_code(drum_send_codes(published), 0) == 7 && drum_send_code(drum_send_codes(published), 1) == 1);
    CHECK(published[7] == base[7]);

    // Two keys on one step: each note gets the step's locks on its own pad.
    lock(-2, 0, 0);
    note(41, 53);
    CHECK(published[7] == gain(-8));
    note(41, 54);
    CHECK(published[7] == gain(-2));

    // An unmarked pad: level needs nothing else; its words stay stock's.
    note(41, 54);
    CHECK(!drum_marked_q31(published[0]) && published[1] == records[1][1] && published[6] == 0x7a5a1234);
    // A pad-control or send lock gives the payload a marked pad's form: the
    // key's defaults packed, stock's choke group kept, the pad fields and sends
    // cleared before the lock. It plays as before, plus the lock.
    lock(0, -4, 2, 0, 3);
    note(41, 54);
    CHECK(drum_marked_q31(published[0]) && published[0] == drum_marked_word(0x43));
    u8 model = 99, controls[8];
    CHECK(drum_unpack(published + 1, &model, controls) && model == 1);
    for (u32 i = 0; i < 8; ++i) CHECK(controls[i] == 30 + i);
    CHECK(drum_field_get(published[6], 16, 5, true) == -4 && drum_field_get(published[6], 21, 4, false) == 2);
    CHECK(drum_send_codes(published) == (0u | 2u << 3) && (published[6] & 0xffff) == 0x1234);
    CHECK(records[1][1] == 0x0abc0000 && bytes(54)[24] == 0x43);       // the record itself is untouched

    // A packed word with low bits set is slide's blend of two steps' locks,
    // not a lock: it decodes as none rather than as nonsense.
    lock(-10, 5, 5);
    words()[kDrumHitMixWord] |= 0x1234;
    note(41, 53);
    CHECK(published[7] == base[7] && drum_field_get(published[6], 16, 5, true) == 2);
    // A padMix kit without pad controls: their locks offset nothing.
    lock(0, 31, 15);
    note(40, 53);
    CHECK(!std::memcmp(published, base, sizeof(base)));
    // A kit without padMix: stock's payload, and no flush of ours.
    const u32 flushed = flushes;
    lock(-10, 3, 3, 5, 5);
    note(34, 53);
    CHECK(!std::memcmp(published, base, sizeof(base)) && flushes == flushed);
    // Another track's locks are not this note's.
    for (u32 i = 36; i < 39; ++i) { words(3)[i] = words()[i]; }
    unlock();
    note(41, 53);
    CHECK(!std::memcmp(published, base, sizeof(base)));
    // Every other caller of the builder (the per-key editor, its previews)
    // publishes the key's own values.
    lock(-5, 3, 3, 4);
    std::memcpy(payload, base, sizeof(payload));
    sdk_drum_lock_publish(kProject, kTrack, 0, payload);
    CHECK(!std::memcmp(published, base, sizeof(base)));
    // And a note's locks end with its own publication.
    note(41, 53);
    CHECK(published[7] == gain(-11));
    std::memcpy(payload, base, sizeof(payload));
    sdk_drum_lock_publish(kProject, kTrack, 0, payload);
    CHECK(!std::memcmp(published, base, sizeof(base)));
    std::puts("note start locks ok");
    return 0;
}
