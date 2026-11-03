#include <opxy/q31.hpp>
#include <cstdio>
#include <cstdlib>

using namespace opxy;

static void require(bool condition) {
    if (!condition) std::abort();
}

static void check(i32 a, i32 b) {
    const q31::Product result = q31::multiply_full(a, b);
    const i64 product = static_cast<i64>(a) * b;
    const i64 scale = static_cast<i64>(1) << 31;
    i64 high = product / scale;
    if (product < 0 && product % scale) --high;
    const bool saturated = high > q31::maximum;
    const u32 low = saturated ? 0xffffffffu : static_cast<u32>(product) * 2u;
    require(result.high == (saturated ? q31::maximum : static_cast<i32>(high)));
    require(result.low == low);
    require(result.high == q31::multiply(a, b));
}

int main() {
    struct Case { i32 a, b, high; u32 low; };
    const i32 minimum = -q31::maximum - 1;
    const Case cases[] = {
        {0, q31::maximum, 0, 0}, {1, 1, 0, 2}, {-1, 1, -1, 0xfffffffeu},
        {minimum, 1, -1, 0}, {minimum, -1, 1, 0},
        {q31::maximum, 1, 0, 0xfffffffeu},
        {q31::maximum, q31::maximum, q31::maximum - 1, 2},
        {minimum, q31::maximum, minimum + 1, 0},
        {minimum, minimum, q31::maximum, 0xffffffffu},
        {0x40000000, 0x40000000, 0x20000000, 0}
    };
    for (const Case& c : cases) {
        const q31::Product p = q31::multiply_full(c.a, c.b);
        require(p.high == c.high && p.low == c.low);
    }
    const i32 edges[] = {minimum, minimum + 1, -1073741824, -32768, -1, 0,
                         1, 32767, 1073741824, q31::maximum - 1, q31::maximum};
    for (i32 a : edges) for (i32 b : edges) check(a, b);
    u32 random = 1;
    for (unsigned i = 0; i < 1000000; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const i32 a = random <= 0x7fffffffu ? static_cast<i32>(random)
            : -1 - static_cast<i32>(~random);
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const i32 b = random <= 0x7fffffffu ? static_cast<i32>(random)
            : -1 - static_cast<i32>(~random);
        check(a, b);
    }
    std::puts("PASS: full fractional product words, saturation and Q31 consistency");
}
