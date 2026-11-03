#include <opxy/packed16.hpp>
#include <cstdio>
#include <cstdlib>

using namespace opxy;

static void require(bool condition) {
    if (!condition) std::abort();
}

static i32 lane(u32 bits) {
    return static_cast<i32>(bits & 32767u) - static_cast<i32>(bits & 32768u);
}

static void check(u32 a, u32 b) {
    const i64 sum = static_cast<i64>(lane(a)) * lane(b)
                  + static_cast<i64>(lane(a >> 16)) * lane(b >> 16);
    const i32 expected = sum > 2147483647 ? 2147483647
        : sum < -2147483647 - 1 ? -2147483647 - 1 : static_cast<i32>(sum);
    require(packed16::dot(a, b) == expected);
    require(packed16::dot(b, a) == expected);
}

int main() {
    require(packed16::pack(0x12345678, 0x23456789) == 0x67895678u);
    require(packed16::pack(-1, -32768) == 0x8000ffffu);
    require(packed16::dot(packed16::pack(1, -2), packed16::pack(3, 4)) == -5);
    require(packed16::dot(0x80008000u, 0x80008000u) == 2147483647);
    require(packed16::dot(0x7fff7fffu, 0x80008000u) == -2147418112);
    require(packed16::dot(0x00008000u, 0x00008000u) == 1073741824);
    const i32 edges[] = {-32768, -32767, -16384, -1, 0, 1, 16384, 32766, 32767};
    for (i32 a : edges) for (i32 b : edges) for (i32 c : edges) for (i32 d : edges)
        check(packed16::pack(a, b), packed16::pack(c, d));
    u32 random = 1;
    for (unsigned i = 0; i < 1000000; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const u32 a = random;
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const u32 b = random;
        check(a, b);
        const i32 first = a <= 0x7fffffffu ? static_cast<i32>(a) : -1 - static_cast<i32>(~a);
        const i32 second = b <= 0x7fffffffu ? static_cast<i32>(b) : -1 - static_cast<i32>(~b);
        require(packed16::pack(first, second) == ((a & 65535u) | (b & 65535u) * 65536u));
    }
    std::puts("PASS: signed packed dot products, lane order, saturation and low-bit packing");
}
