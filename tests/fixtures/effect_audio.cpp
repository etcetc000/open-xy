// SDK ABI fixture: an effect's audio snapshots (docs/display.md#audio-snapshots) through
// its actual generated object and the generated transport registry, on the host. Only
// the stock-address boundary is substituted (host_stock_address), as fx_buffer.cpp does.
#include "fx_object_probefx.cpp"
#include "telemetry_registry.cpp"

namespace host {
const void* objects[16];        // stock's effect object per track (14 and 15: the sends)
opxy::u32 engines[8];           // the engine per track, as the audio side renders it
opxy::u16 frame;
opxy::u32 screen_engines[8];    // the engine per track, as the screen's processor sees it
opxy::u32 track_engine(opxy::u32 track) { return track < 8 ? screen_engines[track] : 0; }
}
namespace opxy { namespace platform {
Address host_stock_address(Address address) {
    if (address >= 0x11b01d88u && address < 0x11b01d88u + 16 * 4)
        return reinterpret_cast<Address>(&host::objects[(address - 0x11b01d88u) / 4]);
    if (address >= 0x11b01d48u && address < 0x11b01d48u + 8 * 4)
        return reinterpret_cast<Address>(&host::engines[(address - 0x11b01d48u) / 4]);
    if (address == 0x84a50b98u) return reinterpret_cast<Address>(&host::frame);
    if (address == 0x8d58781cu) return reinterpret_cast<Address>(&host::track_engine);
    return 0;
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

int main() {
    using namespace opxy::platform::telemetry;
    typedef void (*Init)(void*, opxy::u32);
    typedef void (*Process)(void*, opxy::i32*, opxy::i32*, const opxy::i32*);
    Object first = {}, second = {}, other = {};
    probefx_fx_factory(&first); probefx_fx_factory(&second);
    other.vtable = reinterpret_cast<const void* const*>(&host::frame);
    const Init init = reinterpret_cast<Init>(const_cast<void*>(first.vtable[5]));
    const Process process = reinterpret_cast<Process>(const_cast<void*>(first.vtable[4]));
    const opxy::i32 parameters[8] = {};
    opxy::i32 left[64] = {}, right[64] = {};
    const wire::u16 key = effect_key | 5;
    // Stock places the effect in send 0 (slot 14): its words start at zero.
    host::objects[14] = &first;
    init(&first, 14);
    wire::Bank* bank = resolve_bank(nullptr, key, 0);
    if (!bank || bank->count != 4 || bank->words[0] || resolve_bank(nullptr, key, 1)) return 1;
    left[0] = 1234; right[0] = -5;
    process(&first, left, right, parameters);
    // The effect published its first samples and the stock track of its send.
    if (bank->words[0] != 1234 || bank->words[1] != static_cast<opxy::u32>(-5) || bank->words[2] != 14) return 2;
    if (bank->update_version & 1) return 3;
    // The same effect in send 1 publishes into that send's own words.
    host::objects[15] = &second;
    init(&second, 15);
    left[0] = 77;
    process(&second, left, right, parameters);
    wire::Bank* bank1 = resolve_bank(nullptr, key, 1);
    if (!bank1 || bank1 == bank || bank1->words[0] != 77 || bank1->words[2] != 15 || bank->words[0] != 1234) return 4;
    // An object outside the sends (a call stock makes elsewhere) publishes nothing.
    Object loose = {};
    probefx_fx_factory(&loose);
    left[0] = 9;
    process(&loose, left, right, parameters);
    if (bank->words[0] != 1234 || bank1->words[0] != 77) return 5;
    // Another effect in the send: the transport no longer reads this one's bank.
    host::objects[14] = &other;
    if (resolve_bank(nullptr, key, 0) || !resolve_bank(nullptr, key, 1)) return 6;
    // A reset (init again) in a send clears that send's words.
    host::objects[14] = &first;
    init(&first, 14);
    if (bank->words[0] || bank->words[1] || bank1->words[0] != 77) return 7;
    // Effect keys never resolve to a synth's bank, nor past the two sends.
    if (resolve_bank(nullptr, key, 2) || resolve_bank(nullptr, 5, 0)) return 8;
    opxy::u32 words, mode, prefix; bool reset;
    if (!configuration(key, words, mode, reset, prefix) || words != 4 || mode != 0 || reset || configuration(5, words, mode, reset, prefix))
        return 9;
    // The screen side: an effect page on send 0 or 1 (stock tracks 14 and 15) selects its
    // send's words, and no other track.
    if (wire_track(key, 14) != 0 || wire_track(key, 15) != 1 || wire_track(key, 3) != 8 || wire_track(key, 16) != 8)
        return 10;
    // A synth's page selects its own track while it is that track's engine; never a send.
    host::screen_engines[2] = 5;
    if (wire_track(5, 2) != 2 || wire_track(5, 3) != 8 || wire_track(5, 14) != 8) return 11;
    // The audio side keeps serving a send while it holds the effect, and a track while it
    // renders the synth.
    if (!audio_owner(key, 0) || !audio_owner(key, 1)) return 12;
    host::objects[15] = &other;
    if (!audio_owner(key, 0) || audio_owner(key, 1)) return 13;
    host::engines[2] = 5;
    if (!audio_owner(5, 2) || audio_owner(5, 3) || audio_owner(5, 14) || audio_owner(key | 2, 0)) return 14;
    return 0;
}
