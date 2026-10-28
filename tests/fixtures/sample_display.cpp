#include <cstdlib>
#include <cstring>
static void host_barrier() {}
#include "../../sdk/platform/opxy-1.2.1/sample_display.cpp"

static void check(bool value) { if (!value) std::abort(); }
static unsigned acquired, released, wakes;
extern "C" void sdk_sample_display_wake() { ++wakes; }
alignas(8) static unsigned char first[64], second[64];
static opxy::i16 pcm[384];
extern "C" const void* sdk_sample_cache_sample_acquire(unsigned key) {
    ++acquired;
    return key == 11 ? first : key == 12 ? second : nullptr;
}
extern "C" void sdk_sample_cache_sample_release(unsigned) { ++released; }
static void object(unsigned char* bytes, unsigned frames, unsigned channels) {
    const unsigned valid = 1, rate = 44100;
    const opxy::i16* data = pcm;
    std::memcpy(bytes, &valid, 4); std::memcpy(bytes + 8, &data, sizeof(data));
    std::memcpy(bytes + 0x30, &frames, 4); std::memcpy(bytes + 0x38, &rate, 4);
    bytes[0x3c] = channels == 2;
}
int main() {
    for (unsigned i = 0; i < 192; ++i) { pcm[2*i] = static_cast<opxy::i16>(i); pcm[2*i+1] = -static_cast<opxy::i16>(i); }
    object(first, 192, 2); object(second, 1, 1);
    opxy::SampleDisplay out{};
    check(!sdk_sample_display_read(0, 11, &out) && !acquired && !out.frames);
    check(wakes == 1);
    check(!sdk_sample_display_read(0, 11, &out) && wakes == 1);
    sdk_sample_display_prepare();
    check(acquired == 1 && released == 1);
    check(sdk_sample_display_read(0, 11, &out) && out.frames == 192 && out.channels == 2);
    for (unsigned i = 0; i < 96; ++i) check(out.low[i] == -int(2*i+1) && out.high[i] == int(2*i+1));
    const unsigned generation = out.generation;
    sdk_sample_display_prepare(); check(acquired == 1);
    ++overviews[0].sequence;
    check(!sdk_sample_display_read(0, 11, &out) && !out.frames);
    ++overviews[0].sequence;
    // A key change must never show the previous sample while waiting.
    check(!sdk_sample_display_read(0, 12, &out) && !out.frames);
    check(wakes == 2);
    sdk_sample_display_prepare();
    check(sdk_sample_display_read(0, 12, &out) && out.frames == 1 && out.generation != generation);
    for (unsigned i = 0; i < 96; ++i) check(out.low[i] == 0 && out.high[i] == 0);
    sdk_sample_display_release(second);
    check(!sdk_sample_display_read(0, 12, &out));
    pcm[0] = 1234; // same address/key reused after eviction must refresh.
    sdk_sample_display_prepare();
    check(sdk_sample_display_read(0, 12, &out) && out.low[0] == 1234);
    check(acquired == released);
    check(!sdk_sample_display_read(0, 0, &out) && !out.frames);
    const unsigned stopped = acquired;
    sdk_sample_display_prepare(); check(acquired == stopped);
    check(!sdk_sample_display_read(8, 11, &out));
    check(!sdk_sample_display_read(1, 99, &out));
    sdk_sample_display_prepare(); check(!sdk_sample_display_read(1, 99, &out));
}
