// Stock OS 1.2.1 sample overview for a sample engine's own screen. GUI role
// only: the screen shell refreshes it before each frame of such a screen
// (screen_drawing.hpp), reading the selected track's region-0 sample as the
// sample cache does (its stock path key, borrowed and released through the
// stock manager) a bounded slice at a time, so a long sample never stalls a
// frame. Stock PCM is only read.
#include <opxy/sample_overview.hpp>
#include "stock.hpp"
#include "stock_sample.hpp"

extern "C" {
opxy::u32 sdk_sample_cache_stock_hash(const char*) __asm__("sdk_sample_cache_stock_hash");
const void* sdk_sample_cache_sample_acquire(opxy::u32) __asm__("sdk_sample_cache_sample_acquire");
void sdk_sample_cache_sample_release(opxy::u32) __asm__("sdk_sample_cache_sample_release");
void sdk_sample_overview_update(opxy::u32) __asm__("sdk_sample_overview_update");
}

namespace {
using opxy::u32;
using namespace opxy::platform;
using opxy::draw::overview_bins;

// A track record's region paths (sample_cache.cpp): 128 bytes each, at most 95 characters.
const u32 kRegionPaths = 0x396c, kPathBytes = 96;
// Frames read per frame drawn: about 3 ms of reading, whatever the sample's length.
const u32 kSliceFrames = 65536;

opxy::draw::SampleOverview overview;
u32 key;          // the path key being shown, 0 for none
u32 scanned;      // frames read so far of the current sample
bool complete;

void clear() {
    overview.assigned = overview.loaded = overview.frames = overview.rate = overview.begin = overview.end = 0;
    for (u32 i = 0; i < overview_bins; ++i) overview.peaks[i] = 0;
    scanned = 0;
    complete = false;
}

// Reads frames [scanned, scanned + kSliceFrames) into their bins.
void scan(const opxy::SampleView& view) {
    const u32 bin_frames = (view.frames + overview_bins - 1) / overview_bins;
    const u32 stop = view.frames - scanned > kSliceFrames ? scanned + kSliceFrames : view.frames;
    for (u32 frame = scanned; frame < stop;) {
        const u32 bin = frame / bin_frames, bin_end = (bin + 1) * bin_frames < stop ? (bin + 1) * bin_frames : stop;
        u32 peak = overview.peaks[bin];
        for (; frame < bin_end; ++frame) {
            for (u32 c = 0; c < view.channels; ++c) {
                const opxy::i32 s = view.data[frame * view.channels + c];
                const u32 magnitude = u32(s < 0 ? -s : s) >> 7;   // 0..256
                if (magnitude > peak) peak = magnitude > 255 ? 255 : magnitude;
            }
        }
        overview.peaks[bin] = static_cast<opxy::u8>(peak);
    }
    scanned = stop;
    complete = scanned >= view.frames;
}
}

extern "C" const opxy::draw::SampleOverview* opxy_sample_overview() { return &overview; }

void sdk_sample_overview_update(u32 track) {
    const unsigned char* record = track < kTracks ? stock<const unsigned char*(u32)>(0x8d587708)(track) : nullptr;
    const char* path = record ? reinterpret_cast<const char*>(record + kRegionPaths) : nullptr;
    u32 length = 0;
    while (path && length < kPathBytes && path[length]) ++length;
    if (!length || length == kPathBytes) {
        if (key) { key = 0; clear(); }
        return;
    }
    const u32 next = sdk_sample_cache_stock_hash(path);
    if (next != key) { key = next; clear(); }
    overview.assigned = 1;
    // The outer markers as the engine plays them, from the region's record.
    if (const void* region = pad_region(track, 0)) {
        if (static_cast<opxy::i32>(pad_word(region, pad_record::kFrames)) > 0) {
            const opxy::SampleRegion outer = sample_gui_region(region);
            overview.begin = outer.begin;
            overview.end = outer.end;
        }
    }
    if (complete) return;
    const void* object = sdk_sample_cache_sample_acquire(key);
    if (!object) return;   // still loading: try again next frame
    const opxy::SampleView view = stock_sample_view(object);
    if (view.frames) {
        overview.frames = view.frames;
        overview.rate = view.rate;
        scan(view);
        overview.loaded = complete;
    }
    sdk_sample_cache_sample_release(key);
}
