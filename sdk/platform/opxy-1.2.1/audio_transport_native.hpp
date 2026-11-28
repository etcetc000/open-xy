#pragma once
#include <opxy/audio_transport.hpp>
#include "audio_owner.hpp"
#include "audio_telemetry.hpp"
#include "telemetry/protocol.hpp"
namespace opxy { namespace platform { namespace telemetry {
namespace wire = opxy_transport::telemetry;
// The engine on a track as the audio processor renders it.
inline u32 audio_track_engine(u32 track) { return word(0x11b01d48u + track * 4); }
u32 enter();
void leave(u32 token);
wire::Bank* construct_bank(void* storage, volatile u32* words, u32 count, u32 mode, bool reset, u32 prefix);
struct Snapshot { u32 count, sequence, requested, applied; };
Snapshot capture(u32 engine, u32 track, u32* output, u32 capacity);
void frame_end(u32 engine, u32 track, u32 reset);
void reset_screen(u32 engine);

// An effect's wire key: its effect number with this bit, never a synth engine's
// number (both are below it); its wire track is its send, 0 or 1
// (tooling/audio_telemetry.py EFFECT_KEY).
const u32 effect_key = 0x8000;
const u32 effect_first_send = 14, effect_sends = 2;
// Stock's effect object per track on the audio processor; 14 and 15 are the sends.
inline const void* effect_object(u32 track) { return *at<const void* const volatile>(0x11b01d88u + track * 4); }
// The send whose slot holds this object: 0, 1, or effect_sends for neither (a call
// stock makes outside the sends, or a benchmark).
inline u32 effect_send(const void* object) {
    return effect_object(effect_first_send) == object ? 0 : effect_object(effect_first_send + 1) == object ? 1 : effect_sends;
}
// Whether the send's slot holds an object of this method table now.
inline bool effect_owns(u32 send, const void* vtable) {
    if (send >= effect_sends) return false;
    const void* object = effect_object(effect_first_send + send);
    return object && *static_cast<const void* const volatile*>(object) == vtable;
}

namespace {
// One per declaring machine, in its own adapter alone (internal linkage): a synth's
// eight tracks, or an effect's two sends.
template <u32 Words, u32 Mode, bool Reset, u32 Prefix = 1, u32 Slots = 8> struct NativeBank {
    static_assert(Words > 0 && Words <= 256 && Mode <= 2 && (Mode != 1 || Words == 1) && (Mode != 2 || (Prefix >= 1 && Prefix <= 24 && Words > Prefix)),
                  "Invalid native telemetry contract");
    static_assert(Slots >= 1 && Slots <= 8, "A bank serves at most eight slots");
    volatile u32 words[Slots][Words];
    alignas(wire::Bank) unsigned char state[Slots][sizeof(wire::Bank)];
    u32 ready;
    wire::Bank* resolve(u32 slot) {
        if (slot >= Slots) return nullptr;
        const u32 token = enter();
        if (!(ready & (1u << slot))) {
            construct_bank(state[slot], words[slot], Words, Mode, Reset, Prefix);
            ready |= 1u << slot;
        }
        leave(token);
        return reinterpret_cast<wire::Bank*>(state[slot]);
    }
    // track: the stock track the words belong to (a send's is 14 or 15).
    static AudioContext context(u32 track, wire::Bank* bank, const i32* amplitude = nullptr) {
        return bank ? AudioContext(track, bank->words, bank->count,
            Reset ? &bank->requested_command : nullptr, Reset ? &bank->applied_command : nullptr, Reset ? &bank->requested_target : nullptr, audio_frame_index(), true, amplitude)
            : AudioContext(8, nullptr, 0);
    }
    static void begin(wire::Bank* bank) { if (bank) bank->begin_update(); }
    static void end(wire::Bank* bank) { if (bank) bank->end_update(); }
    // The slot's words read zero until its owner publishes again: an effect's, when
    // stock places it in a send or resets it. Interrupts stay off across the clear, so
    // no block's update can end it part way, wherever stock calls this from.
    void clear(u32 slot) {
        wire::Bank* bank = resolve(slot);
        if (!bank) return;
        const u32 token = enter();
        bank->clear();
        leave(token);
    }
};
}
inline u32 link_number(const char* symbol) {
    Address number = reinterpret_cast<Address>(symbol);
    // A symbol of 0 matches no key: hide the value so the compiler cannot assume a
    // declared object's address is never null and drop the test below.
    __asm__("" : "+r"(number));
    return static_cast<u32>(number);
}
// The owner of a wire key among `owners` (audio_owner.hpp): a synth's engine number, or an effect's number with effect_key.
inline const Owner* find_owner(const Owner* owners, u32 count, u32 engine) {
    for (u32 i = 0; i < count; ++i) {
        const u32 number = link_number(owners[i].number);
        if (number && (owners[i].effect ? effect_key | number : number) == engine) return &owners[i];
    }
    return nullptr;
}
// The registry's owner of a wire key, or null (telemetry_registry.cpp).
const Owner* owner(u32 engine);
// Only a listed synth matching the current audio track owner, or a listed effect its
// send holds, resolves to a role-local bank. No author-provided pointers.
inline wire::Bank* resolve_bank(void*, wire::u16 engine, wire::u8 track) {
    const Owner* o = owner(engine);
    if (!o) return nullptr;
    if (o->effect) return track < effect_sends ? o->bank(track) : nullptr;
    return track < 8 && audio_track_engine(track) == engine ? o->bank(track) : nullptr;
}
inline bool configuration(u32 engine, u32& words, u32& mode, bool& reset, u32& prefix) {
    const Owner* o = owner(engine);
    if (!o) return false;
    words = o->words; mode = o->mode; reset = o->reset; prefix = o->prefix;
    return true;
}

// The wire track of a screen's track: a synth's own (0..7, while it is the track's
// engine), or an effect's send (14, 15 -> 0, 1); 8 when the screen has none.
inline u32 wire_track(u32 owner, u32 track) {
    if (owner & effect_key) return track - effect_first_send < effect_sends ? track - effect_first_send : 8;
    return track < 8 && track_engine(track) == owner ? track : 8;
}
// Whether the audio processor still renders the selection: the track's engine, or
// the effect its send holds.
inline bool audio_owner(u32 owner, u32 track) {
    if (owner & effect_key) return resolve_bank(nullptr, static_cast<wire::u16>(owner), static_cast<wire::u8>(track)) != nullptr;
    return track < 8 && audio_track_engine(track) == owner;
}
}}}
