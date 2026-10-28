// Host boundary double for the real GUI amp envelope read. No machine behavior
// is tested.
#include <cstdio>
#include <cstdlib>
#include <amp_envelope.hpp>

namespace {
using opxy::platform::EnvelopeWords;
using opxy::i32;
using opxy::u32;
const u32 kWords = 0x1000;  // a 32-bit stand-in for the vector's storage
i32 words[8];
EnvelopeWords vector;
const EnvelopeWords* returned;
unsigned calls;
u32 asked;
void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
const EnvelopeWords* envelope_words(u32 track) { ++calls; asked = track; return returned; }
bool unavailable(const opxy::draw::Envelope& amp) {
    using opxy::draw::envelope_unavailable;
    return amp.attack == envelope_unavailable && amp.decay == envelope_unavailable &&
           amp.sustain == envelope_unavailable && amp.release == envelope_unavailable;
}
}

opxy::platform::Address opxy::platform::host_stock_address(opxy::platform::Address address) {
    if (address >= kWords && address < kWords + sizeof(words))
        return reinterpret_cast<Address>(words) + (address - kWords);
    if (address == 0x8d4d7680) return reinterpret_cast<Address>(&envelope_words);
    std::abort();
}

int main() {
    using namespace opxy;
    const i32 stored[8] = {0x00800000, 0x20000000, 0x7fffffff, 0, 5, 6, 7, 8};
    for (u32 i = 0; i < 8; ++i) words[i] = stored[i];
    vector.begin = kWords; vector.end = kWords + sizeof(words);
    returned = &vector;
    draw::Envelope amp = {};
    platform::amp_envelope(3, amp);
    require(calls == 1 && asked == 3, "The selected track was not asked");
    require(amp.attack == 0x00800000 && amp.decay == 0x20000000 && amp.sustain == 0x7fffffff && !amp.release,
            "Words 8..11 are not attack, decay, sustain and release");
    require(draw::envelope_known(amp), "A read envelope is unknown");

    platform::amp_envelope(8, amp);
    require(calls == 1 && unavailable(amp), "A non-instrument track read stock");
    returned = 0;
    platform::amp_envelope(0, amp);
    require(unavailable(amp) && !draw::envelope_known(amp), "A missing vector was read");
    returned = &vector;
    vector.end = kWords + 12;
    platform::amp_envelope(0, amp);
    require(unavailable(amp), "A short vector was read past its end");
    vector.end = kWords + sizeof(words); vector.begin = kWords + 2;
    platform::amp_envelope(0, amp);
    require(unavailable(amp), "A misaligned vector was read");
    vector.begin = kWords; words[2] = -1;
    platform::amp_envelope(0, amp);
    require(unavailable(amp), "A negative word was reported");
    words[2] = 0x7fffffff;
    platform::amp_envelope(7, amp);
    require(amp.sustain == 0x7fffffff && asked == 7, "The last instrument track was not read");
    return 0;
}
