// Stock Drumline's second page: the build option drumlineRow2
// (tooling/machines.py BUILD_OPTIONS), off by default, because it changes
// stock's own engine 4 ("dbox") on every Drumline track.
// - Page 2: FILTER, SRR, FX I and FX II of the key stock edits, as MD KIT's
//   pad fields. A press of any encoder on the engine page (stock's click does
//   nothing there for Drumline: 8c0b3314 -> 8c0b2a6c, page 0, a continuous
//   word, only clears a field at 8c0b2cca) switches pages; the unshifted
//   encoders then edit the page shown. Shift is stock's one row (TYPE, RANDOM,
//   PAN, VOLUME) on either page, and Shift and a press of encoder 1 keep stock's
//   play-mode cycle.
// - Storage: the pad record's word +0x00, which stock's Drumline never reads or
//   writes (stock's samplers keep a frame count there): bits 0..3 SRR, 4..6 FX
//   I's code, 7..9 FX II's, 10..14 FILTER (signed). 0 is neutral in each, so a
//   pad from before plays as it did.
// - Audio: at every publication of a Drumline pad (8d5ad776) the GUI carries
//   the fields in payload bits the dbox DSP ignores (drumline_carry); a wrapper
//   of the dbox render (drumline.cpp) keeps each voice's payload from its first
//   block, applies SRR and then FILTER to the voice's mono block after stock's
//   level and envelope, and hands the send codes to the per-key send hooks
//   (drum_send.hpp), which mix them as a padSend kit's.
// - Locks: with a step held, a turn on page 2 locks that field for the step's
//   hits of the key stock edits, an offset in the step-lock handles' pool
//   (lock_handles.hpp) under fields 8.. (drumline_pool_field), apart from a
//   kit's 0..5, so a pattern copied between a kit and Drumline locks nothing on
//   the other. At note start drumline_fold adds them to the payload. Page 1's
//   held-step turns stay stock's (the four sound words 0..3).
#pragma once
#include "drum_locks.hpp"
#include "drum_storage.hpp"

namespace opxy { namespace platform {
const u32 kDrumlineEngine = 4;
const u32 kDrumlinePads = 24, kDrumlineRecords = 0x394c, kDrumlineRecordBytes = 0x80;
const u32 kDrumlineSrrMax = 15, kDrumlineSendMax = 7;
const i32 kDrumlineFilterMin = -16, kDrumlineFilterMax = 15;
// Bits of the pad record's word +0x00 the option uses; the rest stay as found.
const u32 kDrumlineFieldMask = 0x7fffu;

// A pad's four fields.
struct DrumlineFields {
    u32 srr;        // 0 off, 1..15 a hold of drumline_srr_hold(srr) samples
    u32 send[2];    // FX I's and FX II's codes, as drum_send.hpp: 0 TRK, 1..7 0..100 percent
    i32 filter;     // -16..15: low-pass below 0, high-pass above, 0 open (MD KIT's FILTER)
};
inline i32 drumline_signed5(u32 bits) { return (bits & 16u) ? static_cast<i32>(bits & 31u) - 32 : static_cast<i32>(bits & 31u); }
inline DrumlineFields drumline_fields(u32 word) {
    DrumlineFields f;
    f.srr = word & 15u;
    f.send[0] = (word >> 4) & 7u;
    f.send[1] = (word >> 7) & 7u;
    f.filter = drumline_signed5(word >> 10);
    return f;
}
inline u32 drumline_store(u32 word, const DrumlineFields& f) {
    return (word & ~kDrumlineFieldMask) | (f.srr & 15u) | (f.send[0] & 7u) << 4 | (f.send[1] & 7u) << 7 |
           (static_cast<u32>(f.filter) & 31u) << 10;
}

// The payload carrier. The dbox DSP decodes word 0 as ((w >>> 8) * 66) >>> 23
// (11a06d62..11a06d80), so its bits 0..7 never matter: FX I's code in bits
// 0..2, FX II's in 3..5, and bit 6 set when SRR or FILTER is on. Then word 6
// (random, which the DSP squares, 11a06db8) carries SRR in bits 0..3 and
// FILTER in 4..8, a change below 2^-22 of full scale; with both off word 6 is
// stock's.
const u32 kDrumlineCarried = 1u << 6;
inline void drumline_carry(i32* payload, const DrumlineFields& f) {
    const bool on = f.srr || f.filter;
    payload[0] = static_cast<i32>((static_cast<u32>(payload[0]) & ~0xffu) | (f.send[0] & 7u) | (f.send[1] & 7u) << 3 |
                                  (on ? kDrumlineCarried : 0u));
    if (on)
        payload[6] = static_cast<i32>((static_cast<u32>(payload[6]) & ~0x1ffu) | (f.srr & 15u) |
                                      (static_cast<u32>(f.filter) & 31u) << 4);
}
// What a carried payload holds: both send codes as drum_send_codes gives a
// kit's (FX I in bits 0..2, FX II in 3..5), SRR and FILTER.
inline u32 drumline_codes(const i32* payload) { return static_cast<u32>(payload[0]) & 63u; }
inline u32 drumline_srr(const i32* payload) {
    return static_cast<u32>(payload[0]) & kDrumlineCarried ? static_cast<u32>(payload[6]) & 15u : 0;
}
inline i32 drumline_filter(const i32* payload) {
    return static_cast<u32>(payload[0]) & kDrumlineCarried ? drumline_signed5(static_cast<u32>(payload[6]) >> 4) : 0;
}

// SRR: each held sample stands for this many, as a lower sample rate would;
// the captions are 44.1 kHz over the hold (MD KIT's own table and captions).
inline u32 drumline_srr_hold(u32 srr) {
    switch (srr & 15u) {
    case 0: return 1; case 1: return 2; case 2: return 3; case 3: return 4; case 4: return 5; case 5: return 6;
    case 6: return 8; case 7: return 10; case 8: return 12; case 9: return 16; case 10: return 20;
    case 11: return 24; case 12: return 32; case 13: return 40; case 14: return 48; default: return 64;
    }
}
inline const char* drumline_srr_text(u32 srr) {
    switch (srr & 15u) {
    case 0: return "OFF"; case 1: return "22k"; case 2: return "15k"; case 3: return "11k"; case 4: return "8.8k";
    case 5: return "7.3k"; case 6: return "5.5k"; case 7: return "4.4k"; case 8: return "3.7k"; case 9: return "2.8k";
    case 10: return "2.2k"; case 11: return "1.8k"; case 12: return "1.4k"; case 13: return "1.1k";
    case 14: return "919"; default: return "689";
    }
}
// One block of a voice's mono output, held: every hold-th sample stands until
// the next, `left` frames of the current hold carried between blocks. Runs only
// with SRR on.
inline void drumline_hold(i32* out, u32 frames, u32 hold, i32& held, u32& left) {
    i32 value = held;
    u32 remaining = left;
    for (u32 i = 0; i < frames; ++i) {
        if (!remaining) {
            value = out[i];
            remaining = hold;
        }
        --remaining;
        out[i] = value;
    }
    held = value;
    left = remaining;
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

// Per-hit offsets of the Shift settings, for the lock storage to come. Each
// lock's reach either side of neutral, in the field's own steps: pan +-100,
// volume +-50 dB (its whole span), SRR +-15, a send +-7 codes, random +-99 as
// the page shows it (two of stock's 200 buckets each), FILTER +-31.
enum DrumlineLock { kLinePan, kLineVolume, kLineSrr, kLineFx1, kLineFx2, kLineRandom, kLineFilter, kDrumlineLocks };
const u32 kDrumlineNoLock = kDrumlineLocks;
inline i32 drumline_lock_reach(u32 lock) {
    switch (lock) {
    case kLinePan: return 100; case kLineVolume: return 50; case kLineSrr: return 15;
    case kLineFx1: case kLineFx2: return 7; case kLineRandom: return 99; case kLineFilter: return 31; default: return 0;
    }
}
// What the encoder at a position (0..3) of page 2 edits and locks: FILTER,
// SRR, FX I, FX II.
inline u32 drumline_page_lock(u32 position) {
    return position == 0 ? kLineFilter : position == 1 ? kLineSrr : position == 2 ? kLineFx1 : position == 3 ? kLineFx2
                                                                                                   : kDrumlineNoLock;
}
// A Drumline lock's field in the handles' pool: 8 + its DrumlineLock, apart
// from a padMix kit's 0..5 (DrumHitField), since stock copies a step's handle
// with the step to any track.
const u32 kDrumlinePoolFields = 8;
inline u32 drumline_pool_field(u32 lock) { return kDrumlinePoolFields + lock; }
// A step's offsets from a handle's fields for a key (`mask` and `values` by
// pool field, as step_locks::handle_fields gives them), each within its reach;
// true when any is set.
inline bool drumline_offsets(u32 mask, const i32* values, i32* offsets) {
    bool any = false;
    for (u32 lock = 0; lock < kDrumlineLocks; ++lock) {
        const u32 field = drumline_pool_field(lock);
        const i32 reach = drumline_lock_reach(lock);
        offsets[lock] = mask >> field & 1u ? drum_lock_clamp(values[field], -reach, reach) : 0;
        any = any || offsets[lock];
    }
    return any;
}
// A page-2 field as a step plays it: the pad's own value plus an offset.
inline DrumlineFields drumline_offset_fields(DrumlineFields f, const i32* offsets) {
    f.srr = static_cast<u32>(drum_lock_clamp(static_cast<i32>(f.srr) + offsets[kLineSrr], 0, kDrumlineSrrMax));
    for (u32 s = 0; s < 2; ++s)
        f.send[s] = static_cast<u32>(drum_lock_clamp(static_cast<i32>(f.send[s]) + offsets[kLineFx1 + s], 0, kDrumlineSendMax));
    f.filter = drum_lock_clamp(f.filter + offsets[kLineFilter], kDrumlineFilterMin, kDrumlineFilterMax);
    return f;
}

// Stock's own Drumline pad fields: pan the signed byte at +0x1e (-100..100),
// volume the signed byte at +0x1d (-30..+20 dB), random the word +0x14, stock's
// bucket of 200 (8d4d6cc8: (w >>> 8) * 200 >>> 23, stored as its centre).
const i32 kDrumlineVolumeFloor = -30, kDrumlineVolumeCeiling = 20;
const u32 kDrumlineRandomBuckets = 200;
inline i32 drumline_volume(const void* record) {
    return record ? static_cast<signed char>(static_cast<const unsigned char*>(record)[kDrumLevel]) : 0;
}
inline i32 drumline_random_bucket(i32 word) {
    return word <= 0 ? 0 : static_cast<i32>(((static_cast<u32>(word) >> 8) * kDrumlineRandomBuckets) >> 23);
}
inline i32 drumline_random_word(i32 bucket) {
    const i32 width = static_cast<i32>(0x7fffffffu / kDrumlineRandomBuckets);
    return width * bucket + (width >> 1);
}

// A note's payload with its pad's fields and, given a step's offsets (indexed
// by DrumlineLock), those added: pan into word 5, volume into word 7 through
// stock's dB-to-gain (`gain`), random into word 6, the rest into the carrier,
// each clamped to its field. Null offsets: the pad's own values.
inline void drumline_fold(const unsigned char* record, const i32* offsets, i32* payload, i32 (*gain)(i32)) {
    DrumlineFields f = drumline_fields(*reinterpret_cast<const u32*>(record));
    if (offsets) {
        if (offsets[kLinePan])
            payload[5] = drum_pan_word(drum_lock_clamp(drum_pan(record) + offsets[kLinePan], -kDrumPanLimit, kDrumPanLimit));
        if (offsets[kLineVolume])
            payload[kDrumGainWord] = gain(drum_lock_clamp(drumline_volume(record) + offsets[kLineVolume],
                                                          kDrumlineVolumeFloor, kDrumlineVolumeCeiling));
        if (offsets[kLineRandom])
            payload[6] = drumline_random_word(drum_lock_clamp(drumline_random_bucket(payload[6]) + 2 * offsets[kLineRandom], 0,
                                                              static_cast<i32>(kDrumlineRandomBuckets) - 1));
        f = drumline_offset_fields(f, offsets);
    }
    drumline_carry(payload, f);
}

// The GUI side, which the shared hooks reach while the build has the option
// (drumline.cpp); null otherwise. The generated startup defines the pointer.
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
    // 8c0b15aa): on page 2 it locks that field for the step; true when handled.
    bool (*lock)(u32 track, u32 position, const u32* event);
};
}}

extern "C" {
extern const opxy::platform::Drumline* const sdk_drumline __asm__("sdk_drumline");
// Each stock dbox voice's send codes of its last block (drum_send.cpp), as the
// render wrapper keeps them; 0 (TRK) in a build without the option.
extern opxy::u8 sdk_drum_send_dbox[opxy::platform::kDboxVoices] __asm__("sdk_drum_send_dbox");
}

namespace opxy { namespace platform {
// A Drumline track while the build has the option.
inline bool drumline_track(u32 track) { return sdk_drumline && sdk_drumline->track(track); }
}}
