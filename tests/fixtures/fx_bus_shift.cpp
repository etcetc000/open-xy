// SDK ABI fixture: an effect with a bus shift (send_bus.hpp) sees the block
// raised by SHIFT bits, saturating, and stock gets its result lowered by
// SHIFT, rounded to nearest (halves up), through the generated method table.
// SHIFT 0 is an effect without one.
#include <cstdio>
#include "fx_object_probefx.cpp"
// fx.cpp (written by the test) records the block it sees in seen_left/right and
// writes result_left/right, or leaves the block as it is while pass is set.

namespace {
typedef long long i64;
const i64 kMax = 2147483647LL, kMin = -2147483648LL;

opxy::i32 raised(opxy::i32 x) {
    const i64 v = static_cast<i64>(x) * (1LL << SHIFT);
    return static_cast<opxy::i32>(v > kMax ? kMax : v < kMin ? kMin : v);
}

opxy::i32 lowered(opxy::i32 x) {
    if (!SHIFT) return x;
    // floor((x + 2^(SHIFT-1)) / 2^SHIFT), in 64 bits.
    const i64 d = 1LL << SHIFT, v = static_cast<i64>(x) + d / 2;
    return static_cast<opxy::i32>(v >= 0 ? v / d : -((-v + d - 1) / d));
}

int fail(int step, unsigned i, long long got, long long want) {
    std::fprintf(stderr, "step %d, word %u: %lld, expected %lld\n", step, i, got, want);
    return step;
}
}

int main() {
    Object storage = {};
    Object* object = static_cast<Object*>(probefx_fx_factory(&storage));
    const void* const* table = object->vtable;
    typedef void (*Init)(void*, opxy::u32);
    typedef void (*Process)(void*, opxy::i32*, opxy::i32*, const opxy::i32*);
    const Init init = reinterpret_cast<Init>(const_cast<void*>(table[5]));
    const Process process = reinterpret_cast<Process>(const_cast<void*>(table[4]));
    init(object, 14);
    // Inputs at and around the saturation edge, both signs, and a spread of others.
    const i64 edge = 1LL << (31 - SHIFT);
    const i64 inputs[] = {0, 1, -1, 2, -2, 63, -64, edge - 1, -edge, edge, -edge - 1, kMax, kMin, 0x1234567, -0x7654321,
                          kMax >> 1, kMin >> 1, 12345, -12345};
    // Results with every rounding case: exact, below, at and above a half, both signs.
    const i64 step = 1LL << SHIFT, half = step / 2;
    const i64 results[] = {0, 1, -1, half, -half, half - 1, -half - 1, half + 1, -half + 1, step, -step, 3 * step + half,
                           -3 * step - half, 3 * step + half - 1, -3 * step - half + 1, kMax, kMin, kMax - half, kMin + half,
                           0x55555555, -0x55555555};
    const unsigned in_count = sizeof inputs / sizeof inputs[0], out_count = sizeof results / sizeof results[0];
    opxy::i32 left[64], right[64];
    const opxy::i32 params[8] = {};
    for (unsigned block = 0; block < 4; ++block) {
        for (unsigned i = 0; i < 64; ++i) {
            left[i] = static_cast<opxy::i32>(inputs[(i + block) % in_count]);
            right[i] = static_cast<opxy::i32>(inputs[(i * 7 + block + 3) % in_count]);
            probefx::result_left[i] = static_cast<opxy::i32>(results[(i + block) % out_count]);
            probefx::result_right[i] = static_cast<opxy::i32>(results[(i * 5 + block + 1) % out_count]);
        }
        opxy::i32 in_left[64], in_right[64];
        for (unsigned i = 0; i < 64; ++i) { in_left[i] = left[i]; in_right[i] = right[i]; }
        process(object, left, right, params);
        for (unsigned i = 0; i < 64; ++i) {
            if (probefx::seen_left[i] != raised(in_left[i])) return fail(10, i, probefx::seen_left[i], raised(in_left[i]));
            if (probefx::seen_right[i] != raised(in_right[i])) return fail(11, i, probefx::seen_right[i], raised(in_right[i]));
            if (left[i] != lowered(probefx::result_left[i])) return fail(12, i, left[i], lowered(probefx::result_left[i]));
            if (right[i] != lowered(probefx::result_right[i])) return fail(13, i, right[i], lowered(probefx::result_right[i]));
        }
    }
    // An effect that passes its input on gives the bus back unchanged below the edge.
    probefx::pass = true;
    for (i64 x = -edge; x < edge; x += edge / 97 + 1) {
        for (unsigned i = 0; i < 64; ++i) {
            const i64 v = x + i < edge ? x + i : x;
            left[i] = static_cast<opxy::i32>(v);
            right[i] = static_cast<opxy::i32>(-v - 1 >= -edge ? -v - 1 : v);
        }
        opxy::i32 in_left[64], in_right[64];
        for (unsigned i = 0; i < 64; ++i) { in_left[i] = left[i]; in_right[i] = right[i]; }
        process(object, left, right, params);
        for (unsigned i = 0; i < 64; ++i) {
            if (left[i] != in_left[i]) return fail(20, i, left[i], in_left[i]);
            if (right[i] != in_right[i]) return fail(21, i, right[i], in_right[i]);
        }
    }
    std::puts("ok");
    return 0;
}
