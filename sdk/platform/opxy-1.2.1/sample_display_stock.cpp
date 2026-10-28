// GUI adapter: resolve the selected stock region and request its overview.
// No GUI PCM access or cache acquisition. A changed request queues worker work.
#include "sample_display.hpp"
#include "sample_machines.hpp"
#include "stock_sample.hpp"
#include "stock.hpp"
#include "barrier.hpp"
namespace {
volatile opxy::u32 queued;
void prepare_job(opxy::u32) {
    // Clear before processing so a concurrent newer request can queue another
    // pass, including one for a track this pass has already visited.
    queued = 0;
    opxy::platform::barrier();
    sdk_sample_display_prepare();
}
}
extern "C" void sdk_sample_display_wake() {
    if (queued) return;
    queued = 1;
    opxy::platform::barrier();
    // Existing stock file queue: the sample manager submits the same callback
    // shape at 8d509d38 and 8d50c3ec. GUI-only; stock owns queue allocation.
    opxy::platform::stock<void(opxy::u32, opxy::u32, void(*)(opxy::u32), opxy::u32)>(0x8d4dd728)
        (0, 0, &prepare_job, 0);
}
extern "C" opxy::u32 sdk_sample_cache_stock_hash(const char*) __asm__("sdk_sample_cache_stock_hash");
extern "C" bool opxy_sample_display(opxy::u32 track, opxy::SampleDisplay* out) {
    using namespace opxy;
    using namespace opxy::platform;
    if (!out) return false;
    *out = SampleDisplay{};
    if (track >= kTracks) return false;
    const SampleMachine* machine = sdk_sample_machine(track_engine(track));
    if (!machine || word(kScreen) == kSampleBrowserScreen) {
        sdk_sample_display_read(track, 0, out);
        return false;
    }
    const u32 key = stock<u32(u32)>(0x8d4d6c98)(track);
    const void* raw = pad_region(track, key);
    // 8d58926c returns track + 0x394c + region * 128; its 32-byte
    // pad record is immediately followed by the 96-byte assigned path.
    const char* path = raw ? static_cast<const char*>(raw) + 32 : nullptr;
    u32 length = 0;
    if (path) while (length < 96 && path[length]) ++length;
    const u32 sample_key = length && length < 96 ? sdk_sample_cache_stock_hash(path) : 0;
    const bool ready = sdk_sample_display_read(track, sample_key, out);
    // Stock GUI note gates: 8d595838/7c publishes note-on; 8d5954b2 and
    // 8d595514 clear note-off/all-off. The stock keyboard painter reads the
    // same getter at 8d526984. Audio-role adapter globals are not GUI telemetry.
    for (u32 note = 0; note < 128; ++note)
        out->active_notes += stock<u32(u32, u32)>(0x8d5b7d10)(track, note) != 0;
    if (ready && raw) {
        const SampleRegion region = sample_gui_region(raw);
        out->begin = region.begin < out->frames ? region.begin : out->frames;
        out->end = region.end < out->frames ? region.end : out->frames;
        if (out->end < out->begin) out->end = out->begin;
        out->reverse = region.reverse;
    }
    return ready;
}
