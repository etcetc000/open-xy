// The device identity's service on the host (device_identity.cpp): the GUI
// processor's read and sends, and the audio processor's receiver, against
// stock-shaped boot information, clock, sender and setter. Stock addresses map
// to host memory through host_stock_address: the test writes a host stock.hpp
// beside a copy of the unit, device_identity_unit.cpp. One processor per run:
// "gui", "torn" (the boot information changes while it is read) or "audio".
// Built with STOCK_SETTER, sdk_master_receive is kit_absent, as in a firmware
// without the master processor, and other numbers go to stock's setter.
// Prints "ok".
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
void host_barrier() {}
#include "device_identity_unit.cpp"

namespace {
void require(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        std::exit(1);
    }
}

struct Call { u32 base, id; i32 value; };

// --- Stock doubles ---
unsigned char role;
unsigned char boot[0x30];  // 0x08010e40..0x08010e6f
const u32 kBootBase = 0x08010e40;
u32 marker_reads, torn_at;  // torn_at: the marker read that sees another value (0: none)
unsigned long long now_us;
std::vector<Call> sent, set, master;

void clock(u32* time) {
    time[0] = static_cast<u32>(now_us);
    time[1] = static_cast<u32>(now_us >> 32);
}
void stock_send(u32 id, i32 value) { sent.push_back(Call{0, id, value}); }
void stock_set(u32 base, u32 id, i32 value) { set.push_back(Call{base, id, value}); }
u32 torn_marker;

void put_marker(u32 value) { std::memcpy(boot + 0x28, &value, 4); }
}

int kit_absent() { return 0; }
#if STOCK_SETTER
__asm__(".globl sdk_master_receive\n.set sdk_master_receive, kit_absent");
const bool kMaster = false;
#else
void sdk_master_receive(u32 base, u32 id, i32 value) { master.push_back(Call{base, id, value}); }
const bool kMaster = true;
#endif

Address opxy::platform::host_stock_address(Address address) {
    if (address == 0x86c05ca4) return reinterpret_cast<Address>(&role);
    if (address == 0x8d5be3a0) return reinterpret_cast<Address>(&clock);
    if (address == 0x8d4dc84c) return reinterpret_cast<Address>(&stock_send);
    if (address == 0x8d50eb8c) return reinterpret_cast<Address>(&stock_set);
    if (address == 0x08010e68 && torn_at && ++marker_reads == torn_at) return reinterpret_cast<Address>(&torn_marker);
    if (address >= kBootBase && address < kBootBase + sizeof boot) return reinterpret_cast<Address>(boot + (address - kBootBase));
    std::fprintf(stderr, "Unmapped stock address %08x\n", static_cast<unsigned>(address));
    std::abort();
}

namespace {
const u8 kIdentity[18] = {0x00, 0x7f, 0x80, 0xff, 0x12, 0x34, 0x56, 0x78, 0x9a,
                          0xbc, 0xde, 0xf0, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab};

// FNV-1a's low 24 bits over the sequence's three bytes and the identity's 18.
u32 fnv24(u32 sequence, const u8* identity) {
    unsigned char bytes[21] = {static_cast<unsigned char>(sequence), static_cast<unsigned char>(sequence >> 8),
                               static_cast<unsigned char>(sequence >> 16)};
    std::memcpy(bytes + 3, identity, 18);
    u32 hash = 2166136261u;
    for (unsigned char b : bytes) hash = (hash ^ b) * 16777619u;
    return hash & 0xffffff;
}

// The message the ABI describes, built independently: begin, six words of three
// little-endian bytes, and the check; every value below 2^24.
std::vector<Call> message(u32 sequence, const u8* identity, u32 corrupt = 0) {
    std::vector<Call> calls{{0, 138, static_cast<i32>(sequence)}};
    for (u32 w = 0; w < 6; ++w)
        calls.push_back(Call{0, 139 + w, static_cast<i32>(identity[3 * w] | identity[3 * w + 1] << 8 | identity[3 * w + 2] << 16)});
    calls.push_back(Call{0, 145, static_cast<i32>(fnv24(sequence, identity) ^ corrupt)});
    return calls;
}

bool same(const std::vector<Call>& a, const std::vector<Call>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || a[i].value != b[i].value) return false;
    return true;
}

bool reads(const u8* expected) {
    u8 out[20];
    std::memset(out, 0xee, sizeof out);
    if (!opxy_device_identity(out, 20)) return false;
    return std::memcmp(out, expected, 18) == 0 && out[18] == 0xee && out[19] == 0xee;
}

void deliver(const std::vector<Call>& calls, u32 base = 0x84c696fc) {
    for (const Call& c : calls) sdk_identity_receive(base, c.id, c.value);
}

void place(const u8* identity, u32 marker) {
    std::memcpy(boot + 0x12, identity, 18);
    put_marker(marker);
}

void gui(bool torn) {
    role = 1;
    // Near the clock's 32-bit wrap: every interval spans it.
    now_us = 0xffff0000ull;
    u8 out[18];
    // Before the boot information is in place: nothing read, nothing sent.
    place(kIdentity, 0);
    sdk_identity_gui_service();
    require(!opxy_device_identity(out, 18) && sent.empty(), "no identity before the boot information");
    // The next look is 100 ms later.
    place(kIdentity, 0x84000 + 999);
    now_us += 99999;
    sdk_identity_gui_service();
    require(!opxy_device_identity(out, 18) && sent.empty(), "looks only every 100 ms");
    if (torn) {
        // The marker read after the bytes differs: the read does not count.
        torn_marker = 0x84000 + 5;
        torn_at = marker_reads + 2;
        now_us += 1;
        sdk_identity_gui_service();
        require(!opxy_device_identity(out, 18) && sent.empty(), "a torn read is not the identity");
        torn_at = 0;
        now_us += 100000;
        sdk_identity_gui_service();
        require(reads(kIdentity), "the next look reads it");
        return;
    }
    // An address outside the window: not in place.
    put_marker(0x84000 + 1000);
    now_us += 1;
    sdk_identity_gui_service();
    require(!opxy_device_identity(out, 18) && sent.empty(), "the marker's window ends at 1000 bytes");
    put_marker(0x85000);
    now_us += 100000;
    sdk_identity_gui_service();
    require(!opxy_device_identity(out, 18) && sent.empty(), "the marker's window starts at 0x84000");
    put_marker(0x84000);
    now_us += 100000;
    sdk_identity_gui_service();
    require(reads(kIdentity), "the GUI's copy once read");
    require(same(sent, message(1, kIdentity)), "the first message right away");
    // Partial and empty copies.
    u8 four[5] = {0, 0, 0, 0, 0x5a};
    require(opxy_device_identity(four, 4) && std::memcmp(four, kIdentity, 4) == 0 && four[4] == 0x5a, "the first four bytes");
    require(opxy_device_identity(nullptr, 0), "known, nothing copied");
    // The boot information changing later changes nothing.
    const u8 other[18] = {1};
    place(other, 0x84000);
    // Once a second, 32 messages in all, each with the next sequence number.
    for (u32 n = 2; n <= 40; ++n) {
        sent.clear();
        now_us += 999999;
        sdk_identity_gui_service();
        require(sent.empty(), "not before a second");
        now_us += 1;
        sdk_identity_gui_service();
        require(n <= 32 ? same(sent, message(n, kIdentity)) : sent.empty(), "once a second, 32 times");
    }
    require(reads(kIdentity), "the GUI's copy is kept");
    // The GUI processor takes no identity message.
    sent.clear();
    deliver(message(7, other));
    require(reads(kIdentity) && master.empty() && set.empty(), "the GUI ignores the identity's numbers");
}

void audio() {
    role = 0;
    // The audio processor never reads the boot information or sends.
    place(kIdentity, 0x84000);
    for (u32 i = 0; i < 3; ++i) {
        now_us += 1000000;
        sdk_identity_gui_service();
    }
    u8 out[18];
    require(!opxy_device_identity(out, 18) && sent.empty(), "the audio processor does not read or send");
    // Every other number goes on, unchanged, to the next receiver; none of these does.
    const u32 others[] = {0, 3, 127, 128, 137, 146, 255};
    for (u32 id : others) sdk_identity_receive(0x84c696fc, id, -5 - static_cast<i32>(id));
    // To the master processor's receiver, or without it to stock's setter.
    std::vector<Call>& passed = kMaster ? master : set;
    require(passed.size() == 7 && (kMaster ? set : master).empty(), "other numbers pass on");
    for (u32 i = 0; i < 7; ++i)
        require(passed[i].base == 0x84c696fc && passed[i].id == others[i] && passed[i].value == -5 - static_cast<i32>(others[i]),
                "unchanged");
    passed.clear();
    // Incomplete, unchecked or padded messages, and words without a begin, set nothing.
    std::vector<Call> missing = message(3, kIdentity);
    missing.erase(missing.begin() + 3);
    deliver(missing);
    deliver(message(4, kIdentity, 1));
    std::vector<Call> loose = message(5, kIdentity);
    loose.erase(loose.begin());
    deliver(loose);
    // A value of 2^24 or more ends the message, here a word whose low bytes still check.
    std::vector<Call> wide = message(6, kIdentity);
    wide[3].value |= 0x1000000;
    deliver(wide);
    std::vector<Call> negative = message(7, kIdentity);
    negative[6].value |= static_cast<i32>(0x80000000u);
    deliver(negative);
    const u8 zeros[18] = {};
    std::vector<Call> limit = message(10, zeros);
    limit[1].value = 0x1000000;  // exactly 2^24, its low bytes those of a zero word
    deliver(limit);
    // A begin restarts the message: words before it do not count.
    std::vector<Call> restarted = message(8, kIdentity);
    restarted.insert(restarted.begin() + 3, Call{0, 138, 8});
    deliver(restarted);
    require(!opxy_device_identity(out, 18), "only a complete, checked message sets it");
    require(master.empty() && set.empty(), "the identity's numbers never pass on");
    // A good one sets it; any check value, sequence 0 included, is a value like any other.
    deliver(message(0, kIdentity));
    require(reads(kIdentity), "a complete message sets the audio processor's copy");
    // Later messages change nothing.
    const u8 other[18] = {9, 9, 9};
    deliver(message(9, other));
    require(reads(kIdentity), "the first one stays");
    require(master.empty() && set.empty(), "consumed");
}
}

int main(int argc, char** argv) {
    require(argc == 2, "usage: device_identity gui|torn|audio");
    // The host header's own identity is the documented preview one.
    u8 preview[18];
    require(configure_preview_device_identity(nullptr) && !device_identity(preview, 18), "unknown on request");
    require(configure_preview_device_identity(opxy::preview_device_identity) && device_identity(preview, 18) &&
            preview[0] == 0xa0 && preview[17] == 0xb1, "the preview identity");
    if (!std::strcmp(argv[1], "gui")) gui(false);
    else if (!std::strcmp(argv[1], "torn")) gui(true);
    else if (!std::strcmp(argv[1], "audio")) audio();
    else require(false, "unknown run");
    std::printf("ok\n");
    return 0;
}
