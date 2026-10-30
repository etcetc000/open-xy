#include "drum_storage.hpp"
#include <cstdlib>
#include <cstring>
#include <initializer_list>

using namespace opxy;
using namespace opxy::platform;
static void require(bool valid) { if (!valid) std::abort(); }

static void roundtrip(u32 model, const u8* source, u32 models = kDrumNarrowModels) {
    i32 words[4] = {};
    require(drum_pack(model, source, words, models));
    // Stock preset serialization: integer word >> 16, then integer << 16.
    // Exercise float32 conversion too: every persisted integer is exact.
    for (u32 i = 0; i < 4; ++i) {
        require(words[i] >= 0 && !(static_cast<u32>(words[i]) & 65535u));
        const float json_integer = static_cast<float>(words[i] >> 16);
        words[i] = static_cast<i32>(static_cast<u32>(json_integer) << 16);
    }
    u8 restored[8] = {}, restored_model = 99, expected[8];
    for (u32 i = 0; i < 8; ++i) expected[i] = drum_stored(models, i, source[i]);
    require(drum_unpack(words, &restored_model, restored, models));
    require(restored_model == model && std::memcmp(expected, restored, 8) == 0);
}

int main() {
    for (unsigned count = 1; count <= 128; ++count)
        for (unsigned model = 0; model < count; ++model) {
            const unsigned normalized = opxy::platform::drum_model_q31(model, count);
            if ((((normalized >> 16) * count) >> 15) != model) return 99;
        }
    if (opxy::platform::drum_model_q31(0, 0) || opxy::platform::drum_model_q31(2, 2)) return 98;
    // Every model with every control at every value, the others nonzero.
    u8 controls[8] = {};
    for (u32 model = 0; model < kDrumNarrowModels; ++model) {
        std::memset(controls, 0, sizeof controls);
        roundtrip(model, controls);
        std::memset(controls, 127, sizeof controls);
        roundtrip(model, controls);
        for (u32 slot = 0; slot < 8; ++slot)
            for (u32 value = 0; value < 128; ++value) {
                for (u32 i = 0; i < 8; ++i) controls[i] = static_cast<u8>((i * 29 + value * 11 + model * 5) & 127);
                controls[slot] = static_cast<u8>(value);
                roundtrip(model, controls);
            }
    }
    // Each of the 60 bits alone, so no two fields overlap.
    for (u32 k = 0; k < 60; ++k) {
        u8 one[8] = {};
        u32 model = 0;
        if (k < 56) one[k / 7] = static_cast<u8>(1u << (k % 7));
        else model = 1u << (k - 56);
        i32 words[4];
        require(drum_pack(model, one, words));
        u32 set = 0;
        for (u32 i = 0; i < 4; ++i)
            for (u32 b = 0; b < 32; ++b) set += (static_cast<u32>(words[i]) >> b) & 1u;
        require(set == 1);
        roundtrip(model, one);
    }
    i32 words[4] = {};
    require(drum_pack(3, controls, words));
    i32 baseline[4];
    std::memcpy(baseline, words, sizeof words);
    for (u32 slot = 0; slot < 8; ++slot) {
        u8 invalid[8];
        std::memcpy(invalid, controls, 8);
        invalid[slot] = 128;
        require(!drum_pack(3, invalid, words));
        require(std::memcmp(words, baseline, sizeof words) == 0);
    }
    require(!drum_pack(kDrumNarrowModels, controls, words));
    require(!drum_pack(33, controls, words, 33) && !drum_pack(0, controls, words, kDrumModelLimit + 1));
    require(std::memcmp(words, baseline, sizeof words) == 0);
    u8 output[8], output_model = 91;
    std::memset(output, 91, sizeof output);
    for (u32 slot = 0; slot < 4; ++slot) {
        i32 invalid[4];
        std::memcpy(invalid, baseline, sizeof invalid);
        invalid[slot] |= 1;
        require(!drum_unpack(invalid, &output_model, output));
        invalid[slot] = -1;
        require(!drum_unpack(invalid, &output_model, output));
    }
    for (u8 value : output) require(value == 91);
    require(output_model == 91);
    require(!drum_pack(0, nullptr, words) && !drum_pack(0, controls, nullptr));
    require(!drum_unpack(nullptr, &output_model, output) && !drum_unpack(baseline, nullptr, output) &&
            !drum_unpack(baseline, &output_model, nullptr));

    // A kit of more than 16 models: slots 6 and 7 at 64 steps, to the nearest
    // (127 is the top), and every other control exact.
    require(!drum_coarse(16, 6) && drum_coarse(17, 6) && drum_coarse(64, 7) && !drum_coarse(64, 5));
    for (u32 v = 0; v < 128; ++v) {
        const u8 stored = drum_stored(33, 6, v);
        require(stored == drum_stored(33, 6, stored) && (stored == 127 || !(stored & 1)));
        require(stored + 1 >= v && stored <= v + 1);
        require(drum_stored(33, 5, v) == v && drum_stored(16, 7, v) == v);
    }
    require(drum_stored(33, 7, 126) == 127 && drum_stored(33, 7, 124) == 124 && drum_stored(33, 7, 1) == 2);
    for (u32 models : {17u, 33u, kDrumModelLimit})
        for (u32 model = 0; model < models; ++model)
            for (u32 value = 0; value < 128; value += 3) {
                for (u32 i = 0; i < 8; ++i) controls[i] = static_cast<u8>((i * 29 + value * 11 + model * 5) & 127);
                controls[6] = static_cast<u8>(value);
                controls[7] = static_cast<u8>(127 - value);
                roundtrip(model, controls, models);
            }
    for (u32 k = 0; k < 60; ++k) {
        // Each bit alone: the step's lowest bit is the value's second.
        u8 one[8] = {};
        u32 model = 0;
        const u32 slot = k / 7, bit = k % 7;
        if (k >= 56) model = 1u << (k - 56);
        else if (slot < 6) one[slot] = static_cast<u8>(1u << bit);
        else if (bit) one[slot] = static_cast<u8>(1u << bit);
        else model = 1u << (slot == 6 ? 4 : 5);
        i32 wide[4];
        require(drum_pack(model, one, wide, kDrumModelLimit));
        u32 set = 0;
        for (u32 i = 0; i < 4; ++i)
            for (u32 b = 0; b < 32; ++b) set += (static_cast<u32>(wide[i]) >> b) & 1u;
        require(set == 1);
        roundtrip(model, one, kDrumModelLimit);
    }
    {
        // A pad stored while the kit had 16 models, read by its 33-model self:
        // slots 0..5 exact, 6 and 7 at the step below, the model when both are even.
        const u8 old[8] = {1, 2, 3, 4, 5, 6, 40, 90};
        i32 stored[4];
        require(drum_pack(11, old, stored));
        u8 model = 0, read[8];
        require(drum_unpack(stored, &model, read, 33));
        require(model == 11 && std::memcmp(read, old, 6) == 0 && read[6] == 40 && read[7] == 90);
        const u8 odd[8] = {1, 2, 3, 4, 5, 6, 41, 90};
        require(drum_pack(11, odd, stored) && drum_unpack(stored, &model, read, 33));
        require(model == 27 && read[6] == 40);
    }

    // The mark: Drumline type 7 in the model byte, stock's choke group kept.
    for (u32 byte = 0; byte < 256; ++byte) {
        const u8 marked = drum_mark(static_cast<u8>(byte));
        require(drum_marked(marked) && (marked & 15) == kDrumModelMark);
        require((marked >> 4) == ((byte >> 4) <= 5 ? (byte >> 4) : 0));
        require(drum_marked(static_cast<u8>(byte)) == ((byte & 15) == 7 && (byte >> 4) <= 5));
        // Stock's payload word for a valid model byte: 6 type + group over 66 bins.
        if ((byte & 15) < 11 && (byte >> 4) <= 5) {
            const u32 bin = 6 * (byte & 15) + (byte >> 4);
            const i32 word = static_cast<i32>(bin * 32537631u + 16268815u);
            require(drum_marked_q31(word) == ((byte & 15) == 7));
        }
    }
    // Drumline's default kit never uses type 7: its pads read as unmarked.
    const u8 defaults[24] = {0x40, 0x40, 0x11, 0x11, 0x1a, 0x14, 0x19, 0x13, 0x22, 0x22, 0x22, 0x18,
                             0x18, 0x12, 0x18, 0x16, 0x18, 0x16, 0x15, 0x15, 0x1a, 0x13, 0x16, 0x16};
    for (u8 byte : defaults) require(!drum_marked(byte));
    require(!drum_marked_q31(-1));

    // A step lock is a track offset: the screen and the voice both decode it.
    {
        const u8 stored[8] = {5, 0, 64, 127, 10, 100, 1, 126};
        u8 live[8];
        drum_offset(stored, nullptr, live);
        require(std::memcmp(stored, live, 8) == 0);
        i32 neutral[8];
        for (i32& p : neutral) p = 0x40000000;
        drum_offset(stored, neutral, live);
        require(std::memcmp(stored, live, 8) == 0);
        i32 locked[8];
        std::memcpy(locked, neutral, sizeof locked);
        locked[0] = 0x40000000 + (7 << 24);   // +7 on the first control
        locked[2] = 0x40000000 + (10 << 24);  // +10
        locked[4] = 0x40000000 - (3 << 24);   // -3
        drum_offset(stored, locked, live);
        require(live[0] == 12 && live[2] == 74 && live[4] == 7 && live[1] == 0 && live[3] == 127);
        locked[1] = 0;           // -64 clamps at zero
        locked[7] = 0x7fffffff;  // +63 clamps at 127
        drum_offset(stored, locked, live);
        require(live[1] == 0 && live[7] == 127);
        locked[0] = -1;          // Out-of-range parameters count as zero
        drum_offset(stored, locked, live);
        require(live[0] == 0);
    }
}
