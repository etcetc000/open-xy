#include <input_monitor_audio.cpp>
#include <cstdlib>
#include <cstdio>
#include <cstring>
using namespace opxy;
using namespace opxy::platform;
namespace {
u32 marker;
i32 scratch[block_frames * 2];
void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
}
namespace opxy { namespace platform {
Address host_stock_address(Address address) {
    if (address == 0x84eab39c + 12 * 0xe8 + input_monitor::word_index * 4)
        return reinterpret_cast<Address>(&marker);
    if (address == 0x11903600) return reinterpret_cast<Address>(scratch);
    std::abort();
}
}}
int main() {
    using input_monitor::Envelope;
    const u32 on = input_monitor::mark | 3u, off = on & ~1u;
    Envelope e;
    require(!e.begin(0), "manual monitoring must bypass envelope");
    require(e.begin(on), "automatic request must start envelope");
    i32 previous = 0;
    for (u32 i = 0; i < input_monitor::fade_in_frames; ++i) {
        const i32 gain = e.next();
        require(gain >= previous && gain > 0, "fade-in must be positive and monotonic"); previous = gain;
    }
    require(previous == 0x7fffffff, "fade-in must reach exact unity in 50 ms");
    require(e.begin(off), "lowering keeps ownership through fade-out");
    for (u32 i = 0; i < input_monitor::fade_out_frames; ++i) {
        const i32 gain = e.next();
        require(gain <= previous && gain >= 0, "fade-out must be monotonic and bounded"); previous = gain;
    }
    require(previous == 0, "fade-out must reach silence in 200 ms");
    e.begin(on);
    for (u32 i = 0; i < 1000; ++i) previous = e.next();
    e.begin(off); const i32 turning = e.next();
    require(turning < previous && turning > 0, "reversal must continue from current gain");
    e.begin(on); require(e.next() > turning, "raising during release must reverse smoothly");
    for (u32 i = 0; i < 12; ++i) require(e.begin(0), "brief live-value reset must retain envelope");
    require(!e.begin(0), "missing ownership must eventually return to stock");
    e.begin(on + 2); require(e.next() < turning, "new lease must start from silence");

    for (u32 i = 0; i < block_frames * 2; ++i) scratch[i] = i & 1 ? -0x40000000 : 0x40000000;
    i32 original[block_frames * 2]; std::memcpy(original, scratch, sizeof(scratch));
    sdk_input_monitor_audio();
    require(std::memcmp(original, scratch, sizeof(scratch)) == 0, "manual buffer must be bit-exact");
    marker = on;
    sdk_input_monitor_audio();
    require(scratch[0] > 0 && scratch[0] < scratch[2], "automatic stereo scratch must fade in per sample");
    require(scratch[1] < 0 && scratch[1] > scratch[3], "both channels must use the same envelope");
    for (u32 block = 0; block < 35; ++block) sdk_input_monitor_audio();
    std::memcpy(scratch, original, sizeof(scratch));
    sdk_input_monitor_audio();
    require(std::memcmp(original, scratch, sizeof(scratch)) == 0, "full automatic gain must be bit-exact");
    marker = off;
    for (u32 block = 0; block < 138; ++block) sdk_input_monitor_audio();
    require(scratch[126] == 0 && scratch[127] == 0, "fade must finish within the last block");
    sdk_input_monitor_audio();
    for (i32 value : scratch) require(value == 0, "finished automatic fade must be silent");
    marker = input_monitor::released;
    std::memcpy(scratch, original, sizeof(scratch)); sdk_input_monitor_audio();
    require(std::memcmp(original, scratch, sizeof(scratch)) == 0, "manual monitoring after auto must be bit-exact immediately");
    return 0;
}
