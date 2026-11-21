// Runs the sample kernel's host build (sdk/host/sample_kernel.cpp, the reference)
// over cases read from standard input and prints every output word, plus two
// words past the end that must keep their fill. tests/test_sample_kernel.py
// writes the cases and compares the words with its golden vectors and its own
// model of the header's definition.
//
// Input, whitespace-separated integers: the number of cases, then per case
//   kind (0 interpolate, 1 crossfade) shift last reverse channels level
//   pcm_length pcm[pcm_length] origin_index count positions[count]
//   and for a crossfade also to[count] weights[count]
#include <opxy/sample_kernel.hpp>

#include <cstdio>
#include <vector>

namespace {
const opxy::i32 fill = 0x5a5a5a5a;

bool read(unsigned long long& value) { return std::scanf("%llu", &value) == 1; }
bool read_signed(long long& value) { return std::scanf("%lld", &value) == 1; }

std::vector<opxy::u32> read_words(unsigned long long count) {
    std::vector<opxy::u32> words(count ? count : 1);
    for (unsigned long long i = 0; i < count; ++i) {
        unsigned long long value = 0;
        read(value);
        words[i] = static_cast<opxy::u32>(value);
    }
    return words;
}
}

int main() {
    unsigned long long cases = 0;
    if (!read(cases)) return 2;
    for (unsigned long long c = 0; c < cases; ++c) {
        unsigned long long kind, shift, last, reverse, channels, level, length, origin, count;
        if (!read(kind) || !read(shift) || !read(last) || !read(reverse) || !read(channels) || !read(level) ||
            !read(length))
            return 2;
        std::vector<opxy::i16> pcm(length ? length : 1);
        for (unsigned long long i = 0; i < length; ++i) {
            long long value = 0;
            if (!read_signed(value)) return 2;
            pcm[i] = static_cast<opxy::i16>(value);
        }
        if (!read(origin) || !read(count)) return 2;
        const std::vector<opxy::u32> positions = read_words(count);
        opxy::SampleTaps taps{};
        taps.origin = pcm.data() + origin;
        taps.shift = static_cast<opxy::u32>(shift);
        taps.last = static_cast<opxy::u32>(last);
        taps.reverse = static_cast<opxy::u32>(reverse);
        taps.channels = static_cast<opxy::u32>(channels);
        taps.level = static_cast<opxy::u32>(level);
        std::vector<opxy::i32> output(2 * count + 2, fill);
        if (kind == 0) {
            opxy::sample_interpolate(output.data(), positions.data(), static_cast<opxy::u32>(count), taps);
        } else {
            const std::vector<opxy::u32> to = read_words(count);
            const std::vector<opxy::u32> weights = read_words(count);
            std::vector<opxy::u16> narrow(weights.size());
            for (size_t i = 0; i < weights.size(); ++i) narrow[i] = static_cast<opxy::u16>(weights[i]);
            const opxy::SampleFade fade{positions.data(), to.data(), narrow.data()};
            opxy::sample_crossfade(output.data(), fade, static_cast<opxy::u32>(count), taps);
        }
        for (size_t i = 0; i < output.size(); ++i) std::printf(i ? " %ld" : "%ld", static_cast<long>(output[i]));
        std::printf("\n");
    }
    return 0;
}
