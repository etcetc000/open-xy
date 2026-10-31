#include <cstdlib>
#include <cstdint>
#include <vector>
#include <initializer_list>
#include "sample_preview.hpp"
using namespace opxy;
using namespace opxy::platform;
static void check(bool value) { if (!value) std::abort(); }
int main() {
    // Original-speed progress agrees with timestamp-derived slice markers
    // across rates, stereo layouts and nonzero source starts, without drift.
    for (u32 rate : {8000u, 22050u, 44100u, 48000u, 96000u, 192000u}) {
        for (u32 channels : {1u, 2u}) {
            std::vector<i16> pcm(5000 * channels);
            for (u32 frame = 0; frame < 5000; ++frame) {
                pcm[frame * channels] = i16(frame % 30000);
                if (channels == 2) pcm[frame * channels + 1] = i16(-i32(frame));
            }
            const SampleView sample{pcm.data(), 5000, rate, channels};
            SamplePreviewRange range{123, 4123, 5000};
            SamplePreview preview = sample_preview(sample, &range);
            range.begin = 1000; range.end = 1500; // Publishing cuts cannot retarget this voice.
            i32 output[130]; output[128] = 987; output[129] = 654;
            u32 emitted = 0;
            for (unsigned block = 0; block < 400; ++block) {
                render_sample_preview(preview, output, 2, sample_unity_level);
                check(output[128] == 987 && output[129] == 654);
                for (u32 frame = 0; frame < 64; ++frame, ++emitted) {
                    const std::uint64_t position = std::uint64_t(emitted) * rate;
                    const u32 at = 123 + u32(position / 44100);
                    if (at >= 4123) {
                        check(output[frame * 2] == 0 && output[frame * 2 + 1] == 0);
                    } else {
                        check(output[frame * 2] == i32(at) * 32768);
                        check(output[frame * 2 + 1] == (channels == 2 ? -i32(at) : i32(at)) * 32768);
                    }
                }
                if (preview.position < preview.end)
                    check(preview.position == 123 + std::uint64_t(emitted) * rate / 44100);
            }
        }
    }
    // Interpolation handles the full signed PCM difference without overflow.
    const i16 edge[] = {-32768, 32767, -32768, 32767};
    const SampleView sample{edge, 4, 22050, 1};
    const SamplePreviewRange range{0, 4, 4};
    SamplePreview preview = sample_preview(sample, &range);
    i32 output[64]; render_sample_preview(preview, output, 1, sample_unity_level);
    check(output[0] == -1073741824 && output[1] == -32768);
    check(output[2] == 32767 * 32768 && output[3] == 0);
    check(output[8] == 0 && output[63] == 0);
    for (i32 pcm : {-32768, -123, 0, 123, 32767}) {
        for (u32 level : {0u, 1u, sample_unity_level / 2, sample_unity_level, 0x7fffffffu, 0xffffffffu}) {
            const std::int64_t exact = std::int64_t(pcm) * level / 4096;
            const i32 expected = exact < INT32_MIN ? INT32_MIN : exact > INT32_MAX ? INT32_MAX : i32(exact);
            check(preview_gain(pcm, level) == expected);
        }
    }
    for (SamplePreviewRange bad : {SamplePreviewRange{3, 2, 4}, SamplePreviewRange{0, 4, 5}}) {
        preview = sample_preview(sample, &bad);
        check(preview.active);
        render_sample_preview(preview, output, 1, sample_unity_level);
        for (i32 value : output) check(value == 0);
    }
    check(!sample_preview(sample, nullptr).active);
}
