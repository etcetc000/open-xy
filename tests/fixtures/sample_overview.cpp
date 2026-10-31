// Host checks for the sample overview a sample engine's own screen reads: the
// selected track's region-0 sample, read a bounded slice per frame, borrowed
// and released through the cache, and cleared when the track's sample changes.
#include <sample_overview.cpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define require(value) do { if (!(value)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)

using opxy::u32;
static unsigned char record[0x4000];   // a track record; its region-0 path is at +0x396c
static u32 acquired, released, held;
static bool loading;
static opxy::i16 pcm[100000];
static unsigned char object[0x40];
static opxy::u32 pad[8];
static bool has_pad;

extern "C" u32 sdk_sample_cache_stock_hash(const char* path) {
    u32 h = 5381;
    while (*path) h = h * 33 + static_cast<unsigned char>(*path++);
    return h;
}
extern "C" const void* sdk_sample_cache_sample_acquire(u32) {
    if (loading) return nullptr;
    ++acquired; ++held;
    return object;
}
extern "C" void sdk_sample_cache_sample_release(u32) { ++released; --held; }
static const unsigned char* track_record(u32 track) { require(track == 3); return record; }
static void* region(u32 track, u32 key, u32*, u32) { require(track == 3 && key == 0); return has_pad ? pad : nullptr; }
opxy::platform::Address opxy::platform::host_stock_address(Address address) {
    if (address == 0x8d587708) return reinterpret_cast<Address>(&track_record);
    if (address == 0x8d58926c) return reinterpret_cast<Address>(&region);
    std::abort();
}
static void path(const char* text) { std::memset(record + 0x396c, 0, 96); std::memcpy(record + 0x396c, text, std::strlen(text)); }
static void sample(u32 frames, u32 channels) {
    std::memset(object, 0, sizeof(object));
    *reinterpret_cast<u32*>(object) = 1;
    *reinterpret_cast<const opxy::i16**>(object + 8) = pcm;
    *reinterpret_cast<u32*>(object + 0x30) = frames;
    *reinterpret_cast<u32*>(object + 0x38) = 44100;
    object[0x3c] = channels == 2;
}

int main() {
    const opxy::draw::SampleOverview& o = *opxy_sample_overview();
    // No sample on the track: nothing assigned, and the cache is never asked.
    sdk_sample_overview_update(3);
    require(!o.assigned && !o.loaded && acquired == 0);

    // A sample still loading: assigned, not loaded, nothing borrowed.
    path("/samples/loop.wav");
    loading = true;
    sdk_sample_overview_update(3);
    require(o.assigned && !o.loaded && acquired == 0);

    // 100000 mono frames: one bin a peak, read in two slices of at most 65536 frames.
    loading = false;
    for (u32 i = 0; i < 100000; ++i) pcm[i] = static_cast<opxy::i16>(i % 2 ? 1000 : -1000);
    const u32 bin = (100000 + 63) / 64;   // 1563 frames a bin
    pcm[50 * bin + 7] = -32768;
    pcm[99999] = 16384;
    sample(100000, 1);
    sdk_sample_overview_update(3);
    require(o.assigned && !o.loaded && o.frames == 100000 && o.rate == 44100);
    require(acquired == 1 && released == 1 && held == 0);
    require(o.peaks[50] == 0 && o.peaks[0] == 1000 >> 7);   // bin 50 is past the first slice
    sdk_sample_overview_update(3);
    require(o.loaded && acquired == 2 && released == 2 && held == 0);
    require(o.peaks[50] == 255 && o.peaks[63] == 16384 >> 7 && o.peaks[49] == 1000 >> 7);
    // Loaded: later frames borrow nothing more.
    sdk_sample_overview_update(3);
    require(acquired == 2);

    // The outer markers come from the track's region-0 pad record.
    has_pad = true;
    pad[0] = 100000; pad[1] = 1200; pad[2] = 90000;
    sdk_sample_overview_update(3);
    require(o.begin == 1200 && o.end == 90000);

    // Another sample: the overview starts over; a stereo sample reads both channels
    // (frame 500, right: bin 31 of 16 frames each).
    path("/samples/pad.wav");
    has_pad = false;
    for (u32 i = 0; i < 2000; ++i) pcm[i] = static_cast<opxy::i16>(i == 1001 ? 25600 : 0);
    sample(1000, 2);
    sdk_sample_overview_update(3);
    require(o.loaded && o.frames == 1000 && o.peaks[31] == 200 && o.peaks[32] == 0 && held == 0);

    // The track's sample removed: nothing assigned again.
    path("");
    sdk_sample_overview_update(3);
    require(!o.assigned && !o.loaded && o.frames == 0 && o.peaks[31] == 0);
    std::puts("sample overview ok");
    return 0;
}
