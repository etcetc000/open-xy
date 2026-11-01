#include <sample_slice_clock.cpp>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <cmath>
using namespace opxy;
using namespace opxy::platform;
static void check(bool value) { if (!value) std::abort(); }
static u32 bits(float value) { u32 result; std::memcpy(&result, &value, 4); return result; }
static float number(u32 value) { float result; std::memcpy(&result, &value, 4); return result; }
static bool owned = true;
static int ready() { return owned; }
static PadSettings defaults() { return 123; }
static u16 tempo[] = {500, 0};
static i32 live[8] = {0x40000000, 0x40000000, 0x40000000, 0x40000000,
                     0x40000000, 0x40000000, 0x40000000, 0x7fffffff};
static u32 calls;
static bool stationary;
static void rate_callback(const SampleSlice* input, SampleSliceRate* output) {
    check(input && output && input->rate == 44100 && input->frames == 441000);
    check(input->region.begin == 11025 && input->region.end == 400000 && !input->region.reverse);
    check(input->bpm_x10 == 500 && input->settings == 990);
    check(input->parameters[0] == 0x40000000 && input->track_transpose == 0);
    ++calls;
    *output = SampleSliceRate{stationary ? 0u : input->rate * input->bpm_x10, 44100 * input->settings, 4096};
}
extern "C" {
const SampleMachine sdk_sample_machines[] = {
    {reinterpret_cast<const char*>(35), 7, ready, defaults, nullptr, nullptr, nullptr,
     0, nullptr, nullptr, nullptr, nullptr, rate_callback}
};
const u32 sdk_sample_machine_count = 1;
}
static const volatile u16* project() { return tempo; }
static i32 parameter(u32 context, u32 track, u32 index, u32 include_live) {
    check(context == 0x86bd027c && track == 2 && include_live == 1);
    check(index < 8 || index == 52);
    return index == 52 ? 0x40000000 : live[index];
}
static u32 multiply(u32 a, u32 b) { return bits(number(a) * number(b)); }
Address opxy::platform::host_stock_address(Address address) {
    if (address == 0x8d584210) return reinterpret_cast<Address>(&project);
    if (address == 0x8d5ac9dc) return reinterpret_cast<Address>(&parameter);
    if (address == 0x04021ac0) return reinterpret_cast<Address>(&multiply);
    std::abort();
}
int main() {
    // Boundary and pseudo-random rational conversions against host IEEE floats.
    for (u32 n : {1u, 2u, 44100u, 576000000u, 0x7fffffffu, 0x80000000u, 0xffffffffu})
        for (u32 d : {1u, 2u, 4096u, 44100u, 132300000u, 0x80000000u, 0xffffffffu})
            check(slice_ratio_bits(n, d) == bits(float(double(n) / double(d))));
    u32 seed = 7;
    for (u32 i = 0; i < 100000; ++i) {
        seed = seed * 1664525u + 1013904223u; const u32 n = seed | 1u;
        seed = seed * 1664525u + 1013904223u; const u32 d = seed | 1u;
        check(slice_ratio_bits(n, d) == bits(float(double(n) / double(d))));
    }
    check(!slice_ratio_bits(0, 1) && !slice_ratio_bits(1, 0));
    for (u32 rate : {8000u, 11025u, 22050u, 44100u, 48000u, 96000u, 192000u})
        check(slice_file_rate(bits(float(rate))) == rate);
    check(!slice_file_rate(bits(7999)) && !slice_file_rate(bits(192001)) && !slice_file_rate(0x7fc00000));
    u32 raw[8] = {441000, 11025, 400000, 0, 0, sample_store_settings(990), 0, 0};
    reinterpret_cast<unsigned char*>(raw)[pad_record::kRoot] = 60;
    SamplePreviewRange source{};
    source.begin = raw[1]; source.end = raw[2]; source.frames = raw[0];
    sdk_sample_slice_config(&source, 35, 2, raw);
    check(source.processed && source.bpm_x10 == 500 && source.settings == 990);
    // Mutating GUI state after start cannot change the audition's clock.
    tempo[0] = 1500; live[0] = 0;
    const float actual = number(sdk_sample_slice_clock_rate(35, &source, bits(44100)));
    check(std::fabs(actual - 44100.0f * 50.0f / 99.0f) < 0.01f && calls == 1);
    stationary = true;
    check(sdk_sample_slice_clock_rate(35, &source, bits(44100)) == 0 && calls == 2);
    owned = false;
    check(sdk_sample_slice_clock_rate(35, &source, bits(44100)) == bits(44100) && calls == 2);
    source.processed = 0;
    sdk_sample_slice_config(&source, 35, 2, raw);
    check(!source.processed);
    owned = true;
    check(sdk_sample_slice_clock_rate(3, &source, bits(44100)) == bits(44100));
    return 0;
}
