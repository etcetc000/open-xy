// Host checks for the sample overview a sample engine's own screen reads: the GUI
// asks and copies, only the worker reads PCM, borrowed and released through the
// cache; and the copy follows the sample through replacement, eviction and reload.
extern "C" void host_barrier();
#include <sample_overview.cpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define require(value) do { if (!(value)) { std::fprintf(stderr, "line %d\n", __LINE__); std::abort(); } } while (0)

using opxy::u32;
extern "C" void host_barrier() {}
static unsigned char record[0x4000];   // a track record; its region-0 path is at +0x396c
static u32 acquired, released, held, wakes;
static bool loading;
static opxy::i16 pcm[200000], other[2000];
static unsigned char object[0x40], replacement[0x40];
static const void* current = object;
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
    return current;
}
extern "C" void sdk_sample_cache_sample_release(u32) { ++released; --held; }
void sdk_sample_overview_wake() { ++wakes; }
static const unsigned char* track_record(u32 track) { require(track == 3); return record; }
static void* region(u32 track, u32 key, u32*, u32) { require(track == 3 && key == 0); return has_pad ? pad : nullptr; }
opxy::platform::Address opxy::platform::host_stock_address(Address address) {
    if (address == 0x8d587708) return reinterpret_cast<Address>(&track_record);
    if (address == 0x8d58926c) return reinterpret_cast<Address>(&region);
    std::abort();
}
static void path(const char* text) { std::memset(record + 0x396c, 0, 96); std::memcpy(record + 0x396c, text, std::strlen(text)); }
static void describe(unsigned char* bytes, const opxy::i16* data, u32 frames, u32 channels, u32 rate = 44100) {
    std::memset(bytes, 0, 0x40);
    *reinterpret_cast<u32*>(bytes) = 1;
    *reinterpret_cast<const opxy::i16**>(bytes + 8) = data;
    *reinterpret_cast<u32*>(bytes + 0x30) = frames;
    *reinterpret_cast<u32*>(bytes + 0x38) = rate;
    bytes[0x3c] = channels == 2;
}

int main() {
    const opxy::draw::SampleOverview& o = *opxy_sample_overview();
    // No sample on the track: nothing assigned, nothing woken, the cache never asked.
    sdk_sample_overview_update(3);
    sdk_sample_overview_prepare();
    require(!o.assigned && !o.loaded && acquired == 0 && wakes == 0);

    // A sample still loading: assigned, not loaded, nothing borrowed; one wake per request.
    path("/samples/loop.wav");
    loading = true;
    sdk_sample_overview_update(3);
    sdk_sample_overview_update(3);
    require(o.assigned && !o.loaded && wakes == 1 && acquired == 0);
    sdk_sample_overview_prepare();
    sdk_sample_overview_update(3);
    require(!o.loaded && acquired == 0);

    // The GUI never touches PCM: a frame alone reads nothing, the worker's pass does.
    loading = false;
    for (u32 i = 0; i < 192000; ++i) pcm[i] = static_cast<opxy::i16>(i % 2 ? 1000 : -1000);
    pcm[50 * 2000 + 7] = -32768;
    pcm[191999] = 16384;
    describe(object, pcm, 192000, 1);
    sdk_sample_overview_update(3);
    require(!o.loaded && acquired == 0);
    sdk_sample_overview_prepare();
    require(acquired == 1 && released == 1 && held == 0);
    sdk_sample_overview_update(3);
    require(o.loaded && o.frames == 192000 && o.rate == 44100 && o.channels == 1 && o.generation);
    require(o.low[0] == -1000 && o.high[0] == 1000);
    require(o.low[50] == -32768 && o.high[50] == 1000);
    require(o.high[95] == 16384 && o.low[95] == -1000);
    const u32 first = o.generation;
    // Later frames and passes change nothing, and the bytes aren't read again.
    sdk_sample_overview_prepare();
    sdk_sample_overview_update(3);
    require(o.generation == first && o.loaded && wakes == 1 && held == 0);

    // The outer markers come from the track's region-0 pad record.
    has_pad = true;
    pad[0] = 192000; pad[1] = 1200; pad[2] = 90000;
    sdk_sample_overview_update(3);
    require(o.begin == 1200 && o.end == 90000);
    has_pad = false;

    // Replaced under the same name, in a new object: the worker's next pass finds it,
    // whatever length it has, with no key change and no eviction reported.
    for (u32 i = 0; i < 2000; ++i) other[i] = 100;
    describe(replacement, other, 2000, 1);
    current = replacement;
    sdk_sample_overview_prepare();
    sdk_sample_overview_update(3);
    require(o.loaded && o.frames == 2000 && o.high[0] == 100 && o.low[0] == 100 && o.generation != first);
    const u32 second = o.generation;

    // The same object holding other PCM of another length is another sample too.
    other[0] = -5;
    describe(replacement, other, 1000, 1);
    sdk_sample_overview_prepare();
    sdk_sample_overview_update(3);
    require(o.frames == 1000 && o.low[0] == -5 && o.generation != second);
    const u32 third = o.generation;

    // Evicted: stock's destroy reports it. Nothing stale shows while it is gone,
    // and a reload at the same address reads again.
    sdk_sample_overview_release(replacement);
    sdk_sample_overview_update(3);
    require(o.assigned && !o.loaded && o.frames == 0 && o.high[0] == 0);
    loading = true;
    sdk_sample_overview_prepare();
    sdk_sample_overview_update(3);
    require(!o.loaded);
    loading = false;
    other[0] = -7;
    sdk_sample_overview_prepare();
    sdk_sample_overview_update(3);
    require(o.loaded && o.low[0] == -7 && o.generation != third);
    // Dropped by the manager with no destroy seen: withdrawn on the next pass.
    loading = true;
    sdk_sample_overview_prepare();
    sdk_sample_overview_update(3);
    require(!o.loaded && o.frames == 0);
    loading = false;

    // Another sample: never the last one's bins while it's prepared, and a new wake.
    path("/samples/pad.wav");
    for (u32 i = 0; i < 2000; ++i) pcm[i] = static_cast<opxy::i16>(i == 1001 ? 25600 : 0);
    describe(object, pcm, 1000, 2);
    current = object;
    const u32 woken = wakes;
    sdk_sample_overview_update(3);
    require(!o.loaded && o.frames == 0 && wakes == woken + 1);
    sdk_sample_overview_prepare();
    sdk_sample_overview_update(3);
    // Stereo reads both channels; frame 500 of 1000 is bin 48.
    require(o.loaded && o.frames == 1000 && o.channels == 2 && o.high[48] == 25600 && o.high[47] == 0);

    // Fewer frames than bins: every bin holds a real frame, none reads past the end.
    pcm[0] = 9; pcm[1] = -32768;
    current = replacement;
    describe(replacement, pcm, 1, 1);
    sdk_sample_overview_prepare();
    sdk_sample_overview_update(3);
    require(o.frames == 1 && o.high[0] == 9 && o.high[95] == 9 && o.low[95] == 9);

    // The track's sample removed: nothing assigned; the worker stops asking.
    path("");
    sdk_sample_overview_update(3);
    const u32 stopped = acquired;
    sdk_sample_overview_prepare();
    require(!o.assigned && !o.loaded && o.frames == 0 && acquired == stopped && acquired == released);
    std::puts("sample overview ok");
    return 0;
}
