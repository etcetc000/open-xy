#include <opxy/arranger_storage.hpp>

extern "C" int puts(const char*);
extern "C" void exit(int);
using namespace opxy;
using namespace opxy::arranger;
using namespace opxy::arranger::storage;

void require(bool condition, const char* message) {
    if (!condition) { puts(message); exit(1); }
}

u8 bytes[max_bytes + 1], altered[max_bytes + 1], before[max_bytes], after[max_bytes];
const Binding binding = {0x00010201, 1, 123456, 0x89abcdef};
// Fixed pre-migration schema1 bytes, checksum independently produced with zlib CRC32.
const u8 legacy[] = {
    0x4f, 0x58, 0x59, 0x4c, 0x0d, 0x0a, 0x1a, 0x0a, 0x01, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00,
    0x01, 0x02, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x40, 0xe2, 0x01, 0x00, 0xef, 0xcd, 0xab, 0x89,
    0x03, 0x00, 0x00, 0x00, 0xd2, 0x04, 0x00, 0x00, 0x65, 0x00, 0x00, 0x00, 0xe7, 0x03, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6c, 0xce, 0x0e, 0x57,
    0x03, 0x00, 0x00, 0x02, 0x64, 0x00, 0x00, 0x00, 0xd2, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x60, 0x00, 0x00, 0x00, 0xff, 0x00, 0x01, 0x0f, 0x07, 0x00, 0x00, 0x00, 0x84, 0x03, 0x00, 0x00,
    0x7b, 0x00, 0x00, 0x00, 0xe0, 0x01, 0x00, 0x00
};

// Fixed schema2 fixture retains fractional phases and defaults mute to false.
const u8 legacy2[] = {
    0x4f, 0x58, 0x59, 0x4c, 0x0d, 0x0a, 0x1a, 0x0a, 0x02, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00,
    0x01, 0x02, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x40, 0xe2, 0x01, 0x00, 0xef, 0xcd, 0xab, 0x89,
    0x03, 0x00, 0x00, 0x00, 0xd2, 0x04, 0x00, 0x00, 0x65, 0x00, 0x00, 0x00, 0xe7, 0x03, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd7, 0x3c, 0x53, 0x44,
    0x03, 0x00, 0x00, 0x02, 0x64, 0x00, 0x00, 0x00, 0xd2, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x60, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xff, 0x00, 0x01, 0x0f, 0x07, 0x00, 0x00, 0x00,
    0x84, 0x03, 0x00, 0x00, 0x7b, 0x00, 0x00, 0x00, 0xe0, 0x01, 0x00, 0x00, 0x8f, 0x06, 0x00, 0x00,
};

void copy(u8* to, const u8* from, u32 length) {
    for (u32 i = 0; i < length; ++i) to[i] = from[i];
}

bool same(const u8* a, const u8* b, u32 length) {
    for (u32 i = 0; i < length; ++i) if (a[i] != b[i]) return false;
    return true;
}

void refuses(const u8* input, u32 length, const Binding& expected = binding) {
    State destination;
    destination.enabled = true;
    destination.track = 6;
    destination.model.seek(987654);
    const Clip sentinel = {7, 15, 123, 456, 7, 8};
    require(destination.model.insertAt(200, sentinel), "sentinel");
    const u32 old_size = encode(destination, binding, before, max_bytes);
    require(!decode(input, length, expected, destination), "invalid input accepted");
    const u32 new_size = encode(destination, binding, after, max_bytes);
    require(old_size == new_size && same(before, after, old_size), "failed decode changed destination");
}

void changedWord(u32 length, u32 offset, u32 value) {
    copy(altered, bytes, length);
    write32(altered + offset, value);
    write32(altered + 60, checksum(altered, length));
    refuses(altered, length);
}

int main() {
    const u8 known[] = {'1','2','3','4','5','6','7','8','9'};
    require(crc32(0, known, 9) == 0xcbf43926, "standard CRC32 vector");
    require(crc32(crc32(0, known, 4), known + 4, 5) == 0xcbf43926, "incremental CRC32");
    State source;
    source.enabled = true;
    source.track = 3;
    source.model.seek(1234);
    require(source.model.setLoop(true, 101, 999), "source loop");
    const Clip a = {0, 2, 100, 210, 24, 96, 1}, b = {1, 15, 7, 900, 123, 480, 1679, true};
    require(source.model.insertAt(3, a) && source.model.insertAt(255, b), "sparse slots");
    require(!source.model.insertAt(3, b) && !source.model.insertAt(256, b), "slot insertion guard");
    const u32 length = encode(source, binding, bytes, max_bytes);
    require(length == header_bytes + 2 * clip_bytes, "encoded length");
    require(bytes[16] == 1 && bytes[17] == 2 && bytes[18] == 1 && bytes[19] == 0, "little endian profile");
    require(bytes[28] == 0xef && bytes[29] == 0xcd && bytes[30] == 0xab && bytes[31] == 0x89, "little endian CRC binding");
    require(read32(bytes + 8) == 3 && clip_bytes == 24, "new writes use per-instance mute schema");
    require(bytes[64] == 3 && bytes[64 + clip_bytes] == 255, "stable slot numbers");
    State loaded;
    require(decode(bytes, length, binding, loaded), "round trip");
    require(loaded.enabled && loaded.track == 3 && loaded.model.position() == 1234, "state restored");
    require(loaded.model.loopEnabled() && loaded.model.loopStart() == 101 && loaded.model.loopEnd() == 999, "loop restored");
    require(!loaded.model.clip(0) && loaded.model.clip(3) && loaded.model.clip(255), "holes preserved");
    require(loaded.model.clip(255)->pattern == 15 && loaded.model.clip(255)->offset == 123, "clip fields restored");
    require(loaded.model.clip(3)->phase_remainder == 1 && loaded.model.clip(255)->phase_remainder == 1679,
        "fractional phase boundary values survive round trip");
    require(!loaded.model.clip(3)->muted && loaded.model.clip(255)->muted,
        "per-instance mute independent of other source instances");
    require(encode(loaded, binding, altered, max_bytes) == length && same(bytes, altered, length), "canonical round trip");
    altered[0] = 77;
    require(!encode(source, binding, altered, length - 1) && altered[0] == 77, "short output atomic");
    source.track = 8;
    require(!encode(source, binding, altered, max_bytes) && altered[0] == 77, "invalid metadata output atomic");
    source.track = 3;

    for (u32 i = 0; i < length; ++i) {
        copy(altered, bytes, length); altered[i] ^= 1;
        refuses(altered, length);
    }
    for (u32 n = 0; n < length; ++n) refuses(bytes, n);
    copy(altered, bytes, length); altered[length] = 0;
    refuses(altered, length + 1);
    refuses(nullptr, length);
    refuses(bytes, max_bytes + 1);
    Binding wrong = binding;
    ++wrong.profile; refuses(bytes, length, wrong);
    wrong = binding; ++wrong.time_units; refuses(bytes, length, wrong);
    wrong = binding; ++wrong.project_bytes; refuses(bytes, length, wrong);
    wrong = binding; ++wrong.project_crc32; refuses(bytes, length, wrong);
    wrong = binding; wrong.profile = 0; refuses(bytes, length, wrong);

    changedWord(length, 8, 4);                 // unsupported schema, even with valid checksum
    changedWord(length, 8, 1);                 // schema1 requires the original 20-byte record length
    changedWord(length, 12, length + 20);
    changedWord(length, 32, 4);                // unknown flags
    changedWord(length, 40, 999);              // empty loop
    changedWord(length, 48, 8);                // invalid selected track
    changedWord(length, 52, 0xffffffffu);      // bounded count before size arithmetic
    changedWord(length, 56, 1);                // reserved schema bits
    changedWord(length, 64 + 8, 0);            // zero clip length
    changedWord(length, 64 + 12, 96);          // source offset outside pattern
    changedWord(length, 64 + 16, 0);           // zero source length
    changedWord(length, 64 + 20, 1680);        // fractional phase outside its denominator
    changedWord(length, 64 + 20, 0xffffffffu);
    changedWord(length, 64 + 4, 0xffffffffu);  // overflowing endpoint
    copy(altered, bytes, length);
    altered[64 + clip_bytes] = 3; altered[65 + clip_bytes] = 0;
    write32(altered + 60, checksum(altered, length)); refuses(altered, length); // duplicate slots
    copy(altered, bytes, length);
    altered[64 + clip_bytes] = 0; altered[65 + clip_bytes] = 1;
    write32(altered + 60, checksum(altered, length)); refuses(altered, length); // slot 256
    copy(altered, bytes, length);
    altered[66 + clip_bytes] = 0; write32(altered + 68 + clip_bytes, 100);
    write32(altered + 60, checksum(altered, length)); refuses(altered, length); // same-track overlap
    copy(altered, bytes, length); altered[67] = 16;
    write32(altered + 60, checksum(altered, length)); refuses(altered, length); // pattern bound

    require(decode(legacy2, sizeof legacy2, binding, loaded), "previous firmware schema2 still loads");
    require(!loaded.model.clip(3)->muted && !loaded.model.clip(255)->muted &&
        loaded.model.clip(3)->phase_remainder==1 && loaded.model.clip(255)->phase_remainder==1679,
        "schema2 retains exact phases and defaults audible");
    copy(altered, legacy2, sizeof legacy2);
    write32(altered+64+20, 0x80000001u); write32(altered+60,checksum(altered,sizeof legacy2));
    refuses(altered,sizeof legacy2); // New mute bit cannot bypass schema2 phase validation.
    require(decode(legacy, sizeof legacy, binding, loaded), "previous firmware schema1 still loads");
    require(!loaded.model.clip(3)->muted && !loaded.model.clip(255)->muted &&
        loaded.enabled && loaded.track == 3 && loaded.model.position() == 1234 &&
        loaded.model.loopEnabled() && loaded.model.loopStart() == 101 && loaded.model.loopEnd() == 999,
        "legacy mode, cursor and loop unchanged");
    require(loaded.model.clip(3) && loaded.model.clip(255) && loaded.model.clip(3)->offset == 24 &&
        loaded.model.clip(255)->source_length == 480 && !loaded.model.clip(3)->phase_remainder &&
        !loaded.model.clip(255)->phase_remainder, "legacy sparse clips retain whole phase with zero remainder");
    require(encode(loaded, binding, altered, max_bytes) == length && read32(altered + 8) == 3,
        "legacy model upgrades on next save");
    State migrated;
    require(decode(altered, length, binding, migrated) && !migrated.model.clip(255)->phase_remainder,
        "upgraded legacy model reloads without phase drift");
    copy(altered, legacy, sizeof legacy); altered[80] ^= 1;
    refuses(altered, sizeof legacy);
    Binding legacy_wrong = binding; ++legacy_wrong.project_crc32;
    refuses(legacy, sizeof legacy, legacy_wrong);

    State full;
    for (u32 slot = 0; slot < Model::capacity; ++slot) {
        const Clip clip = {static_cast<u8>(slot % 8), 0, slot, 1, 0, 1};
        require(full.model.insertAt(slot, clip), "full model");
    }
    require(encode(full, binding, bytes, max_bytes) == max_bytes, "maximum serialized size");
    require(decode(bytes, max_bytes, binding, loaded), "maximum decode");
    require(loaded.model.clip(255) && !loaded.enabled && !loaded.model.loopEnabled(), "maximum/default flags");
    State empty;
    require(encode(empty, binding, bytes, max_bytes) == header_bytes, "empty encoded");
    require(decode(bytes, header_bytes, binding, loaded) && !loaded.model.clip(255), "empty replaces full model");
    return 0;
}
