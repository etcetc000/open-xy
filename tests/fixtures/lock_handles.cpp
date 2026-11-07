// Step-lock handles (lock_handles.cpp, lock_pool.hpp) inside a host model of stock
// 1.2.1's lock data: two project buffers of stock's pattern layout, the step and
// pattern clipboards, the recorder's snapshot and the GUI's live words, with stock's
// lock writer (8d586f40), union (8d5843ec), current pattern (8d586014), held step and
// lock pass (8d5936c0) as traced. Stock's step operations are replayed as the traced
// code moves a step's row: its 42 values (0x54 bytes) and its 64-bit mask together.
// The real project companion (project_companion.cpp) writes and reads open-xy.locks
// through stock's save and deserialize, modelled as a disk image of the project.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <project_companion.cpp>
#include "project_master.hpp"
#include "lock_handles.hpp"

extern "C" opxy::u32 sdk_project_kept(void*) { return 0; }
extern "C" void sdk_project_loaded(const char*) {}
extern "C" opxy::u32 sdk_project_locks_size(void* p, opxy::u32 b) { return sdk_step_locks_size(p, b); }
extern "C" void sdk_project_locks_write(void* f) { sdk_step_locks_write(f); }
extern "C" void sdk_project_locks_begin() { sdk_step_locks_begin(); }
extern "C" void sdk_project_locks_byte(opxy::u32 b) { sdk_step_locks_byte(b); }
extern "C" void sdk_project_locks_loaded(void* p, opxy::u32 v) { sdk_step_locks_loaded(p, v); }

using opxy::platform::Address;
using opxy::u32;
using opxy::i32;
using opxy::u16;
using opxy::u8;
namespace sl = opxy::platform::step_locks;

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (0)

namespace {
const char* number(unsigned n) { return reinterpret_cast<const char*>(static_cast<Address>(n)); }
}
const opxy::platform::CompanionMachine sdk_companion_machines[] = {
    {"probe", number(41), number(90), false},
    {nullptr, nullptr, nullptr, false}
};

namespace {
const u32 kProjectBytes = 0x54076c;
const u32 kTrack = 0x53f44, kPattern = 0x53f4;
const u32 kValues = 0x258, kStep = 0x54, kMasks = 0x2c58, kUnion = 0x3058, kSlide = 0x3060, kBase = 0x3864;
const u32 kWord = 37, kBit = 1u << (kWord - 32);
// Stock's fixed addresses this model holds.
const u32 kClipBase = 0x84ffd100, kClipEnd = 0x84ffd208 + 0x53f4;
const u32 kStepValues = 0x84ffd1ac, kStepMask = 0x84ffd200, kPatternClip = 0x84ffd208;
const u32 kSnapshot = 0x860c6adc + 0x461c, kSnapBytes = 0x3188 + 128 * 8;
const u32 kLive = 0x86bd027c + 0x4080;

unsigned char* buffers[2];
Address active;            // 0x840a0524
std::vector<unsigned char> disk;  // the saved project's bytes, as stock's save wrote them
unsigned char clip[kClipEnd - kClipBase];
unsigned char snapshot[kSnapBytes];
i32 live[16 * 58];
u32 held_step = ~0u;       // the step held for locks (0..127), or none
u32 current[16];           // each track's current pattern
bool gate[16];             // whether the lock pass applies the step's locks
u32 writes, redraws, unions;
u32 redraw_object[0x80 / 4];

unsigned char* project(Address p) { return reinterpret_cast<unsigned char*>(p); }
Address pattern_at(Address p, u32 track, u32 index) { return p + 0xd7c + track * kTrack + index * kPattern; }
u32& count(Address p, u32 track) { return *reinterpret_cast<u32*>(p + 0xd7c + track * kTrack + 0x53f40); }
u32* mask(Address pattern, u32 step) { return reinterpret_cast<u32*>(pattern + kMasks + step * 8); }
u16* row(Address pattern, u32 step) { return reinterpret_cast<u16*>(pattern + kValues + step * kStep); }
Address current_pattern(u32 track) { return pattern_at(active, track, current[track]); }
u32 handle(Address pattern, u32 step) { return mask(pattern, step)[1] & kBit ? row(pattern, step)[kWord] : 0; }

// Stock as traced.
void union_of(Address pattern) {
    u32 lo = 0, hi = 0;
    for (u32 s = 0; s < 128; ++s) lo |= mask(pattern, s)[0], hi |= mask(pattern, s)[1];
    reinterpret_cast<u32*>(pattern + kUnion)[0] = lo;
    reinterpret_cast<u32*>(pattern + kUnion)[1] = hi;
    ++unions;
}
void lock_writer(u32 track, u32 step, u32 index, u32 value) {
    const Address p = current_pattern(track);
    row(p, step)[index] = static_cast<u16>(value);
    mask(p, step)[index / 32] |= 1u << (index % 32);
    reinterpret_cast<u32*>(p + kUnion)[index / 32] |= 1u << (index % 32);
    ++writes;
}
Address pattern_of(u32 track) { return current_pattern(track); }
u32 step_held() { return held_step != ~0u; }
u32 held_key() { return held_step % 16; }
u32 page(u32) { return held_step / 16; }
Address redraw_owner() { return reinterpret_cast<Address>(redraw_object); }
void redraw(Address object) { CHECK(object == redraw_owner()); ++redraws; }
// 8d593980 restores the base into live, then the pass writes the step's locks
// when the gate lets it; sliding, a word with locks anywhere gets a blend.
u32 lock_pass(u32 gui, u32 track, u32 position) {
    CHECK(gui == 0x86bd027c);
    const Address p = current_pattern(track);
    live[track * 58 + kWord] = reinterpret_cast<i32*>(p + kBase)[kWord];
    const u32 step = (position + 240) / 480;
    if (!gate[track]) return 7;
    if (*reinterpret_cast<u8*>(p + kSlide)) {
        if (reinterpret_cast<u32*>(p + kUnion)[1] & kBit) live[track * 58 + kWord] = 0x12345678;
    } else if (mask(p, step)[1] & kBit) {
        live[track * 58 + kWord] = static_cast<i32>(static_cast<u32>(row(p, step)[kWord]) << 16);
    }
    return 7;
}

// Stock's step operations, replayed as they move rows.
void copy_step(u32 track, u32 step) {      // 8d5824e0
    const Address p = current_pattern(track);
    std::memcpy(clip + (kStepValues - kClipBase), row(p, step), 84);
    std::memcpy(clip + (kStepMask - kClipBase), mask(p, step), 8);
}
void paste_step(u32 track, u32 step) {     // 8d582218: no union update
    const Address p = current_pattern(track);
    std::memcpy(row(p, step), clip + (kStepValues - kClipBase), 84);
    std::memcpy(mask(p, step), clip + (kStepMask - kClipBase), 8);
}
void nudge(u32 track, u32 length) {        // 8d5930e8: every row one step later, wrapping
    const Address p = current_pattern(track);
    u16 last_row[42];
    u32 last_mask[2];
    std::memcpy(last_row, row(p, length - 1), 84);
    std::memcpy(last_mask, mask(p, length - 1), 8);
    for (u32 s = length - 1; s > 0; --s) {
        std::memcpy(row(p, s), row(p, s - 1), 84);
        std::memcpy(mask(p, s), mask(p, s - 1), 8);
    }
    std::memcpy(row(p, 0), last_row, 84);
    std::memcpy(mask(p, 0), last_mask, 8);
}
void clear_step(u32 track, u32 step) {     // 8d593db0: the whole mask, then the union
    const Address p = current_pattern(track);
    mask(p, step)[0] = mask(p, step)[1] = 0;
    union_of(p);
}
void copy_pattern(u32 track) { std::memcpy(clip + (kPatternClip - kClipBase), project(current_pattern(track)), kPattern); }
void paste_pattern(u32 track) { std::memcpy(project(current_pattern(track)), clip + (kPatternClip - kClipBase), kPattern); }
void snapshot_step(u32 track, u32 step) {  // 8d5adaf4
    const Address p = current_pattern(track);
    std::memcpy(snapshot + 0x788 + step * kStep, row(p, step), 84);
    std::memcpy(snapshot + 0x3188 + step * 8, mask(p, step), 8);
}
void restore_step(u32 track, u32 step) {   // 8d5adde4
    const Address p = current_pattern(track);
    std::memcpy(row(p, step), snapshot + 0x788 + step * kStep, 84);
    std::memcpy(mask(p, step), snapshot + 0x3188 + step * 8, 8);
    union_of(p);
}

// Stock's file calls on host stdio; save writes the project's bytes, deserialize reads them.
void* open_file(const char* path, const char* mode) { return std::fopen(path, mode); }
u32 read_file(void* data, u32 size, u32 n, void* file) { return static_cast<u32>(std::fread(data, size, n, static_cast<std::FILE*>(file))); }
u32 write_file(const void* data, u32 size, u32 n, void* file) { return static_cast<u32>(std::fwrite(data, size, n, static_cast<std::FILE*>(file))); }
int close_file(void* file) { return std::fclose(static_cast<std::FILE*>(file)); }
u32 save_project(void* p, const char* path) {
    // The file stands for the project: a digest of its pattern words, and the image kept.
    disk.assign(project(reinterpret_cast<Address>(p)), project(reinterpret_cast<Address>(p)) + kProjectBytes);
    u32 digest = 2166136261u;
    for (u32 i = 0; i < kProjectBytes; i += 4) digest = (digest ^ *reinterpret_cast<u32*>(&disk[i])) * 16777619u;
    std::FILE* file = std::fopen(path, "wb");
    std::fprintf(file, "project %08x\n", digest);
    std::fclose(file);
    return 1;
}
u32 deserialize_project(void* p, const char*) {
    std::memcpy(p, disk.data(), kProjectBytes);
    return 1;
}
}

Address opxy::platform::host_stock_address(Address address) {
    switch (address) {
    case 0x840a0524: return reinterpret_cast<Address>(&active);
    case 0x8d586f40: return reinterpret_cast<Address>(&lock_writer);
    case 0x8d5843ec: return reinterpret_cast<Address>(&union_of);
    case 0x8d586014: return reinterpret_cast<Address>(&pattern_of);
    case 0x8d5b7e34: return reinterpret_cast<Address>(&step_held);
    case 0x8d5b7e4c: return reinterpret_cast<Address>(&held_key);
    case 0x8d585fb4: return reinterpret_cast<Address>(&page);
    case 0x8d54b438: return reinterpret_cast<Address>(&redraw_owner);
    case 0x8d54b3b6: return reinterpret_cast<Address>(&redraw);
    case 0x8d5936c0: return reinterpret_cast<Address>(&lock_pass);
    case 0x8fed0348: return reinterpret_cast<Address>(&open_file);
    case 0x8fed05bc: return reinterpret_cast<Address>(&read_file);
    case 0x8fed0780: return reinterpret_cast<Address>(&write_file);
    case 0x8fed00ae: return reinterpret_cast<Address>(&close_file);
    case 0x8d599074: return reinterpret_cast<Address>(&save_project);
    case 0x8d599b10: return reinterpret_cast<Address>(&deserialize_project);
    }
    if (address >= kClipBase && address < kClipEnd) return reinterpret_cast<Address>(clip) + (address - kClipBase);
    if (address >= kSnapshot && address < kSnapshot + kSnapBytes) return reinterpret_cast<Address>(snapshot) + (address - kSnapshot);
    if (address >= kLive && address < kLive + sizeof live) return reinterpret_cast<Address>(live) + (address - kLive);
    for (unsigned char* b : buffers)
        if (address >= reinterpret_cast<Address>(b) && address < reinterpret_cast<Address>(b) + kProjectBytes) return address;
    std::fprintf(stderr, "unmodelled stock address %lx\n", static_cast<unsigned long>(address));
    std::abort();
}

// The client glue's checks: tracks 0..7 here, and a pattern whose machine word is not
// 77 (a stock machine's, in these tests).
const u32 kStockMachine = 77;
bool opxy::platform::step_locks::handle_client(u32 track) { return track < 8; }
bool opxy::platform::step_locks::pattern_client(u32 engine) { return engine != kStockMachine; }

namespace {
const u32 kKick = 53, kSnare = 54;
const char* path;
std::string companion_text() {
    std::string name(path);
    name = name.substr(0, name.size() - 2) + "oxy";
    std::FILE* file = std::fopen(name.c_str(), "rb");
    if (!file) return "";
    std::string text;
    for (int c; (c = std::fgetc(file)) != EOF;) text += static_cast<char>(c);
    std::fclose(file);
    return text;
}
void remove_companion() {
    std::string name(path);
    std::remove((name.substr(0, name.size() - 2) + "oxy").c_str());
}
// The pool a companion's open-xy.locks holds, decoded; and the text with another pool.
const char kDigits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::vector<u8> pool_bytes(const std::string& text, size_t& at, size_t& end) {
    const std::string open = "\"pool\": \"";
    at = text.find(open);
    CHECK(at != std::string::npos);
    at += open.size();
    end = text.find('"', at);
    std::vector<u8> bytes;
    u32 bits = 0, n = 0;
    for (size_t i = at; i < end; ++i) {
        if (text[i] == '=') break;
        bits = bits << 6 | static_cast<u32>(std::strchr(kDigits, text[i]) - kDigits);
        if ((n += 6) >= 8) bytes.push_back(static_cast<u8>(bits >> (n -= 8)));
    }
    return bytes;
}
sl::Pool saved_pool;
sl::Pool& saved(const std::string& text) {
    size_t at, end;
    saved_pool.begin();
    for (u8 b : pool_bytes(text, at, end)) saved_pool.byte(b);
    CHECK(saved_pool.finish());
    return saved_pool;
}
std::string with_pool(const std::string& text, const std::vector<u8>& bytes) {
    size_t at, end;
    pool_bytes(text, at, end);
    std::string encoded;
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const u32 left = static_cast<u32>(bytes.size() - i);
        const u32 v = bytes[i] << 16 | (left > 1 ? bytes[i + 1] << 8 : 0) | (left > 2 ? bytes[i + 2] : 0);
        encoded += kDigits[v >> 18];
        encoded += kDigits[v >> 12 & 63];
        encoded += left > 1 ? kDigits[v >> 6 & 63] : '=';
        encoded += left > 2 ? kDigits[v & 63] : '=';
    }
    return text.substr(0, at) + encoded + text.substr(end);
}
void write_companion_text(const std::string& text) {
    std::string name(path);
    std::FILE* file = std::fopen((name.substr(0, name.size() - 2) + "oxy").c_str(), "wb");
    std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
}
void hold(u32 step) { held_step = step; }
bool set(u32 key, u32 field, i32 value) { return sl::held_set(0, key, field, true, value); }
i32 value(u32 key, u32 field) {
    i32 v = 9999;
    return sl::held_value(0, key, field, v) ? v : 9999;
}
// Switches buffers as stock does once a loaded project is valid and made active.
void make_active(u32 index) { active = reinterpret_cast<Address>(buffers[index]); }
// A sequencer step of track 0: the pass, then what its note start takes.
u32 play(u32 step) {
    sdk_lock_step(0x86bd027c, 0, step * 480);
    return sl::take_step(0);
}

void pool_tests() {
    static sl::Pool pool, other;
    pool.clear();
    other.clear();
    i32 v;
    CHECK(!pool.has(1) && pool.unused(nullptr) == 1);
    // Per key over step-wide, field by field.
    CHECK(pool.set(3, sl::kStepWide, 0, 25) && pool.set(3, sl::kStepWide, 4, -7) && pool.set(3, kKick, 0, -40));
    CHECK(pool.get(3, kKick, 0, v) && v == -40 && pool.get(3, kKick, 4, v) && v == -7);
    CHECK(pool.get(3, kSnare, 0, v) && v == 25 && !pool.get(3, kSnare, 1, v) && !pool.own(3, kSnare, 0, v));
    i32 values[sl::kFields];
    CHECK(pool.fields(3, kKick, values) == 0x11 && values[0] == -40 && values[4] == -7);
    CHECK(pool.handles() == 1 && pool.unused(nullptr) == 1);
    // In place when the field exists; rebuilt otherwise.
    const u32 words = pool.live_words();
    CHECK(pool.set(3, kKick, 0, 12) && pool.live_words() == words && pool.get(3, kKick, 0, v) && v == 12);
    CHECK(pool.set(3, kKick, 15, 1) && pool.live_words() == words + 1);
    // Copy into another pool, and within one.
    CHECK(other.copy(9, pool, 3) && other.get(9, kKick, 15, v) && v == 1 && other.get(9, kSnare, 4, v) && v == -7);
    CHECK(pool.copy(4, pool, 3) && pool.set(4, kKick, 0, 100) && pool.get(3, kKick, 0, v) && v == 12);
    // Erasing the last field of every entry frees the handle.
    pool.erase(4, kKick, 0); pool.erase(4, kKick, 15); pool.erase(4, sl::kStepWide, 0);
    CHECK(pool.has(4));
    pool.erase(4, sl::kStepWide, 4);
    CHECK(!pool.has(4) && pool.handles() == 1);
    // Bounds: no field 16, no key 128, no handle 0 or 2458 (the arena holds at most
    // 2,457 blocks), at most 25 entries.
    CHECK(sl::kHandles == 2458 && sl::kArenaWords / 5 == 2457);
    CHECK(!pool.set(3, kKick, 16, 1) && !pool.set(3, 128, 0, 1) && !pool.set(0, kKick, 0, 1) &&
          !pool.set(sl::kHandles, kKick, 0, 1) && pool.set(sl::kHandles - 1, kKick, 0, 1));
    pool.free(sl::kHandles - 1);
    for (u32 k = 0; k < 24; ++k) CHECK(pool.set(5, k, 0, static_cast<i32>(k)));
    CHECK(pool.set(5, sl::kStepWide, 0, 1) && !pool.set(5, 100, 0, 1) && pool.set(5, 3, 2, 3));
    // The arena: full blocks are refused and leave the rest; erasing makes room.
    pool.clear();
    u32 made = 0;
    while (pool.set(made + 1, kKick, 0, 1)) ++made;
    CHECK(made == sl::kArenaWords / 5 && pool.live_words() == made * 5);
    CHECK(!pool.set(made + 1, kKick, 0, 1) && pool.set(1, kKick, 0, 2));   // in place
    // A block grows into its own old room and the 3 words left; then nothing grows.
    CHECK(pool.set(1, kKick, 1, 1) && pool.set(2, kKick, 1, 1) && pool.set(3, kKick, 1, 1));
    CHECK(!pool.set(4, kKick, 1, 1) && pool.get(4, kKick, 0, v) && v == 1 && !pool.get(4, kKick, 1, v));
    pool.erase(5, kKick, 0);
    CHECK(!pool.has(5) && pool.set(4, kKick, 1, 1));   // the freed block's room, after compaction
    // Garbage collection keeps only marked handles.
    u8 marks[(sl::kHandles + 7) / 8] = {};
    marks[0] = 1 << 1;
    CHECK(pool.keep_only(marks) == made - 2 && pool.handles() == 1 && pool.live_words() == 2 + 2 + 2);

    // Saved form round trip: handle order, little-endian words.
    pool.clear();
    CHECK(pool.set(7, kKick, 1, -3) && pool.set(2, sl::kStepWide, 0, 300) && pool.set(7, sl::kStepWide, 5, 9));
    struct Bytes { std::vector<u8> b; void byte(u8 x) { b.push_back(x); } } out;
    CHECK(pool.write(out, sl::kSavedBytes) == out.b.size() && out.b.size() == pool.saved_bytes(sl::kSavedBytes));
    CHECK(out.b.size() == 2 * (5 + 2 + 3 + 3) && out.b[0] == 2 && out.b[1] == 0);
    other.begin();
    for (u8 b : out.b) other.byte(b);
    CHECK(other.finish() && other.handles() == 2 && other.get(7, kSnare, 5, v) && v == 9 && other.get(7, kKick, 1, v) && v == -3);
    // A limit stops before the first block that does not fit.
    Bytes cut;
    CHECK(pool.write(cut, 15) == 10 && cut.b.size() == 10);
    // Invalid saved forms leave the pool empty.
    const std::vector<std::vector<u8>> bad = {
        {1, 0},                                   // a block cut short
        {0, 0, 1, 0, 1, 0, 255, 255, 5, 0},       // handle 0
        {0, 16, 1, 0, 1, 0, 255, 255, 5, 0},      // handle 4096
        {0x9a, 0x09, 1, 0, 1, 0, 255, 255, 5, 0}, // handle 2458
        {2, 0, 1, 0, 1, 0, 255, 255, 5, 0, 1, 0, 1, 0, 1, 0, 255, 255, 5, 0},   // not ascending
        {1, 0, 0, 0},                             // no entries
        {1, 0, 1, 0, 0, 0, 255, 255},             // an empty mask
        {1, 0, 1, 0, 1, 0, 128, 0, 5, 0},         // key 128
        {1, 0, 2, 0, 1, 0, 5, 0, 5, 0, 1, 0, 5, 0, 5, 0},   // a key twice
        {1, 0, 1, 0, 1, 0, 255, 255, 5},          // an odd byte
    };
    for (const auto& form : bad) {
        other.begin();
        for (u8 b : form) other.byte(b);
        CHECK(!other.finish() && !other.handles() && !other.has(1));
    }
    other.begin();
    CHECK(other.finish() && !other.handles());    // an empty pool is valid
}
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    path = argv[1];
    pool_tests();
    for (unsigned char*& b : buffers) {
        b = static_cast<unsigned char*>(std::calloc(kProjectBytes, 1));
        CHECK(b);
    }
    make_active(0);
    for (u32 t = 0; t < 16; ++t) count(active, t) = 2, gate[t] = true;
    const Address p = current_pattern(0);

    // Nothing held: no lock and no handle; nothing is written.
    CHECK(!sl::held_set(0, kKick, 0, true, 5) && !sl::step_held() && play(3) == 0 && writes == 0);
    // Holding step 3 and turning: a handle through stock's writer, per key.
    hold(3);
    CHECK(set(kKick, 0, 20) && writes == 1 && redraws == 1);
    const u32 h = handle(p, 3);
    CHECK(h == 1 && (reinterpret_cast<u32*>(p + kUnion)[1] & kBit));
    CHECK(value(kKick, 0) == 20 && value(kSnare, 0) == 9999);
    // More fields on the same step keep its handle, writing nothing to stock.
    CHECK(set(kKick, 1, -6) && set(kSnare, 0, -15) && writes == 1 && handle(p, 3) == h);
    CHECK(value(kKick, 1) == -6 && value(kSnare, 0) == -15);
    // Another step gets its own handle.
    hold(5);
    CHECK(set(kKick, 0, 7) && handle(p, 5) == 2 && writes == 2);

    // Playback: the pass applied step 3's handle, which its note start takes once.
    i32 values[sl::kFields];
    CHECK(play(3) == h && sl::take_step(0) == 0);
    CHECK(sl::handle_fields(h, kKick, values) == 3 && values[0] == 20 && values[1] == -6);
    CHECK(sl::handle_fields(h, kSnare, values) == 1 && values[0] == -15);
    CHECK(play(4) == 0 && play(5) == 2);
    // The step's gate held its locks back: nothing; a note played live: nothing.
    gate[0] = false;
    CHECK(play(3) == 0);
    gate[0] = true;
    CHECK(sl::take_step(0) == 0);
    // A note start that returned before its payload (8d59576a, 8d59582e, 8d595a02)
    // took nothing: a GUI frame later the handle has expired, so a note played by
    // hand on the track does not take the step's locks.
    sdk_lock_step(0x86bd027c, 0, 3 * 480);
    sdk_step_locks_frame();
    CHECK(sl::take_step(0) == 0);
    CHECK(play(3) == h);   // the next step's note takes its own again
    // Sliding: the pass blends the handle word, so the step's own handle is taken.
    *reinterpret_cast<u8*>(p + kSlide) = 1;
    CHECK(play(3) == h && play(4) == 0);
    *reinterpret_cast<u8*>(p + kSlide) = 0;
    // A track that is not a client takes nothing.
    sdk_lock_step(0x86bd027c, 9, 3 * 480);
    CHECK(sl::take_step(9) == 0);

    // Copy and paste a step: both hold one handle.
    copy_step(0, 3);
    paste_step(0, 9);
    CHECK(handle(p, 9) == h && play(9) == h);
    // Editing the pasted step copies first: the original and the clipboard keep theirs.
    hold(9);
    CHECK(set(kKick, 0, 50));
    const u32 h9 = handle(p, 9);
    CHECK(h9 != h && h9 && value(kKick, 0) == 50 && value(kKick, 1) == -6 && value(kSnare, 0) == -15);
    hold(3);
    CHECK(value(kKick, 0) == 20);
    // Editing the original while the clipboard still holds its handle copies too.
    CHECK(set(kKick, 0, 21) && handle(p, 3) != h && value(kKick, 0) == 21);
    const u32 h3 = handle(p, 3);
    paste_step(0, 11);
    hold(11);
    CHECK(handle(p, 11) == h && value(kKick, 0) == 20);
    // An unshared handle is edited in place.
    hold(3);
    CHECK(set(kKick, 0, 22) && handle(p, 3) == h3);

    // Nudge: every row moves one step, its handle with it.
    nudge(0, 16);
    CHECK(handle(p, 4) == h3 && handle(p, 10) == h9 && handle(p, 12) == h && handle(p, 6) == 2 && !handle(p, 3));
    hold(4);
    CHECK(value(kKick, 0) == 22 && play(4) == h3 && play(3) == 0);
    // Duplicating a page: stock copies 16 rows into the next page; the copies share.
    for (u32 s = 0; s < 16; ++s) {
        std::memcpy(row(p, 16 + s), row(p, s), 84);
        std::memcpy(mask(p, 16 + s), mask(p, s), 8);
    }
    hold(20);
    CHECK(handle(p, 20) == h3 && set(kKick, 0, 30) && handle(p, 20) != h3);
    hold(4);
    CHECK(value(kKick, 0) == 22);

    // Unlocking the last field gives the step its handle word back.
    hold(6);
    CHECK(sl::held_set(0, kKick, 0, false, 0) && !handle(p, 6) && !(mask(p, 6)[1] & kBit));
    // Clearing a step: stock zeroes its mask; the handle is no longer the step's.
    clear_step(0, 10);
    CHECK(!handle(p, 10) && play(10) == 0);
    // The recorder's snapshot and restore bring it back, with its values.
    snapshot_step(0, 12);
    clear_step(0, 12);
    restore_step(0, 12);
    hold(12);
    CHECK(handle(p, 12) == h && value(kKick, 0) == 20);

    // Copy the pattern, change it, paste it back: the old handles return.
    copy_pattern(0);
    hold(4);
    CHECK(set(kKick, 0, 33) && handle(p, 4) != h3);   // the clipboard holds h3
    paste_pattern(0);
    CHECK(handle(p, 4) == h3 && value(kKick, 0) == 22);

    // Saving: garbage collection keeps what the project, the clipboards and the
    // snapshot hold, and the section holds their values.
    remove_companion();
    const u32 written = writes;
    CHECK(sdk_project_save(reinterpret_cast<void*>(active), path) == 1);
    std::string text = companion_text();
    CHECK(text.find("    \"open-xy.locks\": {\n      \"pool\": \"") != std::string::npos);
    CHECK(text.find("\",\n      \"version\": 1\n    },\n    \"open-xy.machines\"") != std::string::npos);
    CHECK(writes == written);
    // Step 4's copy (6, locked at 33) lost its only holder when the pattern was pasted
    // back over it: collected. Every handle a step, a clipboard or the snapshot holds
    // stays: h9 on the duplicated page's step 26 after its own step was cleared, 2 on
    // step 22 though step 6 unlocked its copy.
    {
        sl::Pool& kept = saved(text);
        i32 v;
        CHECK(handle(p, 26) == h9 && handle(p, 22) == 2 && handle(p, 4) == h3 && handle(p, 20) == 5);
        CHECK(!kept.has(6) && kept.has(2) && kept.has(h9) && kept.has(h) && kept.has(h3) && kept.has(5));
        CHECK(kept.handles() == 5);
        CHECK(kept.get(h, kSnare, 0, v) && v == -15 && kept.get(h3, kKick, 0, v) && v == 22);
    }

    // Loading it back into the other buffer: staged, nothing swept, then active.
    hold(~0u);
    CHECK(sdk_project_load(buffers[1], path) == 1);
    const Address q = pattern_at(reinterpret_cast<Address>(buffers[1]), 0, 0);
    CHECK(handle(q, 4) == h3 && handle(q, 12) == h && handle(q, 20));
    make_active(1);
    hold(4);
    CHECK(value(kKick, 0) == 22 && value(kKick, 1) == -6 && value(kSnare, 0) == -15);
    hold(12);
    CHECK(value(kKick, 0) == 20);
    // The clipboards were carried over under handles of the new pool, with their values.
    const Address pattern_clip = kPatternClip;
    const u16 carried = reinterpret_cast<u16*>(clip + (pattern_clip - kClipBase) + kValues + 4 * kStep)[kWord];
    CHECK(carried && carried != handle(q, 4));
    paste_pattern(0);
    hold(4);
    CHECK(handle(q, 4) == carried && value(kKick, 0) == 22);
    // Back in buffer 0's place: the old pool is gone; values come from the new one.
    make_active(0);
    make_active(1);

    // Only a whole version 1 pool from a matching companion says which handles have
    // values. Without one the loaded project's handles all stay, as saved: they play
    // nothing here, and firmware that reads their values still finds them.
    CHECK(sdk_project_save(reinterpret_cast<void*>(active), path) == 1);
    text = companion_text();
    const Address r = pattern_at(reinterpret_cast<Address>(buffers[0]), 0, 0);
    auto all_kept = [&]() {
        CHECK(handle(r, 4) == handle(q, 4) && handle(r, 12) == handle(q, 12) && handle(r, 20) == handle(q, 20));
        CHECK(reinterpret_cast<u32*>(r + kUnion)[1] & kBit);
    };
    // A missing companion (saved by a build without these locks, or by stock).
    remove_companion();
    CHECK(sdk_project_load(buffers[0], path) == 1);
    all_kept();
    // A newer version of the section, which this firmware can't read.
    {
        std::string newer = text;
        const size_t at = newer.find("\"version\": 1\n    },\n    \"open-xy.machines\"");
        CHECK(at != std::string::npos);
        newer.replace(at, std::strlen("\"version\": 1"), "\"version\": 2");
        write_companion_text(newer);
    }
    CHECK(sdk_project_load(buffers[0], path) == 1);
    all_kept();
    // A malformed pool, from a companion that matches.
    write_companion_text(with_pool(text, {1, 2, 3}));
    CHECK(sdk_project_load(buffers[0], path) == 1);
    all_kept();
    // A mismatched companion (the project saved again since, by other firmware).
    write_companion_text(text);
    {
        std::FILE* file = std::fopen(path, "ab");
        std::fputs("changed elsewhere\n", file);
        std::fclose(file);
    }
    CHECK(sdk_project_load(buffers[0], path) == 1);
    all_kept();
    // A load that never becomes active changes nothing for the active project.
    hold(4);
    CHECK(value(kKick, 0) == 22);
    // A valid one whose pool lacks a handle (a block left out): only that one is
    // swept, and each pattern's union recomputed once. A pattern of another machine
    // on an instrument track, and every aux track's, keep their word 37 as it was,
    // through saving's collection and loading's sweep.
    const Address other_machine = pattern_at(active, 1, 0), aux = pattern_at(active, 9, 0);
    for (Address o : {other_machine, aux}) {
        reinterpret_cast<u16*>(o + kValues + 7 * kStep)[kWord] = 999;
        reinterpret_cast<u32*>(o + kMasks + 7 * 8)[1] |= kBit;
    }
    reinterpret_cast<u32*>(other_machine + 0x18)[0] = kStockMachine;
    if (count(active, 1) < 1) count(active, 1) = 1;
    if (count(active, 9) < 1) count(active, 9) = 1;
    CHECK(sdk_project_save(reinterpret_cast<void*>(active), path) == 1);
    text = companion_text();
    {
        sl::Pool& kept = saved(text);
        CHECK(kept.has(handle(q, 4)) && kept.has(handle(q, 12)) && !kept.has(999));
        kept.free(handle(q, 12));
        struct Bytes { std::vector<u8> b; void byte(u8 x) { b.push_back(x); } } out;
        kept.write(out, sl::kSavedBytes);
        write_companion_text(with_pool(text, out.b));
    }
    {
        const u32 before = unions;
        CHECK(sdk_project_load(buffers[0], path) == 1);
        CHECK(handle(r, 4) == handle(q, 4) && !handle(r, 12) && handle(q, 12));
        CHECK(unions == before + 1);
        for (Address o : {pattern_at(reinterpret_cast<Address>(buffers[0]), 1, 0),
                          pattern_at(reinterpret_cast<Address>(buffers[0]), 9, 0)})
            CHECK(handle(o, 7) == 999);
    }

    // Neutral: a project with no locks writes no section and plays no handle.
    for (unsigned char* b : buffers) std::memset(b, 0, kProjectBytes);
    std::memset(clip, 0, sizeof clip);
    std::memset(snapshot, 0, sizeof snapshot);
    make_active(1);
    for (u32 t = 0; t < 16; ++t) count(active, t) = 16;
    CHECK(sdk_project_save(reinterpret_cast<void*>(active), path) == 1);
    CHECK(companion_text().find("open-xy.locks") == std::string::npos);
    CHECK(play(3) == 0);

    // The limit: the pool fills, an edit is refused and changes nothing, and saving
    // keeps the companion within 64 KiB however full the pool.
    u32 locked = 0;
    for (u32 t = 0; t < 8; ++t)
        for (u32 k = 0; k < 16; ++k) {
            current[t] = k;
            for (u32 s = 0; s < 128; ++s) {
                held_step = s;
                if (!sl::held_set(t, kKick, 0, true, 1)) goto full;
                ++locked;
            }
        }
full:
    CHECK(locked == sl::kArenaWords / 5);
    {
        const u32 before = writes;
        CHECK(!sl::held_set(7, kKick, 0, true, 1) && writes == before);
        // A field already held still changes in place.
        current[0] = 0;
        held_step = 0;
        CHECK(sl::held_set(0, kKick, 0, true, -1) && value(kKick, 0) == -1);
    }
    CHECK(sdk_project_save(reinterpret_cast<void*>(active), path) == 1);
    text = companion_text();
    CHECK(text.size() < 64 * 1024 && text.find("open-xy.locks") != std::string::npos);
    CHECK(sdk_project_load(buffers[0], path) == 1);
    make_active(0);
    u32 kept = 0;
    for (u32 t = 0; t < 8; ++t)
        for (u32 k = 0; k < 16; ++k)
            for (u32 s = 0; s < 128; ++s) kept += handle(pattern_at(active, t, k), s) != 0;
    CHECK(kept == locked);
    std::printf("lock handles ok: %u locked steps fill the pool; companion %u bytes\n", locked,
                static_cast<unsigned>(text.size()));
    return 0;
}
