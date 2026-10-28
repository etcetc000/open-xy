// Eight bounded GUI requests. Only the existing file worker reads retained
// PCM and publishes overviews; GUI readers copy them with one sequence check.
#include "sample_display.hpp"
#include "stock_sample.hpp"
#include "barrier.hpp"
extern "C" {
const void* sdk_sample_cache_sample_acquire(opxy::u32) __asm__("sdk_sample_cache_sample_acquire");
void sdk_sample_cache_sample_release(opxy::u32) __asm__("sdk_sample_cache_sample_release");
}
namespace {
using opxy::u32;
using opxy::i16;
using opxy::platform::barrier;
struct Overview {
    volatile u32 wanted, sequence, key, generation, frames, rate, channels;
    volatile i16 low[opxy::sample_display_bins], high[opxy::sample_display_bins];
    const void* owner;  // Worker-only identity, cleared before stock frees it.
};
Overview overviews[8];
}
extern "C" bool sdk_sample_display_read(u32 track, u32 key, opxy::SampleDisplay* out) {
    *out = opxy::SampleDisplay{};
    if (track >= 8) return false;
    Overview& entry = overviews[track];
    const bool changed = entry.wanted != key;
    entry.wanted = key;
    barrier();
    if (!key) return false;
    // A display can open after loading has finished and the worker is idle.
    // One wake on a new request handles cached samples; the load-completion
    // hook retries pending ones after stock publishes their PCM.
    if (changed) sdk_sample_display_wake();
    const u32 sequence = entry.sequence;
    if ((sequence & 1) || entry.key != key || !entry.frames) return false;
    barrier();
    out->generation = entry.generation;
    out->frames = entry.frames; out->rate = entry.rate; out->channels = entry.channels;
    for (u32 i = 0; i < opxy::sample_display_bins; ++i) {
        out->low[i] = entry.low[i]; out->high[i] = entry.high[i];
    }
    barrier();
    if (entry.sequence != sequence || entry.key != key) { *out = opxy::SampleDisplay{}; return false; }
    return true;
}
extern "C" void sdk_sample_display_prepare() {
    for (u32 track = 0; track < 8; ++track) {
        Overview& entry = overviews[track];
        const u32 key = entry.wanted;
        if (!key || (entry.key == key && entry.owner)) continue;
        const void* object = sdk_sample_cache_sample_acquire(key);
        if (!object) continue;
        const opxy::SampleView sample = opxy::platform::stock_sample_view(object);
        if (sample.data && sample.frames) {
            ++entry.sequence; barrier();
            for (u32 bin = 0; bin < opxy::sample_display_bins; ++bin) {
                const u32 first = sample.frames * bin / opxy::sample_display_bins;
                u32 end = sample.frames * (bin + 1) / opxy::sample_display_bins;
                if (end <= first) end = first + 1;
                i16 low = 32767, high = -32768;
                for (u32 frame = first; frame < end; ++frame)
                    for (u32 channel = 0; channel < sample.channels; ++channel) {
                        const i16 value = sample.data[frame * sample.channels + channel];
                        if (value < low) low = value;
                        if (value > high) high = value;
                    }
                entry.low[bin] = low; entry.high[bin] = high;
            }
            entry.frames = sample.frames; entry.rate = sample.rate; entry.channels = sample.channels;
            entry.owner = object; entry.key = key;
            if (!++entry.generation) ++entry.generation;
            barrier(); ++entry.sequence;
        }
        sdk_sample_cache_sample_release(key);
    }
}
extern "C" void sdk_sample_display_release(const void* object) {
    for (u32 track = 0; track < 8; ++track) {
        Overview& entry = overviews[track];
        if (entry.owner != object || !object) continue;
        ++entry.sequence; barrier();
        entry.owner = nullptr; entry.key = 0; entry.frames = 0;
        barrier(); ++entry.sequence;
    }
}
