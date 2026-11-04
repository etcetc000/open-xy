// Stock OS 1.2.1 sample overview for a sample engine's own screen. The screen
// shell asks for the selected track's region-0 sample before each of its frames
// (screen_drawing.hpp), and copies one when it is ready, without touching PCM.
// Only the file worker reads PCM, borrowing it from the stock manager as the sample
// cache does: long samples never stall a frame, and each pass asks the manager
// again, so a sample replaced under the same name is read again. Stock PCM is only
// read. Derived from the overview in #83 (etcetc000).
#include "sample_overview.hpp"
#include "barrier.hpp"
#include "stock.hpp"
#include "stock_sample.hpp"

extern "C" {
opxy::u32 sdk_sample_cache_stock_hash(const char*) __asm__("sdk_sample_cache_stock_hash");
const void* sdk_sample_cache_sample_acquire(opxy::u32) __asm__("sdk_sample_cache_sample_acquire");
void sdk_sample_cache_sample_release(opxy::u32) __asm__("sdk_sample_cache_sample_release");
}

namespace {
using opxy::i16;
using opxy::u32;
using namespace opxy::platform;
using opxy::draw::overview_bins;

// A track record's region paths (sample_cache.cpp): 128 bytes each, at most 95 characters.
const u32 kRegionPaths = 0x396c, kPathBytes = 96;
const u32 kTracksOverviewed = 8;

// One track's overview. The GUI writes wanted; the worker writes the rest,
// odd sequence while it does, and the GUI copies with one sequence check.
struct Entry {
    volatile u32 wanted, sequence, key, generation, frames, rate, channels;
    volatile i16 low[overview_bins], high[overview_bins];
    // Worker only: what was read, cleared before stock frees it.
    const void* owner;
    const void* data;
};
Entry entries[kTracksOverviewed];

opxy::draw::SampleOverview overview;
u32 shown_key;   // the key the GUI's copy is of

void clear() {
    overview.generation = overview.assigned = overview.loaded = 0;
    overview.frames = overview.rate = overview.channels = 0;
    overview.begin = overview.end = overview.reverse = 0;
    for (u32 i = 0; i < overview_bins; ++i) overview.low[i] = overview.high[i] = 0;
    shown_key = 0;
}

// The published overview into the GUI's copy; false when none is ready for key.
bool copy(Entry& entry, u32 key) {
    const u32 sequence = entry.sequence;
    if ((sequence & 1) || entry.key != key || !entry.frames) return false;
    barrier();
    if (shown_key == key && overview.generation == entry.generation) {
        barrier();
        return entry.sequence == sequence;
    }
    overview.frames = entry.frames;
    overview.rate = entry.rate;
    overview.channels = entry.channels;
    overview.generation = entry.generation;
    for (u32 i = 0; i < overview_bins; ++i) {
        overview.low[i] = entry.low[i];
        overview.high[i] = entry.high[i];
    }
    barrier();
    return entry.sequence == sequence && entry.key == key;
}

void withdraw(Entry& entry) {
    ++entry.sequence;
    barrier();
    entry.owner = nullptr;
    entry.data = nullptr;
    entry.key = 0;
    entry.frames = 0;
    barrier();
    ++entry.sequence;
}

void read(Entry& entry, u32 key, const void* object, const opxy::SampleView& view) {
    ++entry.sequence;
    barrier();
    // Frames per bin, whole and the remainder over 96, so no product leaves 32 bits.
    const u32 each = view.frames / overview_bins, over = view.frames % overview_bins;
    for (u32 bin = 0; bin < overview_bins; ++bin) {
        const u32 first = each * bin + over * bin / overview_bins;
        u32 stop = each * (bin + 1) + over * (bin + 1) / overview_bins;
        if (stop <= first) stop = first + 1;
        i16 low = 32767, high = -32768;
        for (u32 frame = first; frame < stop && frame < view.frames; ++frame)
            for (u32 channel = 0; channel < view.channels; ++channel) {
                const i16 value = view.data[frame * view.channels + channel];
                if (value < low) low = value;
                if (value > high) high = value;
            }
        if (low > high) low = high = 0;
        entry.low[bin] = low;
        entry.high[bin] = high;
    }
    entry.frames = view.frames;
    entry.rate = view.rate;
    entry.channels = view.channels;
    entry.owner = object;
    entry.data = view.data;
    entry.key = key;
    if (!++entry.generation) ++entry.generation;
    barrier();
    ++entry.sequence;
}
}

extern "C" const opxy::draw::SampleOverview* opxy_sample_overview() { return &overview; }

void sdk_sample_overview_update(u32 track) {
    if (track >= kTracksOverviewed) { clear(); return; }
    const unsigned char* record = stock<const unsigned char*(u32)>(0x8d587708)(track);
    const char* path = record ? reinterpret_cast<const char*>(record + kRegionPaths) : nullptr;
    u32 length = 0;
    while (path && length < kPathBytes && path[length]) ++length;
    const u32 key = length && length < kPathBytes ? sdk_sample_cache_stock_hash(path) : 0;
    Entry& entry = entries[track];
    // A display can open with the worker idle and the sample already cached: wake
    // it once for each new request. It looks again after every pass of its own.
    const bool asked = entry.wanted != key;
    entry.wanted = key;
    barrier();
    if (!key) { clear(); return; }
    if (asked) sdk_sample_overview_wake();
    if (!copy(entry, key)) {
        clear();
    } else {
        shown_key = key;
        overview.loaded = 1;
    }
    overview.assigned = 1;
    // The outer markers as the engine plays them, from the region's record.
    if (const void* region = pad_region(track, 0)) {
        if (static_cast<opxy::i32>(pad_word(region, pad_record::kFrames)) > 0) {
            const opxy::SampleRegion outer = sample_gui_region(region);
            overview.begin = outer.begin;
            overview.end = outer.end;
            overview.reverse = outer.reverse;
            if (overview.frames) {
                if (overview.begin > overview.frames) overview.begin = overview.frames;
                if (overview.end > overview.frames) overview.end = overview.frames;
            }
        }
    }
}

void sdk_sample_overview_prepare() {
    for (u32 track = 0; track < kTracksOverviewed; ++track) {
        Entry& entry = entries[track];
        const u32 key = entry.wanted;
        if (!key) continue;
        const void* object = sdk_sample_cache_sample_acquire(key);
        if (!object) {
            // Gone or still loading: what was read is no longer the sample.
            if (entry.owner) withdraw(entry);
            continue;
        }
        const opxy::SampleView view = stock_sample_view(object);
        if (view.data && view.frames) {
            const bool same = entry.key == key && entry.owner == object && entry.data == view.data &&
                              entry.frames == view.frames && entry.rate == view.rate && entry.channels == view.channels;
            if (!same) read(entry, key, object, view);
        } else if (entry.owner) {
            withdraw(entry);
        }
        sdk_sample_cache_sample_release(key);
    }
}

void sdk_sample_overview_release(const void* object) {
    if (!object) return;
    for (u32 track = 0; track < kTracksOverviewed; ++track)
        if (entries[track].owner == object) withdraw(entries[track]);
}
