#include <checkpoint_storage.hpp>
#include <cstdio>
#include <cstdlib>
using namespace opxy;
using namespace opxy::platform;
namespace {
void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
u32 encoded[4096];
CheckpointStorage storage = {encoded, 4096, 0};
u32 input[2048], output[2049];
}
int main() {
    u32 seed = 7;
    for (u32 shape = 0; shape < 5; ++shape) {
        storage.used = 0;
        for (u32 i = 0; i < 2048; ++i) {
            seed = seed * 1664525u + 1013904223u;
            input[i] = shape == 0 ? 0 : shape == 1 ? seed :
                shape == 2 ? i % 2 : shape == 3 ? (i < 1024 ? seed : 0) : (i % 23 ? 0 : seed);
            output[i] = 0xabcdef01;
        }
        output[2048] = 0x12345678;
        u32 offset = 999, size = 999;
        require(storage.append(input, 2048, offset, size), "capture representative word runs");
        require(storage.valid(offset, size, 2048), "encoded state validates");
        require(storage.visit(offset, size, 2048, [](u32 i, u32 value) { output[i] = value; return true; }), "decode");
        for (u32 i = 0; i < 2048; ++i) require(input[i] == output[i], "round trip preserves every word");
        require(output[2048] == 0x12345678, "decode stays within destination");
        require(!storage.valid(offset, size, 2047) && !storage.valid(offset, size, 2049), "decoded length is exact");
    }
    u32 tiny[4];
    CheckpointStorage small = {tiny, 4, 0};
    const u32 values[] = {1, 2, 3}; u32 offset = 0, size = 0;
    require(small.append(values, 3, offset, size) && small.used == 4, "exact capacity");
    require(!small.append(values, 1, offset, size) && small.used == 4 && small.words[1] == 1,
            "exhaustion preserves committed prefix");
    small.words[0] = 0;
    require(!small.valid(0, 4, 3), "zero-length run refused");
    small.words[0] = 0x80000004;
    require(!small.valid(0, 1, 3), "oversized zero run refused");
    small.words[0] = 3;
    require(!small.valid(0, 3, 3) && !small.valid(4, 1, 3), "truncated literals and out-of-range spans refused");
    small.used = 5; require(!small.valid(0, 1, 3), "invalid storage extent refused");
    return 0;
}
