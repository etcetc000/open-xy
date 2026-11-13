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
//   pad from before or from another extension, reads as every default. A send
//   (SDK 1.15) is a padMix key's: its amount in the record's byte +0x19 or +0x1b,
//   its 3-bit code the amount's coarse copy (drum_send.hpp drum_send_stored),
//   both read only behind the tag.
// - Audio: at every publication of a Drumline pad (8d5ad776) the GUI carries the
//   send pair and the other codes in payload bits the dbox DSP ignores
//   (drumline_carry). A wrapper of the dbox render (drumline.cpp) keeps each
//   voice's payload from its first block, runs the extension's processor on the
//   voice's mono block after stock's level and envelope while any of its own
//   parameters is off its default, and hands the send pair to the per-key send
//   hooks (drum_send.hpp), which mix it as a padMix kit's.
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
// Whether a parameter is a send, and which (0 FX I, 1 FX II).
inline bool drumline_send(const DrumlinePageParameter& p) { return p.role == kDrumlineSend1 || p.role == kDrumlineSend2; }
inline u32 drumline_send_of(const DrumlinePageParameter& p) { return p.role == kDrumlineSend2 ? 1 : 0; }
// The lowest code bit of the page's own parameters (tooling/extensions.py puts the
// sends' codes first), or 16 when it has none.
inline u32 drumline_own_low(const DrumlinePage& page) {
    u32 low = kDrumlineCodeBits;
    for (u32 i = 0; i < page.count; ++i)
        if (!drumline_send(page.parameters[i]) && page.parameters[i].shift < low) low = page.parameters[i].shift;
    return low;
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
// (11a06d62..11a06d80), and the builder puts it mid-bin, 2^22 from an edge, so
// its bits 0..15 never move a bin: the hit's send pair in bits 0..6 and 8..14
// (drum_send_carry, as a kit's), and bit 7 set when word 6 carries the page's
// own parameters' codes, from `low` (drumline_own_low) up, in its bits 0..15.
// Word 6 is random, which the DSP squares (11a06db8): the codes change it by less
// than 2^(16 - low) LSB, below 2^-21 of full scale for a page with both sends;
// with them 0 it is stock's, so sends alone never touch word 6.
const u32 kDrumlineCarried = 1u << 7;
inline void drumline_carry(i32* payload, u32 pair, u32 codes, u32 low) {
    const u32 high = low < kDrumlineCodeBits ? (codes & kDrumlineCodeMask) >> low : 0;
    drum_send_carry(payload, pair);
    payload[0] = static_cast<i32>((static_cast<u32>(payload[0]) & ~kDrumlineCarried) | (high ? kDrumlineCarried : 0u));
    if (high) payload[6] = static_cast<i32>((static_cast<u32>(payload[6]) & ~kDrumlineCodeMask) | high);
}
// The page's own codes a payload carries (its sends' codes 0: drum_send_carried
// has the sends).
inline u32 drumline_carried(const i32* payload, u32 low) {
    if (!(static_cast<u32>(payload[0]) & kDrumlineCarried) || low >= kDrumlineCodeBits) return 0;
    return ((static_cast<u32>(payload[6]) & kDrumlineCodeMask) << low) & kDrumlineCodeMask;
}
// A pad's send pair: each send parameter's amount byte and code (its choice), read
// as a padMix key's (drum_send_stored); TRK where the page has no such send, and
// on a pad without the page's tag (`tagged`).
inline u32 drumline_pair(const DrumlinePage& page, const unsigned char* record, bool tagged,
                         const drumline::Choices& choices) {
    u32 pair = 0;
    if (!record || !tagged) return 0;
    for (u32 i = 0; i < page.count; ++i)
        if (drumline_send(page.parameters[i])) {
            const u32 send = drumline_send_of(page.parameters[i]);
            pair = drum_send_with(pair, send, drum_send_stored(record[kDrumSendByte[send]], choices.choice[i]));
        }
    return pair;
}
// The choices the processor sees for a hit's sends: each send's 3-bit code as SDK
// 1.10..1.14 gave it (drum_send_shown), so Choices keeps its meaning.
inline void drumline_send_choices(const DrumlinePage& page, u32 pair, drumline::Choices& choices) {
    for (u32 i = 0; i < page.count; ++i)
        if (drumline_send(page.parameters[i]))
            choices.choice[i] = static_cast<u8>(drum_send_shown(drum_send_index(pair, drumline_send_of(page.parameters[i])), false));
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
// step to any track. The value is the locked choice plus 1; a send's is a kit's
// send lock (drum_locks.hpp drum_send_lock_index), whose old form is that too.
const u32 kDrumlinePoolFields = 8;
inline u32 drumline_pool_field(u32 parameter) { return kDrumlinePoolFields + parameter; }
// A locked value as a choice, or false when it is out of the parameter's range.
inline bool drumline_lock_choice(const DrumlinePageParameter& p, i32 value, u32& choice) {
    if (value < 1 || value > static_cast<i32>(p.count)) return false;
    choice = static_cast<u32>(value - 1);
    return true;
}
// A step's choices for a key: the pad's, each locked one (`mask` and `values` by
// pool field, as step_locks::handle_fields gives them) in its place; a locked
// send in its place in `pair`.
inline void drumline_lock_choices(const DrumlinePage& page, u32 mask, const i32* values, drumline::Choices& choices,
                                  u32& pair) {
    for (u32 i = 0; i < page.count; ++i) {
        const u32 field = drumline_pool_field(i);
        if (!(mask >> field & 1u)) continue;
        u32 choice, index;
        if (drumline_send(page.parameters[i])) {
            if (drum_send_lock_index(values[field], index)) pair = drum_send_with(pair, drumline_send_of(page.parameters[i]), index);
        } else if (drumline_lock_choice(page.parameters[i], values[field], choice)) {
            choices.choice[i] = static_cast<u8>(choice);
        }
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
// Each stock dbox voice's send pair (drum_send_pair) of its last block
// (drum_send.cpp), as the render wrapper keeps it; 0 (TRK) in a build without the service.
extern opxy::u16 sdk_drum_send_dbox[opxy::platform::kDboxVoices] __asm__("sdk_drum_send_dbox");
}

namespace opxy { namespace platform {
inline const Drumline* drumline_service() { return static_cast<const Drumline*>(sdk_drumline_get()); }
// A Drumline track while the build links the service.
inline bool drumline_track(u32 track) {
    const Drumline* service = drumline_service();
    return service && service->track(track);
}
}}
