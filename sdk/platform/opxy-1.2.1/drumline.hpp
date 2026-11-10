// Stock Drumline's second page: the drumline service, linked only while the build
// links a drumline_page extension (docs/drumline-page.md). It changes stock's own
// engine 4 ("dbox") on every Drumline track of the firmware.
// - Page 2: the extension's one to four parameters of the key stock edits. A
//   press of any encoder on the engine page switches pages. Stock's click does
//   nothing there for Drumline: 8c0b3314 -> 8c0b2a6c, page 0, a continuous word,
//   only clears a field at 8c0b2cca. The unshifted encoders then edit the page
//   shown. Shift is stock's one row (TYPE, RANDOM, PAN, VOLUME) on either page,
//   and Shift and a press of encoder 1 keep stock's play-mode cycle.
// - Storage: the pad record's word +0x00, which stock's Drumline never reads or
//   writes (stock's samplers keep a frame count there). Its upper half is the
//   page's owner tag, its lower half the parameters' codes (drumline_page.hpp):
//   each choice less its default, so 0 is neutral. A word without the tag, a
//   pad from before or from another extension, reads as every default.
// - Audio: at every publication of a Drumline pad (8d5ad776) the GUI carries the
//   codes in payload bits the dbox DSP ignores (drumline_carry). A wrapper of the
//   dbox render (drumline.cpp) keeps each voice's payload from its first block,
//   runs the extension's processor on the voice's mono block after stock's level
//   and envelope while any of its own parameters is off its default, and hands
//   the send codes to the per-key send hooks (drum_send.hpp), which mix them as a
//   padMix kit's.
// - Locks: with a step held, a turn on page 2 locks that parameter for the step's
//   hits of the key stock edits, a choice in the step-lock handles' pool
//   (lock_handles.hpp) under fields 8.. (drumline_pool_field), apart from a kit's
//   0..5, so a pattern copied between a kit and Drumline locks nothing on the
//   other. At note start drumline_lock_choices puts them in the payload. Page 1's
//   held-step turns stay stock's (the four sound words 0..3).
#pragma once
#include "drum_locks.hpp"
#include "drum_storage.hpp"
#include "drum_send.hpp"
#include "drumline_page.hpp"

namespace opxy { namespace platform {
const u32 kDrumlineEngine = 4;
const u32 kDrumlinePads = 24, kDrumlineRecords = 0x394c, kDrumlineRecordBytes = 0x80;
const u32 kDrumlineCodeBits = 16, kDrumlineCodeMask = 0xffffu;

inline u32 drumline_code_mask(const DrumlinePageParameter& p) { return (1u << p.bits) - 1; }
// A choice as its code (each choice less the default, wrapped to its bits), and back;
// a code past the choices reads as the default.
inline u32 drumline_code(const DrumlinePageParameter& p, u32 choice) {
    return (choice + (1u << p.bits) - p.neutral) & drumline_code_mask(p);
}
inline u32 drumline_choice(const DrumlinePageParameter& p, u32 code) {
    const u32 choice = (p.neutral + code) & drumline_code_mask(p);
    return choice < p.count ? choice : p.neutral;
}
// The page's codes as choices, and back.
inline void drumline_unpack(const DrumlinePage& page, u32 codes, drumline::Choices& out) {
    for (u32 i = 0; i < drumline::max_parameters; ++i)
        out.choice[i] = static_cast<u8>(i < page.count
            ? drumline_choice(page.parameters[i], codes >> page.parameters[i].shift & drumline_code_mask(page.parameters[i]))
            : 0);
}
inline u32 drumline_pack(const DrumlinePage& page, const drumline::Choices& choices) {
    u32 codes = 0;
    for (u32 i = 0; i < page.count; ++i)
        codes |= drumline_code(page.parameters[i], choices.choice[i]) << page.parameters[i].shift;
    return codes & kDrumlineCodeMask;
}
// A pad's word +0x00: its codes while it holds the page's tag, else none.
inline u32 drumline_codes_of(const DrumlinePage& page, u32 word) {
    return word >> kDrumlineCodeBits == page.tag ? word & kDrumlineCodeMask : 0;
}
// The word that stores `codes`: tagged, or 0 when every choice is its default.
inline u32 drumline_word(const DrumlinePage& page, u32 codes) {
    return codes ? page.tag << kDrumlineCodeBits | (codes & kDrumlineCodeMask) : 0;
}

// The payload carrier. The dbox DSP decodes word 0 as ((w >>> 8) * 66) >>> 23
// (11a06d62..11a06d80), so its bits 0..7 never matter: the codes' bits 0..6
// there, and bit 7 set when word 6 carries the rest. Word 6 (random, which the
// DSP squares, 11a06db8) then carries bits 7..15 in its bits 0..8, a change
// below 2^-22 of full scale; with them 0 it is stock's. tooling/extensions.py
// places the sends' codes first, in bits 0..5, so sends alone never touch word 6.
const u32 kDrumlineCarried = 1u << 7, kDrumlineLowBits = 7, kDrumlineHighMask = 0x1ffu;
inline void drumline_carry(i32* payload, u32 codes) {
    const u32 high = (codes & kDrumlineCodeMask) >> kDrumlineLowBits;
    payload[0] = static_cast<i32>((static_cast<u32>(payload[0]) & ~0xffu) | (codes & ((1u << kDrumlineLowBits) - 1)) |
                                  (high ? kDrumlineCarried : 0u));
    if (high) payload[6] = static_cast<i32>((static_cast<u32>(payload[6]) & ~kDrumlineHighMask) | high);
}
inline u32 drumline_carried(const i32* payload) {
    const u32 low = static_cast<u32>(payload[0]);
    u32 codes = low & ((1u << kDrumlineLowBits) - 1);
    if (low & kDrumlineCarried) codes |= (static_cast<u32>(payload[6]) & kDrumlineHighMask) << kDrumlineLowBits;
    return codes;
}
// A hit's send codes, as drum_send_codes gives a kit's (FX I in bits 0..2, FX II
// in 3..5): a send parameter's choice is its code, 0 TRK.
inline u32 drumline_sends(const DrumlinePage& page, const drumline::Choices& choices) {
    u32 codes = 0;
    for (u32 i = 0; i < page.count; ++i) {
        const u32 role = page.parameters[i].role;
        if (role == kDrumlineSend1 || role == kDrumlineSend2)
            codes |= (choices.choice[i] & (kDrumSendCodes - 1)) << ((role - kDrumlineSend1) * kDrumSendCodeBits);
    }
    return codes;
}
// Whether the processor runs for a hit: one of the extension's own parameters is
// off its default.
inline bool drumline_processed(const DrumlinePage& page, const drumline::Choices& choices) {
    for (u32 i = 0; i < page.count; ++i)
        if (page.parameters[i].role == kDrumlineOwn && choices.choice[i] != page.parameters[i].neutral) return true;
    return false;
}

// A stock dbox voice's object comes from its factory 8d513d1c as 0x84c50b9c +
// index * 0x230, its method table 0x8404ce3c (set at 8d513aa6). Stock sounds
// at most 27 voices; the index, or kDboxVoices for any other object.
const u32 kDboxVoices = 27, kDboxObjects = 0x84c50b9c, kDboxObjectBytes = 0x230, kDboxMethods = 0x8404ce3c;
inline u32 dbox_index(u32 object) {
    const u32 offset = object - kDboxObjects;
    if (object < kDboxObjects || offset >= kDboxVoices * kDboxObjectBytes) return kDboxVoices;
    // offset / 0x230 without a division: (offset / 16) / 35, exact below 2^16 / 19.
    const u32 index = ((offset >> 4) * 1873u) >> 16;
    return index * kDboxObjectBytes == offset ? index : kDboxVoices;
}

// A parameter's lock in the handles' pool: field 8 + its index, apart from a
// padMix kit's 0..5 (DrumHitField), since stock copies a step's handle with the
// step to any track. The value is the locked choice plus 1.
const u32 kDrumlinePoolFields = 8;
inline u32 drumline_pool_field(u32 parameter) { return kDrumlinePoolFields + parameter; }
// A locked value as a choice, or false when it is out of the parameter's range.
inline bool drumline_lock_choice(const DrumlinePageParameter& p, i32 value, u32& choice) {
    if (value < 1 || value > static_cast<i32>(p.count)) return false;
    choice = static_cast<u32>(value - 1);
    return true;
}
// A step's choices for a key: the pad's, each locked one (`mask` and `values` by
// pool field, as step_locks::handle_fields gives them) in its place.
inline void drumline_lock_choices(const DrumlinePage& page, u32 mask, const i32* values, drumline::Choices& choices) {
    for (u32 i = 0; i < page.count; ++i) {
        const u32 field = drumline_pool_field(i);
        u32 choice;
        if (mask >> field & 1u && drumline_lock_choice(page.parameters[i], values[field], choice))
            choices.choice[i] = static_cast<u8>(choice);
    }
}

// The GUI side, which the shared hooks reach while the build links the drumline
// service (drumline.cpp), through drumline_service(); null otherwise.
struct Drumline {
    // Whether a track (0..7) is a Drumline track: its own engine is stock's 4,
    // never a kit's alias (sdk_drum_alias).
    bool (*track)(u32 track);
    // An unshifted turn of the per-key editor (8c0b147a, control 0..3) on
    // page 2: true when handled.
    bool (*edit)(u32 track, u32 key, u32 control, const u32* event);
    // A press of encoder 0..3 on the engine page (sdk_synth_press): switches pages.
    void (*press)(u32 track, u32 encoder);
    // The payload of a pad slot the builder publishes (8d5ad776); at a note's
    // start, the step-lock handle its step applied (0 for none) and its key.
    void (*publish)(u32 track, u32 slot, i32* payload, u32 handle, u32 key);
    // With a step held, an unshifted turn at position 0..3 (the generic call,
    // 8c0b15aa): on page 2 it locks that parameter for the step; true when handled.
    bool (*lock)(u32 track, u32 position, const u32* event);
};
}}

extern "C" {
// The service, or null: the generated startup's accessor (tooling/startup.py).
const void* sdk_drumline_get() __asm__("sdk_drumline_get");
// The linked drumline_page extension's page (tooling/extensions.py).
const opxy::platform::DrumlinePage* sdk_drumline_page_extension() __asm__("sdk_drumline_page_extension");
// Each stock dbox voice's send codes of its last block (drum_send.cpp), as the
// render wrapper keeps them; 0 (TRK) in a build without the service.
extern opxy::u8 sdk_drum_send_dbox[opxy::platform::kDboxVoices] __asm__("sdk_drum_send_dbox");
}

namespace opxy { namespace platform {
inline const Drumline* drumline_service() { return static_cast<const Drumline*>(sdk_drumline_get()); }
// A Drumline track while the build links the service.
inline bool drumline_track(u32 track) {
    const Drumline* service = drumline_service();
    return service && service->track(track);
}
}}
