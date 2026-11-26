// opxy::q15 and opxy::q31's saturating helpers against references written
// independently: exact arithmetic in long long or double, then a clamp. Every
// 1.15 operand pair on a stride, every pair near the range ends, every 1.15
// value for the unary helpers, and the exact ties of the rounding multiply.
// The pair helpers are checked half by half against the same references.
#include <cmath>
#include <cstdio>
#include <opxy/q15.hpp>
#include <opxy/q31.hpp>

using opxy::i16;
using opxy::i32;
using opxy::i64;
using opxy::u32;

static long long clamp(long long value, long long low, long long high) {
    return value < low ? low : value > high ? high : value;
}
static int clamp16(long long value) { return int(clamp(value, -32768, 32767)); }
static long long clamp32(long long value) { return clamp(value, -2147483648LL, 2147483647LL); }

// The 1.15 product as a double is exact: |a * b| < 2^31.
static double exact_product(int a, int b) { return double(a) * double(b) / 32768.0; }
static int truncated(int a, int b) { return clamp16((long long)std::floor(exact_product(a, b))); }
// The C library's default rounding: to nearest, ties to even.
static int nearest(int a, int b) { return clamp16((long long)std::nearbyint(exact_product(a, b))); }
static int half_up(int a, int b) { return clamp16((long long)std::floor(exact_product(a, b) + 0.5)); }
// Shift left while the doubled value is still a 1.15 value.
static int normalized(int value) {
    if (value == 0) return 0;
    long long v = value;
    while (v * 2 >= -32768 && v * 2 <= 32767) v *= 2;
    return int(v);
}
static long long floor_shift(long long value, unsigned count) {
    return (long long)std::floor(double(value) / std::ldexp(1.0, int(count)));
}
// The value's low 16 or 32 bits as a two's complement number.
static int wrap16(long long value) {
    const long long bits = ((value % 65536) + 65536) % 65536;
    return int(bits > 32767 ? bits - 65536 : bits);
}
static long long wrap32(long long value) {
    const long long bits = (long long)((unsigned long long)value & 0xffffffffULL);
    return bits > 2147483647LL ? bits - 4294967296LL : bits;
}
// A pair word from its two halves' values, and its halves back.
static long long pair(long long low, long long high) {
    return (low & 0xffff) | ((high & 0xffff) << 16);
}
static int low_half(long long word) { return wrap16(word & 0xffff); }
static int high_half(long long word) { return wrap16((word >> 16) & 0xffff); }

static int failures;
static void expect(bool same, const char* what, long long a, long long b, long long got, long long want) {
    if (same || failures++ > 8) return;
    std::printf("%s(%lld, %lld) = %lld, expected %lld\n", what, a, b, got, want);
}

static void binary16(int a, int b) {
    const i16 x = i16(a), y = i16(b);
    int got = opxy::q15::add(x, y), want = clamp16(a + b);
    expect(got == want, "q15::add", a, b, got, want);
    got = opxy::q15::subtract(x, y); want = clamp16(a - b);
    expect(got == want, "q15::subtract", a, b, got, want);
    got = opxy::q15::multiply(x, y); want = truncated(a, b);
    expect(got == want, "q15::multiply", a, b, got, want);
    got = opxy::q15::multiply_nearest(x, y); want = nearest(a, b);
    expect(got == want, "q15::multiply_nearest", a, b, got, want);
    got = opxy::q15::subtract_wrapped(x, y); want = wrap16(a - b);
    expect(got == want, "q15::subtract_wrapped", a, b, got, want);

    // Pairs: (a, c) and (b, d), with second halves that run through the range
    // on their own course.
    const int c = wrap16(a * 7 + 12345), d = wrap16(b * 3 - 777);
    const u32 ac = opxy::q15::pack(x, i16(c)), bd = opxy::q15::pack(y, i16(d));
    expect(ac == pair(a, c), "q15::pack", a, c, ac, pair(a, c));
    expect(opxy::q15::low(ac) == a && opxy::q15::high(ac) == c, "q15::low/high", a, c, opxy::q15::low(ac),
           opxy::q15::high(ac));
    long long word = opxy::q15::multiply_pair(ac, y), wanted = pair(truncated(a, b), truncated(c, b));
    expect(word == wanted, "q15::multiply_pair", (long long)ac, b, word, wanted);
    word = opxy::q15::add_pair(ac, bd); wanted = pair(clamp16(a + b), clamp16(c + d));
    expect(word == wanted, "q15::add_pair", (long long)ac, (long long)bd, word, wanted);
    expect(opxy::q15::low(u32(word)) == low_half(wanted) && opxy::q15::high(u32(word)) == high_half(wanted),
           "q15::low/high of a sum", a, c, word, wanted);
}

static u32 state = 12345;
static u32 next() { state = state * 1664525u + 1013904223u; return state; }

int main() {
    // Every a; b on a stride of 61 starting at a's residue, so every residue
    // class is covered; every b where either operand is near an end.
    for (int a = -32768; a < 32768; ++a) {
        if (a < -32760 || a > 32759) {
            for (int b = -32768; b < 32768; ++b) binary16(a, b);
            continue;
        }
        for (int b = -32768 + (a + 32768) % 61; b < 32768; b += 61) binary16(a, b);
        for (int b = 0; b < 8; ++b) { binary16(a, -32768 + b); binary16(a, 32767 - b); }
    }
    for (int a = -32768; a < 32768; ++a) {
        int got = opxy::q15::normalize(i16(a)), want = normalized(a);
        expect(got == want, "q15::normalize", a, 0, got, want);
        got = opxy::q15::saturate(a); want = a;
        expect(got == want, "q15::saturate", a, 0, got, want);
        got = opxy::q15::half(i16(a)); want = int(floor_shift(a, 1));
        expect(got == want, "q15::half", a, 0, got, want);
    }
    const long long words[] = {0, 1, -1, 32767, 32768, -32768, -32769, 65535, -65536, 2147483647LL,
                               -2147483647LL - 1, 1073741824LL, -1073741824LL};
    for (long long w : words) {
        const int got = opxy::q15::saturate(i32(w)), want = clamp16(w);
        expect(got == want, "q15::saturate", w, 0, got, want);
    }
    for (int i = 0; i < 1000000; ++i) {
        const i32 w = i32(next());
        const int got = opxy::q15::saturate(w), want = clamp16(w);
        expect(got == want, "q15::saturate", w, 0, got, want);
    }

    // Q31: the range ends and their neighbours, crossed, then random words.
    const long long ends[] = {0, 1, -1, 2, -2, 2147483647LL, 2147483646LL, -2147483647LL - 1, -2147483647LL,
                              1073741824LL, -1073741824LL, 1073741823LL, -1073741825LL, 65536, -65536};
    const long long wide[] = {0, 1, -1, 2147483647LL, 2147483648LL, -2147483648LL, -2147483649LL,
                              4294967296LL, -4294967296LL, 9223372036854775807LL, -9223372036854775807LL - 1};
    for (long long w : wide) {
        const long long got = opxy::q31::saturate(i64(w)), want = clamp32(w);
        expect(got == want, "q31::saturate", w, 0, got, want);
    }
    for (long long a : ends) {
        for (long long b : ends) {
            const long long got = opxy::q31::subtract(i32(a), i32(b)), want = clamp32(a - b);
            expect(got == want, "q31::subtract", a, b, got, want);
        }
        for (unsigned n = 0; n < 32; ++n) {
            const long long got = opxy::q31::shift_right(i32(a), n), want = floor_shift(a, n);
            expect(got == want, "q31::shift_right", a, n, got, want);
        }
        for (int n = -31; n < 32; ++n) {
            const long long got = opxy::q31::shift(i32(a), n);
            const long long want = n < 0 ? floor_shift(a, unsigned(-n)) : wrap32(a * (1LL << n));
            expect(got == want, "q31::shift", a, n, got, want);
        }
    }
    for (int i = 0; i < 2000000; ++i) {
        const long long a = i32(next()), b = i32(next());
        long long got = opxy::q31::subtract(i32(a), i32(b)), want = clamp32(a - b);
        expect(got == want, "q31::subtract", a, b, got, want);
        got = opxy::q31::saturate(i64(a) * 3 + b); want = clamp32(a * 3 + b);
        expect(got == want, "q31::saturate", a * 3 + b, 0, got, want);
        const unsigned n = next() % 32;
        got = opxy::q31::shift_right(i32(a), n); want = floor_shift(a, n);
        expect(got == want, "q31::shift_right", a, n, got, want);
        const int m = int(next() % 63) - 31;
        got = opxy::q31::shift(i32(a), m); want = m < 0 ? floor_shift(a, unsigned(-m)) : wrap32(a * (1LL << m));
        expect(got == want, "q31::shift", a, m, got, want);
    }

    // Exact ties of the rounding multiply, a product exactly halfway between two
    // steps: (a, b, the even neighbour, the one above). The helper's host form
    // takes the even one; biased rounding would take the one above.
    const int ties[][4] = {{0x4000, 1, 0, 1}, {0x4000, -1, 0, 0}, {0x4000, 3, 2, 2}, {0x4000, -3, -2, -1},
                           {0x4000, 5, 2, 3}, {0x4000, -5, -2, -2}, {-0x4000, 1, 0, 0}, {-0x4000, -1, 0, 1},
                           {0x2000, 2, 0, 1}, {0x2000, 6, 2, 2}, {0x4000, 32767, 16384, 16384},
                           {0x4000, -32767, -16384, -16383}, {0x2000, 32766, 8192, 8192}};
    int ties_apart = 0;
    for (const auto& t : ties) {
        const long long product = (long long)t[0] * t[1];
        expect((product & 0x7fff) == 0x4000, "not a tie", t[0], t[1], product & 0x7fff, 0x4000);
        expect(nearest(t[0], t[1]) == t[2] && half_up(t[0], t[1]) == t[3], "tie table", t[0], t[1],
               nearest(t[0], t[1]), t[2]);
        const int got = opxy::q15::multiply_nearest(i16(t[0]), i16(t[1]));
        expect(got == t[2], "q15::multiply_nearest tie", t[0], t[1], got, t[2]);
        ties_apart += t[2] != t[3];
    }
    expect(ties_apart >= 6, "ties where the modes differ", 0, 0, ties_apart, 6);
    expect(opxy::q15::multiply_nearest(-32768, -32768) == 32767, "q15::multiply_nearest", -32768, -32768,
           opxy::q15::multiply_nearest(-32768, -32768), 32767);
    expect(opxy::q15::multiply(-32768, -32768) == 32767, "q15::multiply", -32768, -32768,
           opxy::q15::multiply(-32768, -32768), 32767);

    if (failures) return 1;
    std::printf("ok\n");
    return 0;
}
