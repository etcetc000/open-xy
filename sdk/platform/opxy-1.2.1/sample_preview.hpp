// Frozen source audition carried in the stock tap slicer's note packet.
// Machines with sliceTiming supply the processed clock; others use the raw
// PCM renderer below for one forward pass at the file's sample rate.
#pragma once
#include <opxy/sample_engine.hpp>

namespace opxy { namespace platform {
struct SamplePreviewRange {
    u32 begin, end, frames;
    u32 processed, bpm_x10, settings, level;
    i32 pitch_cents, track_transpose;
    i32 parameters[8];
};
struct SamplePreview {
    SampleView sample;
    u32 position, end, remainder;
    bool active;
};
inline SamplePreview sample_preview(const SampleView& sample, const SamplePreviewRange* range) {
    SamplePreview result{sample, 0, 0, 0, range != nullptr};
    if (range && sample.data && sample.frames == range->frames &&
        sample.rate >= 8000 && sample.rate <= 192000 &&
        (sample.channels == 1 || sample.channels == 2)) {
        result.position = range->begin < sample.frames ? range->begin : sample.frames;
        result.end = range->end < sample.frames ? range->end : sample.frames;
    }
    return result;
}
inline i32 preview_pcm(const SamplePreview& preview, u32 channel) {
    const i32 a = sample_at(preview.sample, preview.position, channel);
    const u32 next = preview.position + 1 < preview.end ? preview.position + 1 : preview.position;
    const i32 difference = i32(sample_at(preview.sample, next, channel)) - a;
    const u32 magnitude = difference < 0 ? u32(-difference) : u32(difference);
    const i32 fraction = i32(magnitude * preview.remainder / 44100u);
    return difference < 0 ? a - fraction : a + fraction;
}
// Signed PCM16 to the SDK's Q31 headroom with the stock unsigned Q27 level.
// Split the product to avoid a 64-bit runtime dependency; saturate loud pads.
inline i32 preview_gain(i32 pcm, u32 level) {
    const u32 magnitude = pcm < 0 ? u32(-pcm) : u32(pcm);
    const u32 low = magnitude * (level & 65535u);
    const u32 high = magnitude * (level >> 16) + (low >> 16);
    if (high >= 0x08000000u) return pcm < 0 ? (-2147483647 - 1) : 2147483647;
    const i32 scaled = i32((high << 4) | ((low & 65535u) >> 12));
    return pcm < 0 ? -scaled : scaled;
}
inline void render_sample_preview(SamplePreview& preview, i32* output, u32 channels, u32 level) {
    for (u32 frame = 0; frame < 64; ++frame) {
        i32 left = 0, right = 0;
        if (preview.position < preview.end) {
            left = preview_pcm(preview, 0);
            right = preview.sample.channels == 2 ? preview_pcm(preview, 1) : left;
            preview.remainder += preview.sample.rate;
            preview.position += preview.remainder / 44100u;
            preview.remainder %= 44100u;
        }
        output[frame * channels] = preview_gain(channels == 2 ? left : (left + right) / 2, level);
        if (channels == 2) output[frame * channels + 1] = preview_gain(right, level);
    }
}
}}

extern "C" const opxy::platform::SamplePreviewRange* sdk_sample_slice_preview(opxy::u32, opxy::u32)
    __asm__("sdk_sample_slice_preview");
