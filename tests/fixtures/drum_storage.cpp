#include "drum_storage.hpp"
#include <cstdlib>
#include <cstring>

using namespace opxy;
using namespace opxy::platform;
static void require(bool valid) { if (!valid) std::abort(); }

static void roundtrip(const u8* source) {
    i32 words[4] = {};
    require(drum_pack(source, words));
    // Stock preset serialization: integer word >> 16, then integer << 16.
    // Exercise float32 conversion too: every persisted integer is exact.
    for (u32 i = 0; i < 4; ++i) {
        require(words[i] >= 0);
        const float json_integer = static_cast<float>(words[i] >> 16);
        words[i] = static_cast<i32>(static_cast<u32>(json_integer) << 16);
    }
    u8 restored[8] = {};
    require(drum_unpack(words, restored));
    require(std::memcmp(source, restored, 8) == 0);
}

int main() {
    for (unsigned count = 1; count <= 128; ++count)
        for (unsigned model = 0; model < count; ++model) {
            const unsigned normalized = opxy::platform::drum_model_q31(model, count);
            if ((((normalized >> 16) * count) >> 15) != model) return 99;
        }
    if (opxy::platform::drum_model_q31(0, 0) || opxy::platform::drum_model_q31(2, 2)) return 98;
    u8 controls[8] = {};
    roundtrip(controls);
    std::memset(controls, 127, sizeof controls);
    roundtrip(controls);
    // Every control value at every field boundary, with other fields nonzero.
    for (u32 slot = 0; slot < 8; ++slot)
        for (u32 value = 0; value < 128; ++value) {
            for (u32 i = 0; i < 8; ++i) controls[i] = static_cast<u8>((i * 29 + value * 11) & 127);
            controls[slot] = static_cast<u8>(value);
            roundtrip(controls);
        }
    i32 words[4] = {};
    require(drum_pack(controls, words));
    i32 baseline[4];
    std::memcpy(baseline, words, sizeof words);
    for (u32 slot = 0; slot < 8; ++slot) {
        u8 invalid[8];
        std::memcpy(invalid, controls, 8);
        invalid[slot] = 128;
        require(!drum_pack(invalid, words));
        require(std::memcmp(words, baseline, sizeof words) == 0);
    }
    u8 output[8];
    std::memset(output, 91, sizeof output);
    for (u32 slot = 0; slot < 4; ++slot) {
        i32 invalid[4];
        std::memcpy(invalid, baseline, sizeof invalid);
        invalid[slot] |= 1;
        require(!drum_unpack(invalid, output));
        invalid[slot] = -1;
        require(!drum_unpack(invalid, output));
    }
    for (u32 tag = 0; tag < 16; ++tag) {
        if (tag == kDrumStorageTag) continue;
        std::memcpy(words, baseline, sizeof words);
        words[3] = static_cast<i32>((static_cast<u32>(words[3]) & 0x07ff0000u) | (tag << 27));
        require(!drum_unpack(words, output));
    }
    for (u8 value : output) require(value == 91);
    // A step lock is a track offset: the screen and the voice both decode it.
    {
        const u8 stored[8] = {5, 0, 64, 127, 10, 100, 1, 126};
        u8 live[8];
        drum_offset(stored, nullptr, 13, live);
        require(std::memcmp(stored, live, 8) == 0);
        i32 neutral[8];
        for (i32& p : neutral) p = 0x40000000;
        drum_offset(stored, neutral, 13, live);
        require(std::memcmp(stored, live, 8) == 0);
        i32 locked[8];
        std::memcpy(locked, neutral, sizeof locked);
        locked[2] = 0x40000000 + (10 << 24);  // +10
        locked[4] = 0x40000000 - (3 << 24);   // -3
        drum_offset(stored, locked, 13, live);
        require(live[2] == 74 && live[4] == 7 && live[1] == 0 && live[3] == 127);
        locked[0] = 0x7fffffff;  // Model clamps to the last model
        locked[1] = 0;           // -64 clamps at zero
        locked[7] = 0x7fffffff;  // +63 clamps at 127
        drum_offset(stored, locked, 13, live);
        require(live[0] == 12 && live[1] == 0 && live[7] == 127);
        locked[0] = -1;          // Out-of-range parameters count as zero
        drum_offset(stored, locked, 13, live);
        require(live[0] == 0);
    }
    require(!drum_pack(nullptr, words) && !drum_pack(controls, nullptr));
    require(!drum_unpack(nullptr, output) && !drum_unpack(baseline, nullptr));
}
