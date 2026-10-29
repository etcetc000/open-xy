#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "arranger_bounce_wire.hpp"

using namespace opxy;
using namespace opxy::platform::bounce_wire;
void require(bool condition, const char* message) {
    if (!condition) { std::puts(message); std::exit(1); }
}
bool equal(const Message& a, const Message& b) {
    return a.kind == b.kind && a.cookie == b.cookie && a.args[0] == b.args[0] &&
        a.args[1] == b.args[1] && a.args[2] == b.args[2];
}
bool receive_current(const void* data, u32 size, bool master, u32 cookie, Message& out) {
    Message incoming;
    if (!decode(data, size, master, incoming) || incoming.cookie != cookie) return false;
    out = incoming;
    return true;
}
int main() {
    const Message sentinel = {Error, 123, {4, 5, 6}};
    u8 packet[kSize];
    for (u32 kind = Prepare; kind <= Error; ++kind) {
        const Message input = {static_cast<Kind>(kind), 0xffffffffu, {0, 0xffffffffu, 0x12345678u}};
        require(encode(input, packet), "encode each kind");
        require(packet[20] == 0x78 && packet[21] == 0x56 && packet[22] == 0x34 && packet[23] == 0x12,
            "little endian words");
        u8 unaligned[kSize + 1]; std::memcpy(unaligned + 1, packet, kSize);
        Message output = sentinel;
        const bool master = kind >= Ready;
        require(decode(unaligned + 1, kSize, master, output) && equal(input, output), "unaligned round trip");
        output = sentinel;
        require(!decode(packet, kSize, !master, output) && equal(output, sentinel), "wrong endpoint unchanged");
        require(!receive_current(packet, kSize, master, 7, output) && equal(output, sentinel),
            "caller rejects stale session without changing output");
        require(receive_current(packet, kSize, master, input.cookie, output) && equal(output, input),
            "caller accepts its session");
    }
    const Message input = {Prepare, 7, {1, 2, 3}};
    require(encode(input, packet), "valid request");
    Message output = sentinel;
    for (u32 size = 0; size <= kSize + 1; ++size) {
        if (size == kSize) continue;
        require(!decode(packet, size, false, output) && equal(output, sentinel), "exact length required");
    }
    require(!decode(nullptr, kSize, false, output) && equal(output, sentinel), "null input rejected");
    for (u32 i = 0; i < 8; ++i) {
        u8 malformed[kSize]; std::memcpy(malformed, packet, kSize);
        malformed[i] = i == 4 ? 0 : static_cast<u8>(malformed[i] + 1);
        require(!decode(malformed, kSize, false, output) && equal(output, sentinel), "header validation unchanged");
    }
    for (u32 kind = Error + 1; kind <= 255; ++kind) {
        packet[4] = static_cast<u8>(kind);
        require(!decode(packet, kSize, false, output) && equal(output, sentinel), "unknown kind rejected");
    }
    require(encode(input, packet), "reset valid request");
    for (u32 i = 8; i < 12; ++i) packet[i] = 0;
    require(!decode(packet, kSize, false, output) && equal(output, sentinel), "zero cookie rejected");
    const Message invalid[] = {{Prepare, 0, {0, 0, 0}}, {static_cast<Kind>(0), 7, {0, 0, 0}},
        {static_cast<Kind>(10), 7, {0, 0, 0}}};
    for (u32 i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        std::memset(packet, 0xa5, kSize);
        require(!encode(invalid[i], packet), "invalid encode rejected");
        for (u32 j = 0; j < kSize; ++j) require(packet[j] == 0xa5, "invalid encode leaves bytes unchanged");
    }
    return 0;
}
