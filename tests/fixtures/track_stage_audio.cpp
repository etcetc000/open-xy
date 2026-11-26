// SDK ABI fixture: a track stage's audio snapshots (trackStage audio, SDK 1.27) through the
// probestagemeter synth's actual generated voice adapter, on the host. Only the stock-address
// boundary is substituted (host_stock_address), as effect_audio.cpp does.
#include <cstdio>
#include "voice.cpp"
#include <cstdlib>

namespace host { opxy::u16 frame = 7; }
namespace opxy { namespace platform {
Address host_stock_address(Address address) {
    if (address == 0x84a50b98u) return reinterpret_cast<Address>(&host::frame);
    std::abort();  // the stage path reads no other stock address
}
}}
inline void* operator new(decltype(sizeof 0), void* destination) { return destination; }
namespace opxy { namespace platform { namespace telemetry {
u32 enter() { return 0; }
void leave(u32) {}
wire::Bank* construct_bank(void* storage, volatile u32* words, u32 count, u32 mode, bool reset, u32 prefix) {
    return new (storage) wire::Bank(words, static_cast<wire::u16>(count), static_cast<wire::Mode>(mode), reset, prefix);
}
}}}
// The diagnostic's own services, as tests/fixtures/benchmark_callbacks.cpp gives them.
static unsigned clock_value;
extern "C" opxy::u32 sdk_benchmark_cycles() { clock_value += 100; return clock_value; }
namespace opxy { namespace benchmark {
void* allocate(u32 bytes) { return std::calloc(1, bytes); }
void release(void* data) { std::free(data); }
}}
opxy::u32 sdk_synth_title_slot(opxy::u32, opxy::u32 index) { return index + 4; }
opxy::u32 sdk_voice_ticks() { return 0; }

int main() {
    using opxy::platform::telemetry::wire::Bank;
    static opxy::i32 words[58];
    opxy::i32 sum[128] = {};
    sum[10] = 1000; sum[11] = -3000;   // frame 5: left 1000, right -3000
    sum[40] = -2001;                   // frame 20: left -2001
    probestagemeter_track_reset(2);
    probestagemeter_track_process(2, sum, words, 1);
    // The stage published to track 2's bank, the one the transport reads for the synth's
    // screen on track 2: each channel's peak magnitude, and the louder one held.
    Bank* bank = probestagemeter_audio_bank(2);
    if (!bank || bank->count != 3) return 1;
    if (bank->words[0] != 2000 || bank->words[1] != 2999 || bank->words[2] != 2999) return 2;
    // As one completed update: the transport never reads it half written.
    if (bank->update_version == 0 || (bank->update_version & 1u)) return 3;
    // The sum passes unchanged.
    if (sum[10] != 1000 || sum[11] != -3000 || sum[40] != -2001) return 4;
    // Another track's words are its own.
    Bank* other = probestagemeter_audio_bank(3);
    if (!other || other == bank || other->words[0] || other->words[2]) return 5;
    // The voices' update of the same block comes first (as the adapter's render does, on the
    // same track's bank): the stage's words replace theirs, and a reset the voices acknowledge
    // is no longer pending for the stage.
    {
        auto* voices_bank = sdk_audio.resolve(2);
        const opxy::AudioContext voices = sdk_audio.context(2, voices_bank);
        sdk_audio.begin(voices_bank);
        voices.publish_word(0, 77);
        voices.publish_word(2, 1);
        sdk_audio.end(voices_bank);
        if (bank->words[0] != 77 || bank->words[2] != 1) return 10;
        probestagemeter_track_process(2, sum, words, 1);
        if (bank->words[0] != 2000 || bank->words[2] != 2999) return 11;
        bank->requested_target = 1;
        bank->requested_command = 3;
        sdk_audio.begin(voices_bank);
        voices.command_applied(voices.pending_command());
        sdk_audio.end(voices_bank);
        probestagemeter_track_process(2, sum, words, 1);
        if (bank->applied_command != 3 || bank->words[2] != 2999) return 12;
    }
    // A quieter block keeps the held peak until the screen's reset request, which the stage
    // applies and acknowledges in its next block.
    for (opxy::i32& s : sum) s = 0;
    sum[0] = 50;
    probestagemeter_track_process(2, sum, words, 1);
    if (bank->words[0] != 50 || bank->words[1] != 0 || bank->words[2] != 2999) return 6;
    bank->requested_target = 1;
    bank->requested_command = 4;
    probestagemeter_track_process(2, sum, words, 1);
    if (bank->words[2] != 50 || bank->applied_command != 4) return 7;
    // The diagnostic's stage run publishes into scratch words, never a track's bank.
    const opxy::u32 version = bank->update_version, other_version = other->update_version;
    const opxy::benchmark::Stats stats = probestagemeter_stage_benchmark();
    if (stats.count != 1500) return 9;
    if (bank->update_version != version || other->update_version != other_version || bank->words[2] != 50) return 8;
    std::printf("ok\n");
    return 0;
}
