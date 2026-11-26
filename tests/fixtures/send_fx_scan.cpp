// The send hook's silence scan (send_fx.cpp quiet(): the largest magnitude of a
// block against the floor) takes the decision of a plain word-by-word check,
// which stops at the first sample outside -(floor - 1)..floor - 1,
// on every block: one sample of every value from -2^17 to 2^17 (each once, at a
// rotating position) and the ends of the range (each at every position) among
// sub-floor samples of both signs, and on random blocks around the floor.
// Built with send_fx.cpp itself (included, so its quiet() is called).
#include <cstdio>
#include <cstdlib>
#include <send_fx.cpp>  // the platform unit (the folder the test builds in), not this folder's fixture

opxy::platform::Address opxy::platform::host_stock_address(Address) { std::abort(); }

namespace {
// The plain check: the scan's reference.
bool word_by_word(const volatile i32* samples, u32 count, u32 floor) {
    for (u32 i = 0; i < count; ++i)
        if (static_cast<u32>(samples[i]) + (floor - 1) > 2 * (floor - 1)) return false;
    return true;
}

u32 state = 0x2545f491u;
u32 random_word() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
// A sample within the floor, of either sign.
i32 below(i32 floor) { return static_cast<i32>(random_word() % (2 * floor - 1)) - (floor - 1); }

u32 blocks, quiet_blocks, differences;
void compare(const i32* block, u32 count) {
    const bool expected = word_by_word(block, count, kFloor);
    const bool scanned = count == 64 ? quiet<64>(block, kFloor) : quiet<128>(block, kFloor);
    ++blocks;
    quiet_blocks += expected;
    if (expected != scanned && ++differences <= 10)
        std::fprintf(stderr, "count %u: the scan said %d, the word-by-word check %d\n", count, scanned, expected);
}
}

int main() {
    const i32 floor = static_cast<i32>(kFloor);
    const i32 most_negative = -0x7fffffff - 1;
    i32 block[128];
    const u32 counts[] = {64, 128};
    for (u32 count : counts) {
        // One sample of every value from -2^17 to 2^17, each once at a rotating position
        // (the next value one position on), among samples within the floor.
        u32 position = 0;
        for (i32 v = -(1 << 17); v <= (1 << 17); ++v) {
            for (u32 i = 0; i < count; ++i) block[i] = below(floor);
            block[position] = v;
            position = (position + 1) % count;
            compare(block, count);
        }
        const i32 ends[] = {most_negative, most_negative + 1, most_negative + floor, 0x7fffffff, 0x7ffffffe,
                            0x7fffffff - floor, floor, -floor, floor - 1, -(floor - 1), 0};
        // The ends of the range, each at every position.
        for (i32 v : ends)
            for (u32 at = 0; at < count; ++at) {
                for (u32 i = 0; i < count; ++i) block[i] = below(floor);
                block[at] = v;
                compare(block, count);
            }
        // Random blocks: each word within a few of the floor, of either sign, or any word at all.
        for (u32 n = 0; n < 200000; ++n) {
            const u32 loud_one_in = 1 + random_word() % 512;
            for (u32 i = 0; i < count; ++i) {
                const u32 r = random_word();
                if (r % loud_one_in) block[i] = below(floor);
                else if (r & 0x10000) block[i] = static_cast<i32>(random_word());
                else block[i] = (r & 0x20000 ? 1 : -1) * (floor - 3 + static_cast<i32>((r >> 18) % 7));
                if ((r >> 25) == 0) block[i] = most_negative;
            }
            compare(block, count);
        }
    }
    if (differences) {
        std::fprintf(stderr, "%u of %u blocks decided differently\n", differences, blocks);
        return 1;
    }
    std::printf("ok %u blocks, %u quiet\n", blocks, quiet_blocks);
    return 0;
}
